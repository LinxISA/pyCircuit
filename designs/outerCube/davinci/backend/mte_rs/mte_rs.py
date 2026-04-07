"""MTE Reservation Station — 16 entries, 2-issue, dual-bus wakeup.

MTE instructions may need both scalar (address/scalar operand via CDB) and
tile (via TCB) operands. TILE.LD needs scalar addr only; TILE.ST needs scalar
addr + tile source; TILE.GET needs tile source; TILE.PUT needs scalar source.
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
    CDB_PORTS,
    DISPATCH_WIDTH,
    MTE_ISSUE_WIDTH,
    MTE_RS_ENTRIES,
    PHYS_GREG_W,
    PHYS_TREG_W,
    TCB_PORTS,
    UOP_W,
)


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def mte_rs(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    n_entries: int = MTE_RS_ENTRIES,
    n_dispatch: int = DISPATCH_WIDTH,
    n_issue: int = MTE_ISSUE_WIDTH,
    n_cdb: int = CDB_PORTS,
    n_tcb: int = TCB_PORTS,
    stag_w: int = PHYS_GREG_W,
    ttag_w: int = PHYS_TREG_W,
    uop_w: int = UOP_W,
    age_w: int = AGE_W,
    prefix: str = "mrs",
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
    disp_psrc = [
        _in(inputs, f"dps{i}", m, domain, prefix, stag_w) for i in range(n_dispatch)
    ]
    disp_srdy = [
        _in(inputs, f"dsr{i}", m, domain, prefix, 1) for i in range(n_dispatch)
    ]
    disp_ptsrc = [
        _in(inputs, f"dts{i}", m, domain, prefix, ttag_w) for i in range(n_dispatch)
    ]
    disp_trdy = [
        _in(inputs, f"dtr{i}", m, domain, prefix, 1) for i in range(n_dispatch)
    ]
    disp_ptdst = [
        _in(inputs, f"dtd{i}", m, domain, prefix, ttag_w) for i in range(n_dispatch)
    ]
    disp_psdst = [
        _in(inputs, f"dpsd{i}", m, domain, prefix, stag_w) for i in range(n_dispatch)
    ]

    cdb_valid = [_in(inputs, f"cdb_v{i}", m, domain, prefix, 1) for i in range(n_cdb)]
    cdb_tag = [
        _in(inputs, f"cdb_t{i}", m, domain, prefix, stag_w) for i in range(n_cdb)
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
    psrc = [
        domain.signal(width=stag_w, reset_value=0, name=f"{prefix}_ps_{e}")
        for e in range(n_entries)
    ]
    srdy = [
        domain.signal(width=1, reset_value=0, name=f"{prefix}_sr_{e}")
        for e in range(n_entries)
    ]
    ptsrc = [
        domain.signal(width=ttag_w, reset_value=0, name=f"{prefix}_ts_{e}")
        for e in range(n_entries)
    ]
    trdy = [
        domain.signal(width=1, reset_value=0, name=f"{prefix}_tr_{e}")
        for e in range(n_entries)
    ]
    ptdst = [
        domain.signal(width=ttag_w, reset_value=0, name=f"{prefix}_td_{e}")
        for e in range(n_entries)
    ]
    psdst = [
        domain.signal(width=stag_w, reset_value=0, name=f"{prefix}_sd_{e}")
        for e in range(n_entries)
    ]

    age_ctr = domain.signal(width=age_w, reset_value=0, name=f"{prefix}_ac")

    # Dual-bus wakeup
    eff_srdy = []
    eff_trdy = []
    for e in range(n_entries):
        sr = srdy[e]
        tr = trdy[e]
        for c in range(n_cdb):
            sr = sr | (cdb_valid[c] & (cdb_tag[c] == psrc[e]) & (~sr))
        for t in range(n_tcb):
            tr = tr | (tcb_valid[t] & (tcb_tag[t] == ptsrc[e]) & (~tr))
        eff_srdy.append(sr)
        eff_trdy.append(tr)

    # Select oldest ready (both scalar and tile sources ready)
    best_v = cas(domain, m.const(0, width=1), cycle=0)
    best_i = cas(domain, m.const(0, width=eidx_w), cycle=0)
    best_a = cas(domain, m.const((1 << age_w) - 1, width=age_w), cycle=0)
    for e in range(n_entries):
        ready = valid[e] & eff_srdy[e] & eff_trdy[e]
        older = age[e] < best_a
        wins = ready & (older | (~best_v))
        best_v = mux(wins, cas(domain, m.const(1, width=1), cycle=0), best_v)
        best_i = mux(wins, cas(domain, m.const(e, width=eidx_w), cycle=0), best_i)
        best_a = mux(wins, age[e], best_a)

    outs["issue_valid"] = best_v
    outs["issue_idx"] = best_i

    issue_op = op[0]
    issue_ptdst = ptdst[0]
    issue_psdst = psdst[0]
    for e in range(n_entries):
        hit = best_i == cas(domain, m.const(e, width=eidx_w), cycle=0)
        issue_op = mux(hit, op[e], issue_op)
        issue_ptdst = mux(hit, ptdst[e], issue_ptdst)
        issue_psdst = mux(hit, psdst[e], issue_psdst)
    outs["issue_op"] = issue_op
    outs["issue_ptdst"] = issue_ptdst
    outs["issue_psdst"] = issue_psdst

    n_v = cas(domain, m.const(0, width=eidx_w + 1), cycle=0)
    for e in range(n_entries):
        n_v = n_v + valid[e]
    outs["full"] = n_v >= cas(
        domain, m.const(n_entries - n_dispatch, width=eidx_w + 1), cycle=0
    )

    if inputs is None:
        for k in outs:
            m.output(f"{prefix}_{k}", wire_of(outs[k]))

    # ── Cycle 1 ──────────────────────────────────────────────────────
    domain.next()

    age_ctr <<= (age_ctr + cas(domain, m.const(1, width=age_w), cycle=0)).trunc(age_w)

    for e in range(n_entries):
        issued = (
            best_v
            & (best_i == cas(domain, m.const(e, width=eidx_w), cycle=0))
            & (~flush)
        )
        srdy[e].assign(eff_srdy[e], when=valid[e] & (~issued))
        trdy[e].assign(eff_trdy[e], when=valid[e] & (~issued))
        valid[e].assign(cas(domain, m.const(0, width=1), cycle=0), when=issued | flush)

    for d in range(n_dispatch):
        for e in range(n_entries):
            alloc = disp_valid[d] & (~valid[e])
            valid[e].assign(cas(domain, m.const(1, width=1), cycle=0), when=alloc)
            op[e].assign(disp_op[d], when=alloc)
            age[e].assign(age_ctr, when=alloc)
            psrc[e].assign(disp_psrc[d], when=alloc)
            srdy[e].assign(disp_srdy[d], when=alloc)
            ptsrc[e].assign(disp_ptsrc[d], when=alloc)
            trdy[e].assign(disp_trdy[d], when=alloc)
            ptdst[e].assign(disp_ptdst[d], when=alloc)
            psdst[e].assign(disp_psdst[d], when=alloc)

    return outs


mte_rs.__pycircuit_name__ = "mte_rs"


if __name__ == "__main__":
    pass
