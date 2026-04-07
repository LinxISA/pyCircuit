# PyCircuit V5 编程规范

**版本：5.0**（统一合并自 API 参考 v4.0 与编程教程 v3.0）

作者：Liao Heng

> **包名**：Python 包为 **`pycircuit`**（全小写）。不要使用 `pyCircuit` 作为 import 目标。
> **并存风格**：pyc4.0 的 `@module` / `Circuit` 流程仍是官方主路径之一，见 `docs/tutorial/unified-signal-model.md` 与 `docs/FRONTEND_API.md`。

---

## 目录

1. [概述](#概述)
2. [信号类型纪律（Non-Negotiable）](#信号类型纪律non-negotiable)
3. [核心组件](#核心组件)
   - [CycleAwareCircuit](#cycleawarecircuit)
   - [CycleAwareDomain](#cycleawaredomain)
   - [CycleAwareSignal](#cycleawaresignal)
4. [Forward Signal（domain.signal()）](#forward-signaldomainsignal)
5. [全局函数](#全局函数)
6. [周期管理](#周期管理)
7. [自动周期平衡](#自动周期平衡)
8. [编程风格](#编程风格)
9. [模块签名规范](#模块签名规范)
10. [子模块调用规范（六步法）](#子模块调用规范六步法)
11. [层次化 MLIR 发射](#层次化-mlir-发射)
12. [仿真与测试（CycleAwareTb）](#仿真与测试cycleawaretb)
13. [编译入口 compile_cycle_aware()](#编译入口-compile_cycle_aware)
14. [RTL 编译流程（Python → MLIR → Verilog）](#rtl-编译流程python--mlir--verilog)
15. [编程范例](#编程范例)
16. [最佳实践](#最佳实践)
17. [附录：API 参考表](#附录api-参考表)

---

## 概述

PyCircuit 是一个基于 Python 的硬件描述语言（HDL）框架，专为数字电路设计而创建。V5 核心特性：

- **周期感知信号（CycleAwareSignal）**：唯一的信号类型，每个信号都携带周期信息
- **多时钟域支持**：独立管理多个时钟域
- **自动周期平衡**：自动插入 DFF 对齐信号时序
- **层次化组合**：`domain.call()` + `submodule_input()` + `wire_of()` 实现可组合模块
- **JIT 编译**：Python 源码编译为 MLIR 硬件描述
- **层次化编译**：`compile_cycle_aware(..., hierarchical=True)` 保留 `domain.call()` 边界为独立 MLIR 模块
- **仿真**：`CycleAwareTb` 封装 `Tb` 实现周期感知测试台

### 导入

```python
from pycircuit import (
    CycleAwareCircuit,    # V5 顶层电路
    CycleAwareDomain,     # 周期感知时钟域
    CycleAwareSignal,     # 周期感知信号（唯一信号类型）
    cas,                  # 将 Wire 包装为 CycleAwareSignal
    compile_cycle_aware,  # V5 编译入口
    mux,                  # 多路选择器
    submodule_input,      # 双模输入辅助函数
    wire_of,              # 安全提取 Wire（仅用于 m.output()）
)
```

---

## 信号类型纪律（Non-Negotiable）

PyCircuit V5 强制使用单一信号类型。以下规则**不可违反**：

| 规则 | 说明 |
|------|------|
| **所有信号都是 `CycleAwareSignal`** | 设计中流动的每一个值都必须是 `CycleAwareSignal`（或 `ForwardSignal`，其委托给 CAS）。不存在其他信号表达方式。 |
| **`domain.state()` 已移除**（内部重命名为 `domain._state()`） | 不要使用 `domain.state()`。使用 `domain.signal()` + `<<=` 代替。 |
| **`.wire` / `.w` 已移除**（属性已从 CAS/ForwardSignal/StateSignal 删除） | 不要在设计逻辑中读取 `.wire`。所有算术、比较、mux、切片运算直接在 `CycleAwareSignal` 上操作。 |
| **`wire_of()` 是唯一提取 Wire 的方式** | 仅在 `m.output()` 边界调用 `wire_of(sig)` 获取裸 `Wire`。 |
| **`cas()` 包装裸 Wire** | 用 `cas(domain, m.input(...), cycle=0)` 或 `submodule_input()` 将输入端口转为 CAS。 |
| **输出 dict 存 CycleAwareSignal** | 子模块返回信号时，dict 值必须是 `CycleAwareSignal`（保留 cycle provenance）。 |

```python
# ✅ 正确
pc = domain.signal(width=64, reset_value=0, name="pc")
enable = cas(domain, m.input("en", width=1), cycle=0)
result = pc + enable           # CAS + CAS → CAS（自动对齐）
m.output("pc", wire_of(pc))    # wire_of() 仅在边界使用

# ❌ 禁止
st = domain.state(...)                   # REMOVED — use domain.signal()
raw = pc.wire                            # REMOVED — property no longer exists
m.output("pc", pc.wire)                  # 使用 wire_of(pc)
outs["result"] = result.wire             # 存 CAS，不存 Wire
x = a.wire + b.wire                      # 直接在 CAS 上操作
```

---

## 核心组件

### CycleAwareCircuit

`Circuit` 的子类，用于 V5 周期感知设计。

```python
m = CycleAwareCircuit("my_circuit")
```

| 方法 | 说明 |
|------|------|
| `create_domain(name, *, frequency_desc="", reset_active_high=False)` | 创建 `CycleAwareDomain` |
| `input(name, width)` | 创建输入端口（返回 `Wire`——需用 `cas()` 或 `submodule_input()` 包装） |
| `output(name, wire)` | 注册模块输出（传入 `wire_of(signal)`） |
| `const(value, width)` | 创建常量 `Wire`（用 `cas()` 包装后参与表达式） |
| `emit_mlir()` | 生成 MLIR 字符串。`hierarchical=True` 编译时，输出包含所有子模块的多模块 `Design`。 |

### CycleAwareDomain

管理特定时钟域的周期状态。

```python
domain = m.create_domain("clk")
```

| 方法 | 说明 |
|------|------|
| `signal(*, width, reset_value=0, name="")` | **前向声明寄存器**——创建状态信号的唯一方式。返回 `ForwardSignal`。 |
| `next()` | 推进当前逻辑周期 +1 |
| `prev()` | 回退当前逻辑周期 -1 |
| `push()` | 将当前周期压栈保存 |
| `pop()` | 从栈中恢复周期（必须与 `push` 配对） |
| `call(fn, *, inputs=None, **kwargs)` | 调用子模块并自动 push/pop 隔离。**扁平模式**（默认）：内联子模块逻辑。**层次化模式**：编译子模块为独立 `func.func` 并发射 `pyc.instance`。 |
| `create_signal(name, *, width)` | 创建输入端口（`Wire`）；需用 `cas()` 包装 |
| `create_const(value, *, width, name="")` | 创建常量 `Wire` |
| `create_reset()` | 返回复位信号（**i1** `Wire`） |
| `cycle_index` | 属性：当前逻辑周期索引 |

> **禁止**：`domain.state()` 已从 V5 公开 API 移除。使用 `domain.signal()` + `<<=` 代替。

**多时钟域示例：**

```python
m = CycleAwareCircuit("soc")
cpu_clk = m.create_domain("CPU_CLK", frequency_desc="100MHz CPU clock")
rtc_clk = m.create_domain("RTC_CLK", frequency_desc="1Hz RTC domain")
```

### CycleAwareSignal

V5 中**唯一的信号类型**。每个信号包含底层硬件线网（内部管理）、周期（cycle）、时钟域（domain）。

**属性：**

| 属性 | 说明 |
|------|------|
| `cycle` | 当前逻辑周期索引 |
| `domain` | 关联的 `CycleAwareDomain` |
| `name` | 调试名称 |
| `signed` | 底层线网是否有符号 |

> **注意**：`.wire` 和 `.w` 在内部存在但**绝不允许**在设计代码中使用。仅在 `m.output()` 边界调用 `wire_of()`。

**运算符重载**——所有标准 Python 运算符均已重载，自动进行周期对齐：

```python
# 算术——所有输入/输出均为 CycleAwareSignal
result = a + b
result = a - b
result = a * b

# 位运算
result = a & b
result = a | b
result = a ^ b
result = ~a

# 比较
result = a == b
result = a < b
result = a > b
result = a <= b
result = a >= b

# 切片/索引（返回 CycleAwareSignal）
low_byte = data[0:8]
bit5     = data[5]
```

**信号方法：**

| 方法 | 说明 |
|------|------|
| `select(true_val, false_val)` | 条件选择（mux） |
| `trunc(width)` | 截断到 width 位 |
| `zext(width)` | 零扩展到 width 位 |
| `sext(width)` | 符号扩展到 width 位 |
| `slice(high, low)` | 提取位片段 |
| `[i]` 或 `[lo:hi]` | 位索引 / 切片（返回 CAS） |
| `named(name)` | 添加调试名称 |
| `as_signed()` | 标记为有符号 |
| `as_unsigned()` | 标记为无符号 |

---

## Forward Signal（domain.signal()）

`domain.signal()` 是**唯一**创建寄存器/状态的方式。核心思想：**先声明后赋值，编译器根据读写周期差自动推导寄存器**。

### 基本模式

```python
# 1. 声明（cycle 0）：Q 端立即可用
counter = domain.signal(width=8, reset_value=0, name="counter")

# 2. 组合逻辑（cycle 0）：直接在 CAS 上运算
count_next = (counter + 1) if enable else counter
m.output("count", wire_of(counter))

# 3. domain.next() → cycle 1
domain.next()

# 4. 赋值：连接 D 端（write cycle=1 > read cycle=0 → 推导出反馈寄存器）
counter <<= count_next
```

### 参数

```python
sig = domain.signal(*, width: int, reset_value: int = 0, name: str = "") -> ForwardSignal
```

| 参数 | 说明 |
|------|------|
| `width` | 位宽（keyword-only，必需） |
| `reset_value` | 复位值（默认 `0`） |
| `name` | 调试 / Verilog 名称 |

**返回**：`ForwardSignal`——参与所有 CAS 表达式。

### 赋值方式

```python
sig <<= expression                          # 无条件赋值
sig.assign(expression, when=condition)       # 条件赋值（寄存器使能）
```

### 周期推导规则

| 读周期 | 写周期 (`<<=`) | 推导结果 |
|--------|---------------|----------|
| 0 | 1 | 反馈寄存器（DFF）——最常见 |
| 0 | 0 | 组合赋值（无寄存器） |
| 0 | 2+ | 多级流水反馈（罕见） |
| N | < N | 编译错误：不能向过去赋值 |

---

## 全局函数

### cas()

将裸 `Wire`（来自 `m.input()`）包装为 `CycleAwareSignal`。这是 Circuit I/O 层与 CAS 类型系统之间的桥梁。

```python
x = cas(domain, m.input("x", width=8), cycle=0)
```

### submodule_input()

双模输入解析——V5 内置 API。当 `io` dict 包含该 key 时，直接返回父模块的 CAS 信号（保留 cycle）；否则创建 `m.input()` 端口用于独立编译。

```python
from pycircuit import submodule_input

pc = submodule_input(inputs, "pc", m, domain, prefix="fe", width=32)
```

| 参数 | 说明 |
|------|------|
| `io` | 父模块传入的 `inputs` dict（或独立模式下为 `None`） |
| `key` | dict 中的信号名 |
| `m` | Circuit 对象 |
| `domain` | 时钟域 |
| `prefix` | 独立模式下端口名的前缀 |
| `width` | 位宽 |
| `cycle` | 独立模式下 `m.input()` 的 cycle 标签（默认 0） |

| `inputs` 状态 | 行为 | 结果 |
|---------------|------|------|
| `None`（独立） | 创建 `m.input(f"{prefix}_{key}", width=W)` 并用 `cas()` 包装 | 新输入端口（CAS） |
| `dict` 包含 `key` | 直接返回 `inputs[key]` | 父模块的 CAS（cycle 保持） |
| `dict` 不含 `key` | 回退到 `m.input()` | 新端口（通常不是你想要的——确保传全所有 key） |

### wire_of()

从任何信号封装中安全提取裸 `Wire`。**仅**用于 `m.output()` 调用。

```python
m.output("result", wire_of(outs["result"]))
```

支持 `CycleAwareSignal`、`ForwardSignal`、裸 `Wire` 和 `Reg` 输入。

### 条件选择

条件表达式会自动进行周期对齐。

```python
result = true_value if condition else false_value
```

两个分支可以是 `CycleAwareSignal` 或 `int` 字面量；结果会被提升为合适的硬件值。

---

## 周期管理

### next() / prev()

```python
domain.next()  # 推进到下一个时钟周期
domain.prev()  # 回退到上一个时钟周期
```

`next()` 标记时序逻辑的分界点，推进周期计数器。`prev()` 允许回退添加同周期的逻辑。

```python
a = cas(domain, m.input("a", width=8), cycle=0)  # Cycle 0

domain.next()                                      # → Cycle 1
b = domain.signal(width=8, name="b")
b <<= a

domain.next()                                      # → Cycle 2
c = domain.signal(width=8, name="c")
c <<= b

domain.prev()                                      # → Cycle 1
d = a + 1                                          # CAS at Cycle 1（自动平衡）
```

### push() / pop() / call()

`push()` 保存当前周期状态，`pop()` 恢复。用于子模块调用时隔离内部的 `domain.next()`。

**推荐使用 `domain.call()` 自动包裹 push/pop：**

```python
# ✅ 推荐：domain.call() 自动管理 push/pop
fetch_out = domain.call(fetch, inputs={"stall": stall}, prefix="fe")

# 等价手动写法：
domain.push()
fetch_out = fetch(m, domain, inputs={"stall": stall}, prefix="fe")
domain.pop()
```

**嵌套调用完全安全**（栈式管理）：

```python
def parent(m, domain, ...):
    a_out = domain.call(child_a, inputs={...})  # push → child_a → pop
    b_out = domain.call(child_b, inputs={...})  # push → child_b → pop
    # child_a 和 child_b 的内部 domain.next() 互不影响，也不影响 parent
```

**`domain.call()` 的周期隔离保证：**

`domain.call()` 返回后，`domain.cycle_index` 恢复为调用前的值。子函数中任意数量的 `domain.next()` 都**不会**改变父函数的周期计数器。实现上通过 `push()`/`pop()` 栈保存/恢复，并用 `try/finally` 防止异常泄漏。

```python
print(domain.cycle_index)              # 0

domain.next()
print(domain.cycle_index)              # 1

# child 内部执行 3 次 domain.next()
child_out = domain.call(child_fn, inputs={...})
print(domain.cycle_index)              # 1  ← 恢复，不是 4

domain.next()
print(domain.cycle_index)              # 2  ← 从 1 继续

# 再调一个 child（内部 5 次 next）
child_out2 = domain.call(child_fn2, inputs={...})
print(domain.cycle_index)              # 2  ← 再次恢复
```

---

## 自动周期平衡

当组合不同周期的信号时：

- **输出周期 = max(输入周期)**
- 周期较早的信号自动插入 DFF 延迟链

```python
# sig_a 在 Cycle 0，sig_b 在 Cycle 2
result = sig_a + sig_b
# → result 在 Cycle 2，sig_a 自动延迟 2 拍
```

**完整示例：**

```python
def design(m: CycleAwareCircuit, domain: CycleAwareDomain):
    data_in = cas(domain, m.input("data_in", width=8), cycle=0)
    data_at_cycle0 = data_in      # CAS at cycle 0

    domain.next()                  # → Cycle 1
    stage1 = domain.signal(width=8, name="stage1")
    stage1 <<= data_in

    domain.next()                  # → Cycle 2
    stage2 = domain.signal(width=8, name="stage2")
    stage2 <<= stage1

    # data_at_cycle0 是 cycle 0, stage2 是 cycle 2
    # 系统自动为 data_at_cycle0 插入 2 级 DFF
    combined = data_at_cycle0 + stage2  # CAS at cycle 2

    m.output("result", wire_of(combined))
```

生成的 MLIR：

```mlir
%a_delayed1 = pyc.reg %clk, %rst, %en, %a, %reset_val : i8
%a_delayed2 = pyc.reg %clk, %rst, %en, %a_delayed1, %reset_val : i8
%result = pyc.add %a_delayed2, %b : i8
```

---

## 编程风格

PyCircuit V5 使用**函数式风格**：以普通 Python 函数作为模块描述单元，通过 `compile_cycle_aware()` 编译。

```python
from pycircuit import (
    CycleAwareCircuit, CycleAwareDomain,
    cas, compile_cycle_aware, mux, submodule_input, wire_of,
)

def my_module(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    inputs: dict | None = None,
    data_width: int = 32,
    prefix: str = "mod",
) -> dict:
    _in = submodule_input

    # 输入
    data_in = _in(inputs, "data_in", m, domain, prefix=prefix, width=data_width)
    valid   = _in(inputs, "valid",   m, domain, prefix=prefix, width=1)

    # 反馈寄存器：前向声明
    acc = domain.signal(width=data_width, reset_value=0, name=f"{prefix}_acc")

    # 组合逻辑（cycle 0）
    acc_next = (acc + data_in) if valid else acc

    # 状态更新（cycle 1）
    domain.next()
    acc <<= acc_next

    # 输出
    outs = {"result": acc_next, "acc": acc}

    if inputs is None:
        m.output(f"{prefix}_result", wire_of(acc_next))
        m.output(f"{prefix}_acc", wire_of(acc))

    return outs

my_module.__pycircuit_name__ = "my_module"

if __name__ == "__main__":
    ir = compile_cycle_aware(my_module, name="my_module", eager=True, data_width=16)
    print(ir.emit_mlir())
```

**核心优势：**

- `(m, domain)` 是贯穿上下文，所有信号操作直接在同一张信号图上
- `domain.next()` 推进时间线，代码按时序线性叙事
- `domain.call()` 实现子模块层次化调用
- 所有信号都是 `CycleAwareSignal`，类型统一

---

## 模块签名规范

### 标准函数签名

每个 V5 模块是一个**普通 Python 函数**，遵循以下标准签名：

```python
def my_module(
    m: CycleAwareCircuit,          # ①  共享电路对象
    domain: CycleAwareDomain,      # ②  共享时钟域
    *,
    inputs: dict | None = None,    # ③  None = 独立; dict = 被组合
    width: int = 64,               # ④  配置参数（keyword-only）
    prefix: str = "mod",           # ⑤  端口/寄存器名前缀
) -> dict:                         # ⑥  返回输出信号字典
```

| # | 参数 | 作用 |
|---|------|------|
| ① | `m` | 整个设计共享的 `CycleAwareCircuit`，所有子模块写入同一个电路图 |
| ② | `domain` | 整个设计共享的 `CycleAwareDomain`，子模块通过 `domain.call()` 的 push/pop 隔离周期 |
| ③ | `inputs` | **双模开关**：`None` = 独立模式（创建端口）；`dict` = 父模块传递 `CycleAwareSignal` |
| ④ | 配置参数 | 硬件参数（位宽、深度、端口数等），必须在 `*` 之后（keyword-only） |
| ⑤ | `prefix` | 命名空间前缀，保证所有端口和寄存器名不冲突 |
| ⑥ | 返回 `dict` | 输出信号——值**必须是 `CycleAwareSignal`**（保留 cycle provenance） |

### 模块注册

定义函数后，注册 RTL 模块名（用于 MLIR 中的 `func.func` 符号名）：

```python
my_module.__pycircuit_name__ = "my_module"
```

### 双模运行

| 模式 | `inputs` | 输入端口 | 输出端口 | 用途 |
|------|----------|----------|----------|------|
| **独立模式** | `None` | 通过 `m.input()` 创建 | 通过 `m.output()` 发射 | 单元测试 / 独立综合 |
| **组合模式** | `{...}` | 从父模块的 dict 读取 | 不调 `m.output()`，仅返回 dict | 被集成到父模块 |

### 完整模块骨架

```python
from pycircuit import (
    CycleAwareCircuit, CycleAwareDomain,
    cas, compile_cycle_aware, mux, submodule_input, wire_of,
)

def my_module(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    inputs: dict | None = None,
    width: int = 64,
    prefix: str = "mod",
) -> dict:
    _in = submodule_input

    # ── Step 1: 声明输入 ──
    a = _in(inputs, "a", m, domain, prefix=prefix, width=width)
    b = _in(inputs, "b", m, domain, prefix=prefix, width=width)

    # ── Step 2: 组合逻辑（cycle 0）──
    result = a + b

    # ── Step 3: 时序逻辑（可选）──
    reg = domain.signal(width=width, reset_value=0, name=f"{prefix}_reg")
    domain.next()                      # → cycle 1
    reg <<= result

    # ── Step 4: 收集输出（CycleAwareSignal）──
    outs = {"result": result, "reg_out": reg}

    # ── Step 5: 独立模式输出端口 ──
    if inputs is None:
        for k, v in outs.items():
            m.output(f"{prefix}_{k}", wire_of(v))

    return outs

my_module.__pycircuit_name__ = "my_module"

# ── Step 6: 独立编译入口 ──
if __name__ == "__main__":
    circ = compile_cycle_aware(my_module, name="my_module", eager=True, width=16)
    print(circ.emit_mlir())
```

---

## 子模块调用规范（六步法）

### 概述

PyCircuit V5 通过 `domain.call()` 从父模块调用子模块函数组合层次。完整流程：

1. 父模块声明自己的输入（`submodule_input()`）
2. 父模块为子模块构造 `inputs` dict
3. 父模块调用 `domain.call(child_fn, inputs={...}, **config)`
4. 父模块读取子模块返回的输出信号
5. 父模块将输出传给下一个子模块（级联）
6. 顶层收集输出并发射 `m.output()`

### 第一步：声明输入 — submodule_input()

**标量输入：**

```python
_in = submodule_input     # 局部别名

valid = _in(inputs, "valid", m, domain, prefix=prefix, width=1)
data  = _in(inputs, "data",  m, domain, prefix=prefix, width=64)
func  = _in(inputs, "func",  m, domain, prefix=prefix, width=4)
```

**数组/循环输入：**

```python
instr = []
for i in range(FETCH_WIDTH):
    instr.append(_in(inputs, f"instr{i}", m, domain, prefix=prefix, width=32))
```

### 第二步：在父模块中构造 inputs 字典

父模块构造一个 `dict[str, CycleAwareSignal]`，**key 必须与子模块中 `submodule_input()` 的 `key` 参数完全匹配**。

**简单标量映射：**

```python
alu_out = domain.call(alu, inputs={
    "valid": issue_valid,     # 子模块: _in(inputs, "valid", ...)
    "func":  issue_op,        # 子模块: _in(inputs, "func", ...)
    "src1":  issue_data1,     # 子模块: _in(inputs, "src1", ...)
    "src2":  issue_data2,     # 子模块: _in(inputs, "src2", ...)
    "pdst":  issue_pdst,      # 子模块: _in(inputs, "pdst", ...)
}, data_w=64, prefix=f"{prefix}_alu0")
```

**循环构造 dict（宽分发总线）：**

```python
dec_inputs = {}
for i in range(FETCH_WIDTH):
    dec_inputs[f"valid{i}"] = fe_valid         # 子模块: _in(inputs, f"valid{i}", ...)
    dec_inputs[f"instr{i}"] = icache_data[i]   # 子模块: _in(inputs, f"instr{i}", ...)

dec_out = domain.call(decoder, inputs=dec_inputs,
                      width=FETCH_WIDTH, prefix=f"{prefix}_dec")
```

**混合标量 + 循环 dict：**

```python
srs_inputs = {"flush": bru_redirect}        # 标量
for i in range(W):                           # 循环：每 slot 一组信号
    srs_inputs[f"disp_valid{i}"]  = ds_to_srs[i]
    srs_inputs[f"disp_op{i}"]     = ds_op[i]
    srs_inputs[f"disp_psrc1_{i}"] = ds_psrc1[i]
    srs_inputs[f"disp_pdst{i}"]   = ds_pdst[i]
for c in range(CDB_PORTS):
    srs_inputs[f"cdb_valid{c}"] = zero1
    srs_inputs[f"cdb_tag{c}"]   = cas(domain, u(TAG_W, 0), cycle=0)

srs_out = domain.call(scalar_rs, inputs=srs_inputs,
                      n_entries=32, n_dispatch=W, n_cdb=CDB_PORTS,
                      prefix=f"{prefix}_srs")
```

**在 dict 中传递常量（无有效信号时）：**

```python
zero1  = cas(domain, u(1, 0), cycle=0)
zero64 = cas(domain, u(64, 0), cycle=0)

fetch_out = domain.call(fetch, inputs={
    "stall":      stall_in,
    "bpu_taken":  zero1,       # 常量 0（暂无 BPU）
    "bpu_target": zero64,      # 常量 0
}, prefix=f"{prefix}_fe")
```

### 第三步：domain.call() 调用子模块

```python
child_out = domain.call(
    child_fn,              # 子模块函数
    inputs={...},          # CycleAwareSignal 字典
    **config_kwargs,       # 配置参数，直接转发给子模块
)
```

**内部执行流程：**

1. `domain.push()` — 保存当前周期计数器
2. `child_fn(m, domain, inputs={...}, **config_kwargs)` — 子模块执行
3. `domain.pop()` — 恢复父模块的周期计数器

子模块内部可以自由调用 `domain.next()`。`pop()` 后，父模块的 cycle 恢复到 `push()` 之前的值。

> **周期隔离保证（Critical）**
>
> `domain.call()` 返回后，`domain.cycle_index` **恢复为调用前的值**，不受子函数内部任何 `domain.next()` 的影响。这是由 `push()`/`pop()` 栈机制保证的，且用 `try/finally` 确保即使子函数抛异常也会恢复。
>
> ```python
> # 父函数
> domain.next()                          # cycle_index = 1
> print(domain.cycle_index)              # → 1
>
> child_out = domain.call(child_fn, inputs={...}, prefix="ch")
> # child_fn 内部执行了 3 次 domain.next()（cycle 推进到 4）
> # 但 call 返回后：
>
> print(domain.cycle_index)              # → 1  ← 恢复到 call 之前的值
>
> domain.next()                          # cycle_index = 2（从 1 继续，不是从 4）
> ```
>
> 这意味着**多个 `domain.call()` 之间的周期计数是互不干扰的**——每个子模块的内部流水深度对父模块完全透明。

**所有 `**kwargs` 原样转发给子模块：**

```python
domain.call(scalar_rs, inputs=srs_inputs,
            n_entries=32,            # → scalar_rs(..., n_entries=32)
            n_dispatch=4,            # → scalar_rs(..., n_dispatch=4)
            n_cdb=6,                 # → scalar_rs(..., n_cdb=6)
            prefix=f"{prefix}_srs")  # → scalar_rs(..., prefix="dv_srs")
```

### 第四步：读取子模块输出

`domain.call()` 返回子模块的 `dict`。所有值是 `CycleAwareSignal`，**cycle 保留子模块内部赋值时的值**（cycle provenance）。

**标量输出：**

```python
alu_out = domain.call(alu, inputs={...}, prefix=f"{prefix}_alu0")

result_valid = alu_out["result_valid"]   # CAS — cycle 来自 alu() 内部
result_data  = alu_out["result_data"]    # CAS — cycle 来自 alu() 内部
result_tag   = alu_out["result_tag"]     # CAS — cycle 来自 alu() 内部
```

**列表输出：**

```python
dec_out = domain.call(decoder, inputs={...}, prefix=f"{prefix}_dec")

dec_valid  = dec_out["out_valid"]   # list[CycleAwareSignal]（长度 = FETCH_WIDTH）
dec_opcode = dec_out["opcode"]      # list[CycleAwareSignal]

for i in range(FETCH_WIDTH):
    print(dec_valid[i].cycle)       # 保留 decoder() 内部的 cycle
```

### 第五步：级联子模块（输出 → 输入链路）

**核心模式：前一个子模块的输出成为下一个子模块的输入。**

```python
# Fetch 产出信号
fetch_out = domain.call(fetch, inputs={...}, prefix=f"{prefix}_fe")

# Fetch 输出 → Decode 输入
dec_inputs = {}
for i in range(W):
    dec_inputs[f"valid{i}"] = fetch_out["valid"]    # CAS 来自 fetch
    dec_inputs[f"instr{i}"] = icache_data[i]
dec_out = domain.call(decoder, inputs=dec_inputs, prefix=f"{prefix}_dec")

# Decode 输出 → Rename 输入
ren_inputs = {}
for i in range(W):
    ren_inputs[f"valid{i}"]   = dec_out["out_valid"][i]
    ren_inputs[f"srd{i}"]     = dec_out["rd"][i]
    ren_inputs[f"srs1_{i}"]   = dec_out["rs1"][i]
    ren_inputs[f"srs2_{i}"]   = dec_out["rs2"][i]
ren_out = domain.call(rename, inputs=ren_inputs, prefix=f"{prefix}_ren")
```

### 第六步：收集顶层输出

```python
outs = {
    "pc":          fetch_out["pc"],
    "fetch_valid": fe_valid,
    "dec_valid":   dec_out["out_valid"],       # 可以是 list
    "alu_result":  alu_out["result_data"],
}

if inputs is None:
    m.output(f"{prefix}_pc", wire_of(outs["pc"]))
    m.output(f"{prefix}_fetch_valid", wire_of(outs["fetch_valid"]))
    # 列表输出：循环
    for i in range(W):
        m.output(f"{prefix}_dec_valid_{i}", wire_of(outs["dec_valid"][i]))

return outs
```

### 完整生命周期图

```text
┌─────────────────────────────────────────────────────────────────────────┐
│                       父模块 (top)                                      │
│                                                                         │
│  1. _in(inputs, "stall", ...) ─── 解析父模块自己的输入                  │
│                                                                         │
│  2. 构造子模块的 inputs dict:                                           │
│     child_inputs = {"stall": stall, "data": data_in, ...}              │
│     （所有值必须是 CycleAwareSignal）                                   │
│                                                                         │
│  3. domain.call(child_fn, inputs=child_inputs, prefix="top_child")     │
│     ┌───────────────────────────────────────────────────────────────┐   │
│     │  domain.push()  ── 保存父周期                                │   │
│     │                                                               │   │
│     │  child_fn(m, domain, inputs=child_inputs, prefix="top_child")│   │
│     │    ├── submodule_input(inputs, "stall", ...) → inputs["stall"]│   │
│     │    ├── submodule_input(inputs, "data", ...)  → inputs["data"] │   │
│     │    ├── domain.signal(...)     ── 创建寄存器                  │   │
│     │    ├── <组合逻辑>              ── cycle 0                    │   │
│     │    ├── domain.next()          ── 推进到 cycle 1              │   │
│     │    ├── reg <<= expr           ── 时序更新                    │   │
│     │    ├── outs = {"result": result, "valid": valid}             │   │
│     │    ├── if inputs is None: m.output(...)  ── 组合模式跳过     │   │
│     │    └── return outs                                           │   │
│     │                                                               │   │
│     │  domain.pop()  ── 恢复父周期                                 │   │
│     └───────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  4. child_out = <返回的 dict>                                           │
│     child_out["result"].cycle → 保留子模块内部的 cycle                  │
│     child_out["valid"].cycle  → 保留子模块内部的 cycle                  │
│                                                                         │
│  5. 传给下一个子模块:                                                   │
│     domain.call(next_child, inputs={"data": child_out["result"]}, ...) │
│                                                                         │
│  6. 收集顶层输出:                                                       │
│     outs = {"final": child_out["result"]}                              │
│     if inputs is None: m.output(f"{prefix}_final", wire_of(...))       │
│     return outs                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### Cycle 属性传递

```text
父函数 cycle 0          子函数（push 后）                 父函数 pop 后
──────────────          ────────────────                 ──────────────
stall (cycle=0) ──→     submodule_input: stall (cycle=0)
                        ↓ 内部计算
                        result = a + b        (cycle=0)
                        ↓ domain.next()
                        pipe_out              (cycle=1)
                        ↓ 返回
              ←返回──   outs["pipe_out"]      (cycle=1)
                                               pipe_out (cycle=1)
```

**关键点**：

- 传入的信号保留原始 cycle
- 返回的信号携带子函数内部产生时的 cycle
- `domain.call()` 隔离子函数的 `domain.next()` 对父函数周期计数的影响，但**不改变信号本身的 cycle 值**

### 命名约定：前缀级联

前缀通过层次级联传递，避免命名冲突：

```python
def soc_top(m, domain, *, inputs=None, prefix="soc"):
    cpu_out = domain.call(cpu_core, inputs={...}, prefix=f"{prefix}_cpu")
    # cpu_core 的内部 prefix = "soc_cpu"
    #   → cpu_core 调 frontend: prefix = "soc_cpu_fe"
    #     → frontend 的寄存器名 = "soc_cpu_fe_pc"
```

| 元素 | 命名模式 | 示例 |
|------|----------|------|
| 输入端口 | `{prefix}_{name}` | `fe_bpu_pc`, `be_rob_idx` |
| 输出端口 | `{prefix}_{name}` | `fe_dec_valid_0`, `mem_ldu_data` |
| 状态寄存器 | `{prefix}_{name}` | `fe_fetch_pc`, `be_stall` |
| 子模块前缀 | `{parent_prefix}_{child_abbrev}` | `dv_fe`, `dv_dec`, `dv_srs` |

### 独立编译

每个模块都可以独立编译——传 `inputs=None` 触发独立模式：

```python
if __name__ == "__main__":
    circ = compile_cycle_aware(fetch, name="fetch", eager=True, addr_w=32)
    print(circ.emit_mlir())
```

### 常见错误与陷阱

#### 错误 1：dict 值传了 Wire 而不是 CAS

```python
# ❌ 错误：m.input() 返回 Wire，不是 CAS
domain.call(alu, inputs={"src1": m.input("x", width=64)}, ...)

# ✅ 正确：先用 cas() 包装
x = cas(domain, m.input("x", width=64), cycle=0)
domain.call(alu, inputs={"src1": x}, ...)
```

#### 错误 2：输出 dict 存了 Wire 而不是 CAS

```python
# ❌ 错误：Wire 丢失 cycle 信息，父模块无法做 cycle-aware 运算
return {"result": wire_of(result)}

# ✅ 正确：存 CAS 信号
return {"result": result}
```

#### 错误 3：key 名不匹配

```python
# 父模块传的 key:
domain.call(alu, inputs={"source1": data_a}, ...)

# 子模块期望的 key:
src1 = _in(inputs, "src1", ...)   # ← "src1" ≠ "source1" → 创建额外端口！

# ✅ 必须完全匹配
domain.call(alu, inputs={"src1": data_a}, ...)
```

#### 错误 4：忘记级联 prefix

```python
# ❌ 错误：所有子模块用同一个 prefix → 端口名冲突
domain.call(alu,     inputs={...}, prefix="exu")
domain.call(muldiv,  inputs={...}, prefix="exu")   # 冲突！

# ✅ 正确：每个子模块有独立 prefix
domain.call(alu,     inputs={...}, prefix=f"{prefix}_alu0")
domain.call(muldiv,  inputs={...}, prefix=f"{prefix}_md")
```

#### 错误 5：忘记在独立模式发射 output

```python
def my_mod(m, domain, *, inputs=None, prefix="mod") -> dict:
    ...
    outs = {"result": result}
    # ❌ 忘了 if inputs is None: m.output(...)
    return outs

# ✅ 正确
def my_mod(m, domain, *, inputs=None, prefix="mod") -> dict:
    ...
    outs = {"result": result}
    if inputs is None:
        m.output(f"{prefix}_result", wire_of(outs["result"]))
    return outs
```

---

## 层次化 MLIR 发射

### 概述

默认情况下，`compile_cycle_aware()` 将所有子模块逻辑扁平化为单一 `func.func`。使用 `hierarchical=True` 时，每个 `domain.call()` 边界被保留为独立模块：

- 每个子模块成为独立的 `func.func`，拥有自己的输入/输出端口
- 父模块通过 `pyc.instance` op 引用子模块
- 输出的 MLIR 包含所有模块定义
- 子模块可以独立编译、优化和综合

### 使用方式

```python
# 扁平模式（默认）— 单一 func.func
circ = compile_cycle_aware(my_top, eager=True, name="my_top")

# 层次化模式 — 多模块 Design
circ = compile_cycle_aware(my_top, eager=True, name="my_top",
                           hierarchical=True)
mlir = circ.emit_mlir()
```

### 工作原理

1. `compile_cycle_aware(..., hierarchical=True)` 创建 `Design` 对象
2. 当执行到 `domain.call(sub_fn, inputs={...})` 时：
   - 子模块以 standalone 模式（`inputs=None`）编译为独立电路
   - 记录输出 dict 的结构和 cycle 元数据
   - 将编译好的模块注册到 Design
   - 在父模块中发射 `pyc.instance` op
   - 从 instance 输出重建 `CycleAwareSignal` 返回给父模块
3. 递归处理：子模块内部的 `domain.call()` 也会层次化编译

### 端口映射约定

- **输入端口**: `{prefix}_{key}` 映射到 `inputs[key]`
- **输出端口**: `{prefix}_{key}` 映射到 `outs[key]`
- **Clock/Reset**: 自动从父模块的时钟域连接

### 命令行使用

```bash
python -m designs.outerCube.davinci.davinci_top --hierarchical
```

### MLIR 输出结构

```text
module attributes {pyc.top = @davinci_top, ...} {
  func.func @fetch(%clk, %rst, ...) -> (...) { ... }
  func.func @decoder(%clk, %rst, ...) -> (...) { ... }
  ...
  func.func @davinci_top(%clk, %rst, ...) -> (...) {
    %v3 = pyc.instance ... {callee = @fetch} : ...
    %v10 = pyc.instance ... {callee = @decoder} : ...
    ...
  }
}
```

### 扁平 vs 层次化对比

| 特性 | 扁平模式 | 层次化模式 |
|------|---------|-----------|
| MLIR 结构 | 单一 `func.func` | 多个 `func.func` + `pyc.instance` |
| 子模块边界 | 消失（内联） | 保留 |
| 增量编译 | 全量重编译 | 子模块可独立编译 |
| 综合工具 | 需要自行划分 | 自然层次化 |
| 输出大小 | 较小 | 略大（实例化开销） |

---

## 仿真与测试（CycleAwareTb）

### 概述

`CycleAwareTb` 是 V5 提供的周期感知测试台封装。它将 `Tb` 的 `at=cycle` 参数模式替换为隐式 `tb.next()` 周期管理，与设计代码中的 `domain.next()` 对称。

### 导入

```python
from pycircuit import CycleAwareTb, Tb, testbench
```

### API

| 方法 | 说明 |
|------|------|
| `CycleAwareTb(t: Tb)` | 包装 `Tb` 实例 |
| `tb.clock(port, **kw)` | 配置时钟（透传到 `Tb`） |
| `tb.reset(port, **kw)` | 配置复位（透传到 `Tb`） |
| `tb.timeout(cycles)` | 设置仿真超时 |
| `tb.next()` | 推进到下一个时钟周期 |
| `tb.cycle` | 属性：当前周期索引 |
| `tb.drive(port, value)` | 在当前周期驱动端口 |
| `tb.expect(port, value, *, phase="post", msg=None)` | 在当前周期检查端口值 |
| `tb.finish(*, at=None)` | 在当前周期（或指定周期）结束仿真 |
| `tb.print(fmt, *, ports=())` | 在当前周期打印 |
| `tb.sva_assert(expr, **kw)` | SVA 断言（透传） |
| `tb.random(port, **kw)` | 随机激励（透传） |

### 完整示例

```python
from pycircuit import (
    CycleAwareCircuit, CycleAwareDomain, CycleAwareTb,
    Tb, cas, compile_cycle_aware, testbench, wire_of,
)

def counter(m: CycleAwareCircuit, domain: CycleAwareDomain):
    enable = cas(domain, m.input("enable", width=1), cycle=0)
    count = domain.signal(width=8, reset_value=0, name="count")
    m.output("count", wire_of(count))
    domain.next()
    count <<= ((count + 1) if enable else count)

counter.__pycircuit_name__ = "counter"

@testbench
def tb(t: Tb) -> None:
    tb = CycleAwareTb(t)
    tb.clock("clk")
    tb.reset("rst", cycles_asserted=2, cycles_deasserted=1)
    tb.timeout(64)

    # Cycle 0: enable=0 → count 保持 0
    tb.drive("enable", 0)
    tb.expect("count", 0)

    tb.next()   # → Cycle 1: enable=1
    tb.drive("enable", 1)
    tb.expect("count", 0)  # count 在下一时钟沿更新

    tb.next()   # → Cycle 2
    tb.expect("count", 1)  # count = 1

    tb.next()   # → Cycle 3
    tb.expect("count", 2)

    tb.finish()
```

### 仿真流程

```text
PyCircuit V5 设计代码                    仿真流程
─────────────────                    ─────────
compile_cycle_aware() → MLIR ──→ pycc backend ──→ C++ / SystemVerilog
                                                    │
CycleAwareTb → Tb payload ──→ MLIR attrs ──→ C++ testbench / SV testbench
                                                    │
                                              执行仿真 → 波形 / 日志
```

### 运行仿真

```bash
# 编译 RTL + 测试台并通过 pycc 后端运行
pycircuit build my_design.py --sim

# 或使用 CLI 指定输出目录
pycircuit build my_design.py -o build_out/
```

---

## 编译入口 compile_cycle_aware()

```python
ir = compile_cycle_aware(fn, name="module_name", eager=True, **kwargs)
ir.emit_mlir()

# 层次化编译
ir = compile_cycle_aware(fn, name="module_name", eager=True,
                         hierarchical=True, **kwargs)
ir.emit_mlir()   # 输出包含所有子模块
```

| 参数 | 说明 |
|------|------|
| `fn` | `def fn(m: CycleAwareCircuit, domain: CycleAwareDomain, ...) -> dict` |
| `name` | 模块/电路名称（可选） |
| `domain_name` | 时钟域名称（默认 `"clk"`） |
| `eager` | `True` 时直接执行 Python 函数体（不经 JIT） |
| `hierarchical` | `True` 时保留 `domain.call()` 边界为独立 MLIR 模块（需 `eager=True`） |
| `**jit_params` | 传递给 `fn` 的额外关键字参数 |

---

## RTL 编译流程（Python → MLIR → Verilog）

PyCircuit V5 的 RTL 编译分为两个阶段：

1. **前端编译**：Python 执行 → MLIR 文本（`.pyc` 文件）
2. **后端编译**：`pycc` 工具处理 MLIR → Verilog / C++

### 第一步：生成 MLIR

```bash
# 扁平模式（所有子模块内联为单一 func.func）
cd $PROJECT_ROOT
PYTHONPATH=compiler/frontend:$PYTHONPATH \
  python3 -m designs.outerCube.davinci.davinci_top

# 层次化模式（保留 domain.call() 确立的子模块边界）
PYTHONPATH=compiler/frontend:$PYTHONPATH \
  python3 -m designs.outerCube.davinci.davinci_top --hierarchical
```

脚本内部的典型 `__main__` 写法：

```python
if __name__ == "__main__":
    import sys
    hier = "--hierarchical" in sys.argv or "-H" in sys.argv

    circ = compile_cycle_aware(
        davinci_top, eager=True, name="davinci_top", hierarchical=hier,
    )
    mlir = circ.emit_mlir()
    print(f"davinci_top: {len(mlir):,} chars MLIR"
          f" ({'hierarchical' if hier else 'flat'})")
    with open("davinci_top.mlir", "w") as f:
        f.write(mlir)
```

### 第二步：pycc 后端生成 Verilog

`pycc` 支持两种互斥的输出模式：

| 标志 | 说明 |
|------|------|
| `--hierarchical` | **层次化输出**：保留所有 `pyc.instance` 边界，每个子模块生成独立的 Verilog `module` |
| `--flatten` | **扁平化输出**：将所有 `pyc.instance` 内联到顶层模块，输出单一 Verilog `module` |

```bash
# ── 层次化 Verilog（每个子模块独立文件） ──
build/bin/pycc davinci_top.mlir \
  --emit=verilog \
  --hierarchical \
  --logic-depth=256 \
  --out-dir=build_out/verilog_hier

# ── 扁平化 Verilog（单一顶层模块） ──
build/bin/pycc davinci_top.mlir \
  --emit=verilog \
  --flatten \
  --logic-depth=256 \
  --out-dir=build_out/verilog_flat

# ── 单文件输出（不拆分子模块文件） ──
build/bin/pycc davinci_top.mlir \
  --emit=verilog \
  --hierarchical \
  --logic-depth=256 \
  -o davinci_top.v
```

### pycc 常用命令行参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--emit=verilog\|cpp\|none` | `verilog` | 输出目标 |
| `--hierarchical` | — | 层次化模式：保留子模块边界 |
| `--flatten` | — | 扁平化模式：内联所有子模块 |
| `--logic-depth=N` | `32` | 允许的最大组合逻辑深度 |
| `--out-dir=DIR` | — | 按模块拆分文件输出到目录 |
| `-o FILE` | `-`(stdout) | 单文件输出 |
| `--target=default\|fpga` | `default` | 目标平台（FPGA 添加 `` `define``） |
| `--include-primitives` | `true` | 是否包含 pyc 原语定义 |
| `--hierarchy-policy=strict\|instantiate` | `strict` | 层次纪律检查策略 |
| `--inline-policy=default\|off\|threshold:N` | `default` | 内联控制 |
| `--noinline` | `false` | 禁用 MLIR inliner |
| `--build-profile=release\|dev-fast` | `release` | 构建配置 |

### 完整构建示例（Davinci OoO Core）

下面以 Davinci 乱序处理器核为例，展示层次化和扁平化两条完整构建流水线。

#### 流水线 A：层次化 Verilog

每个 `domain.call()` 子模块保留为独立 Verilog `module`，各子模块单独文件。

```bash
cd $PROJECT_ROOT

# A-1. 生成层次化 MLIR（--hierarchical 保留 domain.call 边界）
PYTHONPATH=compiler/frontend:$PYTHONPATH \
  python3 -m designs.outerCube.davinci.davinci_top --hierarchical
# 输出: davinci_top.mlir
#   14 个 func.func：fetch, decoder, rename, dispatch,
#   scalar_rs, alu, muldiv, bru, lsu_rs, lsu,
#   vec_rs, cube_rs, mte_rs, davinci_top

# A-2. pycc 层次化模式生成 Verilog
mkdir -p davinci_build/verilog_hier
build/bin/pycc davinci_top.mlir \
  --emit=verilog \
  --hierarchical \
  --logic-depth=256 \
  --out-dir=davinci_build/verilog_hier
# 输出: davinci_build/verilog_hier/
#   davinci_top.v      ← 顶层（仅子模块实例化 + 端口连线）
#   fetch.v            ← 取指单元
#   decoder.v          ← 译码器
#   rename.v           ← 寄存器重命名
#   dispatch.v         ← 派发
#   scalar_rs.v        ← 标量保留站
#   alu.v              ← ALU
#   muldiv.v           ← 乘除法
#   bru.v              ← 分支单元
#   lsu_rs.v           ← 访存保留站
#   lsu.v              ← 访存单元
#   vec_rs.v           ← 向量保留站
#   cube_rs.v          ← Cube 保留站
#   mte_rs.v           ← MTE 保留站
#   pyc_primitives.v   ← pyc 基础原语（pyc_reg 等）
```

顶层 `davinci_top.v` 的结构：

```verilog
module davinci_top (
  input clk, input rst, ...
);
  // --- Submodules
  fetch     dv_fe  (.clk(clk), .rst(rst), ...);
  decoder   dv_dec (.clk(clk), .rst(rst), ...);
  rename    dv_ren (.clk(clk), .rst(rst), ...);
  dispatch  dv_ds  (.clk(clk), .rst(rst), ...);
  scalar_rs dv_srs (.clk(clk), .rst(rst), ...);
  alu       dv_alu0(.clk(clk), .rst(rst), ...);
  muldiv    dv_md  (.clk(clk), .rst(rst), ...);
  bru       dv_bru (.clk(clk), .rst(rst), ...);
  lsu_rs    dv_lrs (.clk(clk), .rst(rst), ...);
  lsu       dv_lsu (.clk(clk), .rst(rst), ...);
  vec_rs    dv_vrs (.clk(clk), .rst(rst), ...);
  cube_rs   dv_crs (.clk(clk), .rst(rst), ...);
  mte_rs    dv_mrs (.clk(clk), .rst(rst), ...);
endmodule
```

#### 流水线 B：扁平化 Verilog

所有子模块逻辑内联到一个顶层 `module`，输出单一 Verilog 文件。

有两种方式实现扁平化：

#### 方式 B-1：从扁平 MLIR 生成（前端不保留层次）

```bash
cd $PROJECT_ROOT

# B-1a. 生成扁平 MLIR（不加 --hierarchical，默认扁平模式）
PYTHONPATH=compiler/frontend:$PYTHONPATH \
  python3 -m designs.outerCube.davinci.davinci_top
# 输出: davinci_top.mlir (单一 func.func，所有逻辑内联)

# B-1b. pycc 默认模式生成 Verilog
build/bin/pycc davinci_top.mlir \
  --emit=verilog \
  --logic-depth=256 \
  -o davinci_build/davinci_top_flat.v
# 输出: davinci_build/davinci_top_flat.v
#   单一 module davinci_top（~39,000 行）
```

#### 方式 B-2：从层次化 MLIR + `--flatten` 生成（pycc 后端内联）

```bash
cd $PROJECT_ROOT

# B-2a. 生成层次化 MLIR（与流水线 A 相同）
PYTHONPATH=compiler/frontend:$PYTHONPATH \
  python3 -m designs.outerCube.davinci.davinci_top --hierarchical
# 输出: davinci_top.mlir (14 个 func.func)

# B-2b. pycc --flatten 将所有 pyc.instance 内联为单一模块
build/bin/pycc davinci_top.mlir \
  --emit=verilog \
  --flatten \
  --logic-depth=256 \
  -o davinci_build/davinci_top_flat.v
# 输出: davinci_build/davinci_top_flat.v
#   单一 module davinci_top（所有子模块逻辑已内联）
```

> **提示**：方式 B-2 的优势在于只需维护一份层次化 MLIR，通过 `--hierarchical` 或 `--flatten`
> 切换输出形式，无需重新执行前端编译。

#### 流水线对比总结

```text
                  ┌─ --hierarchical ─→ 14 个独立 .v（层次化）
Python ─→ MLIR ──┤
 (--hierarchical) └─ --flatten ──────→ 1 个扁平 .v（全内联）

Python ─→ MLIR ──── pycc (default) ──→ 1 个扁平 .v（全内联）
 (default flat)
```

### 层次化 vs 扁平化 Verilog 对比

| 特性 | `--hierarchical` | `--flatten` |
|------|------------------|-------------|
| Verilog 模块数 | 每个 `domain.call()` 对应一个 `module` | 仅顶层一个 `module` |
| 文件组织 | 每模块独立 `.v` 文件 | 单一文件 |
| 综合工具集成 | 自然层次化，便于约束和优化 | 综合工具自行展开 |
| 可读性 | 模块化，便于定位 | 扁平，信号名可能很长 |
| 增量修改 | 只需重编变更的子模块 | 全量重编 |

---

## 编程范例

### 范例1：带使能的计数器

```python
from pycircuit import (
    CycleAwareCircuit, CycleAwareDomain,
    compile_cycle_aware, mux, submodule_input, wire_of,
)

def counter(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    inputs: dict | None = None,
    width: int = 8,
    prefix: str = "cnt",
) -> dict:
    _in = submodule_input

    enable = _in(inputs, "enable", m, domain, prefix=prefix, width=1)

    count = domain.signal(width=width, reset_value=0, name=f"{prefix}_count")

    count_next = (count + 1) if enable else count

    domain.next()
    count <<= count_next

    outs = {"count": count, "count_next": count_next}
    if inputs is None:
        m.output(f"{prefix}_count", wire_of(count))
    return outs

counter.__pycircuit_name__ = "counter"

if __name__ == "__main__":
    circuit = compile_cycle_aware(counter, name="counter", eager=True, width=8)
    print(circuit.emit_mlir())
```

### 范例2：SRAM Bank

```python
def sram_bank(m, domain, *, depth, width, prefix,
              rd_addr, wr_addr, wr_en, wr_data):
    from pycircuit import cas

    storage = [
        domain.signal(width=width, reset_value=0, name=f"{prefix}_{d}")
        for d in range(depth)
    ]

    # Cycle 0: 组合读
    zero = cas(domain, u(width, 0), cycle=0)
    rd_data = zero
    for d in range(depth):
        hit = rd_addr == d
        rd_data = storage[d] if hit else rd_data

    # Deferred write（在 domain.next() 之后调用）
    def commit_write():
        for d in range(depth):
            hit = wr_en & (wr_addr == d)
            storage[d].assign(wr_data, when=hit)

    return rd_data, commit_write
```

### 范例3：SoC 层次化集成

```text
SoC Top
├── CPU Core
│   ├── Frontend (fetch + decode)
│   └── Backend (ALU + MUL/DIV + BRU)
├── Memory Controller
└── UART Peripheral
```

```python
from pycircuit import (
    CycleAwareCircuit, CycleAwareDomain,
    cas, compile_cycle_aware, mux, submodule_input, wire_of,
)

def frontend(m, domain, *, inputs=None, pc_width=32, prefix="fe") -> dict:
    _in = submodule_input

    redirect_valid  = _in(inputs, "redirect_valid",  m, domain, prefix=prefix, width=1)
    redirect_target = _in(inputs, "redirect_target", m, domain, prefix=prefix, width=pc_width)

    pc = domain.signal(width=pc_width, reset_value=0, name=f"{prefix}_pc")

    FOUR = cas(domain, u(pc_width, 4), cycle=0)
    next_pc = redirect_target if redirect_valid else (pc + FOUR)

    domain.next()
    pc <<= next_pc

    outs = {"pc": pc, "next_pc": next_pc}
    if inputs is None:
        m.output(f"{prefix}_pc", wire_of(pc))
    return outs

frontend.__pycircuit_name__ = "frontend"


def backend(m, domain, *, inputs=None, data_width=32, prefix="be") -> dict:
    _in = submodule_input

    op_a   = _in(inputs, "op_a",   m, domain, prefix=prefix, width=data_width)
    op_b   = _in(inputs, "op_b",   m, domain, prefix=prefix, width=data_width)
    alu_op = _in(inputs, "alu_op", m, domain, prefix=prefix, width=4)

    add_r = op_a + op_b
    sub_r = op_a - op_b
    result = sub_r if alu_op[0] else add_r

    wb_data = domain.signal(width=data_width, name=f"{prefix}_wb_data")
    domain.next()
    wb_data <<= result

    outs = {"wb_data": wb_data, "result": result}
    if inputs is None:
        m.output(f"{prefix}_wb_data", wire_of(wb_data))
    return outs

backend.__pycircuit_name__ = "backend"


def cpu_core(m, domain, *, inputs=None, data_width=32, pc_width=32, prefix="cpu") -> dict:
    _in = submodule_input

    redirect = _in(inputs, "redirect", m, domain, prefix=prefix, width=1)
    target   = _in(inputs, "target",   m, domain, prefix=prefix, width=pc_width)

    fe_out = domain.call(frontend, inputs={
        "redirect_valid": redirect,
        "redirect_target": target,
    }, pc_width=pc_width, prefix=f"{prefix}_fe")

    be_out = domain.call(backend, inputs={
        "op_a": fe_out["pc"],
        "op_b": cas(domain, u(data_width, 0), cycle=0),
        "alu_op": cas(domain, u(4, 0), cycle=0),
    }, data_width=data_width, prefix=f"{prefix}_be")

    outs = {"pc": fe_out["pc"], "wb_data": be_out["wb_data"]}
    if inputs is None:
        m.output(f"{prefix}_pc", wire_of(outs["pc"]))
        m.output(f"{prefix}_wb_data", wire_of(outs["wb_data"]))
    return outs

cpu_core.__pycircuit_name__ = "cpu_core"


def soc_top(m, domain, *, inputs=None, data_width=32, pc_width=32, prefix="soc") -> dict:
    _in = submodule_input

    ext_redirect = _in(inputs, "redirect", m, domain, prefix=prefix, width=1)
    ext_target   = _in(inputs, "target",   m, domain, prefix=prefix, width=pc_width)

    cpu_out = domain.call(cpu_core, inputs={
        "redirect": ext_redirect,
        "target": ext_target,
    }, data_width=data_width, pc_width=pc_width, prefix=f"{prefix}_cpu")

    outs = {"pc": cpu_out["pc"], "wb_data": cpu_out["wb_data"]}
    if inputs is None:
        m.output(f"{prefix}_pc", wire_of(outs["pc"]))
        m.output(f"{prefix}_wb_data", wire_of(outs["wb_data"]))
    return outs

soc_top.__pycircuit_name__ = "soc_top"

if __name__ == "__main__":
    ir = compile_cycle_aware(soc_top, name="soc_top", eager=True,
                             data_width=32, pc_width=32)
    print(ir.emit_mlir())
```

**设计要点：**

1. **层次清晰**：`soc_top` → `cpu_core` → `frontend` / `backend`，函数调用链即设计层次
2. **周期管理**：`domain.call()` 自动隔离子系统周期
3. **命名隔离**：前缀 `soc_cpu_fe_*` / `soc_cpu_be_*` 避免端口冲突
4. **信号类型统一**：全程 `CycleAwareSignal`，没有任何 `.wire` 访问
5. **每层可独立编译**：`frontend`、`backend`、`cpu_core`、`soc_top` 都可以独立 `compile_cycle_aware()` 测试

### 范例4：Davinci OoO 处理器核（完整层次化）

Davinci 核（`designs/outerCube/davinci/davinci_top.py`）展示了所有模式的大规模应用——27 个模块、1.5M+ 字符 MLIR：

```python
def davinci_top(m, domain, *, inputs=None, prefix="dv") -> dict:
    _in = submodule_input
    zero1 = cas(domain, u(1, 0), cycle=0)

    # ── 外部输入 ──
    stall_in     = _in(inputs, "stall",        m, domain, prefix=prefix, width=1)
    icache_valid = _in(inputs, "icache_valid",  m, domain, prefix=prefix, width=1)
    bru_redirect = _in(inputs, "bru_redirect", m, domain, prefix=prefix, width=1)
    icache_data = [_in(inputs, f"icache_data{i}", m, domain, prefix=prefix,
                       width=32) for i in range(FETCH_WIDTH)]

    # ── Stage 1: Fetch ──
    fetch_out = domain.call(fetch, inputs={
        "stall": stall_in, "redirect_valid": bru_redirect,
        "bpu_taken": zero1, ...
    }, addr_w=64, prefix=f"{prefix}_fe")

    # ── Stage 2: Decode ──
    dec_inputs = {}
    for i in range(W):
        dec_inputs[f"valid{i}"] = fetch_out["valid"] & icache_valid
        dec_inputs[f"instr{i}"] = icache_data[i]
    dec_out = domain.call(decoder, inputs=dec_inputs, prefix=f"{prefix}_dec")

    # ── Stage 3-5: Rename → Dispatch → RS ──
    ren_out = domain.call(rename, inputs={...}, prefix=f"{prefix}_ren")
    ds_out  = domain.call(dispatch, inputs={...}, prefix=f"{prefix}_ds")
    srs_out = domain.call(scalar_rs, inputs={...}, prefix=f"{prefix}_srs")

    # ── Stage 6+: Execution units ──
    alu_out = domain.call(alu, inputs={
        "valid": srs_out["issue_valid"], "src1": srs_out["issue_data1"], ...
    }, prefix=f"{prefix}_alu0")

    # ── 收集输出 ──
    outs = {"pc": fetch_out["pc"], "alu_result": alu_out["result_data"], ...}
    if inputs is None:
        m.output(f"{prefix}_pc", wire_of(outs["pc"]))
        ...
    return outs

davinci_top.__pycircuit_name__ = "davinci_top"
```

---

## 最佳实践

### 1. 信号类型纪律

```python
# ✅ 所有运算在 CAS 上进行
result = a + b                              # CAS + CAS → CAS
flag   = result == 0                        # CAS → CAS

# ✅ wire_of() 仅在 m.output() 边界
m.output("result", wire_of(result))

# ❌ 禁止
raw = a.wire                                # 已移除
outs["x"] = sig.wire                        # 禁止存 Wire
```

### 2. 寄存器使用 domain.signal()

```python
# ✅ 唯一的寄存器创建方式
count = domain.signal(width=8, reset_value=0, name="count")
domain.next()
count <<= count + 1

# 条件写入
storage.assign(wr_data, when=wr_en)

# ❌ 禁止
st = domain.state(...)       # 已移除
```

### 3. 模块签名规范

```python
def my_module(m, domain, *, inputs=None, prefix="mod", ...) -> dict:
    _in = submodule_input
    ...
    outs = {"result": result}
    if inputs is None:
        m.output(f"{prefix}_result", wire_of(result))
    return outs

my_module.__pycircuit_name__ = "my_module"
```

### 4. 层次化组合

```python
# ✅ domain.call() 自动 push/pop
fetch_out = domain.call(fetch, inputs={...}, prefix="fe")
dec_out   = domain.call(decoder, inputs={...}, prefix="dec")
```

### 5. 输出 dict 存 CAS

```python
outs["result"] = result       # ✅ CAS 对象
outs["result"] = result.wire  # ❌ 禁止
```

### 6. 条件寄存器更新

```python
storage.assign(wr_data, when=wr_en)
```

### 7. 清晰的周期标记

```python
# === Stage 1: Fetch ===
domain.next()
```

### 8. 调试命名

```python
result = (a + b).named("sum_ab")
```

### 9. 大型项目组织

```text
designs/my_soc/
├── common/
│   └── parameters.py         # 全局参数
├── frontend/
│   ├── fetch/fetch.py         # fetch()
│   └── decode/decode.py       # decoder()
├── backend/
│   ├── scalar_exu/alu.py      # alu()
│   └── scalar_rs/scalar_rs.py # scalar_rs()
├── soc_top.py                 # soc_top()
└── tests/
    ├── unit/test_alu.py       # 独立编译测试（CycleAwareTb）
    └── integration/test_*.py  # 集成测试
```

**原则：**

- 每个模块函数独占一个文件，文件名 = 模块名
- 全局参数集中在 `parameters.py`
- 每个模块可独立 `compile_cycle_aware()` 编译测试
- 所有 `inputs`/`outs` dict 都存 `CycleAwareSignal`

---

## 附录：API 参考表

### CycleAwareCircuit

| 方法 | 说明 |
|------|------|
| `CycleAwareCircuit(name)` | 创建顶层电路 |
| `create_domain(name, ...)` | 创建时钟域 |
| `input(name, *, width)` | 声明输入端口（返回 Wire，需用 `cas()` 包装） |
| `output(name, wire)` | 声明输出端口（传入 `wire_of(signal)`） |
| `const(value, *, width)` | 创建常量（用 `cas()` 包装） |
| `emit_mlir()` | 导出 MLIR |

### CycleAwareDomain

| 方法 | 说明 |
|------|------|
| `signal(*, width, reset_value=0, name="")` | **前向声明信号**（`ForwardSignal`）——唯一的寄存器创建方式 |
| `next()` / `prev()` | 推进 / 回退逻辑周期 |
| `push()` / `pop()` | 周期栈（配对使用） |
| `call(fn, *, inputs=None, **kwargs)` | push + 调用子模块 + pop，返回子模块输出 dict |
| `create_signal(name, *, width)` | 创建输入端口（返回 Wire） |
| `create_const(value, *, width)` | 常量 |
| `create_reset()` | 复位信号 |
| `cycle_index` | 当前逻辑周期索引 |

> **禁止**：`domain.state()` 已从 V5 API 中移除。

### 全局函数

| 函数 | 说明 |
|------|------|
| `cas(domain, wire, cycle=N)` | 将 Wire 包装为 CycleAwareSignal |
| `true_val if condition else false_val` | 多路选择（自动周期对齐） |
| `submodule_input(io, key, m, domain, *, prefix, width, cycle=0)` | 双模输入：有 inputs 时返回传入信号，否则创建 m.input() |
| `wire_of(sig)` | 安全提取 Wire（**仅用于 m.output()**） |

### ForwardSignal

| 方法 / 运算符 | 说明 |
|------|------|
| `sig <<= expr` | 无条件赋值 |
| `sig.assign(expr, when=cond)` | 条件赋值（带使能） |
| `sig.cycle` | 读周期 |
| 算术 / 位运算 / 比较 / 索引 | 与 `CycleAwareSignal` 完全相同 |

### compile_cycle_aware

| 参数 | 说明 |
|------|------|
| `fn` | `def fn(m, domain, ...) -> dict` |
| `name` | 模块名称 |
| `domain_name` | 时钟域名（默认 `"clk"`） |
| `eager` | `True` 时直接执行 Python 函数体 |
| `hierarchical` | `True` 时保留 `domain.call()` 边界为独立 MLIR 模块（需 `eager=True`） |
| `**jit_params` | 传递给 `fn` 的额外关键字参数 |

### CycleAwareTb

| 方法 | 说明 |
|------|------|
| `CycleAwareTb(t: Tb)` | 包装 `Tb` 实例 |
| `tb.next()` | 推进到下一个时钟周期 |
| `tb.cycle` | 当前周期索引 |
| `tb.drive(port, value)` | 在当前周期驱动端口 |
| `tb.expect(port, value, ...)` | 在当前周期检查端口值 |
| `tb.finish()` | 在当前周期结束仿真 |

---

**Copyright © 2024-2026 Liao Heng / PyCircuit Contributors. All rights reserved.**
