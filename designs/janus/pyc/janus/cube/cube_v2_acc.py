"""Cube v2 ACC Buffer Implementation.

ACC is the accumulator buffer that stores 16×16 tiles of output matrix results.
Each buffer has 16 entries, with each entry holding 256 32-bit elements (8192 bits).

Storing an entry requires 4 MMIO cycles (2048 bits per cycle).
"""
from __future__ import annotations
from pycircuit import Circuit, Wire, jit_inline, unsigned
from janus.cube.cube_v2_consts import ACC_ENTRIES, ACC_ENTRY_BITS, ACC_IDX_WIDTH, ARRAY_SIZE, MMIO_WIDTH, OUTPUT_WIDTH
from janus.cube.cube_v2_types import AccEntryStatus
from janus.cube.util import Consts

def _make_acc_entry_status(m: Circuit, clk: Wire, rst: Wire, consts: Consts, idx: int) -> AccEntryStatus:
    """Create status registers for a single ACC buffer entry."""
    with m.scope(f'acc_status_{idx}'):
        return AccEntryStatus(valid=m.out('valid', clk=clk, rst=rst, width=1, init=0, en=consts.one1), computing=m.out('computing', clk=clk, rst=rst, width=1, init=0, en=consts.one1), pending_k=m.out('pending_k', clk=clk, rst=rst, width=8, init=0, en=consts.one1), storing=m.out('storing', clk=clk, rst=rst, width=1, init=0, en=consts.one1))

def _make_acc_data_regs(m: Circuit, clk: Wire, rst: Wire, consts: Consts, idx: int) -> list:
    """Create data registers for a single ACC buffer entry.

    Each entry stores 16×16 = 256 elements of 32 bits each.
    We split into 4 quarters for MMIO storing (2048 bits each).
    """
    data_regs = []
    with m.scope(f'acc_data_{idx}'):
        for quarter in range(4):
            quarter_regs = []
            with m.scope(f'q{quarter}'):
                for elem in range(64):
                    reg = m.out(f'e{elem}', clk=clk, rst=rst, width=OUTPUT_WIDTH, init=0, en=consts.one1)
                    quarter_regs.append(reg)
            data_regs.append(quarter_regs)
    return data_regs

@jit_inline
def build_acc_buffer(m: Circuit, *, clk: Wire, rst: Wire, consts: Consts, write_entry_idx: Wire, write_data: list[Wire], write_valid: Wire, write_is_first: Wire, write_is_last: Wire, store_start: Wire, store_entry_idx: Wire, store_quarter: Wire, store_ack: Wire, query_entry_idx: Wire) -> tuple[list[AccEntryStatus], Wire, Wire]:
    """Build ACC buffer.

    Returns:
        (status_list, store_data, store_done): Status for each entry, store data, store complete
    """
    c = m.const
    with m.scope('ACC'):
        status_list = []
        for i in range(ACC_ENTRIES):
            status = _make_acc_entry_status(m, clk, rst, consts, i)
            status_list.append(status)
        data_regs_list = []
        for i in range(ACC_ENTRIES):
            data_regs = _make_acc_data_regs(m, clk, rst, consts, i)
            data_regs_list.append(data_regs)
        with m.scope('WRITE'):
            for i in range(ACC_ENTRIES):
                entry_match = write_entry_idx == c(i, width=ACC_IDX_WIDTH)
                is_writing = entry_match & write_valid
                for row in range(ARRAY_SIZE):
                    for col in range(ARRAY_SIZE):
                        elem_idx = row * ARRAY_SIZE + col
                        quarter_idx = elem_idx // 64
                        elem_in_quarter = elem_idx % 64
                        current_val = data_regs_list[i][quarter_idx][elem_in_quarter].out()
                        new_val = write_data[elem_idx]
                        write_val = new_val if write_is_first else current_val + new_val
                        data_regs_list[i][quarter_idx][elem_in_quarter].set(write_val, when=is_writing)
                first_write = is_writing & write_is_first
                status_list[i].computing.set(consts.one1, when=first_write)
                status_list[i].valid.set(consts.zero1, when=first_write)
                last_write = is_writing & write_is_last
                status_list[i].valid.set(consts.one1, when=last_write)
                status_list[i].computing.set(consts.zero1, when=last_write)
        with m.scope('STORE'):
            store_data = c(0, width=64)
            store_done = consts.zero1
            for i in range(ACC_ENTRIES):
                entry_match = store_entry_idx == c(i, width=ACC_IDX_WIDTH)
                start_this = entry_match & store_start
                status_list[i].storing.set(consts.one1, when=start_this)
                select_this = entry_match
                elem0 = data_regs_list[i][0][0].out()
                elem1 = data_regs_list[i][0][1].out()
                combined = unsigned(elem0) | unsigned(elem1) << 32
                store_data = combined if select_this else store_data
                store_acked = entry_match & store_ack
                status_list[i].storing.set(consts.zero1, when=store_acked)
                status_list[i].valid.set(consts.zero1, when=store_acked)
                store_done = store_done | store_acked
        with m.scope('QUERY'):
            query_valid = consts.zero1
            query_computing = consts.zero1
            for i in range(ACC_ENTRIES):
                entry_match = query_entry_idx == c(i, width=ACC_IDX_WIDTH)
                query_valid = status_list[i].valid.out() if entry_match else query_valid
                query_computing = status_list[i].computing.out() if entry_match else query_computing
        return (status_list, store_data, store_done)
