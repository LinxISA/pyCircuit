from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    CycleAwareSignal,
    Tb,
    cas,
    compile_cycle_aware,
    mux,
    testbench,
    wire_of,
)

PTYPE_C = 0
PTYPE_P = 1
PTYPE_T = 2
PTYPE_U = 3

CYCLE_WB  = 0   # w3: writeback stage — oldest result, available first
CYCLE_MEM = 1   # w2: memory stage
CYCLE_EX  = 2   # w1: execute stage — newest result, available last
CYCLE_ISS = 0   # source operands: issue stage


def _select_stage(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    src_valid: CycleAwareSignal,
    src_ptag: CycleAwareSignal,
    src_ptype: CycleAwareSignal,
    lane_valid: list[CycleAwareSignal],
    lane_ptag: list[CycleAwareSignal],
    lane_ptype: list[CycleAwareSignal],
    lane_data: list[CycleAwareSignal],
    lanes: int,
    lane_w: int,
    data_w: int,
) -> tuple[CycleAwareSignal, CycleAwareSignal, CycleAwareSignal]:
    """Pick the first matching lane from one write-back stage.

    CAS-native: all parameters and return values are CycleAwareSignal.
    When src @ cycle 0 is combined with lane signals @ cycle N,
    auto-cycle-balancing delays src by N cycles (inserts DFFs).
    """
    has      = cas(domain, m.const(0, width=1), cycle=0)
    sel_lane = cas(domain, m.const(0, width=lane_w), cycle=0)
    sel_data = cas(domain, m.const(0, width=data_w), cycle=0)
    one      = cas(domain, m.const(1, width=1), cycle=0)

    for j in range(lanes):
        match = (
            src_valid
            & lane_valid[j]
            & (lane_ptag[j] == src_ptag)
            & (lane_ptype[j] == src_ptype)
        )
        take = match & (one ^ has)
        j_cas = cas(domain, m.const(j, width=lane_w), cycle=0)
        sel_lane = mux(take, j_cas, sel_lane)
        sel_data = mux(take, lane_data[j], sel_data)
        has = has | match

    return has, sel_lane, sel_data


