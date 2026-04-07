"""LSU Reservation Station — 24 entries, 2-issue (1 load + 1 store).

Each entry: valid, age, op, psrc1(addr base), rdy1, data1,
            psrc2(store data), rdy2, data2, pdst(load dest), imm(offset).

Wakeup via CDB (scalar tags). Separate oldest-ready select for load and store.
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
    LSU_ISSUE_WIDTH,
    LSU_RS_ENTRIES,
    PHYS_GREG_W,
    SCALAR_DATA_W,
    UOP_W,
)


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def lsu_rs(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    n_entries: int = LSU_RS_ENTRIES,
    n_dispatch: int = DISPATCH_WIDTH,
    n_issue: int = LSU_ISSUE_WIDTH,
    n_cdb: int = CDB_PORTS,
    tag_w: int = PHYS_GREG_W,
    data_w: int = SCALAR_DATA_W,
    uop_w: int = UOP_W,
    age_w: int = AGE_W,
    prefix: str = "lrs",
    inputs: dict | None = None,
) -> dict:
    eidx_w = max(1, (n_entries - 1).bit_length())
    outs: dict = {}

    # ── Cycle 0: Inputs ──────────────────────────────────────────────
    disp_valid = [
        _in(inputs, f"dv{i}", m, domain, prefix, 1) for i in range(n_dispatch)
    ]
    disp_op = [
        _in(inputs, f"dop{i}", m, domain, prefix, uop_w) for i in range(n_dispatch)
    ]
    disp_psrc1 = [
        _in(inputs, f"dps1_{i}", m, domain, prefix, tag_w) for i in range(n_dispatch)
    ]
    disp_rdy1 = [
        _in(inputs, f"dr1_{i}", m, domain, prefix, 1) for i in range(n_dispatch)
    ]
    disp_psrc2 = [
        _in(inputs, f"dps2_{i}", m, domain, prefix, tag_w) for i in range(n_dispatch)
    ]
    disp_rdy2 = [
        _in(inputs, f"dr2_{i}", m, domain, prefix, 1) for i in range(n_dispatch)
    ]
    disp_pdst = [
        _in(inputs, f"dpd{i}", m, domain, prefix, tag_w) for i in range(n_dispatch)
    ]
    disp_is_store = [
        _in(inputs, f"dst{i}", m, domain, prefix, 1) for i in range(n_dispatch)
    ]

    cdb_valid = [_in(inputs, f"cdb_v{i}", m, domain, prefix, 1) for i in range(n_cdb)]
    cdb_tag = [_in(inputs, f"cdb_t{i}", m, domain, prefix, tag_w) for i in range(n_cdb)]

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
    psrc1 = [
        domain.signal(width=tag_w, reset_value=0, name=f"{prefix}_ps1_{e}")
        for e in range(n_entries)
    ]
    rdy1 = [
        domain.signal(width=1, reset_value=0, name=f"{prefix}_r1_{e}")
        for e in range(n_entries)
    ]
    psrc2 = [
        domain.signal(width=tag_w, reset_value=0, name=f"{prefix}_ps2_{e}")
        for e in range(n_entries)
    ]
    rdy2 = [
        domain.signal(width=1, reset_value=0, name=f"{prefix}_r2_{e}")
        for e in range(n_entries)
    ]
    pdst = [
        domain.signal(width=tag_w, reset_value=0, name=f"{prefix}_pd_{e}")
        for e in range(n_entries)
    ]
    is_store = [
        domain.signal(width=1, reset_value=0, name=f"{prefix}_st_{e}")
        for e in range(n_entries)
    ]

    age_ctr = domain.signal(width=age_w, reset_value=0, name=f"{prefix}_ac")

    # ── CDB wakeup ───────────────────────────────────────────────────
    eff_rdy1 = []
    eff_rdy2 = []
    for e in range(n_entries):
        r1 = rdy1[e]
        r2 = rdy2[e]
        for c in range(n_cdb):
            r1 = r1 | (cdb_valid[c] & (cdb_tag[c] == psrc1[e]) & (~r1))
            r2 = r2 | (cdb_valid[c] & (cdb_tag[c] == psrc2[e]) & (~r2))
        eff_rdy1.append(r1)
        eff_rdy2.append(r2)

    # ── Select: oldest ready load + oldest ready store ────────────────
    for kind, kind_flag in [("ld", 0), ("st", 1)]:
        best_v = cas(domain, m.const(0, width=1), cycle=0)
        best_i = cas(domain, m.const(0, width=eidx_w), cycle=0)
        best_a = cas(domain, m.const((1 << age_w) - 1, width=age_w), cycle=0)
        for e in range(n_entries):
            if kind_flag == 0:
                type_ok = ~is_store[e]
                ready = valid[e] & type_ok & eff_rdy1[e]
            else:
                type_ok = is_store[e]
                ready = valid[e] & type_ok & eff_rdy1[e] & eff_rdy2[e]
            older = age[e] < best_a
            wins = ready & (older | (~best_v))
            best_v = mux(wins, cas(domain, m.const(1, width=1), cycle=0), best_v)
            best_i = mux(wins, cas(domain, m.const(e, width=eidx_w), cycle=0), best_i)
            best_a = mux(wins, age[e], best_a)
        outs[f"{kind}_issue_valid"] = best_v
        outs[f"{kind}_issue_idx"] = best_i

        issue_op = op[0]
        issue_pdst = pdst[0]
        for e in range(n_entries):
            hit = best_i == cas(domain, m.const(e, width=eidx_w), cycle=0)
            issue_op = mux(hit, op[e], issue_op)
            issue_pdst = mux(hit, pdst[e], issue_pdst)
        outs[f"{kind}_issue_op"] = issue_op
        outs[f"{kind}_issue_pdst"] = issue_pdst

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
        cas(domain, m.const(e, width=eidx_w), cycle=0)
        rdy1[e].assign(eff_rdy1[e], when=valid[e])
        rdy2[e].assign(eff_rdy2[e], when=valid[e])
        valid[e].assign(cas(domain, m.const(0, width=1), cycle=0), when=flush)

    for d in range(n_dispatch):
        for e in range(n_entries):
            alloc = disp_valid[d] & (~valid[e])
            valid[e].assign(cas(domain, m.const(1, width=1), cycle=0), when=alloc)
            op[e].assign(disp_op[d], when=alloc)
            age[e].assign(age_ctr, when=alloc)
            psrc1[e].assign(disp_psrc1[d], when=alloc)
            rdy1[e].assign(disp_rdy1[d], when=alloc)
            psrc2[e].assign(disp_psrc2[d], when=alloc)
            rdy2[e].assign(disp_rdy2[d], when=alloc)
            pdst[e].assign(disp_pdst[d], when=alloc)
            is_store[e].assign(disp_is_store[d], when=alloc)

    return outs


lsu_rs.__pycircuit_name__ = "lsu_rs"


if __name__ == "__main__":
    pass
