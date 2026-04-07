"""Dispatch Stage (DS) — routes renamed instructions to 5 RS by domain.

Domain routing (from opcode[6:5]):
  00/01 (scalar)  →  Scalar RS (ALU/MUL/BRU) or LSU RS (load/store)
  10    (vec/MTE) →  Vector RS or MTE RS (based on opcode subfield)
  11    (cube)    →  Cube RS

Stall logic: if target RS is full, stall the entire dispatch group.
"""

from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    mux,
    wire_of,
)

from ..common.parameters import (
    DISPATCH_WIDTH,
    PHYS_GREG_W,
    PHYS_TREG_W,
    UOP_W,
)

DOMAIN_W = 2
DOMAIN_SCALAR = 0b00
DOMAIN_SCALAR_ALT = 0b01
DOMAIN_VEC_MTE = 0b10
DOMAIN_CUBE = 0b11


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def dispatch(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    width: int = DISPATCH_WIDTH,
    uop_w: int = UOP_W,
    stag_w: int = PHYS_GREG_W,
    ttag_w: int = PHYS_TREG_W,
    prefix: str = "ds",
    inputs: dict | None = None,
) -> dict:
    # ── Inputs from rename ───────────────────────────────────────────
    in_valid = [_in(inputs, f"valid{i}", m, domain, prefix, 1) for i in range(width)]
    in_domain = [
        _in(inputs, f"domain{i}", m, domain, prefix, DOMAIN_W) for i in range(width)
    ]
    in_op = [_in(inputs, f"op{i}", m, domain, prefix, uop_w) for i in range(width)]
    in_is_load = [
        _in(inputs, f"is_load{i}", m, domain, prefix, 1) for i in range(width)
    ]
    in_is_store = [
        _in(inputs, f"is_store{i}", m, domain, prefix, 1) for i in range(width)
    ]
    in_is_vec = [_in(inputs, f"is_vec{i}", m, domain, prefix, 1) for i in range(width)]

    in_psrc1 = [
        _in(inputs, f"psrc1_{i}", m, domain, prefix, stag_w) for i in range(width)
    ]
    in_psrc2 = [
        _in(inputs, f"psrc2_{i}", m, domain, prefix, stag_w) for i in range(width)
    ]
    in_pdst = [_in(inputs, f"pdst{i}", m, domain, prefix, stag_w) for i in range(width)]
    in_rdy1 = [_in(inputs, f"rdy1_{i}", m, domain, prefix, 1) for i in range(width)]
    in_rdy2 = [_in(inputs, f"rdy2_{i}", m, domain, prefix, 1) for i in range(width)]
    in_ptsrc = [
        _in(inputs, f"ptsrc{i}", m, domain, prefix, ttag_w) for i in range(width)
    ]
    in_ptdst = [
        _in(inputs, f"ptdst{i}", m, domain, prefix, ttag_w) for i in range(width)
    ]
    in_trdy = [_in(inputs, f"trdy{i}", m, domain, prefix, 1) for i in range(width)]

    # RS full signals
    scalar_rs_full = _in(inputs, "srs_full", m, domain, prefix, 1)
    lsu_rs_full = _in(inputs, "lrs_full", m, domain, prefix, 1)
    vec_rs_full = _in(inputs, "vrs_full", m, domain, prefix, 1)
    cube_rs_full = _in(inputs, "crs_full", m, domain, prefix, 1)
    mte_rs_full = _in(inputs, "mrs_full", m, domain, prefix, 1)

    to_srs: list = []
    to_lrs: list = []
    to_vrs: list = []
    to_crs: list = []
    to_mrs: list = []
    out_valid: list = []
    out_op: list = []
    out_psrc1: list = []
    out_psrc2: list = []
    out_pdst: list = []
    out_rdy1: list = []
    out_rdy2: list = []
    out_ptsrc: list = []
    out_ptdst: list = []
    out_trdy: list = []

    # ── Domain classification + routing ──────────────────────────────
    for i in range(width):
        d = in_domain[i]

        is_scalar = (
            d == cas(domain, m.const(DOMAIN_SCALAR, width=DOMAIN_W), cycle=0)
        ) | (d == cas(domain, m.const(DOMAIN_SCALAR_ALT, width=DOMAIN_W), cycle=0))
        is_cube = d == cas(domain, m.const(DOMAIN_CUBE, width=DOMAIN_W), cycle=0)
        is_vm = d == cas(domain, m.const(DOMAIN_VEC_MTE, width=DOMAIN_W), cycle=0)

        to_scalar = is_scalar & (~in_is_load[i]) & (~in_is_store[i])
        to_lsu = is_scalar & (in_is_load[i] | in_is_store[i])
        to_vec = is_vm & in_is_vec[i]
        to_mte = is_vm & (~in_is_vec[i])
        to_cube = is_cube

        # Check if target RS has space
        target_full = mux(
            to_scalar,
            scalar_rs_full,
            mux(
                to_lsu,
                lsu_rs_full,
                mux(
                    to_vec,
                    vec_rs_full,
                    mux(
                        to_cube,
                        cube_rs_full,
                        mux(
                            to_mte,
                            mte_rs_full,
                            cas(domain, m.const(0, width=1), cycle=0),
                        ),
                    ),
                ),
            ),
        )

        can_dispatch = in_valid[i] & (~target_full)

        to_srs.append(can_dispatch & to_scalar)
        to_lrs.append(can_dispatch & to_lsu)
        to_vrs.append(can_dispatch & to_vec)
        to_crs.append(can_dispatch & to_cube)
        to_mrs.append(can_dispatch & to_mte)

        out_valid.append(can_dispatch)
        out_op.append(in_op[i])
        out_psrc1.append(in_psrc1[i])
        out_psrc2.append(in_psrc2[i])
        out_pdst.append(in_pdst[i])
        out_rdy1.append(in_rdy1[i])
        out_rdy2.append(in_rdy2[i])
        out_ptsrc.append(in_ptsrc[i])
        out_ptdst.append(in_ptdst[i])
        out_trdy.append(in_trdy[i])

    # Global stall: any instruction blocked by full RS
    any_stall = cas(domain, m.const(0, width=1), cycle=0)
    for i in range(width):
        d = in_domain[i]
        is_scalar = (
            d == cas(domain, m.const(DOMAIN_SCALAR, width=DOMAIN_W), cycle=0)
        ) | (d == cas(domain, m.const(DOMAIN_SCALAR_ALT, width=DOMAIN_W), cycle=0))
        is_cube = d == cas(domain, m.const(DOMAIN_CUBE, width=DOMAIN_W), cycle=0)
        is_vm = d == cas(domain, m.const(DOMAIN_VEC_MTE, width=DOMAIN_W), cycle=0)
        to_scalar = is_scalar & (~in_is_load[i]) & (~in_is_store[i])
        to_lsu = is_scalar & (in_is_load[i] | in_is_store[i])
        to_vec = is_vm & in_is_vec[i]
        to_mte = is_vm & (~in_is_vec[i])
        to_cube = is_cube
        blocked = in_valid[i] & (
            (to_scalar & scalar_rs_full)
            | (to_lsu & lsu_rs_full)
            | (to_vec & vec_rs_full)
            | (to_cube & cube_rs_full)
            | (to_mte & mte_rs_full)
        )
        any_stall = any_stall | blocked

    out = {
        "to_srs": to_srs,
        "to_lrs": to_lrs,
        "to_vrs": to_vrs,
        "to_crs": to_crs,
        "to_mrs": to_mrs,
        "out_valid": out_valid,
        "out_op": out_op,
        "out_psrc1": out_psrc1,
        "out_psrc2": out_psrc2,
        "out_pdst": out_pdst,
        "out_rdy1": out_rdy1,
        "out_rdy2": out_rdy2,
        "out_ptsrc": out_ptsrc,
        "out_ptdst": out_ptdst,
        "out_trdy": out_trdy,
        "stall": any_stall,
    }
    if inputs is None:

        for i, w in enumerate(out["to_srs"]):
            m.output(f"{prefix}_to_srs{i}", wire_of(w))
        for i, w in enumerate(out["to_lrs"]):
            m.output(f"{prefix}_to_lrs{i}", wire_of(w))
        for i, w in enumerate(out["to_vrs"]):
            m.output(f"{prefix}_to_vrs{i}", wire_of(w))
        for i, w in enumerate(out["to_crs"]):
            m.output(f"{prefix}_to_crs{i}", wire_of(w))
        for i, w in enumerate(out["to_mrs"]):
            m.output(f"{prefix}_to_mrs{i}", wire_of(w))
        for i, w in enumerate(out["out_valid"]):
            m.output(f"{prefix}_out_valid{i}", wire_of(w))
        for i, w in enumerate(out["out_op"]):
            m.output(f"{prefix}_out_op{i}", wire_of(w))
        for i, w in enumerate(out["out_psrc1"]):
            m.output(f"{prefix}_out_psrc1_{i}", wire_of(w))
        for i, w in enumerate(out["out_psrc2"]):
            m.output(f"{prefix}_out_psrc2_{i}", wire_of(w))
        for i, w in enumerate(out["out_pdst"]):
            m.output(f"{prefix}_out_pdst{i}", wire_of(w))
        for i, w in enumerate(out["out_rdy1"]):
            m.output(f"{prefix}_out_rdy1_{i}", wire_of(w))
        for i, w in enumerate(out["out_rdy2"]):
            m.output(f"{prefix}_out_rdy2_{i}", wire_of(w))
        for i, w in enumerate(out["out_ptsrc"]):
            m.output(f"{prefix}_out_ptsrc{i}", wire_of(w))
        for i, w in enumerate(out["out_ptdst"]):
            m.output(f"{prefix}_out_ptdst{i}", wire_of(w))
        for i, w in enumerate(out["out_trdy"]):
            m.output(f"{prefix}_out_trdy{i}", wire_of(w))
        m.output(f"{prefix}_stall", wire_of(out["stall"]))
    return out


dispatch.__pycircuit_name__ = "dispatch"


if __name__ == "__main__":
    pass
