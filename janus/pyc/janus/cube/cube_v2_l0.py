"""Cube v2 L0 Buffer Implementation (L0A and L0B).

L0A and L0B are input buffer register files that store 16×16 tiles of input matrices.
Each buffer has 16 entries, with each entry holding 256 16-bit elements (4096 bits).

Loading an entry requires multiple MMIO cycles.

Optimization: Uses binary tree mux for O(log n) depth instead of O(n) cascading mux.
"""

from __future__ import annotations

from pycircuit import Circuit, Wire, jit_inline

from janus.cube.cube_v2_consts import (
    ARRAY_SIZE,
    INPUT_WIDTH,
    L0A_ENTRIES,
    L0B_ENTRIES,
    L0_ENTRY_BITS,
    L0_IDX_WIDTH,
    MMIO_WIDTH,
)
from janus.cube.cube_v2_types import L0EntryStatus
from janus.cube.util import Consts


def _make_l0_entry_status(
    m: Circuit, clk: Wire, rst: Wire, consts: Consts, prefix: str, idx: int
) -> L0EntryStatus:
    """Create status registers for a single L0 buffer entry."""
    with m.scope(f"{prefix}_status_{idx}"):
        return L0EntryStatus(
            valid=m.out("valid", clk=clk, rst=rst, width=1, init=0, en=consts.one1),
            loading=m.out("loading", clk=clk, rst=rst, width=1, init=0, en=consts.one1),
            ref_count=m.out("ref_count", clk=clk, rst=rst, width=8, init=0, en=consts.one1),
        )


def _make_l0_data_regs(
    m: Circuit, clk: Wire, rst: Wire, consts: Consts, prefix: str, idx: int
) -> list:
    """Create data registers for a single L0 buffer entry.

    Each entry stores 16×16 = 256 elements of 16 bits each.
    Organized as 16 rows × 16 columns.
    """
    data_regs = []
    with m.scope(f"{prefix}_data_{idx}"):
        for row in range(ARRAY_SIZE):
            row_regs = []
            with m.scope(f"r{row}"):
                for col in range(ARRAY_SIZE):
                    reg = m.out(
                        f"c{col}",
                        clk=clk,
                        rst=rst,
                        width=INPUT_WIDTH,
                        init=0,
                        en=consts.one1,
                    )
                    row_regs.append(reg)
            data_regs.append(row_regs)
    return data_regs


def _binary_tree_mux(m: Circuit, idx: Wire, values: list[Wire], idx_width: int) -> Wire:
    """Build a binary tree mux for O(log n) depth selection.

    Args:
        m: Circuit module
        idx: Selection index wire
        values: List of values to select from (must be power of 2)
        idx_width: Width of index in bits

    Returns:
        Selected value wire
    """
    n = len(values)
    if n == 1:
        return values[0]
    if n == 2:
        # Use bit 0 of idx to select
        return idx[0].select(values[1], values[0])

    # Split into two halves and recurse
    mid = n // 2
    # Lower half selected when MSB is 0
    left = _binary_tree_mux(m, idx, values[:mid], idx_width - 1)
    # Upper half selected when MSB is 1
    right = _binary_tree_mux(m, idx, values[mid:], idx_width - 1)
    # Use MSB to select between halves
    msb_bit = idx_width - 1
    return idx[msb_bit].select(right, left)