def _resolve_src(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    src_valid: CycleAwareSignal,
    src_ptag: CycleAwareSignal,
    src_ptype: CycleAwareSignal,
    src_rf_data: CycleAwareSignal,
    w1_valid: list[CycleAwareSignal],
    w1_ptag: list[CycleAwareSignal],
    w1_ptype: list[CycleAwareSignal],
    w1_data: list[CycleAwareSignal],
    w2_valid: list[CycleAwareSignal],
    w2_ptag: list[CycleAwareSignal],
    w2_ptype: list[CycleAwareSignal],
    w2_data: list[CycleAwareSignal],
    w3_valid: list[CycleAwareSignal],
    w3_ptag: list[CycleAwareSignal],
    w3_ptype: list[CycleAwareSignal],
    w3_data: list[CycleAwareSignal],
    lanes: int,
    lane_w: int,
    data_w: int,
) -> tuple[CycleAwareSignal, CycleAwareSignal, CycleAwareSignal, CycleAwareSignal]:
    """Resolve one source across 3 pipelined write-back stages.

    Pipeline model (auto-cycle-balanced):
      Cycle 0 — w3 (WB):  data available first, lowest priority
      Cycle 1 — w2 (MEM): overrides w3
      Cycle 2 — w1 (EX):  data available last, highest priority
    Source operands @ cycle 0 (issue stage).

    The priority chain mixes CAS signals at different cycles;
    auto-cycle-balancing inserts pipeline DFFs between stages:
      w3 result @ 0  →  DFF  →  w2 mux @ 1  →  DFF  →  w1 mux @ 2
    Output @ cycle 2.
    """
    common = dict(
        src_valid=src_valid, src_ptag=src_ptag, src_ptype=src_ptype,
        lanes=lanes, lane_w=lane_w, data_w=data_w,
    )

    # ── Cycle 0: w3 (WB stage) — lowest priority ──
    # src @ cycle 0, w3 lanes @ cycle 0 → match results @ cycle 0
    has_w3, lane_w3, data_w3 = _select_stage(
        m, domain,
        lane_valid=w3_valid, lane_ptag=w3_ptag,
        lane_ptype=w3_ptype, lane_data=w3_data,
        **common,
    )

    out_data  = mux(has_w3, data_w3, src_rf_data)
    out_hit   = mux(has_w3,
                    cas(domain, m.const(1, width=1), cycle=CYCLE_WB),
                    cas(domain, m.const(0, width=1), cycle=CYCLE_WB))
    out_stage = mux(has_w3,
                    cas(domain, m.const(3, width=2), cycle=CYCLE_WB),
                    cas(domain, m.const(0, width=2), cycle=CYCLE_WB))
    out_lane  = mux(has_w3, lane_w3,
                    cas(domain, m.const(0, width=lane_w), cycle=CYCLE_WB))

    # ── Cycle 1: w2 (MEM stage) — overrides w3 ──
    # src @ cycle 0 combined with w2 lanes @ cycle 1 → match results @ cycle 1
    # out_* from w3 @ cycle 0 auto-delayed to cycle 1 via DFF
    has_w2, lane_w2, data_w2 = _select_stage(
        m, domain,
        lane_valid=w2_valid, lane_ptag=w2_ptag,
        lane_ptype=w2_ptype, lane_data=w2_data,
        **common,
    )

    out_data  = mux(has_w2, data_w2, out_data)
    out_hit   = mux(has_w2, cas(domain, m.const(1, width=1), cycle=CYCLE_MEM), out_hit)
    out_stage = mux(has_w2, cas(domain, m.const(2, width=2), cycle=CYCLE_MEM), out_stage)
    out_lane  = mux(has_w2, lane_w2, out_lane)

    # ── Cycle 2: w1 (EX stage) — highest priority, overrides w2 ──
    # src @ cycle 0 combined with w1 lanes @ cycle 2 → match results @ cycle 2
    # out_* from w2 mux @ cycle 1 auto-delayed to cycle 2 via DFF
    has_w1, lane_w1, data_w1 = _select_stage(
        m, domain,
        lane_valid=w1_valid, lane_ptag=w1_ptag,
        lane_ptype=w1_ptype, lane_data=w1_data,
        **common,
    )

    out_data  = mux(has_w1, data_w1, out_data)
    out_hit   = mux(has_w1, cas(domain, m.const(1, width=1), cycle=CYCLE_EX), out_hit)
    out_stage = mux(has_w1, cas(domain, m.const(1, width=2), cycle=CYCLE_EX), out_stage)
    out_lane  = mux(has_w1, lane_w1, out_lane)

    return out_data, out_hit, out_stage, out_lane


