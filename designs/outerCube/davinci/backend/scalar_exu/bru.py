"""Branch Resolution Unit — 1-cycle latency.

Compares two GPR operands and resolves branch direction. On mispredict,
signals the rename stage to flash-restore the checkpoint.

Operations: BEQ, BNE, BLT, BGE, BLTU, BGEU, JAL, JALR.
"""

from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    mux,
    wire_of,
)

from ...common.parameters import CHECKPOINT_W, PHYS_GREG_W, SCALAR_DATA_W

BR_BEQ = 0
BR_BNE = 1
BR_BLT = 2
BR_BGE = 3
BR_BLTU = 4
BR_BGEU = 5
BR_JAL = 6
BR_JALR = 7
BR_FUNC_W = 3


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def bru(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    data_w: int = SCALAR_DATA_W,
    tag_w: int = PHYS_GREG_W,
    ckpt_w: int = CHECKPOINT_W,
    addr_w: int = 64,
    prefix: str = "bru",
    inputs: dict | None = None,
) -> dict:
    valid = _in(inputs, "valid", m, domain, prefix, 1)
    func = _in(inputs, "func", m, domain, prefix, BR_FUNC_W)
    src1 = _in(inputs, "src1", m, domain, prefix, data_w)
    src2 = _in(inputs, "src2", m, domain, prefix, data_w)
    predicted = _in(inputs, "predicted", m, domain, prefix, 1)  # predicted taken?
    pc = _in(inputs, "pc", m, domain, prefix, addr_w)
    offset = _in(inputs, "offset", m, domain, prefix, data_w)
    pdst = _in(inputs, "pdst", m, domain, prefix, tag_w)
    ckpt_id = _in(inputs, "ckpt", m, domain, prefix, ckpt_w)

    zero = cas(domain, m.const(0, width=1), cycle=0)
    one = cas(domain, m.const(1, width=1), cycle=0)

    # ── Compare ──────────────────────────────────────────────────────
    eq = src1 == src2
    lt = src1 < src2
    ltu = src1 < src2

    taken = zero
    taken = mux(
        func == cas(domain, m.const(BR_BEQ, width=BR_FUNC_W), cycle=0), eq, taken
    )
    taken = mux(
        func == cas(domain, m.const(BR_BNE, width=BR_FUNC_W), cycle=0), ~eq, taken
    )
    taken = mux(
        func == cas(domain, m.const(BR_BLT, width=BR_FUNC_W), cycle=0), lt, taken
    )
    taken = mux(
        func == cas(domain, m.const(BR_BGE, width=BR_FUNC_W), cycle=0), ~lt, taken
    )
    taken = mux(
        func == cas(domain, m.const(BR_BLTU, width=BR_FUNC_W), cycle=0), ltu, taken
    )
    taken = mux(
        func == cas(domain, m.const(BR_BGEU, width=BR_FUNC_W), cycle=0), ~ltu, taken
    )
    taken = mux(
        func == cas(domain, m.const(BR_JAL, width=BR_FUNC_W), cycle=0), one, taken
    )
    taken = mux(
        func == cas(domain, m.const(BR_JALR, width=BR_FUNC_W), cycle=0), one, taken
    )

    mispredict = valid & (taken ^ predicted)

    # Target address
    four = cas(domain, m.const(4, width=addr_w), cycle=0)
    target_branch = (pc + offset).trunc(addr_w)
    target_jalr = (src1 + offset).trunc(addr_w)
    is_jalr = func == cas(domain, m.const(BR_JALR, width=BR_FUNC_W), cycle=0)
    target = mux(is_jalr, target_jalr, target_branch)

    # Link address for JAL/JALR (rd = PC + 4)
    link_addr = (pc + four).trunc(addr_w)
    is_link = (func == cas(domain, m.const(BR_JAL, width=BR_FUNC_W), cycle=0)) | is_jalr
    has_result = valid & is_link

    # ── Outputs ──────────────────────────────────────────────────────
    out = {
        "mispredict": mispredict,
        "taken": taken,
        "target": target,
        "ckpt_id": ckpt_id,
        "result_valid": has_result,
        "result_tag": pdst,
        "result_data": link_addr,
        "dealloc_valid": valid & (~mispredict),
    }
    if inputs is None:
        m.output(f"{prefix}_mispredict", wire_of(out["mispredict"]))
        m.output(f"{prefix}_taken", wire_of(out["taken"]))
        m.output(f"{prefix}_target", wire_of(out["target"]))
        m.output(f"{prefix}_ckpt_id", wire_of(out["ckpt_id"]))
        m.output(f"{prefix}_result_valid", wire_of(out["result_valid"]))
        m.output(f"{prefix}_result_tag", wire_of(out["result_tag"]))
        m.output(f"{prefix}_result_data", wire_of(out["result_data"]))
        m.output(f"{prefix}_dealloc_valid", wire_of(out["dealloc_valid"]))
    return out


bru.__pycircuit_name__ = "bru"


if __name__ == "__main__":
    pass
