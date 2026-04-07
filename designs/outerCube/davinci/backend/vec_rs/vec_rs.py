"""Vector Reservation Station — 16 entries, 1-issue, tile-tag wakeup.

Tile-domain RS: entries carry physical tile tags (8b) + ready bits. No 64-bit
data capture (tiles are 4 KB, too large). Issue waits for Tile RAT ready bits
via TCB. Some vector instructions also consume a scalar operand (for
tile-scalar ops like VADDS).

Wakeup: 16 × 3 tile sources × 4 TCB ports = 192 tile-tag comparators (8b each)
        + 16 × 1 scalar source × 6 CDB ports (optional, for tile-scalar ops)
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
    DISPATCH_WIDTH,
    PHYS_GREG_W,
    PHYS_TREG_W,
    TCB_PORTS,
    UOP_W,
    VEC_RS_ENTRIES,
)


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def vec_rs(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    n_entries: int = VEC_RS_ENTRIES,
    n_dispatch: int = DISPATCH_WIDTH,
    n_tcb: int = TCB_PORTS,
    n_tile_src: int = 3,
    ttag_w: int = PHYS_TREG_W,
    stag_w: int = PHYS_GREG_W,
    uop_w: int = UOP_W,
    age_w: int = AGE_W,
    prefix: str = "vrs",
    inputs: dict | None = None,
) -> dict:
    eidx_w = max(1, (n_entries - 1).bit_length())
    outs: dict = {}

    # ── Cycle 0: Dispatch ────────────────────────────────────────────
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

    # TCB broadcast
    tcb_valid = [_in(inputs, f"tcb_v{i}", m, domain, prefix, 1) for i in range(n_tcb)]
    tcb_tag = [
        _in(inputs, f"tcb_t{i}", m, domain, prefix, ttag_w) for i in range(n_tcb)
    ]

    flush = _in(inputs, "flush", m, domain, prefix, 1)

    # ── State ────────────────────────────────────────────────────────
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

    # ── Combinational: TCB wakeup ────────────────────────────────────
    eff_trdy = [[None] * n_entries for _ in range(n_tile_src)]
    for e in range(n_entries):
        for s in range(n_tile_src):
            r = trdy[s][e]
            for t in range(n_tcb):
                match = tcb_valid[t] & (tcb_tag[t] == ptsrc[s][e]) & (~r)
                r = r | match
            eff_trdy[s][e] = r

    # ── Select oldest ready ──────────────────────────────────────────
    best_valid = cas(domain, m.const(0, width=1), cycle=0)
    best_idx = cas(domain, m.const(0, width=eidx_w), cycle=0)
    best_age = cas(domain, m.const((1 << age_w) - 1, width=age_w), cycle=0)

    for e in range(n_entries):
        all_rdy = eff_trdy[0][e]
        for s in range(1, n_tile_src):
            all_rdy = all_rdy & eff_trdy[s][e]
        is_ready = valid[e] & all_rdy
        is_older = age[e] < best_age
        wins = is_ready & (is_older | (~best_valid))
        best_valid = mux(wins, cas(domain, m.const(1, width=1), cycle=0), best_valid)
        best_idx = mux(wins, cas(domain, m.const(e, width=eidx_w), cycle=0), best_idx)
        best_age = mux(wins, age[e], best_age)

    outs["issue_valid"] = best_valid
    outs["issue_idx"] = best_idx

    issue_op = op[0]
    issue_ptdst = ptdst[0]
    issue_ptsrc_out = [ptsrc[s][0] for s in range(n_tile_src)]
    for e in range(n_entries):
        hit = best_idx == cas(domain, m.const(e, width=eidx_w), cycle=0)
        issue_op = mux(hit, op[e], issue_op)
        issue_ptdst = mux(hit, ptdst[e], issue_ptdst)
        for s in range(n_tile_src):
            issue_ptsrc_out[s] = mux(hit, ptsrc[s][e], issue_ptsrc_out[s])
    outs["issue_op"] = issue_op
    outs["issue_ptdst"] = issue_ptdst
    for s in range(n_tile_src):
        outs[f"issue_ptsrc{s}"] = issue_ptsrc_out[s]

    # Full
    n_v = cas(domain, m.const(0, width=eidx_w + 1), cycle=0)
    for e in range(n_entries):
        n_v = n_v + valid[e]
    outs["full"] = n_v >= cas(
        domain, m.const(n_entries - n_dispatch, width=eidx_w + 1), cycle=0
    )

    if inputs is None:
        for k in outs:
            m.output(f"{prefix}_{k}", wire_of(outs[k]))

    # ── Cycle 1: Update ──────────────────────────────────────────────
    domain.next()

    age_ctr <<= (age_ctr + cas(domain, m.const(1, width=age_w), cycle=0)).trunc(age_w)

    for e in range(n_entries):
        etag = cas(domain, m.const(e, width=eidx_w), cycle=0)
        issued = best_valid & (best_idx == etag) & (~flush)

        for s in range(n_tile_src):
            trdy[s][e].assign(eff_trdy[s][e], when=valid[e] & (~issued))

        valid[e].assign(cas(domain, m.const(0, width=1), cycle=0), when=issued | flush)

    # Dispatch allocation (simplified first-fit)
    for d in range(n_dispatch):
        for e in range(n_entries):
            slot_free = ~valid[e]
            alloc = disp_valid[d] & slot_free
            valid[e].assign(cas(domain, m.const(1, width=1), cycle=0), when=alloc)
            op[e].assign(disp_op[d], when=alloc)
            age[e].assign(age_ctr, when=alloc)
            ptdst[e].assign(disp_ptdst[d], when=alloc)
            for s in range(n_tile_src):
                ptsrc[s][e].assign(disp_ptsrc[s][d], when=alloc)
                trdy[s][e].assign(disp_trdy[s][d], when=alloc)

    return outs


vec_rs.__pycircuit_name__ = "vec_rs"


if __name__ == "__main__":
    pass
