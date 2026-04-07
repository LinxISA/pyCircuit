"""outerCube MXU Controller — manages OPA execution, staging, accumulation, and drain.

Pipeline: 19 stages base (8 OF + 1 MUL + 1 RED + 1 ACC + 8 AD).
Each CUBE.OPA issues Nb OPA steps; total latency = Nb + 18 cycles.

Reads tile data from TRegFile-4K via R0 (A) and R1–R4 (B).
Drains accumulated results to TRegFile-4K via W0.

Functional model: tracks pipeline state, port allocation, and completion timing.
Actual MAC computation is not modeled at RTL level (too large for synthesis demo).
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
    PHYS_TREG_W,
    TREGFILE_EPOCH_CY,
)

# Cube opcodes
CUBE_OPA = 0
CUBE_DRAIN = 1
CUBE_ZERO = 2
CUBE_CFG = 3
CUBE_WAIT = 4
CUBE_OP_W = 3


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def cube_unit(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    ttag_w: int = PHYS_TREG_W,
    op_w: int = CUBE_OP_W,
    epoch_cy: int = TREGFILE_EPOCH_CY,
    prefix: str = "cube",
    inputs: dict | None = None,
) -> dict:
    cnt_w = 8  # supports up to 255 OPA steps

    # ── Cycle 0: Issue interface ─────────────────────────────────────
    issue_valid = _in(inputs, "issue_valid", m, domain, prefix, 1)
    issue_op = _in(inputs, "issue_op", m, domain, prefix, op_w)
    _in(inputs, "pt_a", m, domain, prefix, ttag_w)
    _in(inputs, "pt_b", m, domain, prefix, ttag_w)
    issue_pt_c = _in(inputs, "pt_c", m, domain, prefix, ttag_w)  # drain dest
    issue_nb = _in(inputs, "nb", m, domain, prefix, cnt_w)  # number of B tiles

    # ── State ────────────────────────────────────────────────────────
    busy = domain.signal(width=1, reset_value=0, name=f"{prefix}_busy")
    cur_op = domain.signal(width=op_w, reset_value=0, name=f"{prefix}_cop")
    remain = domain.signal(width=cnt_w, reset_value=0, name=f"{prefix}_rem")
    pt_c = domain.signal(width=ttag_w, reset_value=0, name=f"{prefix}_ptc")
    drain_ctr = domain.signal(
        width=4, reset_value=0, name=f"{prefix}_dctr"
    )  # drain epoch counter

    is_opa = cur_op == cas(domain, m.const(CUBE_OPA, width=op_w), cycle=0)
    is_drain = cur_op == cas(domain, m.const(CUBE_DRAIN, width=op_w), cycle=0)

    # OPA in progress: reading from TRegFile
    opa_active = (
        busy & is_opa & (remain > cas(domain, m.const(0, width=cnt_w), cycle=0))
    )

    # Drain in progress: writing to TRegFile
    drain_active = (
        busy & is_drain & (drain_ctr < cas(domain, m.const(epoch_cy, width=4), cycle=0))
    )

    # Completion
    opa_done = busy & is_opa & (remain == cas(domain, m.const(0, width=cnt_w), cycle=0))
    drain_done = (
        busy
        & is_drain
        & (drain_ctr >= cas(domain, m.const(epoch_cy, width=4), cycle=0))
    )

    outs = {
        "rd_a_req": opa_active,
        "rd_b_req": opa_active,
        "wr_c_req": drain_active,
        "wr_ptile": pt_c,
        "complete_valid": drain_done,
        "complete_ptag": pt_c,
        "busy": busy,
        "ready": ~busy,
    }

    if inputs is None:
        for k in outs:
            m.output(f"{prefix}_{k}", wire_of(outs[k]))

    # ── Cycle 1: State machine ───────────────────────────────────────
    domain.next()

    # Accept new instruction
    start_opa = (
        issue_valid
        & (~busy)
        & (issue_op == cas(domain, m.const(CUBE_OPA, width=op_w), cycle=0))
    )
    start_drain = (
        issue_valid
        & (~busy)
        & (issue_op == cas(domain, m.const(CUBE_DRAIN, width=op_w), cycle=0))
    )
    start_zero = (
        issue_valid
        & (~busy)
        & (issue_op == cas(domain, m.const(CUBE_ZERO, width=op_w), cycle=0))
    )

    # OPA: decrement remaining B-tile count each epoch
    new_remain = mux(
        opa_active,
        (remain - cas(domain, m.const(1, width=cnt_w), cycle=0)).trunc(cnt_w),
        remain,
    )

    # Drain: count write epoch cycles
    new_drain_ctr = mux(
        drain_active,
        (drain_ctr + cas(domain, m.const(1, width=4), cycle=0)).trunc(4),
        drain_ctr,
    )

    # Transitions
    new_busy = busy
    new_busy = mux(
        opa_done | drain_done | start_zero,
        cas(domain, m.const(0, width=1), cycle=0),
        new_busy,
    )
    new_busy = mux(
        start_opa | start_drain, cas(domain, m.const(1, width=1), cycle=0), new_busy
    )

    busy <<= new_busy
    cur_op <<= mux(
        start_opa,
        cas(domain, m.const(CUBE_OPA, width=op_w), cycle=0),
        mux(start_drain, cas(domain, m.const(CUBE_DRAIN, width=op_w), cycle=0), cur_op),
    )
    remain <<= mux(start_opa, issue_nb, new_remain)
    pt_c <<= mux(start_drain, issue_pt_c, pt_c)
    drain_ctr <<= mux(
        start_drain, cas(domain, m.const(0, width=4), cycle=0), new_drain_ctr
    )

    return outs


cube_unit.__pycircuit_name__ = "cube_unit"


if __name__ == "__main__":
    pass