@jit_inline
def build_l0_buffer(
    m: Circuit,
    *,
    clk: Wire,
    rst: Wire,
    consts: Consts,
    prefix: str,  # "l0a" or "l0b"
    num_entries: int,
    # Load interface (simplified - load one element at a time via 64-bit interface)
    load_entry_idx: Wire,  # Which entry to load (6-bit)
    load_row: Wire,        # Which row (4-bit)
    load_col: Wire,        # Which column (4-bit)
    load_data: Wire,       # Data (16-bit element in lower bits of 64-bit)
    load_valid: Wire,      # Load is valid
    # Read interface (for systolic array) - returns list of lists of elements
    read_entry_idx: Wire,  # Which entry to read
) -> tuple[list[L0EntryStatus], list[list[Wire]], Wire]:
    """Build L0 buffer (L0A or L0B).

    Returns:
        (status_list, read_data_matrix, load_done):
            Status for each entry, 16×16 matrix of elements, load complete
    """
    c = m.const

    with m.scope(prefix.upper()):
        # Create status registers for all entries
        status_list = []
        for i in range(num_entries):
            status = _make_l0_entry_status(m, clk, rst, consts, prefix, i)
            status_list.append(status)

        # Create data registers for all entries
        data_regs_list = []
        for i in range(num_entries):
            data_regs = _make_l0_data_regs(m, clk, rst, consts, prefix, i)
            data_regs_list.append(data_regs)

        # Load logic (element-by-element)
        with m.scope("LOAD"):
            load_done = consts.zero1

            for i in range(num_entries):
                entry_match = load_entry_idx.eq(c(i, width=L0_IDX_WIDTH))
                is_loading = entry_match & load_valid

                for row in range(ARRAY_SIZE):
                    row_match = load_row.eq(c(row, width=4))
                    for col in range(ARRAY_SIZE):
                        col_match = load_col.eq(c(col, width=4))
                        write_this = is_loading & row_match & col_match
                        data_regs_list[i][row][col].set(
                            load_data.trunc(width=INPUT_WIDTH), when=write_this
                        )

                # Mark as valid when last element loaded (row=15, col=15)
                last_elem = (
                    is_loading
                    & load_row.eq(c(ARRAY_SIZE - 1, width=4))
                    & load_col.eq(c(ARRAY_SIZE - 1, width=4))
                )
                status_list[i].valid.set(consts.one1, when=last_elem)
                load_done = load_done | last_elem

        # Read logic - returns 16×16 matrix of elements using binary tree mux
        with m.scope("READ"):
            read_data_matrix = []

            for row in range(ARRAY_SIZE):
                row_data = []
                for col in range(ARRAY_SIZE):
                    # Collect all entry values for this element
                    elem_values = [
                        data_regs_list[i][row][col].out()
                        for i in range(num_entries)
                    ]
                    # Use binary tree mux for O(log n) selection depth
                    elem = _binary_tree_mux(m, read_entry_idx, elem_values, L0_IDX_WIDTH)
                    row_data.append(elem)
                read_data_matrix.append(row_data)

        return status_list, read_data_matrix, load_done


def build_l0a(
    m: Circuit,
    *,
    clk: Wire,
    rst: Wire,
    consts: Consts,
    load_entry_idx: Wire,
    load_row: Wire,
    load_col: Wire,
    load_data: Wire,
    load_valid: Wire,
    read_entry_idx: Wire,
) -> tuple[list[L0EntryStatus], list[list[Wire]], Wire]:
    """Build L0A buffer (left matrix input).

    Returns:
        (status_list, read_data_matrix, load_done)
    """
    return build_l0_buffer(
        m,
        clk=clk,
        rst=rst,
        consts=consts,
        prefix="l0a",
        num_entries=L0A_ENTRIES,
        load_entry_idx=load_entry_idx,
        load_row=load_row,
        load_col=load_col,
        load_data=load_data,
        load_valid=load_valid,
        read_entry_idx=read_entry_idx,
    )


def build_l0b(
    m: Circuit,
    *,
    clk: Wire,
    rst: Wire,
    consts: Consts,
    load_entry_idx: Wire,
    load_row: Wire,
    load_col: Wire,
    load_data: Wire,
    load_valid: Wire,
    read_entry_idx: Wire,
) -> tuple[list[L0EntryStatus], list[list[Wire]], Wire]:
    """Build L0B buffer (right matrix input).

    Returns:
        (status_list, read_data_matrix, load_done)
    """
    return build_l0_buffer(
        m,
        clk=clk,
        rst=rst,
        consts=consts,
        prefix="l0b",
        num_entries=L0B_ENTRIES,
        load_entry_idx=load_entry_idx,
        load_row=load_row,
        load_col=load_col,
        load_data=load_data,
        load_valid=load_valid,
        read_entry_idx=read_entry_idx,
    )
