# PyCircuit Programming Tutorial

**作者：Liao Heng**

**版本：1.0**

---

## 目录

1. [概述](#概述)
2. [核心概念](#核心概念)
   - [Clock Domain（时钟域）](#clock-domain时钟域)
   - [Signal（信号）](#signal信号)
   - [Module（模块）](#module模块)
   - [clock_domain.next()](#clock_domainnext)
   - [clock_domain.prev()](#clock_domainprev)
   - [clock_domain.push() / pop()](#clock_domainpush--pop)
   - [clock_domain.cycle()](#clock_domaincycle)
   - [Nested Module（嵌套模块）](#nested-module嵌套模块)
3. [自动周期平衡](#自动周期平衡)
4. [两种输出模式](#两种输出模式)
5. [编程范例](#编程范例)
   - [范例1：频率分频器（testdivider.py）](#范例1频率分频器testdividerpy)
   - [范例2：实时时钟系统（testproject.py）](#范例2实时时钟系统testprojectpy)
   - [范例3：RISC-V CPU（riscv.py）](#范例3risc-v-cpuriscvpy)
6. [生成的电路图](#生成的电路图)
7. [最佳实践](#最佳实践)

---

## 概述

PyCircuit 是一个基于 Python 的硬件描述语言（HDL）框架，专为数字电路设计而创建。它提供了一种直观的方式来描述时序逻辑电路，核心特性包括：

- **周期感知信号（Cycle-aware Signals）**：每个信号都携带其时序周期信息
- **多时钟域支持**：独立管理多个时钟域及其复位信号
- **自动周期平衡**：自动插入延迟（DFF）或反馈（FB）以对齐信号时序
- **自动变量名提取**：使用 JIT 方法从 Python 源码提取变量名
- **层次化/扁平化输出**：支持两种电路描述模式

### 安装与导入

```python
from pyCircuit import (
    pyc_ClockDomain,      # 时钟域
    pyc_Signal,           # 信号类
    pyc_CircuitModule,    # 电路模块基类
    pyc_CircuitLogger,    # 电路日志器
    signal,               # 信号创建快捷方式
    log,                  # 日志函数
    mux                   # 多路选择器
)
import pyCircuit
from pyVisualize import visualize_circuit  # 可视化工具
```

---

## 核心概念

### Clock Domain（时钟域）

时钟域是 PyCircuit 中最基础的概念，它代表一个独立的时钟信号及其相关的时序逻辑。

#### 创建时钟域

```python
# 语法
clock_domain = pyc_ClockDomain(name, frequency_desc="", reset_active_high=False)

# 示例
cpu_clk = pyc_ClockDomain("CPU_CLK", "100MHz CPU clock", reset_active_high=False)
rtc_clk = pyc_ClockDomain("RTC_CLK", "1Hz RTC domain", reset_active_high=False)
```

**参数说明：**
- `name`：时钟域名称（字符串）
- `frequency_desc`：频率描述（可选，用于文档）
- `reset_active_high`：复位信号极性，`False` 表示低电平有效（rstn）

#### 创建复位信号

```python
rst = clock_domain.create_reset()  # 创建复位信号
# 自动命名为 {domain_name}_rstn 或 {domain_name}_rst
```

#### 创建输入信号

```python
clk_in = clock_domain.create_signal("clock_input")
data_in = clock_domain.create_signal("data_input")
```

---

### Signal（信号）

信号是 PyCircuit 中的基本数据单元，每个信号都包含：
- 表达式（expression）
- 周期（cycle）
- 时钟域（domain）
- 位宽（width，可选）

#### 信号创建语法

```python
# 方式1：使用 signal 快捷方式（推荐）
counter = signal[7:0](value=0) | "8-bit counter"
data = signal[31:0](value="input_data") | "32-bit data"
flag = signal(value="condition") | "Boolean flag"

# 方式2：动态位宽
bits = 8
reg = signal[f"{bits}-1:0"](value=0) | "Dynamic width register"

# 方式3：位选择表达式
opcode = signal[6:0](value=f"{instruction}[6:0]") | "Opcode field"
```

**语法说明：**
- `signal[high:low](value=...)`：创建指定位宽的信号
- `| "description"`：管道运算符添加描述（可选但推荐）
- `value` 可以是：
  - 整数常量：`0`, `0xFF`
  - 字符串表达式：`"input_data"`, `"a + b"`
  - 格式化字符串：`f"{other_signal}[7:0]"`

#### 信号运算

PyCircuit 重载了 Python 运算符，支持硬件描述式的信号运算：

```python
# 算术运算
sum_val = (a + b) | "Addition"
diff = (a - b) | "Subtraction"
prod = (a * b) | "Multiplication"

# 逻辑运算
and_result = (a & b) | "Bitwise AND"
or_result = (a | b) | "Bitwise OR"
xor_result = (a ^ b) | "Bitwise XOR"
not_result = (~a) | "Bitwise NOT"

# 比较运算
eq = (a == b) | "Equal"
ne = (a != b) | "Not equal"
lt = (a < b) | "Less than"
gt = (a > b) | "Greater than"

# 多路选择器
result = mux(condition, true_value, false_value) | "Mux selection"
```

---

### Module（模块）

模块是电路设计的基本组织单元，封装了一组相关的信号和逻辑。

#### 定义模块类

```python
class MyModule(pyc_CircuitModule):
    """自定义电路模块"""
    
    def __init__(self, name, clock_domain):
        super().__init__(name, clock_domain=clock_domain)
        # 初始化模块参数
        
    def build(self, input1, input2):
        """构建模块逻辑"""
        with self.module(
            inputs=[input1, input2],
            description="Module description"
        ) as mod:
            # 模块内部逻辑
            result = (input1 + input2) | "Sum"
            
            # 设置输出
            mod.outputs = [result]
        
        return result
```

#### 模块上下文管理器

`self.module()` 返回一个上下文管理器，用于：
- 记录模块边界
- 管理输入/输出信号
- 在嵌套模块中正确处理时钟周期

```python
with self.module(
    inputs=[sig1, sig2],      # 输入信号列表
    description="描述文字"     # 模块描述
) as mod:
    # 模块逻辑
    mod.outputs = [out1, out2]  # 设置输出
```

---

### clock_domain.next()

`next()` 方法推进时钟周期边界，标记时序逻辑的分界点。

#### 语法

```python
self.clock_domain.next()  # 推进到下一个时钟周期
```

#### 语义

- 调用 `next()` 后，所有新创建的信号将属于新的周期
- 用于分隔组合逻辑和时序逻辑
- 在流水线设计中标记各级边界

#### 示例

```python
def build(self, input_data):
    with self.module(inputs=[input_data]) as mod:
        # Cycle 0: 输入处理
        processed = (input_data & 0xFF) | "Masked input"
        
        self.clock_domain.next()  # 推进到 Cycle 1
        
        # Cycle 1: 进一步处理
        result = (processed + 1) | "Incremented"
        
        self.clock_domain.next()  # 推进到 Cycle 2
        
        # Cycle 2: 输出
        output = result | "Final output"
        mod.outputs = [output]
```

---

### clock_domain.prev()

`prev()` 方法将时钟周期回退一步，与 `next()` 相反。

#### 语法

```python
self.clock_domain.prev()  # 回退到上一个时钟周期
```

#### 语义

- 调用 `prev()` 后，当前默认周期减 1
- 允许在过程式编程中灵活调整周期位置
- 周期计数可以变为负数（这是设计允许的）

#### 示例

```python
def build(self, input_data):
    with self.module(inputs=[input_data]) as mod:
        # Cycle 0
        a = input_data | "Input"
        
        self.clock_domain.next()  # -> Cycle 1
        b = (a + 1) | "Incremented"
        
        self.clock_domain.next()  # -> Cycle 2
        c = (b * 2) | "Doubled"
        
        self.clock_domain.prev()  # -> Cycle 1 (回退)
        # 现在我们回到了 Cycle 1，可以添加更多同周期的信号
        d = (a - 1) | "Decremented"
```

---

### clock_domain.push() / pop()

`push()` 和 `pop()` 方法提供周期状态的栈管理，允许子函数拥有独立的周期划分而不影响调用者。

#### 语法

```python
self.clock_domain.push()  # 保存当前周期到栈
# ... 进行周期操作 ...
self.clock_domain.pop()   # 恢复之前保存的周期
```

#### 语义

- `push()` 将当前周期状态保存到用户周期栈
- `pop()` 从栈中弹出并恢复周期状态
- 支持嵌套调用（多层 push/pop）
- 如果 `pop()` 在没有匹配的 `push()` 时调用，会抛出 `RuntimeError`

#### 使用场景

这些方法特别适合过程式编程，允许不同的子函数拥有独立的周期管理策略：

```python
class MyModule(pyc_CircuitModule):
    def helper_function_a(self, data):
        """子函数 A：使用 2 个周期"""
        self.clock_domain.push()  # 保存调用者的周期状态
        
        # 进行自己的周期划分
        result = data | "Input"
        self.clock_domain.next()
        result = (result + 1) | "Processed"
        self.clock_domain.next()
        final = (result * 2) | "Final"
        
        self.clock_domain.pop()   # 恢复调用者的周期状态
        return final
    
    def helper_function_b(self, data):
        """子函数 B：使用 1 个周期"""
        self.clock_domain.push()  # 保存调用者的周期状态
        
        # 不同的周期划分策略
        result = (data & 0xFF) | "Masked"
        self.clock_domain.next()
        output = (result | 0x100) | "Flagged"
        
        self.clock_domain.pop()   # 恢复调用者的周期状态
        return output
    
    def build(self, input_data):
        with self.module(inputs=[input_data]) as mod:
            # Cycle 0
            processed = input_data | "Input"
            
            # 调用子函数，它们各自管理自己的周期
            result_a = self.helper_function_a(processed)
            result_b = self.helper_function_b(processed)
            
            # 仍在 Cycle 0（子函数的周期操作不影响这里）
            combined = (result_a + result_b) | "Combined"
            
            mod.outputs = [combined]
```

#### 嵌套使用示例

```python
def outer_function(self, data):
    self.clock_domain.push()  # 保存周期 0
    
    self.clock_domain.next()  # -> 周期 1
    intermediate = self.inner_function(data)  # inner 也可以 push/pop
    
    self.clock_domain.next()  # -> 周期 2
    result = intermediate | "Result"
    
    self.clock_domain.pop()   # 恢复周期 0
    return result

def inner_function(self, data):
    self.clock_domain.push()  # 保存周期 1
    
    self.clock_domain.next()  # -> 周期 2
    self.clock_domain.next()  # -> 周期 3
    processed = data | "Processed"
    
    self.clock_domain.pop()   # 恢复周期 1
    return processed
```

---

### clock_domain.cycle()

`cycle()` 方法实现 D 触发器（单周期延迟），用于创建时序元件。

#### 语法

```python
registered = self.clock_domain.cycle(signal, description="", reset_value=None)
```

**参数：**
- `signal`：要寄存的信号
- `description`：描述（可选）
- `reset_value`：复位值（可选）

#### 语义

- 输出信号的周期 = 输入信号周期 + 1
- 如果指定 `reset_value`，生成带复位的 DFF
- 等效于 Verilog 的 `always @(posedge clk)` 块

#### 示例

```python
# 简单寄存器
data_reg = self.clock_domain.cycle(data, "Data register")

# 带复位值的计数器
counter_reg = self.clock_domain.cycle(counter_next, reset_value=0) | "Counter register"

# 流水线寄存器
stage1_reg = self.clock_domain.cycle(stage0_out, "Pipeline stage 1")
stage2_reg = self.clock_domain.cycle(stage1_reg, "Pipeline stage 2")
```

---

### Nested Module（嵌套模块）

PyCircuit 支持模块的层次化设计，允许在一个模块内实例化其他模块。

#### 语法

```python
# 在父模块的 build 方法中
submodule = SubModuleClass("instance_name", self.clock_domain)
outputs = submodule.build(input1, input2)
```

#### 子模块周期隔离

子模块内部调用 `clock_domain.next()` 不会影响父模块的周期状态：

```python
class ParentModule(pyc_CircuitModule):
    def build(self, input_data):
        with self.module(inputs=[input_data]) as mod:
            # 父模块 Cycle 0
            processed = input_data | "Input"
            
            self.clock_domain.next()  # 父模块推进到 Cycle 1
            
            # 实例化子模块
            child = ChildModule("child", self.clock_domain)
            result = child.build(processed)  # 子模块内部可以有自己的 next()
            
            # 仍在父模块 Cycle 1（子模块的 next() 不影响父模块）
            output = result | "Output"
            mod.outputs = [output]
```

#### 层次化 vs 扁平化

PyCircuit 支持两种输出模式：

1. **层次化模式（Hierarchical）**：保留模块边界，显示嵌套结构
2. **扁平化模式（Flatten）**：展开所有子模块，信号名带模块前缀

```python
# 层次化模式
hier_logger = pyc_CircuitLogger("circuit.txt", is_flatten=False)

# 扁平化模式
flat_logger = pyc_CircuitLogger("circuit.txt", is_flatten=True)
```

---

## 自动周期平衡

PyCircuit 的核心特性之一是自动周期平衡（Automatic Cycle Balancing）。

### 规则

当组合不同周期的信号时：
- **输出周期 ≥ max(输入周期)**
- 如果输入周期 < 输出周期：自动插入 `DFF`（延迟）
- 如果输入周期 > 输出周期：自动插入 `FB`（反馈）
- 如果输入周期 == 输出周期：直接使用

### 示例

```python
# sig_a 在 Cycle 0，sig_b 在 Cycle 2
result = (sig_a & sig_b) | "Combined"
# 输出：result 在 Cycle 2，sig_a 自动延迟 2 个周期
```

生成的描述：
```
result = (DFF(DFF(sig_a)) & sig_b)
  → Cycle balancing: inputs at [0, 2] → output at 2
```

---

## 两种输出模式

### 层次化模式（Hierarchical Mode）

保留模块层次结构，每个模块独立显示：

```
┌────────────────────────────────────────────────────────────────────────────┐
│ MODULE: ParentModule                                                       │
└────────────────────────────────────────────────────────────────────────────┘
  INPUTS:
    • input_signal                           [cycle=0, domain=CLK]
  
  SUBMODULES:
    • ChildModule
      - Inputs: processed
      - Outputs: result
  
  OUTPUTS:
    • output                                 [cycle=2, domain=CLK]
```

### 扁平化模式（Flatten Mode）

展开所有子模块，信号名带模块前缀：

```
┌────────────────────────────────────────────────────────────────────────────┐
│ MODULE: TopLevel                                                           │
└────────────────────────────────────────────────────────────────────────────┘
  SIGNALS:
    ChildModule.internal_sig = ...
    ChildModule.result = ...
    output = ChildModule.result
```

---

## 编程范例

### 范例1：频率分频器（testdivider.py）

这是一个简单的频率分频器，将输入时钟分频为指定倍数。

#### 代码

```python
class FrequencyDivider(pyc_CircuitModule):
    """
    频率分频器模块
    """
    
    def __init__(self, name, divide_by, input_clock_domain):
        super().__init__(name, clock_domain=input_clock_domain)
        self.divide_by = divide_by
        self.counter_bits = (divide_by - 1).bit_length()
        
    def build(self, clk_in):
        """构建分频器电路"""
        with self.module(
            inputs=[clk_in],
            description=f"Frequency Divider: Divide by {self.divide_by}"
        ) as mod:
            # 初始化计数器（Cycle -1：初始化信号）
            counter = signal[f"{self.counter_bits}-1:0"](value=0) | "Counter initial value"
            
            # 计数器逻辑
            counter_next = (counter + 1) | "Counter increment"
            counter_eq = (counter == (self.divide_by - 1)) | f"Counter == {self.divide_by-1}"
            counter_wrap = mux(counter_eq, 0, counter_next) | "Counter wrap-around"
            
            self.clock_domain.next()  # 推进到下一周期
            
            # 更新计数器（反馈）
            counter = counter_wrap | "update counter"
            
            # 输出使能信号
            clk_enable = (counter == (self.divide_by - 1)) | "Clock enable output"
            
            mod.outputs = [clk_enable]
        
        return clk_enable
```

#### 使用方法

```python
def main():
    # 创建时钟域
    clk_domain = pyc_ClockDomain("DIV_CLK", "Divider clock domain")
    clk_domain.create_reset()
    
    clk_domain.next()
    clk_in = clk_domain.create_signal("clock_in")
    
    # 实例化分频器
    divider = FrequencyDivider("Divider13", 13, clk_domain)
    clk_enable = divider.build(clk_in)
```

#### 生成的电路描述

**层次化模式（hier_testdivider.txt）：**

```
================================================================================
CIRCUIT DESCRIPTION (HIERARCHICAL MODE)
================================================================================

┌────────────────────────────────────────────────────────────────────────────┐
│ MODULE: Divider13                                                          │
│ Frequency Divider: Divide by 13                                           │
└────────────────────────────────────────────────────────────────────────────┘

  INPUTS:
    • clock_in                                 [cycle=-1, domain=DIV_CLK]

  SIGNALS:

    ──────────────────────────────────────────────────────────────────────
    CYCLE -1
    ──────────────────────────────────────────────────────────────────────

    counter = forward_declare("Counter initial value")
      // Counter initial value


    ──────────────────────────────────────────────────────────────────────
    CYCLE 1
    ──────────────────────────────────────────────────────────────────────

    counter_next = (counter + 1)
      // Counter increment
      → Cycle balancing: inputs at [-1] → output at 1

    counter_eq = (counter == (self.divide_by - 1))
      // Counter == 12
      → Cycle balancing: inputs at [-1] → output at 1

    counter_wrap = mux(counter_eq, 0, counter_next)
      // Counter wrap-around (mux)


    ──────────────────────────────────────────────────────────────────────
    CYCLE 2
    ──────────────────────────────────────────────────────────────────────

    counter = counter_wrap
      // Feedback: update counter
      → Cycle balancing: inputs at [1] → output at 2

    clk_enable = (counter == (self.divide_by - 1))
      // Clock enable output

  OUTPUTS:
    • clk_enable                               [cycle=2, domain=DIV_CLK]
```

#### 电路图

![Hierarchical Divider](hier_testdivider.pdf)

![Flatten Divider](flat_testdivider.pdf)

---

### 范例2：实时时钟系统（testproject.py）

这是一个完整的实时时钟系统，包含：
- 高频振荡器时钟域
- 频率分频器（1024分频）
- 带 SET/PLUS/MINUS 按钮的实时时钟

#### 多时钟域示例

```python
# 创建两个独立的时钟域
osc_domain = pyc_ClockDomain("OSC_CLK", "High-frequency oscillator domain")
rtc_domain = pyc_ClockDomain("RTC_CLK", "1Hz RTC domain")

# 各自创建复位信号
osc_rst = osc_domain.create_reset()
rtc_rst = rtc_domain.create_reset()
```

#### 实时时钟模块

```python
class RealTimeClock(pyc_CircuitModule):
    """带按钮控制的实时时钟"""
    
    STATE_RUNNING = 0
    STATE_SETTING_HOUR = 1
    STATE_SETTING_MINUTE = 2
    STATE_SETTING_SECOND = 3
    
    def __init__(self, name, rtc_clock_domain):
        super().__init__(name, clock_domain=rtc_clock_domain)
        
    def build(self, clk_enable, set_btn, plus_btn, minus_btn):
        with self.module(
            inputs=[clk_enable, set_btn, plus_btn, minus_btn],
            description="Real-Time Clock with SET/PLUS/MINUS control"
        ) as mod:
            # 初始化时间计数器
            sec = signal[5:0](value=0) | "Seconds"
            min = signal[5:0](value=0) | "Minutes"
            hr = signal[4:0](value=0) | "Hours"
            state = signal[1:0](value=self.STATE_RUNNING) | "State"
            
            self.clock_domain.next()
            
            # 状态机逻辑
            state_is_running = (state == self.STATE_RUNNING) | "Check RUNNING"
            # ... 更多逻辑 ...
            
            self.clock_domain.next()
            
            # 寄存时间值
            seconds_out = self.clock_domain.cycle(sec_next, reset_value=0)
            minutes_out = self.clock_domain.cycle(min_next, reset_value=0)
            hours_out = self.clock_domain.cycle(hr_next, reset_value=0)
            
            mod.outputs = [seconds_out, minutes_out, hours_out, state]
```

#### 生成的电路描述

**层次化模式部分输出（hier_circuit.txt）：**

```
┌────────────────────────────────────────────────────────────────────────────┐
│ MODULE: FreqDiv1024                                                        │
│ Frequency Divider: Divide by 1024                                         │
└────────────────────────────────────────────────────────────────────────────┘

  INPUTS:
    • oscillator_in                            [cycle=-1, domain=OSC_CLK]

  SIGNALS:
    ...

  OUTPUTS:
    • clk_enable                               [cycle=3, domain=OSC_CLK]


┌────────────────────────────────────────────────────────────────────────────┐
│ MODULE: RTC                                                                │
│ Real-Time Clock with SET/PLUS/MINUS control buttons                       │
└────────────────────────────────────────────────────────────────────────────┘

  INPUTS:
    • clk_enable                               [cycle=3, domain=OSC_CLK]
    • SET_btn                                  [cycle=-1, domain=RTC_CLK]
    • PLUS_btn                                 [cycle=-1, domain=RTC_CLK]
    • MINUS_btn                                [cycle=-1, domain=RTC_CLK]
    ...
```

#### 电路图

**频率分频器模块：**

![FreqDiv1024](hier_FreqDiv1024.pdf)

**实时时钟模块：**

![RTC](hier_RTC.pdf)

**扁平化模式完整电路：**

![Flatten Circuit](flat_circuit_diagram.pdf)

---

### 范例3：RISC-V CPU（riscv.py）

这是一个完整的 RISC-V CPU 实现，展示了 PyCircuit 处理复杂层次化设计的能力。

#### CPU 结构

```
RISCVCpu
├── InstructionDecoder  (指令解码器)
├── RegisterFile        (寄存器文件)
├── ALU                 (算术逻辑单元)
└── ExceptionHandler    (异常处理器)
```

#### 5 级流水线实现

```python
class RISCVCpu(pyc_CircuitModule):
    def build(self, instruction_mem_data, data_mem_data, interrupt_req):
        with self.module(inputs=[...]) as mod:
            # ========== STAGE 1: INSTRUCTION FETCH ==========
            pc = signal[31:0](value=0) | "Program Counter"
            
            self.clock_domain.next()  # Cycle 1
            pc_next = pc + 4 | "PC + 4"
            instruction = instruction_mem_data | "Fetched instruction"
            
            # ========== STAGE 2: INSTRUCTION DECODE ==========
            self.clock_domain.next()  # Cycle 2
            instruction_reg = self.clock_domain.cycle(instruction)
            
            # 实例化解码器子模块
            decoder = InstructionDecoder("Decoder", self.clock_domain)
            (opcode, funct3, ...) = decoder.build(instruction_reg)
            
            # 实例化寄存器文件
            reg_file = RegisterFile("RegFile", self.clock_domain)
            rs1_data, rs2_data = reg_file.build(rs1, rs2, ...)
            
            # ========== STAGE 3: EXECUTE ==========
            self.clock_domain.next()  # Cycle 3
            
            # 实例化 ALU
            alu = ALU("ALU", self.clock_domain)
            alu_result, zero_flag, lt_flag = alu.build(...)
            
            # ========== STAGE 4: MEMORY ACCESS ==========
            self.clock_domain.next()  # Cycle 4
            
            # 异常处理
            exc_handler = ExceptionHandler("ExceptionHandler", self.clock_domain)
            exception_valid, exception_code, ... = exc_handler.build(...)
            
            # ========== STAGE 5: WRITE BACK ==========
            self.clock_domain.next()  # Cycle 5
            
            wb_data = mux(mem_read_wb, mem_data_wb, alu_result_wb) | "Write-back data"
```

#### 子模块示例：ALU

```python
class ALU(pyc_CircuitModule):
    """算术逻辑单元"""
    
    ALU_ADD = 0
    ALU_SUB = 1
    ALU_AND = 2
    # ... 更多操作码
    
    def build(self, operand_a, operand_b, alu_op):
        with self.module(inputs=[operand_a, operand_b, alu_op]) as mod:
            # 算术运算
            add_result = (operand_a + operand_b) | "ALU ADD"
            sub_result = (operand_a - operand_b) | "ALU SUB"
            
            # 逻辑运算
            and_result = (operand_a & operand_b) | "ALU AND"
            or_result = (operand_a | operand_b) | "ALU OR"
            
            # 使用 mux 链选择结果
            result = mux(alu_op == self.ALU_SUB, sub_result, add_result)
            result = mux(alu_op == self.ALU_AND, and_result, result)
            # ...
            
            mod.outputs = [result, zero_flag, lt_flag]
```

#### 生成的电路描述

**层次化模式（hier_riscv.txt）部分：**

```
┌────────────────────────────────────────────────────────────────────────────┐
│ MODULE: RISCVCpu                                                           │
│ RISC-V CPU: 5-stage pipeline with precise exception handling              │
└────────────────────────────────────────────────────────────────────────────┘

  INPUTS:
    • instruction_mem_data                     [cycle=-1, domain=CPU_CLK]
    • data_mem_data                            [cycle=-1, domain=CPU_CLK]
    • interrupt_req                            [cycle=-1, domain=CPU_CLK]

  SUBMODULES:
    • Decoder
    • RegFile
    • ALU
    • ExceptionHandler

  OUTPUTS:
    • pc                                       [cycle=6, domain=CPU_CLK]
    • instruction_mem_addr                     [cycle=6, domain=CPU_CLK]
    ...
```

#### 电路图

**RISC-V CPU 顶层模块（层次化）：**

![RISC-V CPU](hier_riscv_RISCVCpu.pdf)

**指令解码器模块：**

![Decoder](hier_riscv_Decoder.pdf)

**寄存器文件模块：**

![RegFile](hier_riscv_RegFile.pdf)

**ALU 模块：**

![ALU](hier_riscv_ALU.pdf)

**扁平化模式完整 CPU：**

![Flatten RISC-V](flat_riscv_RISCVCpu.pdf)

---

## 生成的电路图

PyCircuit 使用 `pyVisualize` 模块生成电路图，支持 PDF 和 PNG 格式。

### 使用方法

```python
from pyVisualize import visualize_circuit

# 生成完整电路图
pdf_file = visualize_circuit(
    logger,
    figsize=(18, 14),
    output_file="circuit_diagram.pdf"
)

# 生成单个模块的电路图
module_pdf = visualize_circuit(
    logger,
    module_name="ALU",
    output_file="alu_diagram.pdf"
)
```

### 输出文件列表

| 文件名 | 说明 |
|--------|------|
| `hier_testdivider.txt` | 分频器层次化描述 |
| `flat_testdivider.txt` | 分频器扁平化描述 |
| `hier_testdivider.pdf` | 分频器层次化电路图 |
| `flat_testdivider.pdf` | 分频器扁平化电路图 |
| `hier_circuit.txt` | RTC系统层次化描述 |
| `flat_circuit.txt` | RTC系统扁平化描述 |
| `hier_FreqDiv1024.pdf` | 频率分频器电路图 |
| `hier_RTC.pdf` | 实时时钟电路图 |
| `hier_riscv.txt` | RISC-V CPU 层次化描述 |
| `flat_riscv.txt` | RISC-V CPU 扁平化描述 |
| `hier_riscv_*.pdf` | 各模块层次化电路图 |
| `flat_riscv_*.pdf` | 扁平化电路图 |

---

## 最佳实践

### 1. 模块设计原则

```python
class GoodModule(pyc_CircuitModule):
    def __init__(self, name, clock_domain, param1, param2):
        super().__init__(name, clock_domain=clock_domain)
        self.param1 = param1  # 保存配置参数
        self.param2 = param2
        
    def build(self, input1, input2):
        # 使用 with 语句管理模块上下文
        with self.module(
            inputs=[input1, input2],
            description=f"Module with param1={self.param1}"
        ) as mod:
            # 模块逻辑
            result = ...
            
            # 明确设置输出
            mod.outputs = [result]
        
        return result  # 返回输出信号供父模块使用
```

### 2. 信号命名规范

```python
# ✓ 好的命名
counter_next = (counter + 1) | "Counter next value"
data_valid_reg = self.clock_domain.cycle(data_valid) | "Registered valid"

# ✗ 避免的命名
x = (a + b) | "Some signal"  # 太简短
temp = result | ""           # 无描述
```

### 3. 周期管理

```python
# ✓ 明确标记周期边界
self.clock_domain.next()  # Cycle N -> N+1

# 使用 cycle() 创建寄存器
registered_data = self.clock_domain.cycle(data, reset_value=0) | "Registered data"

# ✓ 理解自动周期平衡
# 当组合不同周期的信号时，系统会自动插入延迟
```

### 4. 层次化设计

```python
# ✓ 合理拆分模块
class TopLevel(pyc_CircuitModule):
    def build(self, ...):
        with self.module(...) as mod:
            # 实例化功能子模块
            decoder = Decoder("decoder", self.clock_domain)
            alu = ALU("alu", self.clock_domain)
            
            # 连接子模块
            decoded = decoder.build(instruction)
            result = alu.build(op_a, op_b, alu_op)
```

### 5. 调试技巧

```python
# 使用描述帮助调试
signal_name = expression | "Descriptive comment for debugging"

# 检查生成的 .txt 文件确认：
# - 信号周期是否正确
# - 自动周期平衡是否如预期
# - 模块层次是否正确
```

---

## 附录：API 参考

### pyc_ClockDomain

| 方法 | 说明 |
|------|------|
| `__init__(name, frequency_desc, reset_active_high)` | 创建时钟域 |
| `create_reset()` | 创建复位信号 |
| `create_signal(name)` | 创建输入信号 |
| `next()` | 推进时钟周期（周期 +1） |
| `prev()` | 回退时钟周期（周期 -1） |
| `push()` | 保存当前周期状态到栈 |
| `pop()` | 从栈恢复周期状态 |
| `cycle(signal, description, reset_value)` | 创建寄存器（DFF） |

### pyc_CircuitModule

| 方法 | 说明 |
|------|------|
| `__init__(name, clock_domain)` | 初始化模块 |
| `module(inputs, description)` | 模块上下文管理器 |
| `build(...)` | 构建模块逻辑（需重写） |

### pyc_CircuitLogger

| 方法 | 说明 |
|------|------|
| `__init__(filename, is_flatten)` | 创建日志器 |
| `write_to_file()` | 写入电路描述文件 |
| `reset()` | 重置日志器状态 |

### 全局函数

| 函数 | 说明 |
|------|------|
| `signal[high:low](value=...)` | 创建信号 |
| `mux(condition, true_val, false_val)` | 多路选择器 |
| `log(signal)` | 记录信号（用于调试） |

---

**Copyright © 2024 Liao Heng. All rights reserved.**

