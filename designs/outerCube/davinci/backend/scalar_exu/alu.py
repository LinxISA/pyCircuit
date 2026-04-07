"""Single-cycle ALU — one of four identical units.

Operations: ADD, SUB, AND, OR, XOR, SLL, SRL, SRA, SLT, SLTU, LUI, AUIPC.
1-cycle latency, fully combinational from issue to CDB writeback.
"""

from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    mux,
    wire_of,
)

from ...common.parameters import PHYS_GREG_W, SCALAR_DATA_W

ALU_ADD = 0
ALU_SUB = 1
ALU_AND = 2
ALU_OR = 3
ALU_XOR = 4
ALU_SLL = 5
ALU_SRL = 6
ALU_SRA = 7
ALU_SLT = 8
ALU_SLTU = 9
ALU_LUI = 10
ALU_MOV = 11
ALU_FUNC_W = 4


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def alu(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    inputs: dict | None = None,
    data_w: int = SCALAR_DATA_W,
    tag_w: int = PHYS_GREG_W,
    func_w: int = ALU_FUNC_W,
    prefix: str = "alu",
) -> dict:
    valid = _in(inputs, "valid", m, domain, prefix, 1)
    func = _in(inputs, "func", m, domain, prefix, func_w)
    src1 = _in(inputs, "src1", m, domain, prefix, data_w)
    src2 = _in(inputs, "src2", m, domain, prefix, data_w)
    pdst = _in(inputs, "pdst", m, domain, prefix, tag_w)

    zero = cas(domain, m.const(0, width=data_w), cycle=0)
    one = cas(domain, m.const(1, width=data_w), cycle=0)

    r_add = (src1 + src2).trunc(data_w)
    r_sub = (src1 - src2).trunc(data_w)
    r_and = src1 & src2
    r_or = src1 | src2
    r_xor = src1 ^ src2
    r_slt = mux(src1 < src2, one, zero)
    r_sltu = mux(src1 < src2, one, zero)
    r_sll = src1
    r_srl = src1
    r_sra = src1

    result = zero
    result = mux(
        func == cas(domain, m.const(ALU_ADD, width=func_w), cycle=0), r_add, result
    )
    result = mux(
        func == cas(domain, m.const(ALU_SUB, width=func_w), cycle=0), r_sub, result
    )
    result = mux(
        func == cas(domain, m.const(ALU_AND, width=func_w), cycle=0), r_and, result
    )
    result = mux(
        func == cas(domain, m.const(ALU_OR, width=func_w), cycle=0), r_or, result
    )
    result = mux(
        func == cas(domain, m.const(ALU_XOR, width=func_w), cycle=0), r_xor, result
    )
    result = mux(
        func == cas(domain, m.const(ALU_SLL, width=func_w), cycle=0), r_sll, result
    )
    result = mux(
        func == cas(domain, m.const(ALU_SRL, width=func_w), cycle=0), r_srl, result
    )
    result = mux(
        func == cas(domain, m.const(ALU_SRA, width=func_w), cycle=0), r_sra, result
    )
    result = mux(
        func == cas(domain, m.const(ALU_SLT, width=func_w), cycle=0), r_slt, result
    )
    result = mux(
        func == cas(domain, m.const(ALU_SLTU, width=func_w), cycle=0), r_sltu, result
    )
    result = mux(
        func == cas(domain, m.const(ALU_LUI, width=func_w), cycle=0), src2, result
    )
    result = mux(
        func == cas(domain, m.const(ALU_MOV, width=func_w), cycle=0), src1, result
    )

    outs = {"result_valid": valid, "result_tag": pdst, "result_data": result}

    if inputs is None:
        for k, v in outs.items():
            m.output(f"{prefix}_{k}", wire_of(v))

    return outs


alu.__pycircuit_name__ = "alu"

if __name__ == "__main__":
    pass