def bypass_unit(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    prefix: str = "bp",
    lanes: int = 8,
    data_width: int = 64,
    ptag_count: int = 256,
    ptype_count: int = 4,
    inputs: dict[str, CycleAwareSignal] | None = None,
) -> dict[str, CycleAwareSignal]:
    """Pipelined bypass / forwarding network with cycle-aware pipeline stages.

    Each write-back stage is annotated at its pipeline cycle:
      w3 @ cycle 0 (WB)  — oldest result, lowest priority
      w2 @ cycle 1 (MEM)
      w1 @ cycle 2 (EX)  — newest result, highest priority
    Source operands @ cycle 0 (issue stage).

    Auto-cycle-balancing inserts DFF pipeline registers when signals from
    different cycles are combined in the priority mux chain:
      w3 result@0 → DFF → w2 mux@1 → DFF → w1 mux@2 → output@2
    """
    _in = inputs or {}
    _out: dict[str, CycleAwareSignal] = {}

    if lanes <= 0:
        raise ValueError("bypass_unit lanes must be > 0")
    if data_width <= 0:
        raise ValueError("bypass_unit data_width must be > 0")
    if ptag_count <= 0:
        raise ValueError("bypass_unit ptag_count must be > 0")
    if ptype_count <= PTYPE_U:
        raise ValueError("bypass_unit ptype_count must be >= 4 to represent C/P/T/U")

    ptag_w  = max(1, (ptag_count - 1).bit_length())
    ptype_w = max(1, (ptype_count - 1).bit_length())
    lane_w  = max(1, (lanes - 1).bit_length())

    STAGE_CYCLES = {"w3": CYCLE_WB, "w2": CYCLE_MEM, "w1": CYCLE_EX}

    # ── Write-back stage inputs: each stage at its pipeline cycle ──
    w_valid: dict[str, list[CycleAwareSignal]] = {}
    w_ptag:  dict[str, list[CycleAwareSignal]] = {}
    w_ptype: dict[str, list[CycleAwareSignal]] = {}
    w_data:  dict[str, list[CycleAwareSignal]] = {}

    for stage, cyc in STAGE_CYCLES.items():
        v_list: list[CycleAwareSignal] = []
        pt_list: list[CycleAwareSignal] = []
        py_list: list[CycleAwareSignal] = []
        d_list: list[CycleAwareSignal] = []
        for k in range(lanes):
            key_v = f"{stage}{k}_valid"
            key_t = f"{stage}{k}_ptag"
            key_y = f"{stage}{k}_ptype"
            key_d = f"{stage}{k}_data"

            v_list.append(
                _in[key_v] if key_v in _in
                else cas(domain, m.input(f"{prefix}_{key_v}", width=1), cycle=cyc))
            pt_list.append(
                _in[key_t] if key_t in _in
                else cas(domain, m.input(f"{prefix}_{key_t}", width=ptag_w), cycle=cyc))
            py_list.append(
                _in[key_y] if key_y in _in
                else cas(domain, m.input(f"{prefix}_{key_y}", width=ptype_w), cycle=cyc))
            d_list.append(
                _in[key_d] if key_d in _in
                else cas(domain, m.input(f"{prefix}_{key_d}", width=data_width), cycle=cyc))

        w_valid[stage] = v_list
        w_ptag[stage]  = pt_list
        w_ptype[stage] = py_list
        w_data[stage]  = d_list

    # ── Per-lane, per-source resolution ──
    for i in range(lanes):
        for src in ("srcL", "srcR"):
            key_sv = f"i2{i}_{src}_valid"
            key_st = f"i2{i}_{src}_ptag"
            key_sy = f"i2{i}_{src}_ptype"
            key_sd = f"i2{i}_{src}_rf_data"

            src_valid = (
                _in[key_sv] if key_sv in _in
                else cas(domain, m.input(f"{prefix}_{key_sv}", width=1), cycle=CYCLE_ISS))
            src_ptag = (
                _in[key_st] if key_st in _in
                else cas(domain, m.input(f"{prefix}_{key_st}", width=ptag_w), cycle=CYCLE_ISS))
            src_ptype = (
                _in[key_sy] if key_sy in _in
                else cas(domain, m.input(f"{prefix}_{key_sy}", width=ptype_w), cycle=CYCLE_ISS))
            src_rf_data = (
                _in[key_sd] if key_sd in _in
                else cas(domain, m.input(f"{prefix}_{key_sd}", width=data_width), cycle=CYCLE_ISS))

            out_data, out_hit, out_stage, out_lane = _resolve_src(
                m, domain,
                src_valid=src_valid, src_ptag=src_ptag,
                src_ptype=src_ptype, src_rf_data=src_rf_data,
                w1_valid=w_valid["w1"], w1_ptag=w_ptag["w1"],
                w1_ptype=w_ptype["w1"], w1_data=w_data["w1"],
                w2_valid=w_valid["w2"], w2_ptag=w_ptag["w2"],
                w2_ptype=w_ptype["w2"], w2_data=w_data["w2"],
                w3_valid=w_valid["w3"], w3_ptag=w_ptag["w3"],
                w3_ptype=w_ptype["w3"], w3_data=w_data["w3"],
                lanes=lanes, lane_w=lane_w, data_w=data_width,
            )

            okey_d = f"i2{i}_{src}_data"
            okey_h = f"i2{i}_{src}_hit"
            okey_s = f"i2{i}_{src}_sel_stage"
            okey_l = f"i2{i}_{src}_sel_lane"

            m.output(f"{prefix}_{okey_d}", wire_of(out_data))
            m.output(f"{prefix}_{okey_h}", wire_of(out_hit))
            m.output(f"{prefix}_{okey_s}", wire_of(out_stage))
            m.output(f"{prefix}_{okey_l}", wire_of(out_lane))

            _out[okey_d] = out_data
            _out[okey_h] = out_hit
            _out[okey_s] = out_stage
            _out[okey_l] = out_lane

    return _out


bypass_unit.__pycircuit_name__ = "bypass_unit"


@testbench
def tb(t: Tb) -> None:
    t.clock("clk")
    t.reset("rst", cycles_asserted=1, cycles_deasserted=1)
    t.timeout(4)
    t.finish(at=0)


if __name__ == "__main__":
    print(
        compile_cycle_aware(
            bypass_unit,
            name="bypass_unit",
            eager=True,
            lanes=8,
            data_width=64,
            ptag_count=256,
            ptype_count=4,
        ).emit_mlir()[:500]
    )
