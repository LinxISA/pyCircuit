"""Dispatch — Dispatch Unit for XiangShan-pyc backend.

Takes renamed micro-ops and routes them to the appropriate issue queues
(Integer, Floating-point, Memory) based on functional unit type.  Applies
backpressure when a target issue queue is full.

Reference: XiangShan/src/main/scala/xiangshan/backend/dispatch/

Pipeline:
  Cycle 0 — Receive renamed uops, classify by fu_type, check IQ availability
  Cycle 1 — Write accepted uops into target issue queues, emit ROB enqueue

Key features:
  B-DP-001  FU-type based routing: int / fp / mem issue queues
  B-DP-002  Backpressure: stall rename if any target IQ is full
  B-DP-003  Per-slot dispatch valid: slot fires only if its target IQ has room
  B-DP-004  ROB enqueue output for each dispatched uop
  B-DP-005  Flush on redirect: cancel in-flight dispatches
"""

from __future__ import annotations

import sys
from pathlib import Path

_XS_ROOT = Path(__file__).resolve().parent.parent.parent
if str(_XS_ROOT) not in sys.path:
    sys.path.insert(0, str(_XS_ROOT))

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    CycleAwareSignal,
    cas,
    mux,
    wire_of,
)
from top.parameters import (
    PC_WIDTH,
    PTAG_WIDTH_INT,
    RENAME_WIDTH,
    ROB_IDX_WIDTH,
)

# FU type encoding (3 bits)
FU_TYPE_WIDTH = 3
FU_ALU = 0  # Integer ALU
FU_MUL = 1  # Integer multiply
FU_DIV = 2  # Integer divide
FU_BRU = 3  # Branch unit
FU_FPU = 4  # Floating-point
FU_FMISC = 5  # FP misc (fmv, fcvt)
FU_LDU = 6  # Load unit
FU_STU = 7  # Store unit

# IQ class encoding (2 bits)
IQ_CLASS_WIDTH = 2
IQ_INT = 0
IQ_FP = 1
IQ_MEM = 2


