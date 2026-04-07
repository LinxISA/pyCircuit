"""Vector Execution Unit — epoch-pipelined, 16-cycle latency.

Processes 512-bit rows from TRegFile-4K. Each vector instruction:
  - Read epoch (8 cy): stream source tiles from TRegFile read ports
  - Write epoch (8 cy): stream results to TRegFile write port

Epoch-pipelined: write epoch of instruction N overlaps read epoch of N+1,
giving 1 tile-wide vector op per 8 cycles sustained throughput.

On completion, broadcasts destination physical tile tag on TCB.
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
    UOP_W,
    VEC_LATENCY,
)

EPOCH_W = 3  # log2(8)
PHASE_W = 1  # read=0, write=1


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def vec_unit(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    ttag_w: int = PHYS_TREG_W,
    uop_w: int = UOP_W,
    epoch_cy: int = TREGFILE_EPOCH_CY,
    prefix: str = "vecu",
    inputs: dict | None = None,
) -> dict:
    # ── Cycle 0: Issue interface ─────────────────────────────────────
    issue_valid = _in(inputs, "issue_valid", m, domain, prefix, 1)
    issue_op = _in(inputs, "issue_op", m, domain, prefix, uop_w)
    issue_ptsrc0 = _in(inputs, "ptsrc0", m, domain, prefix, ttag_w)
    issue_ptsrc1 = _in(inputs, "ptsrc1", m, domain, prefix, ttag_w)
    _in(inputs, "ptsrc2", m, domain, prefix, ttag_w)
    issue_ptdst = _in(inputs, "ptdst", m, domain, prefix, ttag_w)

    # Pipeline state: tracks current instruction through 2 epochs (16 cycles)
    pipe_valid = domain.signal(width=1, reset_value=0, name=f"{prefix}_pv")
    pipe_op = domain.signal(width=uop_w, reset_value=0, name=f"{prefix}_pop")
    pipe_ptdst = domain.signal(width=ttag_w, reset_value=0, name=f"{prefix}_ptd")
    pipe_cycle = domain.signal(
        width=EPOCH_W + 1, reset_value=0, name=f"{prefix}_pc"
    )  # 0..15

    total_w = EPOCH_W + 1
    max_cy = VEC_LATENCY - 1

    # Read port requests (active during read epoch: cycle 0..7)
    is_read_phase = pipe_valid & (
        pipe_cycle < cas(domain, m.const(epoch_cy, width=total_w), cycle=0)
    )
    is_write_phase = pipe_valid & (
        pipe_cycle >= cas(domain, m.const(epoch_cy, width=total_w), cycle=0)
    )

    # Completion: final cycle of write epoch
    done = pipe_valid & (
        pipe_cycle == cas(domain, m.const(max_cy, width=total_w), cycle=0)
    )

    # Ready to accept new instruction
    can_accept = (~pipe_valid) | done

    outs = {
        "rd_req": is_read_phase,
        "wr_req": is_write_phase,
        "rd_ptile0": issue_ptsrc0,
        "rd_ptile1": issue_ptsrc1,
        "wr_ptile": pipe_ptdst,
        "complete_valid": done,
        "complete_ptag": pipe_ptdst,
        "ready": can_accept,
    }

    if inputs is None:
        for k in outs:
            m.output(f"{prefix}_{k}", wire_of(outs[k]))

    # ── Cycle 1: Pipeline advance ────────────────────────────────────
    domain.next()

    next_cycle = (pipe_cycle + cas(domain, m.const(1, width=total_w), cycle=0)).trunc(
        total_w
    )

    # Accept new instruction
    pipe_valid <<= mux(
        done,
        issue_valid,
        mux(
            issue_valid & can_accept,
            cas(domain, m.const(1, width=1), cycle=0),
            pipe_valid,
        ),
    )
    pipe_cycle <<= mux(
        done & issue_valid,
        cas(domain, m.const(0, width=total_w), cycle=0),
        mux(
            done & (~issue_valid),
            cas(domain, m.const(0, width=total_w), cycle=0),
            mux(
                pipe_valid, next_cycle, cas(domain, m.const(0, width=total_w), cycle=0)
            ),
        ),
    )
    pipe_op <<= mux(issue_valid & can_accept, issue_op, pipe_op)
    pipe_ptdst <<= mux(issue_valid & can_accept, issue_ptdst, pipe_ptdst)

    return outs


vec_unit.__pycircuit_name__ = "vec_unit"


if __name__ == "__main__":
    pass
