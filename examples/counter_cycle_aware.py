# -*- coding: utf-8 -*-
"""Cycle-aware counter example.

Demonstrates the new CycleAwareSignal system:
- Signals automatically carry cycle information
- Automatic DFF insertion for cycle balancing when signals of different cycles are combined
- Use domain.next() to advance the clock cycle
"""

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    compile_cycle_aware,
    mux,
)


def counter_design(m: CycleAwareCircuit, domain: CycleAwareDomain, width: int = 8) -> None:
    """简单计数器设计。
    
    Args:
        m: 周期感知电路构建器
        domain: 时钟域
        width: 计数器位宽
    """
    # 创建输入信号 (Cycle 0)
    enable = domain.create_signal("enable", width=1)
    
    # 创建计数器值（初始为0）
    count = domain.create_const(0, width=width, name="count_init")
    
    # 计数逻辑 (组合逻辑，仍在 Cycle 0)
    count_next = count + 1
    count_with_enable = mux(enable, count_next, count)
    
    # 推进到下一个周期
    domain.next()  # -> Cycle 1
    
    # 创建寄存器（输入来自Cycle 0，输出在Cycle 1）
    # domain.cycle() 自动处理周期延迟
    count_reg = domain.cycle(count_with_enable, reset_value=0, name="count")
    
    # 输出（Cycle 1）
    m.output("count", count_reg.sig)


def counter_with_auto_balance(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    width: int = 8,
) -> None:
    """展示自动周期平衡的计数器。
    
    当不同周期的信号参与运算时，系统会自动插入DFF链对齐。
    """
    # Cycle 0: 输入
    enable = domain.create_signal("enable", width=1)
    data_in = domain.create_signal("data_in", width=width)
    
    # 保存 data_in 的引用（Cycle 0）
    data_at_cycle0 = data_in
    
    # Cycle 0: 组合逻辑
    processed = data_in + 1
    
    domain.next()  # -> Cycle 1
    
    # Cycle 1: 第一级寄存器
    stage1 = domain.cycle(processed, reset_value=0, name="stage1")
    
    domain.next()  # -> Cycle 2
    
    # Cycle 2: 第二级寄存器
    stage2 = domain.cycle(stage1, reset_value=0, name="stage2")
    
    # 自动周期平衡演示：
    # data_at_cycle0 是 Cycle 0，stage2 是 Cycle 2
    # 当它们相加时，data_at_cycle0 会自动延迟 2 个周期
    combined = data_at_cycle0 + stage2  # 自动插入 2 级 DFF
    
    # combined 的周期是 max(0, 2) = 2
    m.output("combined", combined.sig)
    m.output("stage2", stage2.sig)


def pipeline_example(m: CycleAwareCircuit, domain: CycleAwareDomain) -> None:
    """三级流水线示例。
    
    展示如何使用周期感知系统构建流水线。
    """
    # Stage 0 (Cycle 0): 输入
    domain.push()  # 保存周期状态
    
    data_in = domain.create_signal("data_in", width=16)
    stage0_result = data_in + 1
    stage0_result = stage0_result.named("stage0_result")
    
    domain.next()  # -> Cycle 1
    
    # Stage 1 (Cycle 1): 第一级处理
    stage1_reg = domain.cycle(stage0_result, name="stage1_reg")
    stage1_result = stage1_reg * 2
    stage1_result = stage1_result.named("stage1_result")
    
    domain.next()  # -> Cycle 2
    
    # Stage 2 (Cycle 2): 第二级处理
    stage2_reg = domain.cycle(stage1_result, name="stage2_reg")
    stage2_result = stage2_reg & 0xFF  # 取低8位
    stage2_result = stage2_result.named("stage2_result")
    
    domain.next()  # -> Cycle 3
    
    # Stage 3 (Cycle 3): 输出寄存器
    output_reg = domain.cycle(stage2_result, name="output_reg")
    
    m.output("result", output_reg.sig)
    
    domain.pop()  # 恢复周期状态


# 使用 JIT 编译
if __name__ == "__main__":
    # 编译简单计数器
    counter = compile_cycle_aware(counter_design, name="counter", width=8)
    print("=== 简单计数器 ===")
    print(counter.emit_mlir())
    print()
    
    # 编译带自动平衡的计数器
    auto_balance = compile_cycle_aware(counter_with_auto_balance, name="counter_auto_balance", width=8)
    print("=== 自动周期平衡计数器 ===")
    print(auto_balance.emit_mlir())
    print()
    
    # 编译流水线示例
    pipeline = compile_cycle_aware(pipeline_example, name="pipeline_3stage")
    print("=== 三级流水线 ===")
    print(pipeline.emit_mlir())
