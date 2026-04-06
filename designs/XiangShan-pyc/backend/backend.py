"""Backend — Backend Top-Level Integration for XiangShan-pyc.

Wires the major backend sub-blocks:
  CtrlBlock (Rename → Dispatch → ROB) + Scheduler/IssueQueues
  + Execution Units (ALU, MUL, BRU, FPU) + RegFile + Writeback

This is a simplified pass-through integration module that connects the
key interfaces between sub-blocks without instantiating full sub-modules
(those are separately compiled and tested).

Reference: XiangShan/src/main/scala/xiangshan/backend/Backend.scala

Ports:
  - decoded_uops_in: from Frontend (decode stage)
  - writeback → ROB: execution results
  - commit_out: ROB retire to arch state / rename freelist
  - redirect_out: to Frontend for PC redirect
  - mem_dispatch_out: dispatched memory uops to MemBlock

Key features:
  B-BE-001  CtrlBlock integration: stall / redirect propagation
  B-BE-002  Integer execution: ALU + MUL + DIV + BRU writeback
  B-BE-003  FP execution: FPU writeback
  B-BE-004  Commit path: ROB retire → rename freelist / arch update
  B-BE-005  Redirect to frontend on mispredict / exception
  B-BE-006  Memory dispatch output for MemBlock
"""
from __future__ import annotations

import sys
from pathlib import Path

_XS_ROOT = Path(__file__).resolve().parent.parent
if str(_XS_ROOT) not in sys.path:
    sys.path.insert(0, str(_XS_ROOT))

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    CycleAwareSignal,
    cas,
    compile_cycle_aware,
    mux,
    u,
)

from top.parameters import (
    COMMIT_WIDTH,
    DECODE_WIDTH,
    PC_WIDTH,
    PTAG_WIDTH_INT,
    RENAME_WIDTH,
    ROB_IDX_WIDTH,
    XLEN,
)

FU_TYPE_WIDTH = 3
FU_ALU = 0
FU_MUL = 1
FU_DIV = 2
FU_BRU = 3
FU_FPU = 4
FU_LDU = 6
FU_STU = 7

NUM_WB_PORTS = 4
NUM_INT_EXU = 2
NUM_FP_EXU = 1

from backend.ctrlblock.ctrlblock import build_ctrlblock
from backend.issue.issue_queue import build_issue_queue
from backend.regfile.regfile import build_regfile
from backend.exu.alu import build_alu
from backend.exu.bru import build_bru
from backend.exu.mul import build_mul
from backend.exu.div import build_div
from backend.fu.fpu import build_fpu


