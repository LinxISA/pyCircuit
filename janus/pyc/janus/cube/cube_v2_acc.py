"""Cube v2 ACC Buffer Implementation.

ACC is the accumulator buffer that stores 16×16 tiles of output matrix results.
Each buffer has 16 entries, with each entry holding 256 32-bit elements (8192 bits).

Storing an entry requires 4 MMIO cycles (2048 bits per cycle).
"""

from __future__ import annotations

from pycircuit import Circuit, Wire, jit_inline

from janus.cube.cube_v2_consts import (
    ACC_ENTRIES,
    ACC_ENTRY_BITS,
    ACC_IDX_WIDTH,
    ARRAY_SIZE,
    MMIO_WIDTH,
    OUTPUT_WIDTH,
)
from janus.cube.cube_v2_types import AccEntryStatus
from janus.cube.util import Consts


def _make_acc_entry_status(
    m: Circuit, clk: Wire, rst: Wire, consts: Consts, idx: int
) -> AccEntryStatus:
    """Create status registers for a single ACC buffer entry."""
    with m.scope(f"acc_status_{idx}"):
        return AccEntryStatus(
            valid=m.out("valid", clk=clk, rst=rst, width=1, init=0, en=consts.one1),
            computing=m.out("computing", clk=clk, rst=rst, width=1, init=0, en=consts.one1),
            pending_k=m.out("pending_k", clk=clk, rst=rst, width=8, init=0, en=consts.one1),
            storing=m.out("storing", clk=clk, rst=rst, width=1, init=0, en=consts.one1),
        )


def _make_acc_data_regs(
    m: Circuit, clk: Wire, rst: Wire, consts: Consts, idx: int
) -> list:
    """Create data registers for a single ACC buffer entry.

    Each entry stores 16×16 = 256 elements of 32 bits each.
    We split into 4 quarters for MMIO storing (2048 bits each).
    """
    data_regs = []
    with m.scope(f"acc_data_{idx}"):
        # 4 quarters × 64 elements × 32 bits = 8192 bits total
        for quarter in range(4):
            quarter_regs = []
            with m.scope(f"q{quarter}"):
                for elem in range(64):
                    reg = m.out(
                        f"e{elem}",
                        clk=clk,
                        rst=rst,
                        width=OUTPUT_WIDTH,
                        init=0,
                        en=consts.one1,
                    )
                    quarter_regs.append(reg)
            data_regs.append(quarter_regs)
    return data_regs


@jit_inline
def build_acc_buffer(
    m: Circuit,
    *,
    clk: Wire,
    rst: Wire,
    consts: Consts,
    # Write interface (from systolic array)
    write_entry_idx: Wire,   # Which entry to write (7-bit)
    write_data: list[Wire],  # 256 × 32-bit results from systolic array
    write_valid: Wire,       # Write is valid
    write_is_first: Wire,    # First write (clear before accumulate)
    write_is_last: Wire,     # Last write (mark as complete)
    # Store interface (to MMIO)
    store_start: Wire,       # Start storing an entry
    store_entry_idx: Wire,   # Which entry to store (7-bit)
    store_quarter: Wire,     # Which quarter (0-3)
    store_ack: Wire,         # Store acknowledged (data taken)
    # Status query
    query_entry_idx: Wire,   # Entry to query status
) -> tuple[list[AccEntryStatus], Wire, Wire]:
    """Build ACC buffer.

    Returns:
        (status_list, store_data, store_done): Status for each entry, store data, store complete
    """
    c = m.const

    with m.scope("ACC"):
        # Create status registers for all entries
        status_list = []
        for i in range(ACC_ENTRIES):
            status = _make_acc_entry_status(m, clk, rst, consts, i)
            status_list.append(status)

        # Create data registers for all entries
        data_regs_list = []
        for i in range(ACC_ENTRIES):
            data_regs = _make_acc_data_regs(m, clk, rst, consts, i)
            data_regs_list.append(data_regs)

        # Write logic (from systolic array)
        with m.scope("WRITE"):
            for i in range(ACC_ENTRIES):
                entry_match = write_entry_idx.eq(c(i, width=ACC_IDX_WIDTH))
                is_writing = entry_match & write_valid

                # Clear or accumulate based on is_first
                for row in range(ARRAY_SIZE):
                    for col in range(ARRAY_SIZE):
                        elem_idx = row * ARRAY_SIZE + col
                        quarter_idx = elem_idx // 64
                        elem_in_quarter = elem_idx % 64

                        current_val = data_regs_list[i][quarter_idx][elem_in_quarter].out()
                        new_val = write_data[elem_idx]

                        # If is_first, just write; otherwise accumulate
                        write_val = write_is_first.select(new_val, current_val + new_val)
                        data_regs_list[i][quarter_idx][elem_in_quarter].set(
                            write_val, when=is_writing
                        )

                # Update status
                # Set computing on first write
                first_write = is_writing & write_is_first
                status_list[i].computing.set(consts.one1, when=first_write)
                status_list[i].valid.set(consts.zero1, when=first_write)

                # Set valid and clear computing on last write
                last_write = is_writing & write_is_last
                status_list[i].valid.set(consts.one1, when=last_write)
                status_list[i].computing.set(consts.zero1, when=last_write)

        # Store logic (to MMIO) - simplified to return 64-bit data
        with m.scope("STORE"):
            # Output: 64 bits (2 × 32-bit elements)
            store_data = c(0, width=64)
            store_done = consts.zero1

            for i in range(ACC_ENTRIES):
                entry_match = store_entry_idx.eq(c(i, width=ACC_IDX_WIDTH))

                # Set storing flag on start
                start_this = entry_match & store_start
                status_list[i].storing.set(consts.one1, when=start_this)

                # Select first two elements as placeholder
                select_this = entry_match
                elem0 = data_regs_list[i][0][0].out()
                elem1 = data_regs_list[i][0][1].out()
                combined = elem0.zext(width=64) | (elem1.zext(width=64) << 32)
                store_data = select_this.select(combined, store_data)

                # Clear storing and valid after store acknowledged
                store_acked = entry_match & store_ack
                status_list[i].storing.set(consts.zero1, when=store_acked)
                status_list[i].valid.set(consts.zero1, when=store_acked)

                store_done = store_done | store_acked

        # Query logic
        with m.scope("QUERY"):
            query_valid = consts.zero1
            query_computing = consts.zero1

            for i in range(ACC_ENTRIES):
                entry_match = query_entry_idx.eq(c(i, width=ACC_IDX_WIDTH))
                query_valid = entry_match.select(status_list[i].valid.out(), query_valid)
                query_computing = entry_match.select(
                    status_list[i].computing.out(), query_computing
                )

        return status_list, store_data, store_done
