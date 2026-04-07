"""Scalar Reservation Station — 32 entries, 6-issue (4 ALU + 1 MUL + 1 BRU).

Each entry: valid, age(6), op(8), psrc1(7), rdy1(1), data1(64),
            psrc2(7), rdy2(1), data2(64), pdst(7), ckpt(3) ≈ 170 bits.

CDB wakeup: 32 × 2 × 6 = 384 tag comparators.
Select: oldest-ready for each of 6 issue slots.
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
    CHECKPOINT_W,
    DISPATCH_WIDTH,
    PHYS_GREG_W,
    SCALAR_DATA_W,
    SCALAR_ISSUE_WIDTH,
    SCALAR_RS_ENTRIES,
    UOP_W,
)


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def scalar_rs(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    n_entries: int = SCALAR_RS_ENTRIES,
    n_dispatch: int = DISPATCH_WIDTH,
    n_issue: int = SCALAR_ISSUE_WIDTH,
    n_cdb: int = CDB_PORTS,
    tag_w: int = PHYS_GREG_W,
    data_w: int = SCALAR_DATA_W,
    uop_w: int = UOP_W,
    age_w: int = AGE_W,
    ckpt_w: int = CHECKPOINT_W,
    prefix: str = "srs",
    inputs: dict | None = None,
) -> dict:
    entry_idx_w = max(1, (n_entries - 1).bit_length())

    # ── Cycle 0: Dispatch inputs (up to n_dispatch per cycle) ────────
    disp_valid = [
        _in(inputs, f"disp_valid{i}", m, domain, prefix, 1) for i in range(n_dispatch)
    ]
    disp_op = [
        _in(inputs, f"disp_op{i}", m, domain, prefix, uop_w) for i in range(n_dispatch)
    ]
    disp_psrc1 = [
        _in(inputs, f"disp_psrc1_{i}", m, domain, prefix, tag_w)
        for i in range(n_dispatch)
    ]
    disp_rdy1 = [
        _in(inputs, f"disp_rdy1_{i}", m, domain, prefix, 1) for i in range(n_dispatch)
    ]
    disp_data1 = [
        _in(inputs, f"disp_data1_{i}", m, domain, prefix, data_w)
        for i in range(n_dispatch)
    ]
    disp_psrc2 = [
        _in(inputs, f"disp_psrc2_{i}", m, domain, prefix, tag_w)
        for i in range(n_dispatch)
    ]
    disp_rdy2 = [
        _in(inputs, f"disp_rdy2_{i}", m, domain, prefix, 1) for i in range(n_dispatch)
    ]
    disp_data2 = [
        _in(inputs, f"disp_data2_{i}", m, domain, prefix, data_w)
        for i in range(n_dispatch)
    ]
    disp_pdst = [
        _in(inputs, f"disp_pdst{i}", m, domain, prefix, tag_w)
        for i in range(n_dispatch)
    ]
    disp_ckpt = [
        _in(inputs, f"disp_ckpt{i}", m, domain, prefix, ckpt_w)
        for i in range(n_dispatch)
    ]

    # CDB broadcast
    cdb_valid = [
        _in(inputs, f"cdb_valid{i}", m, domain, prefix, 1) for i in range(n_cdb)
    ]
    cdb_tag = [
        _in(inputs, f"cdb_tag{i}", m, domain, prefix, tag_w) for i in range(n_cdb)
    ]
    cdb_data = [
        _in(inputs, f"cdb_data{i}", m, domain, prefix, data_w) for i in range(n_cdb)
    ]

    # Flush (mispredict)
    flush = _in(inputs, "flush", m, domain, prefix, 1)

    # ── State: RS entries ────────────────────────────────────────────
    valid = [
        domain.signal(width=1, reset_value=0, name=f"{prefix}_v_{e}")
        for e in range(n_entries)
    ]
    op = [
        domain.signal(width=uop_w, reset_value=0, name=f"{prefix}_op_{e}")
        for e in range(n_entries)
    ]
    age = [
        domain.signal(width=age_w, reset_value=0, name=f"{prefix}_age_{e}")
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
    data1 = [
        domain.signal(width=data_w, reset_value=0, name=f"{prefix}_d1_{e}")
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
    data2 = [
        domain.signal(width=data_w, reset_value=0, name=f"{prefix}_d2_{e}")
        for e in range(n_entries)
    ]
    pdst = [
        domain.signal(width=tag_w, reset_value=0, name=f"{prefix}_pd_{e}")
        for e in range(n_entries)
    ]
    ckpt = [
        domain.signal(width=ckpt_w, reset_value=0, name=f"{prefix}_ck_{e}")
        for e in range(n_entries)
    ]

    age_counter = domain.signal(width=age_w, reset_value=0, name=f"{prefix}_age_ctr")

    # ── Combinational: CDB snoop → update ready + data ───────────────
    eff_rdy1 = []
    eff_data1 = []
    eff_rdy2 = []
    eff_data2 = []
    for e in range(n_entries):
        r1 = rdy1[e]
        d1 = data1[e]
        r2 = rdy2[e]
        d2 = data2[e]
        for c in range(n_cdb):
            m1 = cdb_valid[c] & (cdb_tag[c] == psrc1[e]) & (~r1)
            r1 = r1 | m1
            d1 = mux(m1, cdb_data[c], d1)
            m2 = cdb_valid[c] & (cdb_tag[c] == psrc2[e]) & (~r2)
            r2 = r2 | m2
            d2 = mux(m2, cdb_data[c], d2)
        eff_rdy1.append(r1)
        eff_data1.append(d1)
        eff_rdy2.append(r2)
        eff_data2.append(d2)

    # ── Combinational: Select oldest ready for issue ─────────────────
    # Single-issue select (oldest ready among all entries)
    # For simplicity, we select 1 instruction to issue per cycle
    best_valid = cas(domain, m.const(0, width=1), cycle=0)
    best_idx = cas(domain, m.const(0, width=entry_idx_w), cycle=0)
    best_age = cas(domain, m.const((1 << age_w) - 1, width=age_w), cycle=0)

    for e in range(n_entries):
        is_ready = valid[e] & eff_rdy1[e] & eff_rdy2[e]
        is_older = age[e] < best_age
        wins = is_ready & (is_older | (~best_valid))
        best_valid = mux(wins, cas(domain, m.const(1, width=1), cycle=0), best_valid)
        best_idx = mux(
            wins, cas(domain, m.const(e, width=entry_idx_w), cycle=0), best_idx
        )
        best_age = mux(wins, age[e], best_age)

    # Issue data outputs
    issue_op = op[0]
    issue_d1 = eff_data1[0]
    issue_d2 = eff_data2[0]
    issue_pdst_out = pdst[0]
    issue_ckpt_out = ckpt[0]
    for e in range(n_entries):
        hit = best_idx == cas(domain, m.const(e, width=entry_idx_w), cycle=0)
        issue_op = mux(hit, op[e], issue_op)
        issue_d1 = mux(hit, eff_data1[e], issue_d1)
        issue_d2 = mux(hit, eff_data2[e], issue_d2)
        issue_pdst_out = mux(hit, pdst[e], issue_pdst_out)
        issue_ckpt_out = mux(hit, ckpt[e], issue_ckpt_out)

    # Full signal
    n_valid = cas(domain, m.const(0, width=entry_idx_w + 1), cycle=0)
    for e in range(n_entries):
        n_valid = n_valid + valid[e]
    n_avail = cas(domain, m.const(n_entries, width=entry_idx_w + 1), cycle=0) - n_valid
    stall = n_avail < cas(domain, m.const(n_dispatch, width=entry_idx_w + 1), cycle=0)

    out = {
        "issue_valid": best_valid,
        "issue_idx": best_idx,
        "issue_op": issue_op,
        "issue_data1": issue_d1,
        "issue_data2": issue_d2,
        "issue_pdst": issue_pdst_out,
        "issue_ckpt": issue_ckpt_out,
        "full": stall,
    }
    if inputs is None:
        m.output(f"{prefix}_issue_valid", wire_of(out["issue_valid"]))
        m.output(f"{prefix}_issue_idx", wire_of(out["issue_idx"]))
        m.output(f"{prefix}_issue_op", wire_of(out["issue_op"]))
        m.output(f"{prefix}_issue_data1", wire_of(out["issue_data1"]))
        m.output(f"{prefix}_issue_data2", wire_of(out["issue_data2"]))
        m.output(f"{prefix}_issue_pdst", wire_of(out["issue_pdst"]))
        m.output(f"{prefix}_issue_ckpt", wire_of(out["issue_ckpt"]))
        m.output(f"{prefix}_full", wire_of(out["full"]))

    # ── Cycle 1: Sequential update ───────────────────────────────────
    domain.next()

    # Advance age counter
    next_age = (age_counter + cas(domain, m.const(1, width=age_w), cycle=0)).trunc(
        age_w
    )
    age_counter <<= next_age

    # Find first free slots for dispatch
    for e in range(n_entries):
        etag = cas(domain, m.const(e, width=entry_idx_w), cycle=0)

        # Issue: clear entry
        issued = best_valid & (best_idx == etag) & (~flush)

        # CDB snoop: latch forwarded data
        new_rdy1 = eff_rdy1[e]
        new_data1 = eff_data1[e]
        new_rdy2 = eff_rdy2[e]
        new_data2 = eff_data2[e]

        rdy1[e].assign(new_rdy1, when=valid[e] & (~issued))
        data1[e].assign(new_data1, when=valid[e] & (~issued))
        rdy2[e].assign(new_rdy2, when=valid[e] & (~issued))
        data2[e].assign(new_data2, when=valid[e] & (~issued))

        # Invalidate on issue or flush
        clear = issued | flush
        valid[e].assign(cas(domain, m.const(0, width=1), cycle=0), when=clear)

    # Dispatch: allocate new entries (simplified: first-fit)
    allocated = cas(domain, m.const(0, width=entry_idx_w), cycle=0)
    for d in range(n_dispatch):
        for e in range(n_entries):
            etag = cas(domain, m.const(e, width=entry_idx_w), cycle=0)
            slot_free = ~valid[e]
            dtag = cas(domain, m.const(d, width=entry_idx_w), cycle=0)
            is_this_disp = disp_valid[d] & slot_free & (allocated == dtag)
            valid[e].assign(
                cas(domain, m.const(1, width=1), cycle=0), when=is_this_disp
            )
            op[e].assign(disp_op[d], when=is_this_disp)
            age[e].assign(age_counter, when=is_this_disp)
            psrc1[e].assign(disp_psrc1[d], when=is_this_disp)
            rdy1[e].assign(disp_rdy1[d], when=is_this_disp)
            data1[e].assign(disp_data1[d], when=is_this_disp)
            psrc2[e].assign(disp_psrc2[d], when=is_this_disp)
            rdy2[e].assign(disp_rdy2[d], when=is_this_disp)
            data2[e].assign(disp_data2[d], when=is_this_disp)
            pdst[e].assign(disp_pdst[d], when=is_this_disp)
            ckpt[e].assign(disp_ckpt[d], when=is_this_disp)

    return out


scalar_rs.__pycircuit_name__ = "scalar_rs"


if __name__ == "__main__":
    pass
