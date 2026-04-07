"""D1 Stage — 4-wide Instruction Decoder.

Decodes 4 instructions per cycle. For each instruction:
  - Extract opcode[6:0], rd, rs1, rs2, funct3, funct7, immediate
  - Classify domain: scalar (opcode[6:5]=00/01), vec/MTE (10), cube (11)
  - Detect branches for checkpoint allocation at D2
  - Detect TILE.MOVE for move-elimination at D2
"""

from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    wire_of,
)

from ...common.parameters import (
    DECODE_WIDTH,
    INSTR_WIDTH,
)

DOMAIN_W = 2  # 2-bit domain code


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def decoder(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    width: int = DECODE_WIDTH,
    prefix: str = "dec",
    inputs: dict | None = None,
) -> dict:
    # ── Cycle 0: Inputs ──────────────────────────────────────────────
    instr_valid = [_in(inputs, f"valid{i}", m, domain, prefix, 1) for i in range(width)]
    instr_bits = [
        _in(inputs, f"instr{i}", m, domain, prefix, INSTR_WIDTH) for i in range(width)
    ]

    dec_opcode_list: list = []
    dec_rd_list: list = []
    dec_rs1_list: list = []
    dec_rs2_list: list = []
    dec_funct3_list: list = []
    dec_funct7_list: list = []
    dec_domain_list: list = []
    dec_is_branch_list: list = []
    dec_tile_move_list: list = []
    has_srd_list: list = []
    has_srs1_list: list = []
    has_srs2_list: list = []
    has_trd_list: list = []
    has_trs_list: list = []
    out_valid_list: list = []

    for i in range(width):
        raw = instr_bits[i]

        opcode = raw[0:7]
        rd = raw[7:12]
        funct3 = raw[12:15]
        rs1 = raw[15:20]
        rs2 = raw[20:25]
        funct7 = raw[25:32]

        domain_code = raw[5:7]

        is_scalar = (
            domain_code == cas(domain, m.const(0b00, width=DOMAIN_W), cycle=0)
        ) | (domain_code == cas(domain, m.const(0b01, width=DOMAIN_W), cycle=0))
        is_vec_mte = domain_code == cas(domain, m.const(0b10, width=DOMAIN_W), cycle=0)
        is_cube = domain_code == cas(domain, m.const(0b11, width=DOMAIN_W), cycle=0)

        # Branch detection: BEQ/BNE/BLT/BGE/BLTU/BGEU share opcode[6:2]=11000
        branch_opcode = raw[2:7]
        is_branch = is_scalar & (
            branch_opcode == cas(domain, m.const(0b11000, width=5), cycle=0)
        )
        is_jal = is_scalar & (
            opcode == cas(domain, m.const(0b1101111, width=7), cycle=0)
        )
        is_jalr = is_scalar & (
            opcode == cas(domain, m.const(0b1100111, width=7), cycle=0)
        )
        needs_checkpoint = is_branch | is_jal | is_jalr

        # TILE.MOVE detection (funct7 = 0100010, domain = 10)
        tile_move_funct7 = funct7 == cas(domain, m.const(0b0100010, width=7), cycle=0)
        is_tile_move = is_vec_mte & tile_move_funct7

        has_scalar_rd = is_scalar & instr_valid[i]
        has_scalar_rs1 = is_scalar & instr_valid[i]
        has_scalar_rs2 = is_scalar & instr_valid[i]
        has_tile_rd = (is_vec_mte | is_cube) & instr_valid[i] & (~is_tile_move)
        has_tile_rs = (is_vec_mte | is_cube) & instr_valid[i]

        dec_opcode_list.append(opcode)
        dec_rd_list.append(rd)
        dec_rs1_list.append(rs1)
        dec_rs2_list.append(rs2)
        dec_funct3_list.append(funct3)
        dec_funct7_list.append(funct7)
        dec_domain_list.append(domain_code)
        dec_is_branch_list.append(needs_checkpoint)
        dec_tile_move_list.append(is_tile_move)
        has_srd_list.append(has_scalar_rd)
        has_srs1_list.append(has_scalar_rs1)
        has_srs2_list.append(has_scalar_rs2)
        has_trd_list.append(has_tile_rd)
        has_trs_list.append(has_tile_rs)
        out_valid_list.append(instr_valid[i])

        # ── Outputs ──────────────────────────────────────────────────
        if inputs is None:
            m.output(f"{prefix}_opcode{i}", wire_of(opcode))
            m.output(f"{prefix}_rd{i}", wire_of(rd))
            m.output(f"{prefix}_rs1_{i}", wire_of(rs1))
            m.output(f"{prefix}_rs2_{i}", wire_of(rs2))
            m.output(f"{prefix}_funct3_{i}", wire_of(funct3))
            m.output(f"{prefix}_funct7_{i}", wire_of(funct7))
            m.output(f"{prefix}_domain{i}", wire_of(domain_code))
            m.output(f"{prefix}_is_branch{i}", wire_of(needs_checkpoint))
            m.output(f"{prefix}_tile_move{i}", wire_of(is_tile_move))
            m.output(f"{prefix}_has_srd{i}", wire_of(has_scalar_rd))
            m.output(f"{prefix}_has_srs1_{i}", wire_of(has_scalar_rs1))
            m.output(f"{prefix}_has_srs2_{i}", wire_of(has_scalar_rs2))
            m.output(f"{prefix}_has_trd{i}", wire_of(has_tile_rd))
            m.output(f"{prefix}_has_trs{i}", wire_of(has_tile_rs))
            m.output(f"{prefix}_out_valid{i}", wire_of(instr_valid[i]))

    outs = {
        "opcode": dec_opcode_list,
        "rd": dec_rd_list,
        "rs1": dec_rs1_list,
        "rs2": dec_rs2_list,
        "funct3": dec_funct3_list,
        "funct7": dec_funct7_list,
        "domain": dec_domain_list,
        "is_branch": dec_is_branch_list,
        "tile_move": dec_tile_move_list,
        "has_srd": has_srd_list,
        "has_srs1": has_srs1_list,
        "has_srs2": has_srs2_list,
        "has_trd": has_trd_list,
        "has_trs": has_trs_list,
        "out_valid": out_valid_list,
    }
    return outs


decoder.__pycircuit_name__ = "decoder"


if __name__ == "__main__":
    pass
