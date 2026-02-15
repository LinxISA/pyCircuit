"""Cube v2 L0 Buffer Implementation with Module Reuse.

Uses m.instance() for L0 entry module reuse to reduce generated code size.
"""
from __future__ import annotations
from pycircuit import Circuit, Wire, jit_inline
from janus.cube.cube_v2_consts import ARRAY_SIZE, INPUT_WIDTH, L0A_ENTRIES, L0B_ENTRIES, L0_IDX_WIDTH
from janus.cube.cube_v2_l0_entry import build_l0_entry
from janus.cube.cube_v2_types import L0EntryStatus
from janus.cube.util import Consts

def _binary_tree_mux(m: Circuit, idx: Wire, values: list[Wire], idx_width: int) -> Wire:
    """Build a binary tree mux for O(log n) depth selection."""
    _ = m
    n = len(values)
    if n == 1:
        return values[0]
    if n == 2:
        return idx[0]._select_internal(values[1], values[0])
    mid = n // 2
    left = _binary_tree_mux(m, idx, values[:mid], idx_width - 1)
    right = _binary_tree_mux(m, idx, values[mid:], idx_width - 1)
    msb_bit = idx_width - 1
    return idx[msb_bit]._select_internal(right, left)

@jit_inline
def build_l0_buffer_reuse(m: Circuit, *, clk: Wire, rst: Wire, consts: Consts, prefix: str, num_entries: int, load_entry_idx: Wire, load_row: Wire, load_col: Wire, load_data: Wire, load_valid: Wire, read_entry_idx: Wire) -> tuple[list[L0EntryStatus], list[list[Wire]], Wire]:
    """Build L0 buffer using module instances for entries.

    Returns:
        (status_list, read_data_matrix, load_done)
    """
    c = m.const
    with m.scope(prefix.upper()):
        entry_instances = []
        status_list = []
        for i in range(num_entries):
            entry_match = load_entry_idx == c(i, width=L0_IDX_WIDTH)
            entry_load_valid = load_valid & entry_match
            entry = m.instance(build_l0_entry, name=f'{prefix}_entry_{i}', clk=clk, rst=rst, load_valid=entry_load_valid, load_row=load_row, load_col=load_col, load_data=load_data[0:INPUT_WIDTH])
            entry_instances.append(entry)
            with m.scope(f'{prefix}_status_{i}'):
                loading_reg = m.out('loading', clk=clk, rst=rst, width=1, init=0, en=consts.one1)
                ref_count_reg = m.out('ref_count', clk=clk, rst=rst, width=8, init=0, en=consts.one1)
                valid_reg = m.out('valid', clk=clk, rst=rst, width=1, init=0, en=consts.one1)
                valid_reg.set(entry['valid'], when=consts.one1)
                status = L0EntryStatus(valid=valid_reg, loading=loading_reg, ref_count=ref_count_reg)
                status_list.append(status)
        load_done = consts.zero1
        for i in range(num_entries):
            entry_match = load_entry_idx == c(i, width=L0_IDX_WIDTH)
            last_elem = load_valid & entry_match & (load_row == c(ARRAY_SIZE - 1, width=4)) & (load_col == c(ARRAY_SIZE - 1, width=4))
            load_done = load_done | last_elem
        with m.scope('READ'):
            read_data_matrix = []
            for row in range(ARRAY_SIZE):
                row_data = []
                for col in range(ARRAY_SIZE):
                    elem_values = [entry_instances[i][f'd_r{row}_c{col}'] for i in range(num_entries)]
                    elem = _binary_tree_mux(m, read_entry_idx, elem_values, L0_IDX_WIDTH)
                    row_data.append(elem)
                read_data_matrix.append(row_data)
        return (status_list, read_data_matrix, load_done)

def build_l0a_reuse(m: Circuit, *, clk: Wire, rst: Wire, consts: Consts, load_entry_idx: Wire, load_row: Wire, load_col: Wire, load_data: Wire, load_valid: Wire, read_entry_idx: Wire) -> tuple[list[L0EntryStatus], list[list[Wire]], Wire]:
    """Build L0A buffer with module reuse."""
    return build_l0_buffer_reuse(m, clk=clk, rst=rst, consts=consts, prefix='l0a', num_entries=L0A_ENTRIES, load_entry_idx=load_entry_idx, load_row=load_row, load_col=load_col, load_data=load_data, load_valid=load_valid, read_entry_idx=read_entry_idx)

def build_l0b_reuse(m: Circuit, *, clk: Wire, rst: Wire, consts: Consts, load_entry_idx: Wire, load_row: Wire, load_col: Wire, load_data: Wire, load_valid: Wire, read_entry_idx: Wire) -> tuple[list[L0EntryStatus], list[list[Wire]], Wire]:
    """Build L0B buffer with module reuse."""
    return build_l0_buffer_reuse(m, clk=clk, rst=rst, consts=consts, prefix='l0b', num_entries=L0B_ENTRIES, load_entry_idx=load_entry_idx, load_row=load_row, load_col=load_col, load_data=load_data, load_valid=load_valid, read_entry_idx=read_entry_idx)
