# 问题说明
当前看到PTO ISA包含了1src指令(如TABS)和2src指令(如TADD、TMUL)，
在vector4k_v2文档中看到了TFMA指令相关描述：TFMA_ACC D=A*B+Acc, Acc来源于staging reg,而非从treg中读取，等同2src指令。


因此缺失3src相关指令，如FMA指令： 
$$dst_{i,j} = src0_{i,j}*src1_{i,j} + src2_{i,j}$$ 


目前看到FMA在两类场景有使用到：
1. reduce_sum相关场景，当前在vector4k_v2中已经描述
2. 提升计算精度和吞吐量场景，如LayerNorm中的FMA优化点。
   
相比于独立的乘法和加法指令，FMA 具有两大决定性优势：
1. **吞吐量翻倍**：将两条独立指令融合成一条，极大地提高了算术逻辑单元 (ALU) 的利用率。
2. **精度提升**：在计算 $A \times B$ 的中间结果时持有无穷精度，仅在最终加上 $C$ 后做一次舍入 (Rounding)，有效避免了两次舍入带来的精度损失。。

当前实际网络或者算子涉及到FMA指令的场景：norm类(layernorm/rmsnorm)、激活函数(gelu、swiglu等)、三角函数(sin、cos)等场景中。


---
# 问题需求
如果需要在vector4k架构中支持3src FMA指令，当前需要适配项：
1. ISA扩展，添加TFMA指令：$$dst_{i,j} = src0_{i,j}*src1_{i,j} + src2_{i,j}$$ 
2. 为用满TFMA算力，需要Tregfile提供512B/C*3的read带宽，当前trefile对vector4k只存在2个read port
   
是否需要考虑针对FMA等3src指令增加vector4k读取trefile带宽？


# 具体场景说明
以layernom为例，说明FMA指令使用场景：

# FMA在layernorm中的使用


---

## 1. Welford 局部状态更新 (Local State Update)

在遍历输入向量的每一个元素 $x_i$ 时，单线程需要实时更新局部的均值 ($\mu$) 和平方差和 ($M_2$)。

### 优化点 1：均值更新 (Mean Update)
原数学公式为：
$$\mu_{new} = \mu_{old} + \frac{x_i - \mu_{old}}{n}$$

**工程转换：** GPU 和 CPU 上的硬件除法延迟极高。我们通常提前计算数量的倒数 $\text{inv\_n} = 1.0 / n$，并设 $\delta = x_i - \mu_{old}$，将除法转换为乘法：
$$\mu_{new} = \delta \times \text{inv\_n} + \mu_{old}$$
此时，它完美契合了 FMA 的形式 `$fmaf(\delta, inv\_n, \mu_{old})$`。

### 优化点 2：平方差和更新 (M2 Update)
原数学公式为，设 $\delta_2 = x_i - \mu_{new}$：
$$M2_{new} = M2_{old} + \delta \times \delta_2$$

**工程转换：** 这里天然就是一个乘加结构，直接映射为 FMA 指令 `$fmaf(\delta, \delta_2, M2_{old})$`。由于这里涉及累加极小的二次项，使用 FMA 可以显著降低灾难性数值抵消 (Catastrophic Cancellation) 的风险。

---

## 2. Welford 状态合并 (State Merge / Reduction)

在 GPU 的 Warp/Block 级别归约，或 CPU 的多线程合并时，我们需要合并两个独立的 Welford 状态（状态 A 和状态 B）。设合并后的总数为 $n = n_A + n_B$，均值差为 $\delta = \mu_B - \mu_A$。

同样，为了避开除法，我们预计算 $inv\_n = 1.0 / n$。

### 优化点 3：合并均值 (Merged Mean)
原计算逻辑为：
$$\mu = \mu_A + \delta \times \frac{n_B}{n}$$

**工程转换：** 提取乘法因子 $\text{factor} = n_B \times inv\_n$。
公式转化为：
$$\mu = \delta \times \text{factor} + \mu_A$$
映射为 FMA：`$fmaf(\delta, factor, \mu_A)$`。

### 优化点 4：合并平方差和 (Merged M2)
原计算逻辑为：
$$M2 = M2_A + M2_B + \delta^2 \times \frac{n_A \times n_B}{n}$$

**工程转换：** 提取组合因子 $\text{factor\_m2} = (n_A \times n_B) \times inv\_n$。
公式转化为：
$$M2 = M2_A + \delta \times (\delta \times \text{factor\_m2}) + M2_B$$
这可以被视作一次针对 $M2_B$ 的基础加法，结合一次核心的 FMA 操作 `$fmaf(\delta \times factor\_m2, \delta, M2_B)$`，然后再与 $M2_A$ 相加。

---

## 3. 仿射变换 (Scale and Shift)

这是 LayerNorm 的最后一步，也是计算最密集、访存最频繁的一步。我们需要将标准化后的结果 $\hat{x}$ 乘以可学习的缩放参数 $\gamma$，并加上平移参数 $\beta$。

### 优化点 5：最终写回 (Final Affine Transform)
原数学公式为：
$$y_i = \gamma_i \times \hat{x}_i + \beta_i$$

**工程转换：** 这是整个深度学习中最经典、最纯粹的 FMA 场景。无论是在 CUDA 还是 C++ AVX/NEON 指令集中，这一步都**必须**被编译为一条独立的 FMA 指令。
映射为：`$fmaf(\gamma_i, \hat{x}_i, \beta_i)$`。

