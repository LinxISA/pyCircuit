"""Cube Reservation Station — 4 entries, 1-issue, tile-tag wakeup.

Each CUBE instruction requires multiple tile operands (A, B, accumulator).
Wakeup via TCB for tile sources.
"""

from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    mux,
    wire_of,
)

from ...common.parameters import (
    AGE_W,
    CUBE_RS_ENTRIES,
    DISPATCH_WIDTH,
    PHYS_TREG_W,
    TCB_PORTS,
    UOP_W,
)


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def cube_rs(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    n_entries: int = CUBE_RS_ENTRIES,
    n_dispatch: int = DISPATCH_WIDTH,
    n_tcb: int = TCB_PORTS,
    n_tile_src: int = 2,
    ttag_w: int = PHYS_TREG_W,
    uop_w: int = UOP_W,
    age_w: int = AGE_W,
    prefix: str = "crs",
    inputs: dict | None = None,
) -> dict:
    eidx_w = max(1, (n_entries - 1).bit_length())
    outs: dict = {}

    disp_valid = [
        _in(inputs, f"dv{i}", m, domain, prefix, 1) for i in range(n_dispatch)
    ]
    disp_op = [
        _in(inputs, f"dop{i}", m, domain, prefix, uop_w) for i in range(n_dispatch)
    ]
    disp_ptsrc = [
        [
            _in(inputs, f"dts{s}_{i}", m, domain, prefix, ttag_w)
            for i in range(n_dispatch)
        ]
        for s in range(n_tile_src)
    ]
    disp_trdy = [
        [_in(inputs, f"dtr{s}_{i}", m, domain, prefix, 1) for i in range(n_dispatch)]
        for s in range(n_tile_src)
    ]
    disp_ptdst = [
        _in(inputs, f"dtd{i}", m, domain, prefix, ttag_w) for i in range(n_dispatch)
    ]

    tcb_valid = [_in(inputs, f"tcb_v{i}", m, domain, prefix, 1) for i in range(n_tcb)]
    tcb_tag = [
        _in(inputs, f"tcb_t{i}", m, domain, prefix, ttag_w) for i in range(n_tcb)
    ]

    flush = _in(inputs, "flush", m, domain, prefix, 1)

    valid = [
        domain.signal(width=1, reset_value=0, name=f"{prefix}_v_{e}")
        for e in range(n_entries)
    ]
    op = [
        domain.signal(width=uop_w, reset_value=0, name=f"{prefix}_op_{e}")
        for e in range(n_entries)
    ]
    age = [
        domain.signal(width=age_w, reset_value=0, name=f"{prefix}_ag_{e}")
        for e in range(n_entries)
    ]
    ptsrc = [
        [
            domain.signal(width=ttag_w, reset_value=0, name=f"{prefix}_ts{s}_{e}")
            for e in range(n_entries)
        ]
        for s in range(n_tile_src)
    ]
    trdy = [
        [
            domain.signal(width=1, reset_value=0, name=f"{prefix}_tr{s}_{e}")
            for e in range(n_entries)
        ]
        for s in range(n_tile_src)
    ]
    ptdst = [
        domain.signal(width=ttag_w, reset_value=0, name=f"{prefix}_td_{e}")
        for e in range(n_entries)
    ]

    age_ctr = domain.signal(width=age_w, reset_value=0, name=f"{prefix}_ac")

    # TCB wakeup
    eff_trdy = [[None] * n_entries for _ in range(n_tile_src)]
    for e in range(n_entries):
        for s in range(n_tile_src):
            r = trdy[s][e]
            for t in range(n_tcb):
                r = r | (tcb_valid[t] & (tcb_tag[t] == ptsrc[s][e]) & (~r))
            eff_trdy[s][e] = r

    # Select oldest ready
    best_v = cas(domain, m.const(0, width=1), cycle=0)
    best_i = cas(domain, m.const(0, width=eidx_w), cycle=0)
    best_a = cas(domain, m.const((1 << age_w) - 1, width=age_w), cycle=0)
    for e in range(n_entries):
        all_rdy = eff_trdy[0][e]
        for s in range(1, n_tile_src):
            all_rdy = all_rdy & eff_trdy[s][e]
        ready = valid[e] & all_rdy
        older = age[e] < best_a
        wins = ready & (older | (~best_v))
        best_v = mux(wins, cas(domain, m.const(1, width=1), cycle=0), best_v)
        best_i = mux(wins, cas(domain, m.const(e, width=eidx_w), cycle=0), best_i)
        best_a = mux(wins, age[e], best_a)

    outs["issue_valid"] = best_v
    outs["issue_idx"] = best_i

    issue_op = op[0]
    issue_ptdst = ptdst[0]
    for e in range(n_entries):
        hit = best_i == cas(domain, m.const(e, width=eidx_w), cycle=0)
        issue_op = mux(hit, op[e], issue_op)
        issue_ptdst = mux(hit, ptdst[e], issue_ptdst)
    outs["issue_op"] = issue_op
    outs["issue_ptdst"] = issue_ptdst

    n_v = cas(domain, m.const(0, width=eidx_w + 1), cycle=0)
    for e in range(n_entries):
        n_v = n_v + valid[e]
    outs["full"] = n_v >= cas(domain, m.const(n_entries, width=eidx_w + 1), cycle=0)

    if inputs is None:
        for k in outs:
            m.output(f"{prefix}_{k}", wire_of(outs[k]))

    # ── Cycle 1 ──────────────────────────────────────────────────────
    domain.next()

    age_ctr <<= (age_ctr + cas(domain, m.const(1, width=age_w), cycle=0)).trunc(age_w)

    for e in range(n_entries):
        etag = cas(domain, m.const(e, width=eidx_w), cycle=0)
        issued = best_v & (best_i == etag) & (~flush)
        for s in range(n_tile_src):
            trdy[s][e].assign(eff_trdy[s][e], when=valid[e] & (~issued))
        valid[e].assign(cas(domain, m.const(0, width=1), cycle=0), when=issued | flush)

    for d in range(n_dispatch):
        for e in range(n_entries):
            alloc = disp_valid[d] & (~valid[e])
            valid[e].assign(cas(domain, m.const(1, width=1), cycle=0), when=alloc)
            op[e].assign(disp_op[d], when=alloc)
            age[e].assign(age_ctr, when=alloc)
            ptdst[e].assign(disp_ptdst[d], when=alloc)
            for s in range(n_tile_src):
                ptsrc[s][e].assign(disp_ptsrc[s][d], when=alloc)
                trdy[s][e].assign(disp_trdy[s][d], when=alloc)

    return outs


cube_rs.__pycircuit_name__ = "cube_rs"


if __name__ == "__main__":
    pass
