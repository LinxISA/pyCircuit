"""Rename Top — integrates Scalar RAT, Tile RAT, and Checkpoint Store.

D2 stage: for each of 4 decoded instructions per cycle:
  1. Read Scalar RAT for scalar source tags + ready bits
  2. Read Tile RAT for tile source tags + ready bits
  3. Allocate physical destination from scalar/tile free list
  4. Update RAT with new mapping (dest arch → new phys)
  5. Intra-group bypass (earlier slots in same cycle)
  6. On branch: checkpoint both RATs + free list pointers
  7. On mispredict: flash-restore both RATs from checkpoint
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
    ARCH_GREG_W,
    ARCH_GREGS,
    ARCH_TREG_W,
    ARCH_TREGS,
    CDB_PORTS,
    CHECKPOINT_SLOTS,
    CHECKPOINT_W,
    PHYS_GREG_W,
    PHYS_TREG_W,
    RENAME_WIDTH,
    TCB_PORTS,
)


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def rename(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    width: int = RENAME_WIDTH,
    n_sarch: int = ARCH_GREGS,
    sarch_w: int = ARCH_GREG_W,
    sphys_w: int = PHYS_GREG_W,
    n_tarch: int = ARCH_TREGS,
    tarch_w: int = ARCH_TREG_W,
    tphys_w: int = PHYS_TREG_W,
    n_cdb: int = CDB_PORTS,
    n_tcb: int = TCB_PORTS,
    n_ckpt: int = CHECKPOINT_SLOTS,
    ckpt_w: int = CHECKPOINT_W,
    prefix: str = "ren",
    inputs: dict | None = None,
) -> dict:
    # ── Cycle 0: Inputs from decode ──────────────────────────────────
    instr_valid = [_in(inputs, f"valid{i}", m, domain, prefix, 1) for i in range(width)]
    has_srd = [_in(inputs, f"has_srd{i}", m, domain, prefix, 1) for i in range(width)]
    has_srs1 = [
        _in(inputs, f"has_srs1_{i}", m, domain, prefix, 1) for i in range(width)
    ]
    has_srs2 = [
        _in(inputs, f"has_srs2_{i}", m, domain, prefix, 1) for i in range(width)
    ]
    srd_arch = [
        _in(inputs, f"srd{i}", m, domain, prefix, sarch_w) for i in range(width)
    ]
    srs1_arch = [
        _in(inputs, f"srs1_{i}", m, domain, prefix, sarch_w) for i in range(width)
    ]
    srs2_arch = [
        _in(inputs, f"srs2_{i}", m, domain, prefix, sarch_w) for i in range(width)
    ]

    has_trd = [_in(inputs, f"has_trd{i}", m, domain, prefix, 1) for i in range(width)]
    has_trs = [_in(inputs, f"has_trs{i}", m, domain, prefix, 1) for i in range(width)]
    trd_arch = [
        _in(inputs, f"trd{i}", m, domain, prefix, tarch_w) for i in range(width)
    ]
    trs_arch = [
        _in(inputs, f"trs{i}", m, domain, prefix, tarch_w) for i in range(width)
    ]

    is_branch = [
        _in(inputs, f"is_branch{i}", m, domain, prefix, 1) for i in range(width)
    ]

    # Free list inputs
    sfl_tag = [
        _in(inputs, f"sfl_tag{i}", m, domain, prefix, sphys_w) for i in range(width)
    ]
    tfl_tag = [
        _in(inputs, f"tfl_tag{i}", m, domain, prefix, tphys_w) for i in range(width)
    ]

    # CDB/TCB writeback (mark ready)
    cdb_valid = [_in(inputs, f"cdb_v{i}", m, domain, prefix, 1) for i in range(n_cdb)]
    cdb_tag = [
        _in(inputs, f"cdb_t{i}", m, domain, prefix, sphys_w) for i in range(n_cdb)
    ]
    tcb_valid = [_in(inputs, f"tcb_v{i}", m, domain, prefix, 1) for i in range(n_tcb)]
    tcb_tag = [
        _in(inputs, f"tcb_t{i}", m, domain, prefix, tphys_w) for i in range(n_tcb)
    ]

    # Mispredict restore
    restore = _in(inputs, "restore", m, domain, prefix, 1)
    restore_ckpt = _in(inputs, "restore_ckpt", m, domain, prefix, ckpt_w)

    # ── Scalar RAT state ─────────────────────────────────────────────
    srat_map = [
        domain.signal(width=sphys_w, reset_value=i, name=f"{prefix}_sm_{i}")
        for i in range(n_sarch)
    ]
    srat_rdy = [
        domain.signal(width=1, reset_value=1, name=f"{prefix}_sr_{i}")
        for i in range(n_sarch)
    ]

    # ── Tile RAT state ───────────────────────────────────────────────
    trat_map = [
        domain.signal(width=tphys_w, reset_value=i, name=f"{prefix}_tm_{i}")
        for i in range(n_tarch)
    ]
    trat_rdy = [
        domain.signal(width=1, reset_value=1, name=f"{prefix}_tr_{i}")
        for i in range(n_tarch)
    ]

    # ── Checkpoint store ─────────────────────────────────────────────
    ckpt_srat = [
        [
            domain.signal(width=sphys_w, reset_value=a, name=f"{prefix}_cs_{s}_{a}")
            for a in range(n_sarch)
        ]
        for s in range(n_ckpt)
    ]
    ckpt_trat = [
        [
            domain.signal(width=tphys_w, reset_value=a, name=f"{prefix}_ct_{s}_{a}")
            for a in range(n_tarch)
        ]
        for s in range(n_ckpt)
    ]
    ckpt_alloc_ptr = domain.signal(
        width=ckpt_w, reset_value=0, name=f"{prefix}_ckpt_ptr"
    )

    # ── Combinational: rename each slot with intra-group bypass ──────
    out_psrc1 = []
    out_psrc2 = []
    out_pdst = []
    out_src1_rdy = []
    out_src2_rdy = []
    out_ptsrc = []
    out_ptdst = []
    out_tsrc_rdy = []

    for i in range(width):
        # Scalar source 1 lookup
        ps1 = srat_map[0]
        ps1_rdy = srat_rdy[0]
        for a in range(n_sarch):
            hit = srs1_arch[i] == cas(domain, m.const(a, width=sarch_w), cycle=0)
            ps1 = mux(hit, srat_map[a], ps1)
            ps1_rdy = mux(hit, srat_rdy[a], ps1_rdy)

        # Scalar source 2 lookup
        ps2 = srat_map[0]
        ps2_rdy = srat_rdy[0]
        for a in range(n_sarch):
            hit = srs2_arch[i] == cas(domain, m.const(a, width=sarch_w), cycle=0)
            ps2 = mux(hit, srat_map[a], ps2)
            ps2_rdy = mux(hit, srat_rdy[a], ps2_rdy)

        # Tile source lookup
        pts = trat_map[0]
        pts_rdy = trat_rdy[0]
        for a in range(n_tarch):
            hit = trs_arch[i] == cas(domain, m.const(a, width=tarch_w), cycle=0)
            pts = mux(hit, trat_map[a], pts)
            pts_rdy = mux(hit, trat_rdy[a], pts_rdy)

        # Intra-group bypass (earlier slots in same group)
        for j in range(i):
            bp_s1 = instr_valid[j] & has_srd[j] & (srd_arch[j] == srs1_arch[i])
            ps1 = mux(bp_s1, out_pdst[j], ps1)
            ps1_rdy = mux(bp_s1, cas(domain, m.const(0, width=1), cycle=0), ps1_rdy)

            bp_s2 = instr_valid[j] & has_srd[j] & (srd_arch[j] == srs2_arch[i])
            ps2 = mux(bp_s2, out_pdst[j], ps2)
            ps2_rdy = mux(bp_s2, cas(domain, m.const(0, width=1), cycle=0), ps2_rdy)

            bp_ts = instr_valid[j] & has_trd[j] & (trd_arch[j] == trs_arch[i])
            pts = mux(bp_ts, out_ptdst[j] if j < len(out_ptdst) else pts, pts)
            pts_rdy = mux(bp_ts, cas(domain, m.const(0, width=1), cycle=0), pts_rdy)

        pd = mux(
            has_srd[i], sfl_tag[i], cas(domain, m.const(0, width=sphys_w), cycle=0)
        )
        ptd = mux(
            has_trd[i], tfl_tag[i], cas(domain, m.const(0, width=tphys_w), cycle=0)
        )

        out_psrc1.append(ps1)
        out_psrc2.append(ps2)
        out_pdst.append(pd)
        out_src1_rdy.append(
            mux(has_srs1[i], ps1_rdy, cas(domain, m.const(1, width=1), cycle=0))
        )
        out_src2_rdy.append(
            mux(has_srs2[i], ps2_rdy, cas(domain, m.const(1, width=1), cycle=0))
        )
        out_ptsrc.append(pts)
        out_ptdst.append(ptd)
        out_tsrc_rdy.append(
            mux(has_trs[i], pts_rdy, cas(domain, m.const(1, width=1), cycle=0))
        )

    # ── Outputs ──────────────────────────────────────────────────────
    any_branch = cas(domain, m.const(0, width=1), cycle=0)
    for i in range(width):
        any_branch = any_branch | (instr_valid[i] & is_branch[i])

    if inputs is None:
        for i in range(width):
            m.output(f"{prefix}_out_psrc1_{i}", wire_of(out_psrc1[i]))
            m.output(f"{prefix}_out_psrc2_{i}", wire_of(out_psrc2[i]))
            m.output(f"{prefix}_out_pdst_{i}", wire_of(out_pdst[i]))
            m.output(f"{prefix}_out_src1_rdy_{i}", wire_of(out_src1_rdy[i]))
            m.output(f"{prefix}_out_src2_rdy_{i}", wire_of(out_src2_rdy[i]))
            m.output(f"{prefix}_out_ptsrc_{i}", wire_of(out_ptsrc[i]))
            m.output(f"{prefix}_out_ptdst_{i}", wire_of(out_ptdst[i]))
            m.output(f"{prefix}_out_tsrc_rdy_{i}", wire_of(out_tsrc_rdy[i]))
            m.output(f"{prefix}_out_valid_{i}", wire_of(instr_valid[i]))
        m.output(f"{prefix}_ckpt_id", wire_of(ckpt_alloc_ptr))

    # ── Cycle 1: Sequential updates ──────────────────────────────────
    domain.next()

    # Update Scalar RAT
    for i in range(width):
        d = srd_arch[i]
        for a in range(n_sarch):
            hit = (
                instr_valid[i]
                & has_srd[i]
                & (d == cas(domain, m.const(a, width=sarch_w), cycle=0))
                & (~restore)
            )
            srat_map[a].assign(out_pdst[i], when=hit)
            srat_rdy[a].assign(cas(domain, m.const(0, width=1), cycle=0), when=hit)

    # Update Tile RAT
    for i in range(width):
        d = trd_arch[i]
        for a in range(n_tarch):
            hit = (
                instr_valid[i]
                & has_trd[i]
                & (d == cas(domain, m.const(a, width=tarch_w), cycle=0))
                & (~restore)
            )
            trat_map[a].assign(out_ptdst[i], when=hit)
            trat_rdy[a].assign(cas(domain, m.const(0, width=1), cycle=0), when=hit)

    # CDB writeback: mark scalar ready
    for c in range(n_cdb):
        for a in range(n_sarch):
            hit = cdb_valid[c] & (cdb_tag[c] == srat_map[a])
            srat_rdy[a].assign(cas(domain, m.const(1, width=1), cycle=0), when=hit)

    # TCB writeback: mark tile ready
    for t in range(n_tcb):
        for a in range(n_tarch):
            hit = tcb_valid[t] & (tcb_tag[t] == trat_map[a])
            trat_rdy[a].assign(cas(domain, m.const(1, width=1), cycle=0), when=hit)

    # Checkpoint: save on branch
    for s in range(n_ckpt):
        save = any_branch & (
            ckpt_alloc_ptr == cas(domain, m.const(s, width=ckpt_w), cycle=0)
        )
        for a in range(n_sarch):
            ckpt_srat[s][a].assign(srat_map[a], when=save)
        for a in range(n_tarch):
            ckpt_trat[s][a].assign(trat_map[a], when=save)

    next_ckpt = mux(
        any_branch,
        (ckpt_alloc_ptr + cas(domain, m.const(1, width=ckpt_w), cycle=0)).trunc(ckpt_w),
        ckpt_alloc_ptr,
    )
    ckpt_alloc_ptr <<= next_ckpt

    # Restore on mispredict
    for s in range(n_ckpt):
        is_this = restore & (
            restore_ckpt == cas(domain, m.const(s, width=ckpt_w), cycle=0)
        )
        for a in range(n_sarch):
            srat_map[a].assign(ckpt_srat[s][a], when=is_this)
            srat_rdy[a].assign(cas(domain, m.const(1, width=1), cycle=0), when=is_this)
        for a in range(n_tarch):
            trat_map[a].assign(ckpt_trat[s][a], when=is_this)
            trat_rdy[a].assign(cas(domain, m.const(1, width=1), cycle=0), when=is_this)

    return {
        "out_psrc1": out_psrc1,
        "out_psrc2": out_psrc2,
        "out_pdst": out_pdst,
        "out_src1_rdy": out_src1_rdy,
        "out_src2_rdy": out_src2_rdy,
        "out_ptsrc": out_ptsrc,
        "out_ptdst": out_ptdst,
        "out_tsrc_rdy": out_tsrc_rdy,
        "out_valid": instr_valid,
        "ckpt_id": ckpt_alloc_ptr,
    }


rename.__pycircuit_name__ = "rename"


if __name__ == "__main__":
    pass
