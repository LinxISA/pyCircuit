"""Cube v2 L0 Buffer Entry Module.

A single L0 buffer entry that stores a 16×16 tile of 16-bit elements.
This module is designed for reuse via m.instance() to reduce code size.
"""

from __future__ import annotations

from pycircuit import Circuit, module

from janus.cube.cube_v2_consts import (
    ARRAY_SIZE,
    INPUT_WIDTH,
)


@module(name="L0Entry")
def build_l0_entry(m: Circuit) -> None:
    """Build a single L0 buffer entry.

    Stores 16×16 = 256 elements of 16 bits each.

    Inputs:
        clk, rst: Clock and reset
        load_valid: Load enable
        load_row: Row index for load (4-bit)
        load_col: Column index for load (4-bit)
        load_data: Data to load (16-bit)
        read_row: Row index for read (4-bit)
        read_col: Column index for read (4-bit)

    Outputs:
        valid: Entry is valid (fully loaded)
        read_data: Data at read position (16-bit)
        For systolic array, we output all 256 elements as flat outputs
    """
    clk = m.clock("clk")
    rst = m.reset("rst")
    c = m.const

    # Load interface
    load_valid = m.input("load_valid", width=1)
    load_row = m.input("load_row", width=4)
    load_col = m.input("load_col", width=4)
    load_data = m.input("load_data", width=INPUT_WIDTH)

    # Status register
    valid_reg = m.out("valid_reg", clk=clk, rst=rst, width=1, init=0, en=c(1, width=1))

    # Data registers - 16×16 = 256 elements
    data_regs = []
    for row in range(ARRAY_SIZE):
        row_regs = []
        for col in range(ARRAY_SIZE):
            reg = m.out(
                f"d_r{row}_c{col}",
                clk=clk,
                rst=rst,
                width=INPUT_WIDTH,
                init=0,
                en=c(1, width=1),
            )
            row_regs.append(reg)
        data_regs.append(row_regs)

    # Load logic - write to specific element
    for row in range(ARRAY_SIZE):
        row_match = load_row.eq(c(row, width=4))
        for col in range(ARRAY_SIZE):
            col_match = load_col.eq(c(col, width=4))
            write_this = load_valid & row_match & col_match
            data_regs[row][col].set(load_data, when=write_this)

    # Mark as valid when last element loaded (row=15, col=15)
    last_elem = (
        load_valid
        & load_row.eq(c(ARRAY_SIZE - 1, width=4))
        & load_col.eq(c(ARRAY_SIZE - 1, width=4))
    )
    valid_reg.set(c(1, width=1), when=last_elem)

    # Output valid status
    m.output("valid", valid_reg.out())

    # Output all 256 elements for systolic array access
    for row in range(ARRAY_SIZE):
        for col in range(ARRAY_SIZE):
            m.output(f"d_r{row}_c{col}", data_regs[row][col].out())
