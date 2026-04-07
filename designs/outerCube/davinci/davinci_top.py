"""Davinci OoO Core — Top-Level Structural Wiring.

Composes all sub-modules into a single 12-stage pipeline using
``domain.call()`` which wraps push/pop cycle isolation automatically.

Pipeline:  F1 → F2 → D1 → D2 → DS → IS → EX1–EXn → WB

Follows the standard sub-module convention:
  - ``inputs: dict | None = None`` for composed / standalone dual-mode.
  - Returns ``dict[str, CycleAwareSignal | list]`` of output signals.
  - ``m.output()`` only emitted in standalone mode (``inputs is None``).
"""

from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    compile_cycle_aware,
    submodule_input,
    wire_of,
)

from .backend.cube_rs.cube_rs import cube_rs
from .backend.lsu.lsu import lsu
from .backend.lsu_rs.lsu_rs import lsu_rs
from .backend.mte_rs.mte_rs import mte_rs
from .backend.scalar_exu.alu import alu
from .backend.scalar_exu.bru import bru
from .backend.scalar_exu.muldiv import muldiv
from .backend.scalar_rs.scalar_rs import scalar_rs
from .backend.vec_rs.vec_rs import vec_rs
from .common.parameters import (
    ARCH_GREGS,
    CDB_PORTS,
    CHECKPOINT_W,
    CUBE_RS_ENTRIES,
    FETCH_WIDTH,
    INSTR_WIDTH,
    LSU_RS_ENTRIES,
    MTE_RS_ENTRIES,
    PHYS_GREG_W,
    PHYS_TREG_W,
    SCALAR_DATA_W,
    SCALAR_ISSUE_WIDTH,
    SCALAR_RS_ENTRIES,
    TCB_PORTS,
    VEC_RS_ENTRIES,
)
from .dispatch.dispatch import dispatch
from .frontend.decode.decode import decoder
from .frontend.fetch.fetch import fetch
from .frontend.rename.rename import rename


