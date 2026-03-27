# PyCircuit Designs — V5 Cycle-Aware 升级计划

**版本**: 1.0  
**日期**: 2026-03-26

---

## 目标

将 `designs/` 下**全部**设计升级为 PyCircuit V5 的 cycle-aware 编程风格：

1. **函数签名** `(m: CycleAwareCircuit, domain: CycleAwareDomain, ...)`
2. **编译入口** `compile_cycle_aware(build, name=..., eager=True)`
3. **输入信号** 用 `cas(domain, m.input(...), cycle=0)` 包装为 `CycleAwareSignal`
4. **反馈寄存器** 用 `domain.state(width=..., reset_value=..., name=...)` 声明
5. **流水级边界** 用 `domain.next()` 标记，不同周期的逻辑分段书写
6. **组合选择** 用 `mux(cond, a, b)` 替代 `if Wire else` 或 `_select_internal()`
7. **管线寄存器** 用 `domain.cycle(sig, name=...)` 替代手动 `m.out().set()`
8. **子模块** 保留 `@module` / `m.new` / `m.array` 用法不变

---

## 改造难度分级

| 等级 | 含义 | 工作量 |
|------|------|--------|
| ★☆☆ | 纯组合或单寄存器，无 JIT `if Wire`，改签名+换 `domain.state()`/`mux()` 即可 | < 30 min |
| ★★☆ | 有 JIT `if Wire` 或多寄存器，需逐个替换为 `mux()` 并加 `domain.next()` | 1–3 h |
| ★★★ | 多级流水/复杂 FSM/大量 JIT 条件，需重构逻辑结构、划分 cycle 阶段 | 3–8 h |

---

## 时序分类总览

在深入分析每个设计的源代码后，按**实际时序结构**分类如下：

