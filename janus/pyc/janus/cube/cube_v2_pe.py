"""Cube v2 Processing Element (PE) Module.

A single PE performs dot product computation for one output element.
This module is designed for reuse via m.instance() to reduce code size.
"""

from __future__ import annotations

from pycircuit import Circuit, module

from janus.cube.cube_v2_consts import (
    ARRAY_SIZE,
    INPUT_WIDTH,
    OUTPUT_WIDTH,
)


@module(name="CubePE")
def build_pe(m: Circuit) -> None:
    """Build a single Processing Element.

    Inputs:
        clk, rst: Clock and reset
        compute: Enable computation
        clear_acc: Clear accumulator (for first uop)
        a0-a15: 16 input elements from L0A row (16-bit each)
        b0-b15: 16 input elements from L0B column (16-bit each)
        partial_in: Incoming partial sum from previous cluster (32-bit)

    Outputs:
        result: Computed result (32-bit)
    """
    clk = m.clock("clk")
    rst = m.reset("rst")

    compute = m.input("compute", width=1)
    clear_acc = m.input("clear_acc", width=1)

    # 16 input elements from L0A row
    a_inputs = [m.input(f"a{k}", width=INPUT_WIDTH) for k in range(ARRAY_SIZE)]

    # 16 input elements from L0B column
    b_inputs = [m.input(f"b{k}", width=INPUT_WIDTH) for k in range(ARRAY_SIZE)]

    # Incoming partial sum
    partial_in = m.input("partial_in", width=OUTPUT_WIDTH)

    # Accumulator register
    acc = m.out("acc_reg", clk=clk, rst=rst, width=OUTPUT_WIDTH, init=0, en=m.const(1, width=1))

    # Compute products and sum using tree reduction
    products = []
    for k in range(ARRAY_SIZE):
        # MAC: a × b (using addition as placeholder)
        # TODO: Replace with multiplication when supported
        product = a_inputs[k].zext(width=OUTPUT_WIDTH) + b_inputs[k].zext(width=OUTPUT_WIDTH)
        products.append(product)

    # Tree-based reduction for dot product
    def tree_reduce(vals):
        if len(vals) == 1:
            return vals[0]
        if len(vals) == 2:
            return vals[0] + vals[1]
        mid = len(vals) // 2
        return tree_reduce(vals[:mid]) + tree_reduce(vals[mid:])

    dot_product = tree_reduce(products)

    # Clear or accumulate based on clear_acc
    current_acc = acc.out()
    acc_base = clear_acc.select(m.const(0, width=OUTPUT_WIDTH), current_acc)
    result = acc_base + dot_product + partial_in

    # Store result
    acc.set(result, when=compute)

    m.output("result", result)