def build_backend(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    prefix: str = "be",
    decode_width: int = DECODE_WIDTH,
    commit_width: int = COMMIT_WIDTH,
    num_wb: int = NUM_WB_PORTS,
    num_int_exu: int = NUM_INT_EXU,
    num_fp_exu: int = NUM_FP_EXU,
    ptag_w: int = PTAG_WIDTH_INT,
    data_width: int = XLEN,
    pc_width: int = PC_WIDTH,
    rob_idx_w: int = ROB_IDX_WIDTH,
    fu_type_w: int = FU_TYPE_WIDTH,
    inputs: dict[str, CycleAwareSignal] | None = None,
) -> dict[str, CycleAwareSignal]:
    """Backend: top-level integration wiring CtrlBlock + ExeUnits + RegFile."""
    _in = inputs or {}
    _out: dict[str, CycleAwareSignal] = {}

    # ── Sub-module calls ──
    domain.push()
    ctrl_out = build_ctrlblock(m, domain, prefix=f"{prefix}_s_ctrl",
                               decode_width=decode_width, commit_width=commit_width,
                               ptag_w=ptag_w, pc_width=pc_width, rob_idx_w=rob_idx_w,
                               inputs={})
    domain.pop()

    domain.push()
    iq_out = build_issue_queue(m, domain, prefix=f"{prefix}_s_iq",
                               ptag_w=ptag_w, rob_idx_w=rob_idx_w,
                               inputs={})
    domain.pop()

    domain.push()
    rf_out = build_regfile(m, domain, prefix=f"{prefix}_s_rf",
                           data_width=data_width,
                           inputs={})
    domain.pop()

    for _i in range(num_int_exu):
        domain.push()
        build_alu(m, domain, prefix=f"{prefix}_s_alu{_i}",
                  data_width=data_width, inputs={})
        domain.pop()

    domain.push()
    bru_out = build_bru(m, domain, prefix=f"{prefix}_s_bru",
                        data_width=data_width, pc_width=pc_width,
                        inputs={})
    domain.pop()

    domain.push()
    mul_out = build_mul(m, domain, prefix=f"{prefix}_s_mul",
                        data_width=data_width, inputs={})
    domain.pop()

    domain.push()
    div_out = build_div(m, domain, prefix=f"{prefix}_s_div",
                        data_width=data_width, inputs={})
    domain.pop()

    for _i in range(num_fp_exu):
        domain.push()
        build_fpu(m, domain, prefix=f"{prefix}_s_fpu{_i}",
                  data_width=data_width, inputs={})
        domain.pop()


    # ================================================================
    # Cycle 0 — Inputs from Frontend and MemBlock
    # ================================================================

    # Decoded uops from Frontend
    in_valid = [cas(domain, m.input(f"{prefix}_dec_valid_{i}", width=1), cycle=0)
                for i in range(decode_width)]
    in_pc = [cas(domain, m.input(f"{prefix}_dec_pc_{i}", width=pc_width), cycle=0)
             for i in range(decode_width)]
    in_fu_type = [cas(domain, m.input(f"{prefix}_dec_fu_type_{i}", width=fu_type_w), cycle=0)
                  for i in range(decode_width)]
    in_pdest = [cas(domain, m.input(f"{prefix}_dec_pdest_{i}", width=ptag_w), cycle=0)
                for i in range(decode_width)]
    in_psrc1 = [cas(domain, m.input(f"{prefix}_dec_psrc1_{i}", width=ptag_w), cycle=0)
                for i in range(decode_width)]
    in_psrc2 = [cas(domain, m.input(f"{prefix}_dec_psrc2_{i}", width=ptag_w), cycle=0)
                for i in range(decode_width)]
    in_old_pdest = [cas(domain, m.input(f"{prefix}_dec_old_pdest_{i}", width=ptag_w), cycle=0)
                    for i in range(decode_width)]

    # Issue queue backpressure
    iq_int_ready = (_in["iq_int_ready"] if "iq_int_ready" in _in else
        cas(domain, m.input(f"{prefix}_iq_int_ready", width=1), cycle=0))
    iq_fp_ready = (_in["iq_fp_ready"] if "iq_fp_ready" in _in else
        cas(domain, m.input(f"{prefix}_iq_fp_ready", width=1), cycle=0))
    iq_mem_ready = (_in["iq_mem_ready"] if "iq_mem_ready" in _in else
        cas(domain, m.input(f"{prefix}_iq_mem_ready", width=1), cycle=0))

    # Writeback from execution units (int + fp)
    wb_valid = [cas(domain, m.input(f"{prefix}_wb_valid_{i}", width=1), cycle=0)
                for i in range(num_wb)]
    wb_pdest = [cas(domain, m.input(f"{prefix}_wb_pdest_{i}", width=ptag_w), cycle=0)
                for i in range(num_wb)]
    wb_data = [cas(domain, m.input(f"{prefix}_wb_data_{i}", width=data_width), cycle=0)
               for i in range(num_wb)]
    wb_rob_idx = [cas(domain, m.input(f"{prefix}_wb_rob_idx_{i}", width=rob_idx_w), cycle=0)
                  for i in range(num_wb)]

    # Branch redirect from BRU
    bru_redirect_valid = (_in["bru_redirect_valid"] if "bru_redirect_valid" in _in else
        cas(domain, m.input(f"{prefix}_bru_redirect_valid", width=1), cycle=0))
    bru_redirect_target = (_in["bru_redirect_target"] if "bru_redirect_target" in _in else
        cas(domain, m.input(f"{prefix}_bru_redirect_target", width=pc_width), cycle=0))

    # ROB exception
    rob_exception_valid = (_in["rob_exception_valid"] if "rob_exception_valid" in _in else
        cas(domain, m.input(f"{prefix}_rob_exception_valid", width=1), cycle=0))
    rob_exception_pc = (_in["rob_exception_pc"] if "rob_exception_pc" in _in else
        cas(domain, m.input(f"{prefix}_rob_exception_pc", width=pc_width), cycle=0))

    # ── Constants ────────────────────────────────────────────────
    ZERO_1 = cas(domain, m.const(0, width=1), cycle=0)
    ONE_1 = cas(domain, m.const(1, width=1), cycle=0)
    ZERO_PC = cas(domain, m.const(0, width=pc_width), cycle=0)

    # ================================================================
    # Redirect: priority ROB exception > BRU misprediction
    # ================================================================
    redirect_valid = bru_redirect_valid | rob_exception_valid
    redirect_target = mux(rob_exception_valid, rob_exception_pc, bru_redirect_target)
    redirect_flush = rob_exception_valid

    m.output(f"{prefix}_redirect_valid", redirect_valid.wire)
    _out["redirect_valid"] = redirect_valid
    m.output(f"{prefix}_redirect_target", redirect_target.wire)
    _out["redirect_target"] = redirect_target
    m.output(f"{prefix}_redirect_flush", redirect_flush.wire)
    _out["redirect_flush"] = redirect_flush

    # ================================================================
    # Dispatch classification (FU type → IQ class)
    # ================================================================
    FU_FPU_C = cas(domain, m.const(FU_FPU, width=fu_type_w), cycle=0)
    FU_LDU_C = cas(domain, m.const(FU_LDU, width=fu_type_w), cycle=0)
    FU_STU_C = cas(domain, m.const(FU_STU, width=fu_type_w), cycle=0)

    # Backpressure: stall if any IQ is not ready for its class
    any_blocked = ZERO_1
    for i in range(decode_width):
        is_fp = in_fu_type[i] == FU_FPU_C
        is_mem = (in_fu_type[i] == FU_LDU_C) | (in_fu_type[i] == FU_STU_C)
        is_int = (~is_fp) & (~is_mem)

        slot_ready = mux(is_int, iq_int_ready, ZERO_1)
        slot_ready = mux(is_fp, iq_fp_ready, slot_ready)
        slot_ready = mux(is_mem, iq_mem_ready, slot_ready)

        blocked = in_valid[i] & (~slot_ready)
        any_blocked = any_blocked | blocked

    dispatch_stall = any_blocked | redirect_valid
    pipeline_stall = dispatch_stall

    m.output(f"{prefix}_stall_to_frontend", pipeline_stall.wire)
    _out["stall_to_frontend"] = pipeline_stall

    # ================================================================
    # Dispatch outputs: gated by stall/flush
    # ================================================================
    dp_cnt_w = max(1, decode_width.bit_length())

    for i in range(decode_width):
        is_fp = in_fu_type[i] == FU_FPU_C
        is_mem = (in_fu_type[i] == FU_LDU_C) | (in_fu_type[i] == FU_STU_C)
        is_int = (~is_fp) & (~is_mem)

        slot_valid = in_valid[i] & (~pipeline_stall)

        m.output(f"{prefix}_iq_int_valid_{i}", (slot_valid & is_int).wire)
        m.output(f"{prefix}_iq_fp_valid_{i}", (slot_valid & is_fp).wire)
        m.output(f"{prefix}_iq_mem_valid_{i}", (slot_valid & is_mem).wire)

        m.output(f"{prefix}_dp_pdest_{i}", in_pdest[i].wire)
        m.output(f"{prefix}_dp_psrc1_{i}", in_psrc1[i].wire)
        m.output(f"{prefix}_dp_psrc2_{i}", in_psrc2[i].wire)
        m.output(f"{prefix}_dp_fu_type_{i}", in_fu_type[i].wire)
        m.output(f"{prefix}_dp_pc_{i}", in_pc[i].wire)

    # ================================================================
    # Writeback → ROB (forwarding)
    # ================================================================
    for i in range(num_wb):
        m.output(f"{prefix}_rob_wb_valid_{i}", wb_valid[i].wire)
        m.output(f"{prefix}_rob_wb_pdest_{i}", wb_pdest[i].wire)
        m.output(f"{prefix}_rob_wb_rob_idx_{i}", wb_rob_idx[i].wire)

    # ================================================================
    # Commit: ROB retire state tracking (simplified counter)
    # ================================================================
    cur_cm = domain.signal(width=dp_cnt_w, reset_value=0, name=f"{prefix}_be_cm_cnt")

    # Count incoming writeback as proxy for commit readiness
    wb_cnt = cas(domain, m.const(0, width=dp_cnt_w), cycle=0)
    ONE_DP = cas(domain, m.const(1, width=dp_cnt_w), cycle=0)
    for i in range(num_wb):
        wb_cnt = mux(wb_valid[i],
                     cas(domain, (wb_cnt.wire + ONE_DP.wire)[0:dp_cnt_w], cycle=0),
                     wb_cnt)

    m.output(f"{prefix}_wb_count", wb_cnt.wire)
    _out["wb_count"] = wb_cnt

    # Commit outputs (pass-through placeholder — real commit comes from ROB)
    for i in range(min(commit_width, num_wb)):
        m.output(f"{prefix}_commit_valid_{i}", wb_valid[i].wire)
        m.output(f"{prefix}_commit_pdest_{i}", wb_pdest[i].wire)

    # Memory dispatch: pass memory-class uops out to MemBlock
    for i in range(decode_width):
        is_mem = (in_fu_type[i] == FU_LDU_C) | (in_fu_type[i] == FU_STU_C)
        slot_valid = in_valid[i] & (~pipeline_stall) & is_mem
        m.output(f"{prefix}_mem_dp_valid_{i}", slot_valid.wire)
        m.output(f"{prefix}_mem_dp_pdest_{i}", in_pdest[i].wire)
        m.output(f"{prefix}_mem_dp_psrc1_{i}", in_psrc1[i].wire)
        m.output(f"{prefix}_mem_dp_psrc2_{i}", in_psrc2[i].wire)
        m.output(f"{prefix}_mem_dp_fu_type_{i}", in_fu_type[i].wire)

    # ================================================================
    # Cycle 1: pipeline registers + commit counter update
    # ================================================================
    domain.next()

    for i in range(decode_width):
        slot_valid = in_valid[i] & (~pipeline_stall)
        domain.cycle(slot_valid.wire, name=f"{prefix}_be_v_{i}")
        domain.cycle(in_pdest[i].wire, name=f"{prefix}_be_pd_{i}")
        domain.cycle(in_fu_type[i].wire, name=f"{prefix}_be_fu_{i}")

    # Commit counter: saturate at max
    MAX_CM = cas(domain, m.const((1 << dp_cnt_w) - 1, width=dp_cnt_w), cycle=0)
    new_cm = mux(cur_cm == MAX_CM, cur_cm,
                 cas(domain, (cur_cm.wire + wb_cnt.wire)[0:dp_cnt_w], cycle=0))
    cur_cm <<= mux(redirect_flush,
                    cas(domain, m.const(0, width=dp_cnt_w), cycle=0),
                    new_cm)
    return _out


build_backend.__pycircuit_name__ = "backend"


if __name__ == "__main__":
    print(compile_cycle_aware(
        build_backend, name="backend", eager=True,
        decode_width=2, commit_width=2, num_wb=2,
        num_int_exu=1, num_fp_exu=1,
        ptag_w=4, data_width=16, pc_width=16,
        rob_idx_w=4, fu_type_w=3,
    ).emit_mlir())