| 时序类型 | 设计数 | 设计列表 |
|---------|--------|---------|
| **纯组合** (0 寄存器) | 11 | jit_control_flow, hier_modules, module_collection, interface_wiring, instance_map, fastfwd, decode_rules, cache_params, arith, bundle_probe_expand, BypassUnit |
| **单寄存器反馈** | 6 | counter(1), wire_ops(1), obs_points(1), net_resolution_depth_smoke(1), xz_value_model_smoke(1), reset_invalidate_order_smoke(1) |
| **多寄存器/FSM** | 8 | multiclock_regs(2), digital_filter(5), digital_clock(6,FSM), calculator(5,FSM), traffic_lights_ce(5,FSM), dodgeball_game(14+2,FSM), trace_dsl_smoke(2子模块), issue_queue_2picker(8) |
| **多级流水线** | 3 | bf16_fmac(**30**寄存器/**4**级), jit_pipeline_vec(**6**/**3**级), pipeline_builder(**2**/**2**级) |
| **大型设计** | 2 | RegisterFile(**256** domain.state, 2 cycle), IssueQueue(**321** m.out, 单周期状态机) |
| **IP 封装** | 5 | fifo_loopback(rv_queue), mem_rdw_olddata(sync_mem), sync_mem_init_zero(sync_mem), npu_node(rv_queue×4), sw5809s(rv_queue×16 + 4寄存器) |
| **层次化** | 2 | huge_hierarchy_stress(叶子含寄存器), struct_transform(1 m.state) |
| **非硬件** | 1 | fm16_system(纯 Python 行为模型，无需迁移) |

---

## 一、大型设计（designs/ 根目录）

### 1. RegisterFile (`designs/RegisterFile/regfile.py`) — ✅ 已完成

| 项目 | 内容 |
|------|------|
| **功能** | 256 条目、128 常量 ROM、10R/5W、64-bit 参数化寄存器堆 |
| **时序类型** | **多寄存器 2-cycle 设计**（读写分相） |
| **寄存器数** | **256 个 `domain.state()`**（128 × bank0[32b] + 128 × bank1[32b]） |
| **端口** | 25 输入（10 raddr + 5 wen + 5 waddr + 5 wdata） · 10 输出（rdata0–9） |
| **当前状态** | **已完成 V5 改造** |

#### 详细时序结构

```
┌─── Cycle 0：组合读 ──────────────────────────────────────┐
│  • 25 个输入 cas() 包装（raddr/wen/waddr/wdata）           │
│  • 256 个 domain.state() 声明（bank0[0..127], bank1[0..127]）│
│  • 对每个读口 i (0..9)：                                    │
│    - 地址比较：是常量区? 是合法 ptag?                        │
│    - mux() 选择：常量拼接 / 存储体读出 / 零值               │
│  • m.output("rdata{i}", lane_data.wire)                    │
│  • ~3860 次 mux() 调用                                     │
├─── domain.next() ────────────────────────────────────────┤
│                                                           │
├─── Cycle 1：同步写回 ────────────────────────────────────┐
│  • 对每个存储项 sidx (0..127)：                            │
│    - 累加各写口的 hit → we_any                             │
│    - mux() 链选出 next_lo / next_hi                       │
│    - bank0[sidx].set(next_lo, when=we_any)                │
│    - bank1[sidx].set(next_hi, when=we_any)                │
└──────────────────────────────────────────────────────────┘
```

| V5 API 使用 | 数量 |
|-------------|------|
| `cas()` | ~2135 |
| `mux()` | ~3860 |
| `domain.state()` | 256 |
| `domain.next()` | 1 |
| `domain.cycle()` | 0 |

#### 验证状态
- 29/29 功能测试通过，100K 周期仿真 57.4 Kcycles/s

---

### 2. IssueQueue (`designs/IssueQueue/issq.py`) — ★★★

| 项目 | 内容 |
|------|------|
| **功能** | 多入多出发射队列：entry 状态管理、年龄矩阵排序、ptag 就绪广播、按龄优先发射 |
| **时序类型** | **大量寄存器的单周期状态机**（所有组合决策 + 状态更新在同一拍完成） |
| **寄存器数** | **321 个 `m.out()`**（默认 entries=16, ptag_count=64）|
| **端口** | `enq_ports`×(1+struct) 输入 · `issue_ports`×(1+struct) + `enq_ports` + 2 输出 |
| **JIT `if Wire`** | issq_config.py 中 4 处 |
| **`@function`** | issq.py 10 个 + issq_config.py 8 个 = **18 个** |

#### 寄存器分解（默认参数）

| 寄存器组 | 公式 | 默认数量 | 位宽/个 | 总 bit |
|---------|------|---------|---------|--------|
| entry 状态（valid/src/dst/payload） | entries | 16 | 57b | 912b |
| 年龄矩阵 `age_{i}_{j}` | entries² | 256 | 1b | 256b |
| 就绪表 `ready_ptag_{t}` | ptag_count | 64 | 1b | 64b |
| 已发射计数 `issued_total_q` | 1 | 1 | 16b | 16b |
| **合计** | | **321** m.out + 16 entry | | **~1248b** |

#### 详细时序结构

```
┌─── 单周期逻辑（当前拍输入 + 上拍状态 → 本拍输出 + 下拍状态）──┐
│                                                              │
│  1. _snapshot_entries：从 entry_state[0..15] 读取当前状态       │
│  2. _select_oldest_ready：                                    │
│     • entry_ready = valid & src0_ready & src1_ready           │
│     • 年龄矩阵仲裁 → 选最老 ready entry（one-hot）             │
│     • 多发射口串行扣除已选 → issue_sel[], issue_valid[]        │
│  3. _allocate_enqueue_lanes：                                 │
│     • 在空槽上分配入队 → alloc_lane[], next_valid[]            │
│  4. _emit_issue_ports：                                       │
│     • one-hot mux → iss{k}_valid, iss{k}_* 输出              │
│  5. _issue_wake_vectors：                                     │
│     • 同拍旁路 wakeup: wake_valid/wake_ptag                   │
│  6. _write_entry_next_state：                                 │
│     • 对每个 slot: keep / new_alloc 选择                       │
│     • src ready 合并: 原值 | ready_table查找 | 同拍wake旁路    │
│     → entry_state[i].set(next)                                │
│  7. _update_age_state：                                       │
│     • age[i][j] 更新: keep+keep→保留, keep+new→1, new+new→lane_lt │
│     → age[i][j].set(next)                                     │
│  8. _update_ready_table：                                     │
│     • ready_state[t].set(old | wake_t)                        │
│  9. _emit_debug_and_ready：                                   │
│     • occupancy, issued_total 计数 → 输出                     │
│     • issued_total_q.set(issued_total_q.out() + issue_count)  │
└──────────────────────────────────────────────────────────────┘
```

#### V5 改造方案

| 步骤 | 改造内容 |
|------|---------|
| **签名** | `def build(m: CycleAwareCircuit, domain: CycleAwareDomain, ...)` |
| **Cycle 0：输入** | `enq_valid/data/ptag` 用 `cas()` 包装 |
| **Cycle 0：状态声明** | 16 个 entry → `domain.state()` × 16（需按 struct 字段分别声明或用 batch API） |
| **Cycle 0：年龄矩阵** | 256 个 1-bit `domain.state(width=1, name=f"age_{i}_{j}")` |
| **Cycle 0：就绪表** | 64 个 `domain.state(width=1, name=f"ready_ptag_{t}")` |
| **Cycle 0：issued_total** | `domain.state(width=16, name="issued_total")` |
| **Cycle 0：仲裁逻辑** | `_select_oldest_ready` 保持组合；`issq_config.py` 中 4 处 `if Wire else` → `mux()` |
| **Cycle 0：输出** | `iss{k}_*`, `enq{k}_ready`, `occupancy` 在 cycle 0 组合输出 |
| **`domain.next()`** | → **Cycle 1：状态更新** |
| **Cycle 1** | 全部 `.set()` 调用：entry[i].set(next), age[i][j].set(next), ready[t].set(next), issued_total.set(next) |
| **`@function` 保留** | 18 个 `@function` 保持 Wire 级；不在其中使用 CAS 对象 |

**关键难点：**
- entry 是结构化类型（valid/src0.ptag/src0.ready/…），需将 `m.state(uop_spec)` 拆分为多个 `domain.state()` 或扩展 V5 API 支持 struct state
- `@function` 辅助函数内部不能使用 `CycleAwareSignal`，需在调用前 `.wire` 解包、返回后 `cas()` 重包
- 年龄矩阵 256 个 1-bit state 的声明与更新循环需保持 Python 循环展开

| **难度** | ★★★（321 寄存器 + 18 个辅助函数 + 结构化状态） |

---

### 3. BypassUnit (`designs/BypassUnit/bypass_unit.py`) — ★★☆

| 项目 | 内容 |
|------|------|
| **功能** | 8-lane 旁路网络：按 ptag+ptype 在 w1/w2/w3 写回级与 RF 数据之间做优先级选择 |
| **时序类型** | **纯组合**（0 寄存器） |
| **寄存器数** | **0** |
| **端口** | **160 输入**（3 stage × 8 lane × 4 域 + 8 lane × 2 src × 4 域） · **64 输出**（8 lane × 2 src × 4 域） |
| **JIT `if Wire`** | **14 处**（`_select_stage` 2 处 × 8 lane + `_resolve_src` 4×3=12） |
| **`@function`** | 3 个：`_not1`, `_select_stage`, `_resolve_src` |

#### 旁路优先级结构

```
对每条 lane i、每个 src (srcL/srcR)：

  _resolve_src(src_valid, src_ptag, src_ptype, src_rf_data,
               w1[0..7], w2[0..7], w3[0..7])
  ├── _select_stage(w3[0..7])  →  如果 ptag+ptype 匹配 → hit_w3, data_w3
  ├── _select_stage(w2[0..7])  →  如果 ptag+ptype 匹配 → hit_w2, data_w2
  ├── _select_stage(w1[0..7])  →  如果 ptag+ptype 匹配 → hit_w1, data_w1
  └── 优先级链（更晚的 stage 优先）：
      out_data = data_w3 if hit_w3 else (data_w2 if hit_w2 else (data_w1 if hit_w1 else rf_data))
      out_hit  = hit_w3 | hit_w2 | hit_w1
      out_stage = 3 if hit_w3 else (2 if hit_w2 else (1 if hit_w1 else 0))

  同一 stage 内 lane 优先级：lane 0 > lane 1 > ... > lane 7（先匹配先胜）
```

#### V5 改造方案

| 步骤 | 改造内容 |
|------|---------|
| **签名** | `def build(m: CycleAwareCircuit, domain: CycleAwareDomain, ...)` |
| **Cycle 0（唯一 cycle）** | 全部 160 个输入 `cas()` 包装 |
| **Cycle 0** | 14 处 `if Wire else` 全部替换为 `mux()`：`out_data = mux(hit_w1, data_w1, mux(hit_w2, data_w2, mux(hit_w3, data_w3, rf_data)))` |
| **Cycle 0** | `_select_stage` 内 `take = match & ~has` → `sel_data = mux(take, lane_data, sel_data)` |
| **输出** | 全部组合输出，**无 `domain.next()`** |
| **`@function` 保留** | 3 个 `@function` 保持；内部 `if Wire else` → `mux()` |

**关键难点：**
- `_select_stage` 和 `_resolve_src` 内的条件链必须保持优先级语义
- 替换时注意 `mux(cond, true_val, false_val)` 的参数顺序与 `true_val if cond else false_val` 一致

| **难度** | ★★☆（14 处 `if Wire` → `mux()`，纯组合无时序风险） |

---

## 二、示例设计（designs/examples/）

### 4. counter — ★☆☆ 【单寄存器反馈 · 2 cycle】

| 项目 | 内容 |
|------|------|
| **功能** | 使能可控的上行计数器 |
| **时序类型** | 单寄存器反馈 |
| **寄存器** | 1 个 `m.out("count_q", width=width)`，enable 门控 `+1` |
| **端口** | 1 输入 `enable` · 1 输出 `count` |
| **JIT `if Wire`** | 0 |

#### V5 周期结构

```
┌─── Cycle 0 ─────────────────────┐
│  enable = cas(m.input("enable")) │
│  count = domain.state(width=W)   │
│  m.output("count", count.wire)   │
├─── domain.next() ───────────────┤
├─── Cycle 1 ─────────────────────┐
│  count.set(mux(enable, count+1, count)) │
└─────────────────────────────────┘
```

---

### 5. multiclock_regs — ★☆☆ 【多时钟域 · 各域 2 cycle】

| 项目 | 内容 |
|------|------|
| **功能** | 两个独立时钟域各一个自增计数器 |
| **时序类型** | 2 个独立时钟域，各含 1 个自增寄存器 |
| **寄存器** | 2 个 `m.out()`：`a_count_q`（clk_a 域）、`b_count_q`（clk_b 域） |
| **端口** | 2 clk + 2 rst → 2 输出（`a_count`, `b_count`） |
| **JIT `if Wire`** | 0 |

#### V5 周期结构（每个域）

```
┌─── domain_a Cycle 0 ────────────┐
│  a = domain_a.state(width=W)     │
│  m.output("a_count", a.wire)     │
├─── domain_a.next() ─────────────┤
├─── domain_a Cycle 1 ────────────┐
│  a.set(a + 1)                    │
└──────────────────────────────────┘
（domain_b 同理）
```

**注意：** 多时钟域需在 `build` 内手动 `m.create_domain()` 创建额外域

---

### 6. wire_ops — ★★☆ 【单寄存器 · 2 cycle】

| 项目 | 内容 |
|------|------|
| **功能** | 按 `sel` 选择 `a & b` 或 `a ^ b`，结果打入寄存器输出 |
| **时序类型** | 组合选择 → 单寄存器捕获 |
| **寄存器** | 1 个 `m.out("r")`：存储 mux 结果 |
| **端口** | 3 输入（a, b, sel） · 1 输出（y） |
| **JIT `if Wire`** | **1 处**：`a & b if sel else a ^ b` |

#### V5 周期结构

```
┌─── Cycle 0 ─────────────────────┐
│  a, b, sel = cas(m.input(...))   │
│  result = mux(sel, a & b, a ^ b) │
├─── domain.next() ───────────────┤
├─── Cycle 1 ─────────────────────┐
│  r = domain.cycle(result, name="r") │
│  m.output("y", r.wire)          │
└──────────────────────────────────┘
```

---

### 7. jit_control_flow — ★★☆ 【纯组合 · 单 cycle】

| 项目 | 内容 |
|------|------|
| **功能** | 按 `op` 对 `a, b` 做算术/逻辑运算，再固定轮数 `+1`，输出组合结果 |
| **时序类型** | **纯组合**（0 寄存器） |
| **寄存器** | 0 |
| **端口** | 3 输入（a, b, op） · 1 输出（result） |
| **JIT `if Wire`** | **4 处** `if/elif op == ...` |

#### V5 周期结构

```
┌─── Cycle 0（唯一 cycle）──────────────────────┐
│  a, b, op = cas(m.input(...))                  │
│  r = mux(op==0, a+b, mux(op==1, a-b, ...))    │
│  for _ in range(rounds): r = r + 1    # 展开   │
│  m.output("result", r.wire)                    │
│  无 domain.next()                               │
└────────────────────────────────────────────────┘
```

---

### 8. fifo_loopback — ★☆☆ 【IP 封装 · 无自建寄存器】

| 项目 | 内容 |
|------|------|
| **功能** | `rv_queue` FIFO push/pop 回环测试 |
| **时序类型** | **IP 封装**（`m.rv_queue` 内含寄存器，外部无自建寄存器） |
| **自建寄存器** | 0（FIFO 寄存器在 `rv_queue` IP 内部） |
| **端口** | 3 输入（in_valid, in_data, out_ready） · 3 输出（in_ready, out_valid, out_data） |
| **JIT `if Wire`** | 0 |

#### V5 周期结构

```
┌─── Cycle 0 ──────────────────────────────────┐
│  in_valid, in_data, out_ready = cas(m.input(...))│
│  fifo = m.rv_queue(depth=2, width=W)          │
│  fifo 接口连接（Wire 级，保持不变）             │
│  m.output(...)                                │
│  无 domain.next()（IP 内部自管时序）            │
└──────────────────────────────────────────────┘
```

---

### 9. hier_modules — ★☆☆ 【纯组合 · 单 cycle】

| 项目 | 内容 |
|------|------|
| **功能** | 辅助函数串行 `+1` 共 `stages=3` 次（组合链） |
| **时序类型** | **纯组合**（0 寄存器） |
| **寄存器** | 0 |
| **端口** | 1 输入（x） · 1 输出（y） |

#### V5 周期结构（改为真流水的方案）

```
┌─── Cycle 0 ──────────────┐
│  val = cas(m.input("x"))  │
│  val = val + 1             │
├─── domain.next() ────────┤
├─── Cycle 1 ──────────────┐
│  val = domain.cycle(val)  │
│  val = val + 1             │
├─── domain.next() ────────┤
├─── Cycle 2 ──────────────┐
│  val = domain.cycle(val)  │
│  val = val + 1             │
│  m.output("y", val.wire)  │
└──────────────────────────┘
```

> 注：若保持纯组合语义，则无需 `domain.next()`，仅 `cas()` 包装输入即可

---

### 10. bf16_fmac — ★★★ 【4 级流水线 · 5 cycle】

| 项目 | 内容 |
|------|------|
| **功能** | BF16×BF16 乘加 → FP32 累加器 |
| **时序类型** | **4 级流水线** + 反馈累加器 |
| **寄存器** | **30 个 `m.out()`** 手工管理的流水线寄存器 |
| **端口** | 4 输入（a_in, b_in, acc_in, valid） · 2 输出（result, out_valid） |
| **JIT `if Wire`** | **~20 处**（NaN/Inf/零/符号异常路径） |

#### 实际流水线时序

```
┌─── Cycle 0：Stage 1 解包 ─────────────────────────────────┐
│  a_in, b_in, acc_in, valid = cas(m.input(...))             │
│  解包 BF16 → 指数 e_a/e_b、尾数 m_a/m_b、符号 s_a/s_b     │
│  部分乘积启动；NaN/Inf/Zero 检测                            │
│  ~8 个流水寄存器锁存中间结果                                 │
├─── domain.next() ────────────────────────────────────────┤
├─── Cycle 1：Stage 2 乘法完成 ─────────────────────────────┐
│  完成 8×8 尾数乘 → 16-bit 乘积                             │
│  指数相加 → 乘积指数                                       │
│  ~8 个流水寄存器                                           │
├─── domain.next() ────────────────────────────────────────┤
├─── Cycle 2：Stage 3 对齐加减 ─────────────────────────────┐
│  指数对齐 → 尾数右移                                       │
│  尾数加减（同符号/异符号处理）                               │
│  ~7 个流水寄存器                                           │
├─── domain.next() ────────────────────────────────────────┤
├─── Cycle 3：Stage 4 归一化打包 ───────────────────────────┐
│  前导零检测 → 归一化移位                                    │
│  舍入 → FP32 打包                                          │
│  异常优先级：NaN > Inf > Zero > Normal                      │
│  m.output("result", ...)  m.output("out_valid", ...)       │
│  acc 反馈 → domain.state() 或 domain.cycle()                │
│  ~7 个流水寄存器                                           │
└──────────────────────────────────────────────────────────┘
```

**改造要点：**
- 30 个 `m.out()` → `domain.cycle()` / `domain.state()`
- 3 个 `domain.next()` 分割 4 级流水
- ~20 处 `if Wire else` → `mux()`（异常处理路径需嵌套 `mux`）
- 累加器反馈用 `domain.state()`

---

### 11. digital_filter — ★★☆ 【移位寄存器 + 输出锁存 · 2 cycle】

| 项目 | 内容 |
|------|------|
| **功能** | 参数化 4-tap FIR 滤波器：移位寄存器 + MAC |
| **时序类型** | 移位寄存器链 + 组合 MAC + 输出锁存 |
| **寄存器** | **5 个 `m.out()`**：3 个延迟线 `tap[1..3]` + 1 个输出 `y` + 1 个 `y_valid` |
| **端口** | 2 输入（x_in, x_valid） · 2 输出（y_out, y_valid） |
| **JIT `if Wire`** | 0 |

#### V5 周期结构

```
┌─── Cycle 0：组合读取 + MAC ───────────────────────────────┐
│  x_in, x_valid = cas(m.input(...))                         │
│  tap[0..3] = domain.state(width=W) × 4（含 x_in 即 tap[0]） │
│  acc = Σ(coeff[i] * tap[i])  # 组合 MAC                    │
│  m.output("y_out", acc.wire)                               │
├─── domain.next() ────────────────────────────────────────┤
├─── Cycle 1：移位 + 输出锁存 ─────────────────────────────┐
│  tap[3].set(tap[2]); tap[2].set(tap[1]); tap[1].set(x_in) │
│  y_valid_state.set(x_valid)                                │
└──────────────────────────────────────────────────────────┘
```

---

### 12. digital_clock — ★★★ 【FSM · 2 cycle】

| 项目 | 内容 |
|------|------|
| **功能** | 1Hz 预分频 + 4 模式 FSM (RUN/SET_HOUR/SET_MIN/SET_SEC) + BCD 输出 |
| **时序类型** | **FSM + 多寄存器**（6 个状态寄存器，单 `domain.next()` 分相） |
| **寄存器** | **6 个 `m.out()`**：prescaler, seconds, minutes, hours, mode, blink_cnt |
| **端口** | 3 输入（btn_mode, btn_set, btn_inc） · 5 输出（hours_bcd, minutes_bcd, seconds_bcd, mode, blink） |
| **JIT `if Wire`** | **~22 处**（FSM 状态转换 + BCD 进位链） |
| **`@function`** | 若干 BCD/计时辅助函数 |

#### V5 周期结构

```
┌─── Cycle 0：FSM 次态计算 ────────────────────────────────┐
│  btn_mode, btn_set, btn_inc = cas(m.input(...))           │
│  prescaler, sec, min, hr, mode, blink = domain.state() × 6│
│  tick = (prescaler == 0)  # 1Hz 节拍                      │
│  FSM 次态逻辑（全部 ~22 处 if → mux()）：                   │
│    next_mode = mux(btn_mode_pressed, mode+1, mode)        │
│    next_sec  = mux(tick & is_RUN, sec+1, mux(...))        │
│    ...（进位、设时、BCD 转换）                               │
│  m.output("hours_bcd", ...) 等                            │
├─── domain.next() ────────────────────────────────────────┤
├─── Cycle 1：状态更新 ───────────────────────────────────┐
│  prescaler.set(next_prescaler)                            │
│  sec.set(next_sec); min.set(next_min); hr.set(next_hr)    │
│  mode.set(next_mode); blink.set(next_blink)               │
└──────────────────────────────────────────────────────────┘
```

---

### 13. calculator — ★★★ 【FSM · 2 cycle】

| 项目 | 内容 |
|------|------|
| **功能** | 16-bit 十进制计算器：数字输入/四则运算/等号/全清 |
| **时序类型** | **FSM**（输入模式 → 运算 → 输出） |
| **寄存器** | **5 个 `m.out()`**：lhs, rhs, op, display, input_state |
| **端口** | 2 输入（key_code, key_valid） · 2 输出（display, overflow） |
| **JIT `if Wire`** | **~14 处**（数字/运算符/等号判断） |

#### V5 周期结构

```
┌─── Cycle 0：组合计算 ─────────────────────────────────────┐
│  key_code, key_valid = cas(m.input(...))                   │
│  lhs, rhs, op, display, state = domain.state() × 5        │
│  is_digit = (key_code < 10)                                │
│  is_op = ...; is_eq = ...; is_ac = ...                     │
│  next_lhs = mux(is_digit & is_lhs_mode, lhs*10+key, ...)  │
│  next_display = mux(is_eq, result, mux(is_ac, 0, display))│
│  m.output("display", display.wire)                         │
├─── domain.next() ─────────────────────────────────────────┤
├─── Cycle 1 ──────────────────────────────────────────────┐
│  lhs.set(next_lhs); rhs.set(next_rhs); op.set(next_op)    │
│  display.set(next_display); state.set(next_state)          │
└──────────────────────────────────────────────────────────┘
```

---

### 14. traffic_lights_ce — ★★★ 【FSM · 2 cycle】

| 项目 | 内容 |
|------|------|
| **功能** | 交通灯：4 相倒计时 (EW_GREEN→EW_YELLOW→NS_GREEN→NS_YELLOW) + 紧急覆盖 + 黄灯闪烁 |
| **时序类型** | **FSM + 多寄存器** |
| **寄存器** | **5 个 `m.out()`**：phase, countdown, prescaler, emergency_latch, blink_cnt |
| **端口** | 2 输入（emergency, pause） · 8 输出（ew_red/yellow/green, ns_red/yellow/green, countdown_bcd, phase） |
| **JIT `if Wire`** | **~27 处**（相位判断 + 紧急/暂停逻辑 + BCD） |

#### V5 周期结构

```
┌─── Cycle 0：次态逻辑 ──────────────────────────────────┐
│  emergency, pause = cas(m.input(...))                    │
│  phase, countdown, prescaler, emg, blink = domain.state()×5│
│  ~27 处 if Wire → mux() 链                               │
│  m.output(灯光信号 + BCD + phase)                        │
├─── domain.next() ────────────────────────────────────────┤
├─── Cycle 1 ───────────────────────────────────────────────┐
│  phase.set(next_phase); countdown.set(next_countdown)     │
│  prescaler.set(next_prescaler); emg.set(next_emg)         │
│  blink.set(next_blink)                                    │
└──────────────────────────────────────────────────────────┘
```

---

### 15–16. dodgeball_game — ★★★ 【FSM + VGA · 各 2 cycle】

| 项目 | 内容 |
|------|------|
| **功能** | `lab_final_VGA.py`：VGA 640×480@60Hz 时序；`lab_final_top.py`：3 态游戏 FSM + VGA + 碰撞 |
| **时序类型** | **FSM + 计数器** |
| **寄存器** | VGA: **2** (h_count, v_count) · Top: **14** (game_state, player_x/y, obstacle_x/y, score, tick_div, pixel_div, …) |
| **JIT `if Wire`** | VGA **7 处** + Top **~7 处** = **~14 处** |

#### V5 周期结构（lab_final_VGA）

```
┌─── Cycle 0 ─────────────────────────┐
│  h_count, v_count = domain.state()×2 │
│  组合输出：hsync, vsync, active, x, y │
│  m.output(...)                        │
├─── domain.next() ────────────────────┤
├─── Cycle 1 ─────────────────────────┐
│  h_count.set(mux(h_end, 0, h+1))    │
│  v_count.set(mux(h_end, mux(v_end, 0, v+1), v))│
└──────────────────────────────────────┘
```

#### V5 周期结构（lab_final_top）

```
┌─── Cycle 0 ──────────────────────────────────────────┐
│  btns, switches = cas(m.input(...))                    │
│  14 个 domain.state() 声明                              │
│  game_state FSM (IDLE/PLAY/GAMEOVER) → mux() 链       │
│  碰撞检测、移动逻辑、VGA 子模块实例化（m.new 保留）      │
│  RGB 输出 → mux() 链                                   │
│  m.output(vga_signals + rgb + score + leds)            │
├─── domain.next() ─────────────────────────────────────┤
├─── Cycle 1 ──────────────────────────────────────────┐
│  game_state.set(next_state); player_x.set(next_px); ...│
└──────────────────────────────────────────────────────┘
```

---

### 17. obs_points — ★☆☆ 【单寄存器 · 2 cycle】

| 项目 | 内容 |
|------|------|
| **寄存器** | 1 个 `m.out()`：采样保持 |
| **V5** | `r = domain.state(...)` → Cycle 0 读/输出 → `domain.next()` → Cycle 1 `r.set(x+1)` |

---

### 18. net_resolution_depth_smoke — ★☆☆ 【单寄存器 · 2 cycle】

| 项目 | 内容 |
|------|------|
| **寄存器** | 1 个 `m.out()`：4 级组合加法后锁存 |
| **V5** | `r = domain.state(...)` → Cycle 0 组合 x+4 → `domain.next()` → Cycle 1 `r.set(...)` |

---

### 19–20. mem_rdw_olddata / sync_mem_init_zero — ★☆☆ 【IP 封装 · 无自建寄存器】

| 项目 | 内容 |
|------|------|
| **时序类型** | `m.sync_mem` 同步存储器 IP 封装（IP 内部含寄存器） |
| **自建寄存器** | 0 |
| **V5** | 输入 `cas()` → IP 保持不变 → 输出 `cas()` |

---

### 21. jit_pipeline_vec — ★★☆ 【3 级流水线 · 4 cycle】

| 项目 | 内容 |
|------|------|
| **功能** | `stages=3` 级寄存器流水，每级含 `if sel` 选择 |
| **时序类型** | **3 级流水线** |
| **寄存器** | **6 个 `m.out()`**：每级 1 tag(1b) + 1 data(16b) = 2 寄存器/级 |
| **JIT `if Wire`** | **1 处/级** → `mux(sel, a & b, a ^ b)` |

#### V5 周期结构

```
┌─── Cycle 0 ──────────────────────────────┐
│  a, b, sel = cas(m.input(...))            │
│  compare = (a < b)                        │
│  data0 = mux(sel, a & b, a ^ b)          │
├─── domain.next() ────────────────────────┤
├─── Cycle 1 ──────────────────────────────┐
│  tag1 = domain.cycle(compare, name="t1") │
│  data1 = domain.cycle(data0, name="d1")  │
│  data1 = mux(tag1, data1 | 0xFF, data1)  │
├─── domain.next() ────────────────────────┤
├─── Cycle 2 ──────────────────────────────┐
│  tag2 = domain.cycle(tag1, name="t2")    │
│  data2 = domain.cycle(data1, name="d2")  │
│  data2 = mux(tag2, data2 & 0xFF, data2)  │
├─── domain.next() ────────────────────────┤
├─── Cycle 3 ──────────────────────────────┐
│  tag3 = domain.cycle(tag2, name="t3")    │
│  data3 = domain.cycle(data2, name="d3")  │
│  m.output("lo8", data3.wire[0:8])        │
│  m.output("hi8", data3.wire[8:16])       │
│  m.output("tag_out", tag3.wire)          │
└──────────────────────────────────────────┘
```

**这是 `domain.next()` 流水的最佳示范设计。**

---

### 22–23. xz_value_model_smoke / reset_invalidate_order_smoke — ★☆☆ 【单寄存器 · 2 cycle】

| 项目 | 内容 |
|------|------|
| **寄存器** | 各 1 个 `m.out()` |
| **V5** | `domain.state()` → Cycle 0 读 → `domain.next()` → Cycle 1 `.set()` |

---

### 24. pipeline_builder — ★★☆ 【2 级流水线 · 3 cycle】

| 项目 | 内容 |
|------|------|
| **功能** | `spec.struct` 载荷两级流水 |
| **时序类型** | **2 级流水线**（`m.state()` 管理） |
| **寄存器** | **2 个 `m.state()`**（st0 捕获输入、st1 对 payload.word+1） |
| **端口** | 2 输入（struct） · 2 输出（struct） |
| **`@const`** | 1 个（struct 定义） |

#### V5 周期结构

```
┌─── Cycle 0：输入 ─────────────────┐
│  in_ctrl, in_payload = cas(m.input(...)) │
├─── domain.next() ────────────────┤
├─── Cycle 1：Stage 0 ─────────────┐
│  st0 = domain.cycle(...)          │
├─── domain.next() ────────────────┤
├─── Cycle 2：Stage 1 ─────────────┐
│  word_plus_1 = st0.word + 1       │
│  st1 = domain.cycle(word_plus_1)  │
│  m.output(...)                    │
└──────────────────────────────────┘
```

---

### 25. struct_transform — ★☆☆ 【单级寄存器 · 2 cycle】

| 项目 | 内容 |
|------|------|
| **寄存器** | 1 个 `m.state()`（struct 格式） |
| **V5** | Cycle 0 输入 + 变换 → `domain.next()` → Cycle 1 `domain.cycle()` 锁存 |

---

### 26. module_collection — ★★☆ 【纯组合 · 单 cycle】

| 项目 | 内容 |
|------|------|
| **时序类型** | 纯组合（8 路并行子模块 + 累加） |
| **寄存器** | 0 |
| **V5** | `build` 签名改 V5；`@module` 子模块保留；顶层 `cas()` 包装 + `m.array` 保留 |

---

### 27. interface_wiring — ★☆☆ 【纯组合 · 单 cycle】

| 项目 | 内容 |
|------|------|
| **时序类型** | 纯组合（struct 接口绑定） |
| **寄存器** | 0 |
| **V5** | `build` 签名改 V5；`m.new` 保留 |

---

### 28. instance_map — ★☆☆ 【纯组合 · 单 cycle】

| 项目 | 内容 |
|------|------|
| **时序类型** | 纯组合（3 类子模块实例累加） |
| **寄存器** | 0 |
| **V5** | 同 module_collection |

---

### 29. huge_hierarchy_stress — ★★★ 【层次化 · 叶子 2 cycle】

| 项目 | 内容 |
|------|------|
| **功能** | 32 个 `_node` 实例树（深度=2, fanout=2），叶子含 `pipe` + `acc` 寄存器；顶层含 Cache(4-way, 64-set) |
| **时序类型** | **层次化** — 叶子有寄存器，节点/顶层为组合连接 |
| **寄存器** | 叶子每个含 `m.out("acc")` 1 个 + `m.pipe` 内部寄存器 |
| **`@module`** | `_leaf` + `_node` 各 1 个 |

#### V5 改造

| 层级 | 改造 |
|------|------|
| `_leaf` | `acc` → `domain.state()` + `domain.next()` + `.set()` |
| `_node` | 保持 `@module(structural=True)` + `m.new` |
| 顶层 `build` | 签名改 V5；`cas()` 包装 `seed` 输入；`m.array` + Cache IP 保留 |

---

### 30. fastfwd — ★☆☆ 【纯组合 · 单 cycle】

| 项目 | 内容 |
|------|------|
| **时序类型** | 纯组合直通 |
| **寄存器** | 0 |
| **端口** | 20 输入 · 29 输出 |
| **V5** | `cas()` 包装输入输出即可 |

---

### 31. decode_rules — ★★☆ 【纯组合 · 单 cycle】

| 项目 | 内容 |
|------|------|
| **时序类型** | 纯组合优先级解码 |
| **寄存器** | 0 |
| **JIT `if Wire`** | **6 处**（3 条规则各 2 处 `if hit else`） |
| **V5** | 规则命中链改 `mux()` — **注意保持优先级不反转** |

---

### 32. cache_params — ★☆☆ 【纯组合 · 单 cycle】

| 项目 | 内容 |
|------|------|
| **时序类型** | 纯组合参数推导 |
| **寄存器** | 0 |
| **V5** | `cas()` 包装输入；`@const` 保留 |

---

### 33. bundle_probe_expand — ★☆☆ 【Stub · 单 cycle】

| 项目 | 内容 |
|------|------|
| **时序类型** | 占位（仅声明端口，无逻辑） |
| **V5** | `cas()` 包装输入；probe 基础设施不变 |

---

### 34. boundary_value_ports — ★★☆ 【纯组合 · 单 cycle】

| 项目 | 内容 |
|------|------|
| **时序类型** | 纯组合（3 个 `_lane` 子模块各有 gain/bias/enable 值参数） |
| **寄存器** | 0 |
| **JIT `if Wire`** | **1 处**（`_lane` 内 `if enable else`） |
| **V5** | `_lane` 内 `if` → `mux()`；`build` 签名改 V5；`@module` + `m.new` 保留 |

---

### 35. arith — ★☆☆ 【纯组合 · 单 cycle】

| 项目 | 内容 |
|------|------|
| **时序类型** | 纯组合加法 + 常量配置 |
| **寄存器** | 0 |
| **V5** | `cas()` 包装输入；`@const` 保留 |

---

### 36. issue_queue_2picker — ★★☆ 【队列寄存器 · 2 cycle】

| 项目 | 内容 |
|------|------|
| **功能** | 4 槽移位队列，双 pop 口，单 push 口 |
| **时序类型** | **寄存器队列**（移位 + 仲裁） |
| **寄存器** | **8 个 `m.out()`**：4 slot × (valid + data) |
| **端口** | 4 输入（push/pop 控制 + data） · 5 输出（valid/data + in_ready） |
| **JIT `if Wire`** | **~20 处**（移位/pop/push 条件） |

#### V5 周期结构

```
┌─── Cycle 0：仲裁 + 移位计算 ──────────────────────────────┐
│  push_valid, push_data, pop0_ready, pop1_ready = cas(...)  │
│  slot[0..3] = domain.state() × 8（valid+data 各 4）        │
│  组合逻辑：pop0 取 slot[0], pop1 取 slot[1]                │
│  移位：根据 pop 数量计算 slot[i] 的下一值                    │
│  push：向首个空位写入                                      │
│  全部 ~20 处 if Wire → mux()                               │
│  m.output(pop0_valid/data, pop1_valid/data, in_ready)      │
├─── domain.next() ─────────────────────────────────────────┤
├─── Cycle 1：状态更新 ──────────────────────────────────────┐
│  slot[0].set(next_slot0); ... slot[3].set(next_slot3)      │
└──────────────────────────────────────────────────────────┘
```

---

### 37. trace_dsl_smoke — ★☆☆ 【子模块寄存器 · 2 cycle】

| 项目 | 内容 |
|------|------|
| **时序类型** | 2 个 `leaf` 子模块实例，每个含 1 寄存器 |
| **寄存器** | 2 个（均在 `@module leaf` 内） |
| **V5** | `build` 改 V5 签名；`leaf` 保留 `@module`（`@probe(target=leaf)` 依赖）；`m.new` 保留 |
| **注意** | leaf 内部需：`r = domain.state(...)` → `domain.next()` → `r.set(in_x)` |

---

### 38. npu_node (fm16) — ★★☆ 【FIFO IP + 组合路由 · 单 cycle】

| 项目 | 内容 |
|------|------|
| **功能** | NPU 节点：HBM 注入 + 4 端口双向网络，按 dst 路由 |
| **时序类型** | **4 个 rv_queue IP**（内含寄存器） + 组合路由逻辑 |
| **自建寄存器** | 0（FIFO 在 IP 内部） |
| **JIT `if Wire`** | **~20 处**（路由 dst→port 匹配 + 合并 push） |

#### V5 周期结构

```
┌─── Cycle 0（唯一 cycle）──────────────────────────────────┐
│  hbm_in, port[0..3]_in = cas(m.input(...))                │
│  4 × m.rv_queue(depth=8) → IP 保留                        │
│  路由逻辑：dst mod 4 → push 到目标 FIFO                    │
│  ~20 处 if Wire → mux()                                   │
│  m.output(port[0..3]_out, hbm_out, ...)                   │
│  无 domain.next()（IP 内部自管时序）                        │
└──────────────────────────────────────────────────────────┘
```

---

### 39. sw5809s (fm16) — ★★☆ 【FIFO + RR 仲裁寄存器 · 2 cycle】

| 项目 | 内容 |
|------|------|
| **功能** | 4×4 交叉开关：16 个 VOQ 队列 + round-robin 仲裁 |
| **时序类型** | **16 个 rv_queue IP** + **4 个 RR 指针寄存器** |
| **自建寄存器** | **4 个 `m.out()`**（rr_ptr[0..3]，每个 2-bit） |
| **JIT `if Wire`** | **~52 处**（VOQ push/pop 条件 + RR 仲裁链） |

#### V5 周期结构

```
┌─── Cycle 0：仲裁 + 路由 ──────────────────────────────────┐
│  port[0..3]_in = cas(m.input(...))                        │
│  16 × m.rv_queue() → IP 保留                               │
│  rr_ptr[0..3] = domain.state() × 4                        │
│  对每个输出端口：RR 扫描 4 个 VOQ → 选择非空最优先          │
│  ~52 处 if Wire → mux()                                   │
│  m.output(port[0..3]_out, ...)                            │
├─── domain.next() ─────────────────────────────────────────┤
├─── Cycle 1：RR 指针更新 ─────────────────────────────────┐
│  rr_ptr[i].set(mux(grant_valid, next_rr, rr_ptr[i]))     │
└──────────────────────────────────────────────────────────┘
```

---

### 40. fm16_system — ⊘ 无需迁移

| 项目 | 内容 |
|------|------|
| **类型** | **纯 Python 行为级仿真器**，不使用 pycircuit 硬件构造 |
| **内容** | `class NPUNode`, `class SW5809s`, `class FM16System`, `class SW16System` — 全为 Python 类的功能模型 |
| **结论** | **无需迁移**，不属于硬件设计 |

---

## 优先级与执行顺序

### Phase 1：★☆☆ 简单设计（15 个，预计 1–2 天）

| # | 设计 | 时序类型 | 寄存器 | 要点 |
|---|------|---------|--------|------|
| 1 | counter | 单寄存器 | 1 | `domain.state()` + `domain.next()` + `.set()` |
| 2 | obs_points | 单寄存器 | 1 | 同上 |
| 3 | net_resolution_depth_smoke | 单寄存器 | 1 | 同上 |
| 4 | xz_value_model_smoke | 单寄存器 | 1 | 同上 |
| 5 | reset_invalidate_order_smoke | 单寄存器 | 1 | 同上 |
| 6 | struct_transform | 单级 m.state | 1 | `domain.state()` + `domain.next()` |
| 7 | fifo_loopback | IP 封装 | 0 | `cas()` 包装，IP 不动 |
| 8 | mem_rdw_olddata | IP 封装 | 0 | `cas()` 包装，IP 不动 |
| 9 | sync_mem_init_zero | IP 封装 | 0 | `cas()` 包装，IP 不动 |
| 10 | fastfwd | 纯组合 | 0 | `cas()` 包装 |
| 11 | cache_params | 纯组合 | 0 | `cas()` + `@const` 保留 |
| 12 | arith | 纯组合 | 0 | `cas()` + `@const` 保留 |
| 13 | bundle_probe_expand | Stub | 0 | `cas()` 包装 |
| 14 | interface_wiring | 纯组合 | 0 | 签名改 V5，`m.new` 保留 |
| 15 | instance_map | 纯组合 | 0 | 签名改 V5，`m.array` 保留 |

### Phase 2：★★☆ 中等设计（13 个，预计 3–4 天）

| # | 设计 | 时序类型 | 寄存器 | 核心改动 |
|---|------|---------|--------|----------|
| 1 | wire_ops | 单寄存器 | 1 | 1 处 `if` → `mux()` + `domain.next()` |
| 2 | multiclock_regs | 多时钟 | 2 | 双域 `domain.state()` + 各自 `domain.next()` |
| 3 | hier_modules | 纯组合→可选真流水 | 0→3 | 可选 `domain.cycle()` × `stages` |
| 4 | jit_control_flow | 纯组合 | 0 | 4 处 `if/elif` → `mux()` 嵌套链 |
| 5 | BypassUnit | **纯组合** | **0** | **14 处** `if` → `mux()`；优先级链语义 |
| 6 | digital_filter | 移位寄存器 | 5 | 延迟线 `domain.state()` × 3 + `domain.next()` |
| 7 | **jit_pipeline_vec** | **3 级流水** | **6** | `domain.next()` × 3 循环流水 **（示范设计）** |
| 8 | pipeline_builder | 2 级流水 | 2 | `domain.next()` × 2 + struct `domain.cycle()` |
| 9 | decode_rules | 纯组合 | 0 | 6 处 `if hit` → `mux()`，保持优先级 |
| 10 | module_collection | 纯组合 | 0 | `@module` 子模块保留；`cas()` 规约 |
| 11 | boundary_value_ports | 纯组合 | 0 | `_lane` 内 1 处 `if` → `mux()` |
| 12 | npu_node | FIFO IP | 0 | ~20 处路由 `if` → `mux()`，rv_queue 保留 |
| 13 | trace_dsl_smoke | 子模块寄存器 | 2 | `@module` leaf 内 `domain.state()` + `domain.next()` |

### Phase 3：★★★ 复杂设计（9 个，预计 5–8 天）

| # | 设计 | 时序类型 | 寄存器 | 核心挑战 |
|---|------|---------|--------|----------|
| 1 | **bf16_fmac** | **4 级流水** | **30** | 3 × `domain.next()` 分割流水 + ~20 处 `mux()` + 异常路径 |
| 2 | **IssueQueue** | 单周期状态机 | **321** | 大量 `domain.state()` + struct 状态 + 18 个 `@function` |
| 3 | **issue_queue_2picker** | 队列寄存器 | **8** | ~20 处 `if Wire` → `mux()` 移位逻辑 |
| 4 | **sw5809s** | FIFO+RR | **4** | **~52 处** `if Wire` → `mux()`；16 个 VOQ |
| 5 | **calculator** | FSM | 5 | ~14 处 `if Wire` → `mux()` FSM 链 |
| 6 | **digital_clock** | FSM | 6 | ~22 处 `if Wire` → `mux()` + BCD 进位 |
| 7 | **traffic_lights_ce** | FSM | 5 | **~27 处** `if Wire` → `mux()` |
| 8 | **dodgeball_game** (2 files) | FSM+VGA | **16** | ~14 处 `if Wire` + VGA 时序 + 碰撞 |
| 9 | **huge_hierarchy_stress** | 层次化 | ~32+ | `@module` 叶子 `domain.state()` + Cache IP 接口 |

---

## 通用改造模板

### 模板 A：纯组合设计（无寄存器）

```python
def build(m: CycleAwareCircuit, domain: CycleAwareDomain, ...) -> None:
    # Cycle 0: inputs
    a = cas(domain, m.input("a", width=W), cycle=0)
    b = cas(domain, m.input("b", width=W), cycle=0)

    # Cycle 0: combinational logic
    result = mux(sel, a + b, a - b)

    m.output("out", result.wire)
```

### 模板 B：单寄存器反馈（计数器/累加器）

```python
def build(m: CycleAwareCircuit, domain: CycleAwareDomain, ...) -> None:
    # Cycle 0: inputs + state
    enable = cas(domain, m.input("en", width=1), cycle=0)
    count = domain.state(width=8, reset_value=0, name="count")

    m.output("count", count.wire)

    # Cycle 1: update
    domain.next()
    count.set(mux(enable, count + 1, count))
```

### 模板 C：多级流水

```python
def build(m: CycleAwareCircuit, domain: CycleAwareDomain, ...) -> None:
    # Cycle 0: inputs
    data = cas(domain, m.input("data", width=W), cycle=0)
    valid = cas(domain, m.input("valid", width=1), cycle=0)

    # Cycle 0 → 1: Stage 1
    s1_data = data + 1
    domain.next()
    s1_reg = domain.cycle(s1_data, name="s1")
    s1_valid = domain.cycle(valid, name="s1_valid")

    # Cycle 1 → 2: Stage 2
    s2_data = s1_reg * 2
    domain.next()
    s2_reg = domain.cycle(s2_data, name="s2")

    m.output("out", s2_reg)
```

### 模板 D：FSM（状态机）

```python
def build(m: CycleAwareCircuit, domain: CycleAwareDomain, ...) -> None:
    # Cycle 0: inputs + state
    cmd = cas(domain, m.input("cmd", width=2), cycle=0)
    state = domain.state(width=2, reset_value=0, name="fsm")

    IDLE, RUN, DONE = 0, 1, 2

    # Next-state logic (combinational)
    is_idle = state == cas(domain, m.const(IDLE, width=2), cycle=0)
    is_run  = state == cas(domain, m.const(RUN,  width=2), cycle=0)
    start   = cmd == cas(domain, m.const(1, width=2), cycle=0)

    next_state = state  # default: hold
    next_state = mux(is_idle & start, cas(domain, m.const(RUN, width=2), cycle=0), next_state)
    next_state = mux(is_run,          cas(domain, m.const(DONE, width=2), cycle=0), next_state)

    m.output("state", state.wire)

    # Cycle 1: update
    domain.next()
    state.set(next_state)
```

---

## 验证策略（总则）

每个设计改造后**必须**通过以下三关：

1. **MLIR 结构对比**：新旧版 `pyc.reg` 数量一致、端口签名（`arg_names` / `result_names`）一致
2. **功能仿真**（如有 `tb_*.py`）：全部 `t.expect` 通过，无新增 FAIL
3. **性能基准**（如有 `emulate_*.py`）：100K 周期吞吐无回归（±5%）

---

## 各设计现有验证资产 & 升级后验证计划

> **图例**  
> - TB = `tb_*.py` testbench（`@testbench` + `Tb` API）  
> - CFG = `*_config.py`（含 `DEFAULT_PARAMS` / `TB_PRESETS`）  
> - EMU = `emulate_*.py` / `test_*.py`（RTL 仿真/基准）  
> - SVA = `t.sva_assert` 断言  
> - E(N) = N 次 `t.expect` 调用

### 一、大型设计

#### 1. RegisterFile

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_regfile.py`：10 个周期序列，每周期 10 读口 `t.expect`，共 **E(100)** |
| CFG | 无（参数内联于 TB） |
| EMU | `emulate_regfile.py`：ctypes RTL 仿真——功能正确性 29 项 + **100K 周期性能基准** |
| SVA | 无 |

**升级后验证计划：**
- [x] **已验证**：V5 改造后 29/29 功能测试 PASS，100K 仿真 57.4 Kcycles/s（无回归）
- [ ] MLIR 对比：`pyc.reg` 数量 = 256（128×2 bank），端口签名不变
- [ ] TB 编译：`compile_cycle_aware(build, name="tb_regfile_top", eager=True)` 出 MLIR 成功

---

#### 2. IssueQueue

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_issq.py`：Python 黄金模型 `_tb_step` 生成最多 512 周期入队/发射轨迹；**E(1)** `occupancy` 初始为 0 |
| CFG | `issq_config.py`：`IqCfg` 规格 + `TbState`/`TbUop` 参考模型（无 `DEFAULT_PARAMS`/`TB_PRESETS`） |
| EMU | 无 |
| SVA | 无 |

**升级后验证计划：**
- [ ] MLIR 对比：entry 数 × (valid+age+ready+ptag+payload) 寄存器总数不变
- [ ] TB 编译通过，占用量为 0 的初始检查仍 PASS
- [ ] **新增**：在 TB 中对 `issued_total` 添加终态 `t.expect`，确认发射总量 = 入队总量

---

#### 3. BypassUnit

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_bypass_unit.py`：8 手写场景 + 系统化 sweep（184 周期），**E(11776)** + **SVA(1344)** |
| CFG | 无 |
| EMU | 无 |
| SVA | `t.sva_assert`：同 stage 禁止双命中 |

**升级后验证计划：**
- [ ] MLIR 对比：纯组合设计，`pyc.reg` = 0，端口数不变
- [ ] TB 全部 11776 次 `t.expect` 通过
- [ ] SVA 全部 1344 条 `t.sva_assert` 通过
- [ ] **关键**：`if Wire else` → `mux()` 改造后，每个旁路优先级链须逐一验证

---

### 二、示例设计

#### 4. counter

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_counter.py`：**E(5)**（`count` 每周期 +1） |
| CFG | `counter_config.py`：`DEFAULT_PARAMS = {width: 8}`，smoke/nightly |

**升级后验证计划：**
- [ ] TB 5 次 `t.expect` 全部 PASS
- [ ] MLIR：1 个 `pyc.reg`（计数器），端口 `clk/rst/enable → count`

---

#### 5. multiclock_regs

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_multiclock_regs.py`：双时钟驱动，**E(0)**（仅驱动无检查） |
| CFG | `multiclock_regs_config.py`：`DEFAULT_PARAMS = {}`，smoke/nightly |

**升级后验证计划：**
- [ ] MLIR 对比：2 个 `pyc.reg`（`a_q`/`b_q`），4 个 clock/reset 端口
- [ ] **新增**：在 TB 中追加 `t.expect("a_count", 3, at=5)` 等基本计数检查

---

#### 6. wire_ops

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_wire_ops.py`：**E(1)** |
| CFG | `wire_ops_config.py`：smoke/nightly |

**升级后验证计划：**
- [ ] TB `t.expect` PASS
- [ ] **关键**：`if sel else` → `mux(sel, a & b, a ^ b)` 的语义等价

---

#### 7. jit_control_flow

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_jit_control_flow.py`：**E(1)**（`result == 7`） |
| CFG | `jit_control_flow_config.py`：`rounds: 4` |

**升级后验证计划：**
- [ ] TB 组合结果 `t.expect` PASS
- [ ] **关键**：多分支 `if/elif op ==` → `mux()` 嵌套链的等价验证

---

#### 8. fifo_loopback

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_fifo_loopback.py`：**E(0)**（仅驱动） |
| CFG | `fifo_loopback_config.py`：`depth: 2` |

**升级后验证计划：**
- [ ] MLIR 编译通过（`m.rv_queue` IP 接口不变）
- [ ] **新增**：追加 `t.expect("out_data", ...)` 验证 FIFO 先入先出行为

---

#### 9. hier_modules

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_hier_modules.py`：**E(1)** |
| CFG | `hier_modules_config.py`：`width`/`stages` |

**升级后验证计划：**
- [ ] TB `t.expect` PASS
- [ ] 若改为真流水（`domain.cycle()` × stages），MLIR `pyc.reg` 数应 = `stages`

---

#### 10. bf16_fmac

| 验证资产 | 详情 |
|---------|------|
| TB | 无标准 `tb_*.py`；有 `test_bf16_fmac.py`：ctypes RTL 100 用例，BF16 乘加与 Python 对比（≤2% 误差） |
| CFG | 无 |
| EMU | 无 |

**升级后验证计划：**
- [ ] `test_bf16_fmac.py` 100 用例全部 PASS（误差阈值不变）
- [ ] MLIR：4 级流水寄存器总数不变
- [ ] **关键**：50+ 处 `if Wire` → `mux()` 改造后须全量回归

---

#### 11. digital_filter

| 验证资产 | 详情 |
|---------|------|
| TB | 无标准 `tb_*.py` |
| EMU | `emulate_filter.py`：4-tap FIR RTL 终端动画 |

**升级后验证计划：**
- [ ] MLIR：`TAPS-1` 个延迟寄存器 + 1 个输出寄存器 + 1 个 valid 寄存器
- [ ] `emulate_filter.py` 运行无崩溃
- [ ] **新增**：编写 `tb_digital_filter.py`，对已知输入序列验证 FIR 输出

---

#### 12. digital_clock

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_digital_clock.py`：**E(1)**（复位后 `seconds_bcd`） |
| CFG | `digital_clock_config.py`：`clk_freq: 50_000_000` |
| EMU | `emulate_digital_clock.py`：RTL 动画时钟 |

**升级后验证计划：**
- [ ] TB `t.expect` PASS
- [ ] `emulate_digital_clock.py` 运行无崩溃
- [ ] **关键**：FSM `if` 链 → `mux()` 链须保持状态转换语义

---

#### 13. calculator

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_calculator.py`：**E(1)**（`display`） |
| CFG | `calculator_config.py`：`DEFAULT_PARAMS = {}` |
| EMU | `emulate_calculator.py`：RTL 动画计算器 |

**升级后验证计划：**
- [ ] TB `t.expect` PASS
- [ ] `emulate_calculator.py` 运行无崩溃
- [ ] **新增**：在 TB 中追加 `1+2=3`、`9*9=81` 等算术序列检查

---

#### 14. traffic_lights_ce

| 验证资产 | 详情 |
|---------|------|
| TB | 无标准 `tb_*.py` |
| EMU | `emulate_traffic_lights.py`：RTL 可视化（含 `stimuli/` 激励） |

**升级后验证计划：**
- [ ] `emulate_traffic_lights.py` 运行无崩溃
- [ ] **新增**：编写 `tb_traffic_lights_ce.py`，验证相位切换、紧急模式覆盖、倒计时归零

---

#### 15–16. dodgeball_game (lab_final_VGA + lab_final_top)

| 验证资产 | 详情 |
|---------|------|
| TB | 无标准 `tb_*.py` |
| EMU | `emulate_dodgeball.py`：RTL 游戏可视化（含 `stimuli/`） |

**升级后验证计划：**
- [ ] `emulate_dodgeball.py` 运行无崩溃
- [ ] MLIR 编译通过
- [ ] **新增**：编写 `tb_lab_final_VGA.py`，验证 hsync/vsync 时序（640×480@60Hz 标准值）

---

#### 17. obs_points

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_obs_points.py`：**E(6)**（`y`/`q` 的 pre/post 观测点） |
| CFG | `obs_points_config.py`：`width: 8` |

**升级后验证计划：**
- [ ] TB 6 次 `t.expect` 全部 PASS

---

#### 18. net_resolution_depth_smoke

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_net_resolution_depth_smoke.py`：**E(4)** |
| CFG | `net_resolution_depth_smoke_config.py`：`width: 8` |

**升级后验证计划：**
- [ ] TB 4 次 `t.expect` PASS

---

#### 19. mem_rdw_olddata

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_mem_rdw_olddata.py`：**E(2)**（同址读写返回旧值，再读新值） |
| CFG | `mem_rdw_olddata_config.py`：`depth/data_width/addr_width` |

**升级后验证计划：**
- [ ] TB 2 次 `t.expect` PASS（旧数据语义）

---

#### 20. sync_mem_init_zero

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_sync_mem_init_zero.py`：**E(2)**（读未写地址应为 0） |
| CFG | `sync_mem_init_zero_config.py` |

**升级后验证计划：**
- [ ] TB 2 次 `t.expect` PASS

---

#### 21. jit_pipeline_vec

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_jit_pipeline_vec.py`：**E(0)**（仅驱动 `a/b/sel`） |
| CFG | `jit_pipeline_vec_config.py`：`stages: 3` |

**升级后验证计划：**
- [ ] MLIR：`pyc.reg` 数 = `stages`（tag 链）+ `stages`（data 链）
- [ ] **新增**：在 TB 中追加 `t.expect` 验证 `stages` 拍延迟后的 `lo8` 输出值

---

#### 22. xz_value_model_smoke

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_xz_value_model_smoke.py`：**E(4)** |
| CFG | `xz_value_model_smoke_config.py`：`width: 8` |

**升级后验证计划：**
- [ ] TB 4 次 `t.expect` PASS

---

#### 23. reset_invalidate_order_smoke

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_reset_invalidate_order_smoke.py`：**E(4)** |
| CFG | `reset_invalidate_order_smoke_config.py`：`width: 8` |

**升级后验证计划：**
- [ ] TB 4 次 `t.expect` PASS

---

#### 24. pipeline_builder

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_pipeline_builder.py`：**E(1)**（`out_ctrl_valid` 流水级差） |
| CFG | `pipeline_builder_config.py`：`width: 32` |

**升级后验证计划：**
- [ ] TB `t.expect` PASS
- [ ] MLIR：2 级流水寄存器数不变

---

#### 25. struct_transform

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_struct_transform.py`：**E(2)**（bundle 位域变换） |
| CFG | `struct_transform_config.py`：`width: 32` |

**升级后验证计划：**
- [ ] TB 2 次 `t.expect` PASS

---

#### 26. module_collection

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_module_collection.py`：**E(1)**（`acc` 规约） |
| CFG | `module_collection_config.py`：`width`/`lanes` |

**升级后验证计划：**
- [ ] TB `t.expect` PASS
- [ ] `m.array` 子模块实例数不变

---

#### 27. interface_wiring

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_interface_wiring.py`：**E(2)** |
| CFG | `interface_wiring_config.py`：`width: 16` |

**升级后验证计划：**
- [ ] TB 2 次 `t.expect` PASS

---

#### 28. instance_map

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_instance_map.py`：**E(4)** |
| CFG | `instance_map_config.py`：`width: 32` |

**升级后验证计划：**
- [ ] TB 4 次 `t.expect` PASS

---

#### 29. huge_hierarchy_stress

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_huge_hierarchy_stress.py`：**E(0)**（应力测试，仅驱动 `seed`） |
| CFG | `huge_hierarchy_stress_config.py`：`SIM_TIER: "heavy"`，`module_count/hierarchy_depth/fanout/cache_ways/cache_sets` |

**升级后验证计划：**
- [ ] MLIR 编译通过（深层次 + Cache 实例化无报错）
- [ ] `pyc.reg` 总数不变
- [ ] **新增**：追加 `t.expect("out", ...)` 在固定 `seed` 下对 `out` 做 golden 比对

---

#### 30. fastfwd

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_fastfwd.py`：**E(1)**（`pkt_in_bkpr`） |
| CFG | `fastfwd_config.py`：`DEFAULT_PARAMS = {}` |

**升级后验证计划：**
- [ ] TB `t.expect` PASS

---

#### 31. decode_rules

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_decode_rules.py`：**E(2)**（`op`/`len`） |
| CFG | `decode_rules_config.py` |

**升级后验证计划：**
- [ ] TB 2 次 `t.expect` PASS
- [ ] **关键**：规则 `if hit else` → `mux()` 链优先级不能反转

---

#### 32. cache_params

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_cache_params.py`：**E(3)**（`tag`/`line_words`/`tag_bits`） |
| CFG | `cache_params_config.py`：`ways/sets/line_bytes/addr_width/data_width` |

**升级后验证计划：**
- [ ] TB 3 次 `t.expect` PASS

---

#### 33. bundle_probe_expand

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_bundle_probe_expand.py`：**E(4)**（bundle 展开 pre/post） |
| CFG | `bundle_probe_expand_config.py` |

**升级后验证计划：**
- [ ] TB 4 次 `t.expect` PASS

---

#### 34. boundary_value_ports

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_boundary_value_ports.py`：**E(1)** |
| CFG | `boundary_value_ports_config.py`：`width: 32` |

**升级后验证计划：**
- [ ] TB `t.expect` PASS
- [ ] `_lane` 子模块 `if enable else` → `mux()` 验证

---

#### 35. arith

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_arith.py`：**E(3)**（`sum/lane_mask/acc_width`） |
| CFG | `arith_config.py`：`lanes/lane_width` |

**升级后验证计划：**
- [ ] TB 3 次 `t.expect` PASS

---

#### 36. issue_queue_2picker

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_issue_queue_2picker.py`：**E(1)**（`in_ready` backpressure） |
| CFG | `issue_queue_2picker_config.py` |

**升级后验证计划：**
- [ ] TB `t.expect` PASS
- [ ] **关键**：队列移位逻辑 `if pop else` → `mux()` 等价验证

---

#### 37. trace_dsl_smoke

| 验证资产 | 详情 |
|---------|------|
| TB | `tb_trace_dsl_smoke.py`：**E(12)**（双输出 trace 多周期 pre/post） |
| CFG | `trace_dsl_smoke_config.py`：smoke `timeout: 16, finish: 3` |

**升级后验证计划：**
- [ ] TB 12 次 `t.expect` PASS
- [ ] **注意**：`leaf` 必须保留 `@module`（`@probe(target=leaf)` 依赖） |

---

#### 38. npu_node (fm16)

| 验证资产 | 详情 |
|---------|------|
| TB | 无 |
| EMU | `fm16_system.py`（系统级整合脚本） |

**升级后验证计划：**
- [ ] MLIR 编译通过
- [ ] **新增**：编写 `tb_npu_node.py`，验证单端口 push/pop 数据一致性

---

#### 39. sw5809s (fm16)

| 验证资产 | 详情 |
|---------|------|
| TB | 无 |
| EMU | 共享 `fm16_system.py` |

**升级后验证计划：**
- [ ] MLIR 编译通过
- [ ] **新增**：编写 `tb_sw5809s.py`，验证 RR 仲裁公平性（每端口等概率获得授权）

---

## 验证缺口汇总 & 补充测试计划

以下设计在升级前**缺少充分的功能验证**，改造时应同步补充：

| 设计 | 现有验证 | 需补充 |
|------|---------|--------|
| multiclock_regs | TB 仅驱动，E(0) | 追加计数值 `t.expect` |
| fifo_loopback | TB 仅驱动，E(0) | 追加 FIFO 读出 `t.expect` |
| jit_pipeline_vec | TB 仅驱动，E(0) | 追加延迟后输出 `t.expect` |
| huge_hierarchy_stress | TB 仅驱动，E(0) | 追加固定 seed 输出 golden |
| digital_filter | 无 TB | 新建 `tb_digital_filter.py` |
| traffic_lights_ce | 无 TB | 新建 `tb_traffic_lights_ce.py` |
| dodgeball_game | 无 TB | 新建 `tb_lab_final_VGA.py` |
| fm16 (npu_node) | 无 TB | 新建 `tb_npu_node.py` |
| fm16 (sw5809s) | 无 TB | 新建 `tb_sw5809s.py` |

---

## 自动化回归脚本

改造完成后，应创建一键回归脚本 `scripts/regress_v5.sh`：

```bash
#!/bin/bash
set -e
PYTHONPATH=pyCircuit/compiler/frontend

echo "=== Phase 1: MLIR 编译检查 ==="
for design in designs/RegisterFile/regfile.py \
              designs/IssueQueue/issq.py \
              designs/BypassUnit/bypass_unit.py \
              designs/examples/*/[!t]*.py; do
    echo "  Compiling $design ..."
    python3 "$design" > /dev/null 2>&1
done

echo "=== Phase 2: Testbench 编译 ==="
for tb in designs/*/tb_*.py designs/examples/*/tb_*.py; do
    [ -f "$tb" ] || continue
    echo "  Compiling $tb ..."
    python3 "$tb" > /dev/null 2>&1
done

echo "=== Phase 3: Emulation 烟雾 ==="
for emu in designs/RegisterFile/emulate_regfile.py; do
    echo "  Running $emu ..."
    python3 "$emu"
done

echo "ALL PASSED"
```

---

**Copyright (C) 2024-2026 PyCircuit Contributors**
