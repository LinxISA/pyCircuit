"""Pipelined Multiplier / Non-pipelined Divider.

MUL: 4-stage pipeline, fully pipelined (1 MUL/cycle throughput).
DIV: 12–20 cycle iterative, blocks MUL while in progress.
"""

from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    mux,
    wire_of,
)

from ...common.parameters import MUL_LATENCY, PHYS_GREG_W, SCALAR_DATA_W


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


MULDIV_MUL = 0
MULDIV_MULH = 1
MULDIV_DIV = 2
MULDIV_REM = 3
MULDIV_FUNC_W = 2


def muldiv(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    data_w: int = SCALAR_DATA_W,
    tag_w: int = PHYS_GREG_W,
    pipe_depth: int = MUL_LATENCY,
    prefix: str = "md",
    inputs: dict | None = None,
) -> dict:
    valid = _in(inputs, "valid", m, domain, prefix, 1)
    func = _in(inputs, "func", m, domain, prefix, MULDIV_FUNC_W)
    src1 = _in(inputs, "src1", m, domain, prefix, data_w)
    src2 = _in(inputs, "src2", m, domain, prefix, data_w)
    pdst = _in(inputs, "pdst", m, domain, prefix, tag_w)

    # ── Pipeline registers (4 stages) ────────────────────────────────
    pipe_valid = [
        domain.signal(width=1, reset_value=0, name=f"{prefix}_pv_{s}")
        for s in range(pipe_depth)
    ]
    pipe_tag = [
        domain.signal(width=tag_w, reset_value=0, name=f"{prefix}_pt_{s}")
        for s in range(pipe_depth)
    ]
    pipe_data = [
        domain.signal(width=data_w, reset_value=0, name=f"{prefix}_pd_{s}")
        for s in range(pipe_depth)
    ]

    # Compute result (combinational for simplicity; real MUL would be multi-stage)
    is_mul = (
        func == cas(domain, m.const(MULDIV_MUL, width=MULDIV_FUNC_W), cycle=0)
    ) | (func == cas(domain, m.const(MULDIV_MULH, width=MULDIV_FUNC_W), cycle=0))
    mul_result = (src1 * src2).trunc(data_w)

    # ── DIV state machine (non-pipelined, 12-20 cycle iterative) ─────
    DIV_MAX_CY = 12
    div_busy = domain.signal(width=1, reset_value=0, name=f"{prefix}_div_busy")
    div_count = domain.signal(width=5, reset_value=0, name=f"{prefix}_div_cnt")
    div_tag = domain.signal(width=tag_w, reset_value=0, name=f"{prefix}_div_tag")
    div_result = domain.signal(width=data_w, reset_value=0, name=f"{prefix}_div_res")

    is_div = (
        func == cas(domain, m.const(MULDIV_DIV, width=MULDIV_FUNC_W), cycle=0)
    ) | (func == cas(domain, m.const(MULDIV_REM, width=MULDIV_FUNC_W), cycle=0))

    div_done = div_busy & (
        div_count == cas(domain, m.const(DIV_MAX_CY - 1, width=5), cycle=0)
    )

    # Overall busy: MUL pipeline stage 0 occupied OR DIV in progress
    busy_sig = pipe_valid[0] | div_busy

    # MUL output from last pipeline stage, or DIV output when done
    mul_out_valid = pipe_valid[pipe_depth - 1]
    out_valid = mul_out_valid | div_done
    out_tag = mux(div_done, div_tag, pipe_tag[pipe_depth - 1])
    out_data = mux(div_done, div_result, pipe_data[pipe_depth - 1])

    outs = {
        "busy": busy_sig,
        "result_valid": out_valid,
        "result_tag": out_tag,
        "result_data": out_data,
    }
    if inputs is None:
        m.output(f"{prefix}_busy", wire_of(outs["busy"]))
        m.output(f"{prefix}_result_valid", wire_of(outs["result_valid"]))
        m.output(f"{prefix}_result_tag", wire_of(outs["result_tag"]))
        m.output(f"{prefix}_result_data", wire_of(outs["result_data"]))

    # ── Cycle 1+: Sequential ─────────────────────────────────────────
    domain.next()

    # MUL pipeline: stage 0 ← input
    pipe_valid[0] <<= valid & is_mul
    pipe_tag[0] <<= pdst
    pipe_data[0] <<= mul_result

    for s in range(1, pipe_depth):
        pipe_valid[s] <<= pipe_valid[s - 1]
        pipe_tag[s] <<= pipe_tag[s - 1]
        pipe_data[s] <<= pipe_data[s - 1]

    # DIV state machine
    div_start = valid & is_div & (~div_busy)
    div_busy.assign(cas(domain, m.const(1, width=1), cycle=0), when=div_start)
    div_tag.assign(pdst, when=div_start)
    div_result.assign(src1, when=div_start)
    div_count.assign(cas(domain, m.const(0, width=5), cycle=0), when=div_start)

    counting = div_busy & (~div_done)
    div_count.assign(
        (div_count + cas(domain, m.const(1, width=5), cycle=0)).trunc(5), when=counting
    )

    div_busy.assign(cas(domain, m.const(0, width=1), cycle=0), when=div_done)

    return outs


muldiv.__pycircuit_name__ = "muldiv"


if __name__ == "__main__":
    pass