def davinci_top(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    inputs: dict | None = None,
    prefix: str = "dv",
) -> dict:
    W = FETCH_WIDTH
    ADDR_W = 64
    zero1 = cas(domain, m.const(0, width=1), cycle=0)
    _in = submodule_input

    # ═══════════════════════════════════════════════════════════════════
    #  External Inputs
    # ═══════════════════════════════════════════════════════════════════
    stall_in = _in(inputs, "stall", m, domain, prefix=prefix, width=1)
    icache_valid = _in(inputs, "icache_valid", m, domain, prefix=prefix, width=1)
    _in(
        inputs, "dmem_rdata", m, domain, prefix=prefix, width=SCALAR_DATA_W
    )
    _in(inputs, "dmem_rvalid", m, domain, prefix=prefix, width=1)
    bru_redirect = _in(inputs, "bru_redirect", m, domain, prefix=prefix, width=1)
    bru_target = _in(inputs, "bru_target", m, domain, prefix=prefix, width=ADDR_W)

    icache_data = []
    for i in range(W):
        icache_data.append(
            _in(inputs, f"icache_data{i}", m, domain, prefix=prefix, width=INSTR_WIDTH)
        )

    # ═══════════════════════════════════════════════════════════════════
    #  1. Fetch (F1/F2)
    # ═══════════════════════════════════════════════════════════════════
    fetch_out = domain.call(
        fetch,
        inputs={
            "stall": stall_in,
            "redirect_valid": bru_redirect,
            "redirect_target": bru_target,
            "bpu_taken": zero1,
            "bpu_target": cas(domain, m.const(0, width=ADDR_W), cycle=0),
        },
        addr_w=ADDR_W,
        fetch_w=W,
        prefix=f"{prefix}_fe",
    )

    fe_pc = fetch_out["pc"]
    fe_valid = fetch_out["valid"] & icache_valid

    # ═══════════════════════════════════════════════════════════════════
    #  2. Decode (D1)
    # ═══════════════════════════════════════════════════════════════════
    dec_inputs = {}
    for i in range(W):
        dec_inputs[f"valid{i}"] = fe_valid
        dec_inputs[f"instr{i}"] = icache_data[i]

    dec_out = domain.call(decoder, inputs=dec_inputs, width=W, prefix=f"{prefix}_dec")

    dec_out_valid = dec_out["out_valid"]
    dec_domain = dec_out["domain"]
    dec_opcode = dec_out["opcode"]
    dec_rd = dec_out["rd"]
    dec_rs1 = dec_out["rs1"]
    dec_rs2 = dec_out["rs2"]
    dec_has_srd = dec_out["has_srd"]
    dec_has_trd = dec_out["has_trd"]
    dec_has_trs = dec_out["has_trs"]
    dec_is_branch = dec_out["is_branch"]

    # ═══════════════════════════════════════════════════════════════════
    #  3. Rename (D2) — Scalar RAT + Tile RAT + Checkpoint
    # ═══════════════════════════════════════════════════════════════════
    ren_inputs = {
        "restore": bru_redirect,
        "restore_ckpt": cas(domain, m.const(0, width=CHECKPOINT_W), cycle=0),
    }
    for i in range(W):
        ren_inputs[f"valid{i}"] = dec_out_valid[i]
        ren_inputs[f"has_srd{i}"] = dec_has_srd[i]
        ren_inputs[f"has_srs1_{i}"] = dec_has_srd[i]
        ren_inputs[f"has_srs2_{i}"] = dec_has_srd[i]
        ren_inputs[f"srd{i}"] = dec_rd[i]
        ren_inputs[f"srs1_{i}"] = dec_rs1[i]
        ren_inputs[f"srs2_{i}"] = dec_rs2[i]
        ren_inputs[f"has_trd{i}"] = dec_has_trd[i]
        ren_inputs[f"has_trs{i}"] = dec_has_trs[i]
        ren_inputs[f"trd{i}"] = dec_rd[i]
        ren_inputs[f"trs{i}"] = dec_rs1[i]
        ren_inputs[f"is_branch{i}"] = dec_is_branch[i]
        ren_inputs[f"sfl_tag{i}"] = cas(
            domain, m.const(ARCH_GREGS + i, width=PHYS_GREG_W), cycle=0
        )
        ren_inputs[f"tfl_tag{i}"] = cas(domain, m.const(i, width=PHYS_TREG_W), cycle=0)

    for c in range(CDB_PORTS):
        ren_inputs[f"cdb_v{c}"] = zero1
        ren_inputs[f"cdb_t{c}"] = cas(domain, m.const(0, width=PHYS_GREG_W), cycle=0)
    for t in range(TCB_PORTS):
        ren_inputs[f"tcb_v{t}"] = zero1
        ren_inputs[f"tcb_t{t}"] = cas(domain, m.const(0, width=PHYS_TREG_W), cycle=0)

    ren_out = domain.call(rename, inputs=ren_inputs, width=W, prefix=f"{prefix}_ren")

    ren_psrc1 = ren_out["out_psrc1"]
    ren_psrc2 = ren_out["out_psrc2"]
    ren_pdst = ren_out["out_pdst"]
    ren_src1_rdy = ren_out["out_src1_rdy"]
    ren_src2_rdy = ren_out["out_src2_rdy"]
    ren_ptsrc = ren_out["out_ptsrc"]
    ren_ptdst = ren_out["out_ptdst"]
    ren_tsrc_rdy = ren_out["out_tsrc_rdy"]
    ren_valid = ren_out["out_valid"]

    # ═══════════════════════════════════════════════════════════════════
    #  4. Dispatch (DS) — route to 5 RS by domain
    # ═══════════════════════════════════════════════════════════════════
    ds_inputs = {
        "srs_full": zero1,
        "lrs_full": zero1,
        "vrs_full": zero1,
        "crs_full": zero1,
        "mrs_full": zero1,
    }
    for i in range(W):
        ds_inputs[f"valid{i}"] = ren_valid[i]
        ds_inputs[f"domain{i}"] = dec_domain[i]
        ds_inputs[f"op{i}"] = dec_opcode[i]
        ds_inputs[f"is_load{i}"] = zero1
        ds_inputs[f"is_store{i}"] = zero1
        ds_inputs[f"is_vec{i}"] = zero1
        ds_inputs[f"psrc1_{i}"] = ren_psrc1[i]
        ds_inputs[f"psrc2_{i}"] = ren_psrc2[i]
        ds_inputs[f"pdst{i}"] = ren_pdst[i]
        ds_inputs[f"rdy1_{i}"] = ren_src1_rdy[i]
        ds_inputs[f"rdy2_{i}"] = ren_src2_rdy[i]
        ds_inputs[f"ptsrc{i}"] = ren_ptsrc[i]
        ds_inputs[f"ptdst{i}"] = ren_ptdst[i]
        ds_inputs[f"trdy{i}"] = ren_tsrc_rdy[i]

    ds_out = domain.call(dispatch, inputs=ds_inputs, width=W, prefix=f"{prefix}_ds")

    ds_to_srs = ds_out["to_srs"]
    ds_to_lrs = ds_out["to_lrs"]
    ds_to_vrs = ds_out["to_vrs"]
    ds_to_crs = ds_out["to_crs"]
    ds_to_mrs = ds_out["to_mrs"]
    ds_op = ds_out["out_op"]
    ds_psrc1 = ds_out["out_psrc1"]
    ds_psrc2 = ds_out["out_psrc2"]
    ds_pdst = ds_out["out_pdst"]
    ds_rdy1 = ds_out["out_rdy1"]
    ds_rdy2 = ds_out["out_rdy2"]
    ds_ptsrc = ds_out["out_ptsrc"]
    ds_ptdst = ds_out["out_ptdst"]
    ds_trdy = ds_out["out_trdy"]

    # ═══════════════════════════════════════════════════════════════════
    #  5. Scalar RS (32 entries)
    # ═══════════════════════════════════════════════════════════════════
    srs_inputs = {"flush": bru_redirect}
    for i in range(W):
        srs_inputs[f"disp_valid{i}"] = ds_to_srs[i]
        srs_inputs[f"disp_op{i}"] = ds_op[i]
        srs_inputs[f"disp_psrc1_{i}"] = ds_psrc1[i]
        srs_inputs[f"disp_rdy1_{i}"] = ds_rdy1[i]
        srs_inputs[f"disp_data1_{i}"] = cas(
            domain, m.const(0, width=SCALAR_DATA_W), cycle=0
        )
        srs_inputs[f"disp_psrc2_{i}"] = ds_psrc2[i]
        srs_inputs[f"disp_rdy2_{i}"] = ds_rdy2[i]
        srs_inputs[f"disp_data2_{i}"] = cas(
            domain, m.const(0, width=SCALAR_DATA_W), cycle=0
        )
        srs_inputs[f"disp_pdst{i}"] = ds_pdst[i]
        srs_inputs[f"disp_ckpt{i}"] = cas(
            domain, m.const(0, width=CHECKPOINT_W), cycle=0
        )
    for c in range(CDB_PORTS):
        srs_inputs[f"cdb_valid{c}"] = zero1
        srs_inputs[f"cdb_tag{c}"] = cas(domain, m.const(0, width=PHYS_GREG_W), cycle=0)
        srs_inputs[f"cdb_data{c}"] = cas(
            domain, m.const(0, width=SCALAR_DATA_W), cycle=0
        )

    srs_out = domain.call(
        scalar_rs,
        inputs=srs_inputs,
        n_entries=SCALAR_RS_ENTRIES,
        n_dispatch=W,
        n_issue=SCALAR_ISSUE_WIDTH,
        n_cdb=CDB_PORTS,
        prefix=f"{prefix}_srs",
    )

    srs_issue_valid = srs_out["issue_valid"]
    srs_issue_op = srs_out["issue_op"]
    srs_issue_data1 = srs_out["issue_data1"]
    srs_issue_data2 = srs_out["issue_data2"]
    srs_issue_pdst = srs_out["issue_pdst"]
    srs_issue_ckpt = srs_out["issue_ckpt"]

    # ═══════════════════════════════════════════════════════════════════
    #  6. ALU (×1 for demonstration; full design has 4)
    # ═══════════════════════════════════════════════════════════════════
    alu_out = domain.call(
        alu,
        inputs={
            "valid": srs_issue_valid,
            "func": srs_issue_op,
            "src1": srs_issue_data1,
            "src2": srs_issue_data2,
            "pdst": srs_issue_pdst,
        },
        data_w=SCALAR_DATA_W,
        prefix=f"{prefix}_alu0",
    )

    # ═══════════════════════════════════════════════════════════════════
    #  7. MUL/DIV
    # ═══════════════════════════════════════════════════════════════════
    md_out = domain.call(
        muldiv,
        inputs={
            "valid": srs_issue_valid,
            "func": srs_issue_op,
            "src1": srs_issue_data1,
            "src2": srs_issue_data2,
            "pdst": srs_issue_pdst,
        },
        data_w=SCALAR_DATA_W,
        prefix=f"{prefix}_md",
    )

    # ═══════════════════════════════════════════════════════════════════
    #  8. BRU
    # ═══════════════════════════════════════════════════════════════════
    bru_out = domain.call(
        bru,
        inputs={
            "valid": srs_issue_valid,
            "func": srs_issue_op,
            "src1": srs_issue_data1,
            "src2": srs_issue_data2,
            "predicted": zero1,
            "pc": fe_pc,
            "offset": cas(domain, m.const(0, width=SCALAR_DATA_W), cycle=0),
            "pdst": srs_issue_pdst,
            "ckpt": srs_issue_ckpt,
        },
        data_w=SCALAR_DATA_W,
        addr_w=ADDR_W,
        prefix=f"{prefix}_bru",
    )

    # ═══════════════════════════════════════════════════════════════════
    #  9. LSU RS + LSU
    # ═══════════════════════════════════════════════════════════════════
    lrs_inputs = {"flush": bru_redirect}
    for i in range(W):
        lrs_inputs[f"dv{i}"] = ds_to_lrs[i]
        lrs_inputs[f"dop{i}"] = ds_op[i]
        lrs_inputs[f"dps1_{i}"] = ds_psrc1[i]
        lrs_inputs[f"dr1_{i}"] = ds_rdy1[i]
        lrs_inputs[f"dps2_{i}"] = ds_psrc2[i]
        lrs_inputs[f"dr2_{i}"] = ds_rdy2[i]
        lrs_inputs[f"dpd{i}"] = ds_pdst[i]
        lrs_inputs[f"dst{i}"] = zero1
    for c in range(CDB_PORTS):
        lrs_inputs[f"cdb_v{c}"] = zero1
        lrs_inputs[f"cdb_t{c}"] = cas(domain, m.const(0, width=PHYS_GREG_W), cycle=0)

    lrs_out = domain.call(
        lsu_rs,
        inputs=lrs_inputs,
        n_entries=LSU_RS_ENTRIES,
        n_dispatch=W,
        n_cdb=CDB_PORTS,
        prefix=f"{prefix}_lrs",
    )

    lrs_ld_valid = lrs_out["ld_issue_valid"]
    lrs_ld_pdst = lrs_out["ld_issue_pdst"]
    lrs_st_valid = lrs_out["st_issue_valid"]

    lsu_out = domain.call(
        lsu,
        inputs={
            "ld_valid": lrs_ld_valid,
            "ld_addr": cas(domain, m.const(0, width=ADDR_W), cycle=0),
            "ld_pdst": lrs_ld_pdst,
            "st_valid": lrs_st_valid,
            "st_addr": cas(domain, m.const(0, width=ADDR_W), cycle=0),
            "st_data": cas(domain, m.const(0, width=SCALAR_DATA_W), cycle=0),
        },
        data_w=SCALAR_DATA_W,
        addr_w=ADDR_W,
        prefix=f"{prefix}_lsu",
    )

    # ═══════════════════════════════════════════════════════════════════
    #  10. Vec RS (tile-domain)
    # ═══════════════════════════════════════════════════════════════════
    vrs_inputs = {"flush": bru_redirect}
    for i in range(W):
        vrs_inputs[f"dv{i}"] = ds_to_vrs[i]
        vrs_inputs[f"dop{i}"] = ds_op[i]
        for s in range(2):
            vrs_inputs[f"dts{s}_{i}"] = ds_ptsrc[i]
            vrs_inputs[f"dtr{s}_{i}"] = ds_trdy[i]
        vrs_inputs[f"dtd{i}"] = ds_ptdst[i]
    for t in range(TCB_PORTS):
        vrs_inputs[f"tcb_v{t}"] = zero1
        vrs_inputs[f"tcb_t{t}"] = cas(domain, m.const(0, width=PHYS_TREG_W), cycle=0)

    vrs_out = domain.call(
        vec_rs,
        inputs=vrs_inputs,
        n_entries=VEC_RS_ENTRIES,
        n_dispatch=W,
        n_tcb=TCB_PORTS,
        n_tile_src=2,
        prefix=f"{prefix}_vrs",
    )

    # ═══════════════════════════════════════════════════════════════════
    #  11. Cube RS (tile-domain)
    # ═══════════════════════════════════════════════════════════════════
    crs_inputs = {"flush": bru_redirect}
    for i in range(W):
        crs_inputs[f"dv{i}"] = ds_to_crs[i]
        crs_inputs[f"dop{i}"] = ds_op[i]
        for s in range(2):
            crs_inputs[f"dts{s}_{i}"] = ds_ptsrc[i]
            crs_inputs[f"dtr{s}_{i}"] = ds_trdy[i]
        crs_inputs[f"dtd{i}"] = ds_ptdst[i]
    for t in range(TCB_PORTS):
        crs_inputs[f"tcb_v{t}"] = zero1
        crs_inputs[f"tcb_t{t}"] = cas(domain, m.const(0, width=PHYS_TREG_W), cycle=0)

    crs_out = domain.call(
        cube_rs,
        inputs=crs_inputs,
        n_entries=CUBE_RS_ENTRIES,
        n_dispatch=W,
        n_tcb=TCB_PORTS,
        n_tile_src=2,
        prefix=f"{prefix}_crs",
    )

    # ═══════════════════════════════════════════════════════════════════
    #  12. MTE RS (dual-bus wakeup)
    # ═══════════════════════════════════════════════════════════════════
    mrs_inputs = {"flush": bru_redirect}
    for i in range(W):
        mrs_inputs[f"dv{i}"] = ds_to_mrs[i]
        mrs_inputs[f"dop{i}"] = ds_op[i]
        mrs_inputs[f"dps{i}"] = ds_psrc1[i]
        mrs_inputs[f"dsr{i}"] = ds_rdy1[i]
        mrs_inputs[f"dts{i}"] = ds_ptsrc[i]
        mrs_inputs[f"dtr{i}"] = ds_trdy[i]
        mrs_inputs[f"dtd{i}"] = ds_ptdst[i]
        mrs_inputs[f"dpsd{i}"] = ds_pdst[i]
    for c in range(CDB_PORTS):
        mrs_inputs[f"cdb_v{c}"] = zero1
        mrs_inputs[f"cdb_t{c}"] = cas(domain, m.const(0, width=PHYS_GREG_W), cycle=0)
    for t in range(TCB_PORTS):
        mrs_inputs[f"tcb_v{t}"] = zero1
        mrs_inputs[f"tcb_t{t}"] = cas(domain, m.const(0, width=PHYS_TREG_W), cycle=0)

    mrs_out = domain.call(
        mte_rs,
        inputs=mrs_inputs,
        n_entries=MTE_RS_ENTRIES,
        n_dispatch=W,
        n_cdb=CDB_PORTS,
        n_tcb=TCB_PORTS,
        prefix=f"{prefix}_mrs",
    )

    # ═══════════════════════════════════════════════════════════════════
    #  Collect outputs
    # ═══════════════════════════════════════════════════════════════════
    outs: dict = {
        "pc": fe_pc,
        "fetch_valid": fe_valid,
        "dec_valid": dec_out_valid,
        "dec_domain": dec_domain,
        "ren_psrc1": ren_psrc1,
        "ren_psrc2": ren_psrc2,
        "ren_pdst": ren_pdst,
        "ds_stall": ds_out["stall"],
        "srs_issue_valid": srs_issue_valid,
        "srs_full": srs_out["full"],
        "alu0_result_valid": alu_out["result_valid"],
        "alu0_result_tag": alu_out["result_tag"],
        "alu0_result_data": alu_out["result_data"],
        "md_busy": md_out["busy"],
        "bru_mispredict": bru_out["mispredict"],
        "lsu_ld_result_valid": lsu_out["ld_result_valid"],
        "vrs_issue_valid": vrs_out["issue_valid"],
        "crs_issue_valid": crs_out["issue_valid"],
        "mrs_issue_valid": mrs_out["issue_valid"],
    }

    # ═══════════════════════════════════════════════════════════════════
    #  Standalone m.output() — only when not composed into a parent
    # ═══════════════════════════════════════════════════════════════════
    if inputs is None:
        m.output(f"{prefix}_pc", wire_of(outs["pc"]))
        m.output(f"{prefix}_fetch_valid", wire_of(outs["fetch_valid"]))

        for i in range(W):
            m.output(f"{prefix}_dec_valid_{i}", wire_of(outs["dec_valid"][i]))
            m.output(f"{prefix}_dec_domain_{i}", wire_of(outs["dec_domain"][i]))
            m.output(f"{prefix}_ren_psrc1_{i}", wire_of(outs["ren_psrc1"][i]))
            m.output(f"{prefix}_ren_psrc2_{i}", wire_of(outs["ren_psrc2"][i]))
            m.output(f"{prefix}_ren_pdst_{i}", wire_of(outs["ren_pdst"][i]))

        m.output(f"{prefix}_ds_stall", wire_of(outs["ds_stall"]))
        m.output(f"{prefix}_srs_issue_valid", wire_of(outs["srs_issue_valid"]))
        m.output(f"{prefix}_srs_full", wire_of(outs["srs_full"]))
        m.output(f"{prefix}_alu0_result_valid", wire_of(outs["alu0_result_valid"]))
        m.output(f"{prefix}_alu0_result_tag", wire_of(outs["alu0_result_tag"]))
        m.output(f"{prefix}_alu0_result_data", wire_of(outs["alu0_result_data"]))
        m.output(f"{prefix}_md_busy", wire_of(outs["md_busy"]))
        m.output(f"{prefix}_bru_mispredict", wire_of(outs["bru_mispredict"]))
        m.output(f"{prefix}_lsu_ld_result_valid", wire_of(outs["lsu_ld_result_valid"]))
        m.output(f"{prefix}_vrs_issue_valid", wire_of(outs["vrs_issue_valid"]))
        m.output(f"{prefix}_crs_issue_valid", wire_of(outs["crs_issue_valid"]))
        m.output(f"{prefix}_mrs_issue_valid", wire_of(outs["mrs_issue_valid"]))

    return outs


davinci_top.__pycircuit_name__ = "davinci_top"


if __name__ == "__main__":
    import sys

    hier = "--hierarchical" in sys.argv or "-H" in sys.argv

    circ = compile_cycle_aware(
        davinci_top,
        eager=True,
        name="davinci_top",
        hierarchical=hier,
    )
    mlir = circ.emit_mlir()
    out_file = "davinci_top.mlir"
    with open(out_file, "w") as f:
        f.write(mlir)
