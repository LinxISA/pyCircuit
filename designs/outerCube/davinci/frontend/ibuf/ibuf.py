"""Instruction Buffer — 16-entry FIFO, 4-wide enqueue/dequeue.

Sits between Fetch (F2) and Decode (D1). Absorbs fetch bubbles caused by
I-cache misses or branch redirects, keeping the decode stage fed.

Each entry holds one 32-bit instruction plus its PC and a valid bit.
"""

from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    mux,
    wire_of,
)

from ...common.parameters import FETCH_WIDTH, IBUF_ENTRIES, INSTR_WIDTH


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def ibuf(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    depth: int = IBUF_ENTRIES,
    width: int = FETCH_WIDTH,
    instr_w: int = INSTR_WIDTH,
    addr_w: int = 64,
    prefix: str = "ib",
    inputs: dict | None = None,
) -> dict:
    ptr_w = max(1, depth.bit_length())
    cnt_w = ptr_w + 1
    outs: dict = {}

    # ── Cycle 0: Inputs ──────────────────────────────────────────────
    enq_valid = [
        _in(inputs, f"enq_valid{i}", m, domain, prefix, 1) for i in range(width)
    ]
    enq_instr = [
        _in(inputs, f"enq_instr{i}", m, domain, prefix, instr_w) for i in range(width)
    ]
    enq_pc = [
        _in(inputs, f"enq_pc{i}", m, domain, prefix, addr_w) for i in range(width)
    ]

    deq_ready = _in(inputs, "deq_ready", m, domain, prefix, 1)
    flush = _in(inputs, "flush", m, domain, prefix, 1)

    # ── State ────────────────────────────────────────────────────────
    buf_valid = [
        domain.signal(width=1, reset_value=0, name=f"{prefix}_v_{i}")
        for i in range(depth)
    ]
    buf_instr = [
        domain.signal(width=instr_w, reset_value=0, name=f"{prefix}_ins_{i}")
        for i in range(depth)
    ]
    buf_pc = [
        domain.signal(width=addr_w, reset_value=0, name=f"{prefix}_pc_{i}")
        for i in range(depth)
    ]

    head = domain.signal(width=ptr_w, reset_value=0, name=f"{prefix}_head")
    tail = domain.signal(width=ptr_w, reset_value=0, name=f"{prefix}_tail")
    count = domain.signal(width=cnt_w, reset_value=0, name=f"{prefix}_count")

    # ── Combinational: dequeue up to `width` from head ───────────────
    for i in range(width):
        idx = (head + cas(domain, m.const(i, width=ptr_w), cycle=0)).trunc(ptr_w)
        r_valid = cas(domain, m.const(0, width=1), cycle=0)
        r_instr = cas(domain, m.const(0, width=instr_w), cycle=0)
        r_pc = cas(domain, m.const(0, width=addr_w), cycle=0)
        for s in range(depth):
            hit = idx == cas(domain, m.const(s, width=ptr_w), cycle=0)
            r_valid = mux(hit, buf_valid[s], r_valid)
            r_instr = mux(hit, buf_instr[s], r_instr)
            r_pc = mux(hit, buf_pc[s], r_pc)
        has_data = r_valid & (count > cas(domain, m.const(i, width=cnt_w), cycle=0))
        outs[f"deq_valid{i}"] = has_data & deq_ready
        outs[f"deq_instr{i}"] = r_instr
        outs[f"deq_pc{i}"] = r_pc

    # Count enqueues / dequeues
    n_enq = cas(domain, m.const(0, width=cnt_w), cycle=0)
    for i in range(width):
        n_enq = n_enq + enq_valid[i]

    n_deq_max = mux(
        count < cas(domain, m.const(width, width=cnt_w), cycle=0),
        count,
        cas(domain, m.const(width, width=cnt_w), cycle=0),
    )
    n_deq = mux(deq_ready, n_deq_max, cas(domain, m.const(0, width=cnt_w), cycle=0))

    space = cas(domain, m.const(depth, width=cnt_w), cycle=0) - count
    stall = n_enq > space
    outs["full"] = stall
    outs["count"] = count
    outs["empty"] = count == cas(domain, m.const(0, width=cnt_w), cycle=0)

    if inputs is None:
        for k in outs:
            m.output(f"{prefix}_{k}", wire_of(outs[k]))

    # ── Cycle 1: Sequential update ───────────────────────────────────
    domain.next()

    # Enqueue
    for i in range(width):
        wr_idx = (tail + cas(domain, m.const(i, width=ptr_w), cycle=0)).trunc(ptr_w)
        for s in range(depth):
            hit = (
                enq_valid[i]
                & (wr_idx == cas(domain, m.const(s, width=ptr_w), cycle=0))
                & (~flush)
            )
            buf_valid[s].assign(cas(domain, m.const(1, width=1), cycle=0), when=hit)
            buf_instr[s].assign(enq_instr[i], when=hit)
            buf_pc[s].assign(enq_pc[i], when=hit)

    # Dequeue: invalidate consumed entries
    for i in range(width):
        rd_idx = (head + cas(domain, m.const(i, width=ptr_w), cycle=0)).trunc(ptr_w)
        do_deq = (
            deq_ready
            & (count > cas(domain, m.const(i, width=cnt_w), cycle=0))
            & (~flush)
        )
        for s in range(depth):
            hit = do_deq & (rd_idx == cas(domain, m.const(s, width=ptr_w), cycle=0))
            buf_valid[s].assign(cas(domain, m.const(0, width=1), cycle=0), when=hit)

    next_tail = mux(
        flush,
        cas(domain, m.const(0, width=ptr_w), cycle=0),
        (tail + n_enq).trunc(ptr_w),
    )
    next_head = mux(
        flush,
        cas(domain, m.const(0, width=ptr_w), cycle=0),
        (head + n_deq).trunc(ptr_w),
    )
    next_count = mux(
        flush,
        cas(domain, m.const(0, width=cnt_w), cycle=0),
        (count + n_enq - n_deq).trunc(cnt_w),
    )

    head <<= next_head
    tail <<= next_tail
    count <<= next_count

    # Flush: clear all entries
    for s in range(depth):
        buf_valid[s].assign(cas(domain, m.const(0, width=1), cycle=0), when=flush)

    return outs


ibuf.__pycircuit_name__ = "ibuf"


if __name__ == "__main__":
    pass
