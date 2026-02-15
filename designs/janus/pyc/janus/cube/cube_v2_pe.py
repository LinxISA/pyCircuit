"""Cube v2 Processing Element (PE) Module.

A single PE performs dot product computation for one output element.
This module is designed for reuse via m.instance() to reduce code size.
"""
from __future__ import annotations
from pycircuit import Circuit, module, u, unsigned
from janus.cube.cube_v2_consts import ARRAY_SIZE, INPUT_WIDTH, OUTPUT_WIDTH

@module(name='CubePE')
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
    clk = m.clock('clk')
    rst = m.reset('rst')
    compute = m.input('compute', width=1)
    clear_acc = m.input('clear_acc', width=1)
    a_inputs = [m.input(f'a{k}', width=INPUT_WIDTH) for k in range(ARRAY_SIZE)]
    b_inputs = [m.input(f'b{k}', width=INPUT_WIDTH) for k in range(ARRAY_SIZE)]
    partial_in = m.input('partial_in', width=OUTPUT_WIDTH)
    acc = m.out('acc_reg', clk=clk, rst=rst, width=OUTPUT_WIDTH, init=0, en=u(1, 1))
    products = []
    for k in range(ARRAY_SIZE):
        product = unsigned(a_inputs[k]) + unsigned(b_inputs[k])
        products.append(product)
    dot_product = products[0]
    for p in products[1:]:
        dot_product = dot_product + p
    current_acc = acc.out()
    acc_base = clear_acc._select_internal(u(OUTPUT_WIDTH, 0), current_acc)
    result = acc_base + dot_product + partial_in
    acc.set(result, when=compute)
    m.output('result', result)
