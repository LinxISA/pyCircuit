"""Parameterized FIFO free list for scalar physical GPRs or tile physical registers.

Provides up to `deq_width` allocations per cycle and up to `enq_width` returns.
Stall signal raised when insufficient entries available for the requested allocations.
"""

from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    mux,
    wire_of,
)


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def free_list(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    depth: int = 96,
    tag_w: int = 7,
    deq_width: int = 4,
    enq_width: int = 4,
    prefix: str = "fl",
    inputs: dict | None = None,
) -> dict:
    ptr_w = max(1, depth.bit_length())
    cnt_w = ptr_w + 1

    # ── Cycle 0: Inputs ──────────────────────────────────────────────
    alloc_req = [
        _in(inputs, f"alloc_req{i}", m, domain, prefix, 1) for i in range(deq_width)
    ]
    free_valid = [
        _in(inputs, f"free_valid{i}", m, domain, prefix, 1) for i in range(enq_width)
    ]
    free_tag = [
        _in(inputs, f"free_tag{i}", m, domain, prefix, tag_w) for i in range(enq_width)
    ]
    restore = _in(inputs, "restore", m, domain, prefix, 1)
    restore_head = _in(inputs, "restore_head", m, domain, prefix, ptr_w)

    # ── State ────────────────────────────────────────────────────────
    fifo = [
        domain.signal(width=tag_w, reset_value=i, name=f"{prefix}_fifo_{i}")
        for i in range(depth)
    ]
    head = domain.signal(width=ptr_w, reset_value=0, name=f"{prefix}_head")
    tail = domain.signal(width=ptr_w, reset_value=depth - 1, name=f"{prefix}_tail")
    count = domain.signal(width=cnt_w, reset_value=depth, name=f"{prefix}_count")

    # ── Combinational read: up to `deq_width` allocations from head ──
    alloc_tags = []
    for i in range(deq_width):
        idx = head + cas(domain, m.const(i, width=ptr_w), cycle=0)
        idx_trunc = idx.trunc(ptr_w) if wire_of(idx).width > ptr_w else idx
        read_val = fifo[0]
        for s in range(depth):
            hit = idx_trunc == cas(domain, m.const(s, width=ptr_w), cycle=0)
            read_val = mux(hit, fifo[s], read_val)
        alloc_tags.append(read_val)

    # ── Count valid alloc requests ───────────────────────────────────
    n_alloc = cas(domain, m.const(0, width=cnt_w), cycle=0)
    for i in range(deq_width):
        n_alloc = n_alloc + alloc_req[i]

    n_free = cas(domain, m.const(0, width=cnt_w), cycle=0)
    for i in range(enq_width):
        n_free = n_free + free_valid[i]

    has_enough = count >= n_alloc

    outs = {f"alloc_tag{i}": alloc_tags[i] for i in range(deq_width)}
    outs["stall"] = ~has_enough
    outs["head"] = head
    outs["count"] = count

    if inputs is None:
        for k in outs:
            m.output(f"{prefix}_{k}", wire_of(outs[k]))

    # ── Cycle 1: Sequential update ───────────────────────────────────
    domain.next()

    next_head = (head + n_alloc).trunc(ptr_w)
    next_count = (count - n_alloc + n_free).trunc(cnt_w)

    new_tail = tail
    for i in range(enq_width):
        slot = (new_tail + cas(domain, m.const(1, width=ptr_w), cycle=0)).trunc(ptr_w)
        for s in range(depth):
            hit = slot == cas(domain, m.const(s, width=ptr_w), cycle=0)
            fifo[s].assign(free_tag[i], when=hit & free_valid[i])
        new_tail = mux(free_valid[i], slot, new_tail)

    head <<= mux(restore, restore_head, next_head)
    tail <<= new_tail
    count <<= mux(
        restore,
        (new_tail - restore_head + cas(domain, m.const(1, width=cnt_w), cycle=0)).trunc(
            cnt_w
        ),
        next_count,
    )

    return outs


free_list.__pycircuit_name__ = "free_list"


if __name__ == "__main__":
    pass