def dispatch(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    prefix: str = "dp",
    dispatch_width: int = RENAME_WIDTH,
    fu_type_width: int = FU_TYPE_WIDTH,
    ptag_w: int = PTAG_WIDTH_INT,
    pc_width: int = PC_WIDTH,
    rob_idx_w: int = ROB_IDX_WIDTH,
    inputs: dict[str, CycleAwareSignal] | None = None,
) -> dict[str, CycleAwareSignal]:
    """Dispatch: route renamed uops to int / fp / mem issue queues."""
    _in = inputs or {}
    _out: dict[str, CycleAwareSignal] = {}

    dp_cnt_w = max(1, dispatch_width.bit_length())

    # ================================================================
    # Cycle 0 — Receive renamed uops, classify, check availability
    # ================================================================

    flush = (
        _in["flush"]
        if "flush" in _in
        else cas(domain, m.input(f"{prefix}_flush", width=1), cycle=0)
    )

    # Renamed uop inputs (from rename stage)
    in_valid = [
        cas(domain, m.input(f"{prefix}_in_valid_{i}", width=1), cycle=0)
        for i in range(dispatch_width)
    ]
    in_pdest = [
        cas(domain, m.input(f"{prefix}_in_pdest_{i}", width=ptag_w), cycle=0)
        for i in range(dispatch_width)
    ]
    in_psrc1 = [
        cas(domain, m.input(f"{prefix}_in_psrc1_{i}", width=ptag_w), cycle=0)
        for i in range(dispatch_width)
    ]
    in_psrc2 = [
        cas(domain, m.input(f"{prefix}_in_psrc2_{i}", width=ptag_w), cycle=0)
        for i in range(dispatch_width)
    ]
    in_old_pdest = [
        cas(domain, m.input(f"{prefix}_in_old_pdest_{i}", width=ptag_w), cycle=0)
        for i in range(dispatch_width)
    ]
    in_fu_type = [
        cas(domain, m.input(f"{prefix}_in_fu_type_{i}", width=fu_type_width), cycle=0)
        for i in range(dispatch_width)
    ]
    in_rob_idx = [
        cas(domain, m.input(f"{prefix}_in_rob_idx_{i}", width=rob_idx_w), cycle=0)
        for i in range(dispatch_width)
    ]
    in_pc = [
        cas(domain, m.input(f"{prefix}_in_pc_{i}", width=pc_width), cycle=0)
        for i in range(dispatch_width)
    ]

    # Issue queue ready signals (backpressure)
    iq_int_ready = (
        _in["iq_int_ready"]
        if "iq_int_ready" in _in
        else cas(domain, m.input(f"{prefix}_iq_int_ready", width=1), cycle=0)
    )
    iq_fp_ready = (
        _in["iq_fp_ready"]
        if "iq_fp_ready" in _in
        else cas(domain, m.input(f"{prefix}_iq_fp_ready", width=1), cycle=0)
    )
    iq_mem_ready = (
        _in["iq_mem_ready"]
        if "iq_mem_ready" in _in
        else cas(domain, m.input(f"{prefix}_iq_mem_ready", width=1), cycle=0)
    )

    # ── Constants ────────────────────────────────────────────────
    ZERO_1 = cas(domain, m.const(0, width=1), cycle=0)
    cas(domain, m.const(1, width=1), cycle=0)

    FU_FPU_C = cas(domain, m.const(FU_FPU, width=fu_type_width), cycle=0)
    FU_FMISC_C = cas(domain, m.const(FU_FMISC, width=fu_type_width), cycle=0)
    FU_LDU_C = cas(domain, m.const(FU_LDU, width=fu_type_width), cycle=0)
    FU_STU_C = cas(domain, m.const(FU_STU, width=fu_type_width), cycle=0)

    # ── FU-type classification ───────────────────────────────────
    is_fp = []
    is_mem = []
    is_int = []

    for i in range(dispatch_width):
        fp = (in_fu_type[i] == FU_FPU_C) | (in_fu_type[i] == FU_FMISC_C)
        mem = (in_fu_type[i] == FU_LDU_C) | (in_fu_type[i] == FU_STU_C)
        intg = (~fp) & (~mem)
        is_fp.append(fp)
        is_mem.append(mem)
        is_int.append(intg)

    # ── Check target IQ availability per slot ────────────────────
    slot_iq_ready = []
    for i in range(dispatch_width):
        ready = mux(is_int[i], iq_int_ready, ZERO_1)
        ready = mux(is_fp[i], iq_fp_ready, ready)
        ready = mux(is_mem[i], iq_mem_ready, ready)
        slot_iq_ready.append(ready)

    # ── Dispatch fire: all valid slots must have target IQ ready ──
    # Any blocked slot stalls the entire dispatch group
    any_blocked = ZERO_1
    for i in range(dispatch_width):
        blocked = in_valid[i] & (~slot_iq_ready[i])
        any_blocked = any_blocked | blocked

    dispatch_fire = (~any_blocked) & (~flush)

    m.output(f"{prefix}_stall", wire_of(any_blocked))
    _out["stall"] = any_blocked

    # ── Per-slot dispatch outputs ────────────────────────────────
    for i in range(dispatch_width):
        slot_fire = in_valid[i] & dispatch_fire

        int_fire = slot_fire & is_int[i]
        fp_fire = slot_fire & is_fp[i]
        mem_fire = slot_fire & is_mem[i]

        m.output(f"{prefix}_iq_int_valid_{i}", wire_of(int_fire))
        _out[f"iq_int_valid_{i}"] = int_fire
        m.output(f"{prefix}_iq_fp_valid_{i}", wire_of(fp_fire))
        _out[f"iq_fp_valid_{i}"] = fp_fire
        m.output(f"{prefix}_iq_mem_valid_{i}", wire_of(mem_fire))
        _out[f"iq_mem_valid_{i}"] = mem_fire

        m.output(f"{prefix}_out_pdest_{i}", wire_of(in_pdest[i]))
        _out[f"out_pdest_{i}"] = in_pdest[i]
        m.output(f"{prefix}_out_psrc1_{i}", wire_of(in_psrc1[i]))
        _out[f"out_psrc1_{i}"] = in_psrc1[i]
        m.output(f"{prefix}_out_psrc2_{i}", wire_of(in_psrc2[i]))
        _out[f"out_psrc2_{i}"] = in_psrc2[i]
        m.output(f"{prefix}_out_fu_type_{i}", wire_of(in_fu_type[i]))
        _out[f"out_fu_type_{i}"] = in_fu_type[i]
        m.output(f"{prefix}_out_rob_idx_{i}", wire_of(in_rob_idx[i]))
        _out[f"out_rob_idx_{i}"] = in_rob_idx[i]
        m.output(f"{prefix}_out_pc_{i}", wire_of(in_pc[i]))
        _out[f"out_pc_{i}"] = in_pc[i]

    # ── ROB enqueue outputs (matching rename interface) ──────────
    for i in range(dispatch_width):
        slot_fire = in_valid[i] & dispatch_fire
        m.output(f"{prefix}_rob_enq_valid_{i}", wire_of(slot_fire))
        _out[f"rob_enq_valid_{i}"] = slot_fire
        m.output(f"{prefix}_rob_enq_pdest_{i}", wire_of(in_pdest[i]))
        _out[f"rob_enq_pdest_{i}"] = in_pdest[i]
        m.output(f"{prefix}_rob_enq_old_pdest_{i}", wire_of(in_old_pdest[i]))
        _out[f"rob_enq_old_pdest_{i}"] = in_old_pdest[i]
        m.output(f"{prefix}_rob_enq_pc_{i}", wire_of(in_pc[i]))
        _out[f"rob_enq_pc_{i}"] = in_pc[i]

    # ── Count dispatched uops per IQ class ───────────────────────
    int_cnt = cas(domain, m.const(0, width=dp_cnt_w), cycle=0)
    fp_cnt = cas(domain, m.const(0, width=dp_cnt_w), cycle=0)
    mem_cnt = cas(domain, m.const(0, width=dp_cnt_w), cycle=0)
    ONE_DP = cas(domain, m.const(1, width=dp_cnt_w), cycle=0)

    for i in range(dispatch_width):
        slot_fire = in_valid[i] & dispatch_fire
        int_cnt = mux(
            slot_fire & is_int[i],
            cas(domain, (wire_of(int_cnt) + wire_of(ONE_DP))[0:dp_cnt_w], cycle=0),
            int_cnt,
        )
        fp_cnt = mux(
            slot_fire & is_fp[i],
            cas(domain, (wire_of(fp_cnt) + wire_of(ONE_DP))[0:dp_cnt_w], cycle=0),
            fp_cnt,
        )
        mem_cnt = mux(
            slot_fire & is_mem[i],
            cas(domain, (wire_of(mem_cnt) + wire_of(ONE_DP))[0:dp_cnt_w], cycle=0),
            mem_cnt,
        )

    m.output(f"{prefix}_int_dispatch_count", wire_of(int_cnt))
    _out["int_dispatch_count"] = int_cnt
    m.output(f"{prefix}_fp_dispatch_count", wire_of(fp_cnt))
    _out["fp_dispatch_count"] = fp_cnt
    m.output(f"{prefix}_mem_dispatch_count", wire_of(mem_cnt))
    _out["mem_dispatch_count"] = mem_cnt

    # ── Cycle 1: pipeline register for downstream latching ───────
    domain.next()

    # Pipeline registers capturing dispatched uops for IQ write stage
    for i in range(dispatch_width):
        slot_fire = in_valid[i] & dispatch_fire
        domain.cycle(wire_of(slot_fire), name=f"{prefix}_dp1_v_{i}")
        domain.cycle(wire_of(in_pdest[i]), name=f"{prefix}_dp1_pdest_{i}")
        domain.cycle(wire_of(in_psrc1[i]), name=f"{prefix}_dp1_psrc1_{i}")
        domain.cycle(wire_of(in_psrc2[i]), name=f"{prefix}_dp1_psrc2_{i}")
        domain.cycle(wire_of(in_fu_type[i]), name=f"{prefix}_dp1_fu_{i}")
        domain.cycle(wire_of(in_rob_idx[i]), name=f"{prefix}_dp1_rob_{i}")
    return _out


dispatch.__pycircuit_name__ = "dispatch"


if __name__ == "__main__":
    pass
