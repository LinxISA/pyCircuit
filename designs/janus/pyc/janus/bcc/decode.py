from __future__ import annotations
from dataclasses import dataclass
from pycircuit import Circuit, Wire, cat, jit_inline, unsigned
from .isa import OP_ADDTPC, OP_ADDI, OP_ADDIW, OP_ADD, OP_ADDW, OP_AND, OP_ANDI, OP_ANDIW, OP_BSTART_STD_COND, OP_BSTART_STD_DIRECT, OP_BSTART_STD_FALL, OP_ANDW, OP_BXS, OP_BXU, OP_BSTART_STD_CALL, OP_CMP_EQ, OP_CMP_EQI, OP_CMP_NE, OP_CMP_NEI, OP_CMP_ANDI, OP_CMP_ORI, OP_CMP_LT, OP_CMP_LTI, OP_CMP_LTUI, OP_CMP_LTU, OP_CMP_GEI, OP_CMP_GEUI, OP_C_ADD, OP_C_ADDI, OP_C_AND, OP_C_OR, OP_C_SUB, OP_CSEL, OP_C_BSTART_DIRECT, OP_C_BSTOP, OP_C_BSTART_COND, OP_C_BSTART_STD, OP_C_LDI, OP_C_LWI, OP_C_MOVI, OP_C_MOVR, OP_C_SETC_EQ, OP_C_SETC_NE, OP_C_SETC_TGT, OP_C_SDI, OP_C_SEXT_W, OP_C_SETRET, OP_C_SWI, OP_C_ZEXT_W, OP_EBREAK, OP_FENTRY, OP_FEXIT, OP_FRET_RA, OP_FRET_STK, OP_HL_LB_PCR, OP_HL_LBU_PCR, OP_HL_LD_PCR, OP_HL_LH_PCR, OP_HL_LHU_PCR, OP_HL_LW_PCR, OP_HL_LUI, OP_HL_LWU_PCR, OP_HL_SB_PCR, OP_HL_SD_PCR, OP_HL_SH_PCR, OP_HL_SW_PCR, OP_INVALID, OP_LB, OP_LBI, OP_LBU, OP_LBUI, OP_LD, OP_LH, OP_LHI, OP_LHU, OP_LHUI, OP_LDI, OP_LUI, OP_LW, OP_LWI, OP_LWU, OP_LWUI, OP_MADD, OP_MADDW, OP_MUL, OP_MULW, OP_OR, OP_ORI, OP_ORIW, OP_ORW, OP_XOR, OP_XORIW, OP_DIV, OP_DIVU, OP_DIVW, OP_DIVUW, OP_REM, OP_REMU, OP_REMW, OP_REMUW, OP_SB, OP_SETC_AND, OP_SETC_EQ, OP_SETC_GE, OP_SETC_GEI, OP_SETC_GEU, OP_SETC_GEUI, OP_SETC_LT, OP_SETC_LTI, OP_SETC_LTU, OP_SETC_LTUI, OP_SETC_NE, OP_SETC_NEI, OP_SETC_OR, OP_SETC_ORI, OP_SETC_ANDI, OP_SETC_EQI, OP_SETRET, OP_SBI, OP_SD, OP_SH, OP_SHI, OP_SDI, OP_SLL, OP_SLLI, OP_SLLIW, OP_SRL, OP_SRA, OP_SRAIW, OP_SRLIW, OP_SW, OP_SUB, OP_SUBI, OP_SUBIW, OP_SUBW, OP_SWI, OP_XORW, REG_INVALID
from .util import lshr_var, masked_eq

@dataclass(frozen=True)
class Decode:
    op: Wire
    len_bytes: Wire
    regdst: Wire
    srcl: Wire
    srcr: Wire
    srcr_type: Wire
    shamt: Wire
    srcp: Wire
    imm: Wire

@dataclass(frozen=True)
class DecodeBundle:
    valid: list[Wire]
    off_bytes: list[Wire]
    dec: list[Decode]
    total_len_bytes: Wire

def decode_window(m: Circuit, window: Wire) -> Decode:
    c = m.const
    zero3 = c(0, width=3)
    zero2 = c(0, width=2)
    zero6 = c(0, width=6)
    zero64 = c(0, width=64)
    reg_invalid = c(REG_INVALID, width=6)
    insn16 = window[0:16]
    insn32 = window[0:32]
    insn48 = window[0:48]
    low4 = insn16[0:4]
    is_hl = low4 == 14
    is32 = insn16[0]
    in32 = ~is_hl & is32
    in16 = ~is_hl & ~is32
    rd32 = insn32[7:12]
    rs1_32 = insn32[15:20]
    rs2_32 = insn32[20:25]
    srcr_type_32 = insn32[25:27]
    shamt5_32 = insn32[27:32]
    srcp_32 = insn32[27:32]
    shamt6_32 = insn32[20:26]
    imm12_u64 = insn32[20:32]
    imm12_s64 = insn32[20:32].as_signed()
    imm20_s64 = insn32[12:32].as_signed()
    imm20_u64 = unsigned(insn32[12:32])
    swi_lo5 = insn32[7:12]
    swi_hi7 = insn32[25:32]
    simm12_raw = cat(unsigned(swi_lo5), unsigned(swi_hi7))
    simm12_s64 = simm12_raw.as_signed()
    simm17_s64 = insn32[15:32].as_signed()
    pfx16 = insn48[0:16]
    main32 = insn48[16:48]
    imm_hi12 = pfx16[4:16]
    imm_lo20 = main32[12:32]
    imm32 = unsigned(imm_hi12).shl(amount=20) | unsigned(imm_lo20)
    imm_hl_lui = imm32.as_signed()
    rd_hl = main32[7:12]
    rd16 = insn16[11:16]
    rs16 = insn16[6:11]
    simm5_11_s64 = insn16[11:16].as_signed()
    simm5_6_s64 = insn16[6:11].as_signed()
    simm12_s64_c = insn16[4:16].as_signed()
    uimm5 = insn16[6:11]
    brtype = insn16[11:14]
    op = c(OP_INVALID, width=12)
    len_bytes = zero3
    regdst = reg_invalid
    srcl = reg_invalid
    srcr = reg_invalid
    srcr_type = zero2
    shamt = zero6
    srcp = reg_invalid
    imm = zero64
    cond = in16 & masked_eq(insn16, mask=63, match=12)

    def aw(x: Wire | int, width: int) -> Wire:
        if isinstance(x, Wire):
            if x.width == width:
                return x
            if x.width < width:
                return x._sext(width=width) if x.signed else x._zext(width=width)
            return x[0:width]
        return c(int(x), width=width)

    def set_if(cond: Wire, *, op_v: Wire | int | None=None, len_v: Wire | int | None=None, regdst_v: Wire | int | None=None, srcl_v: Wire | int | None=None, srcr_v: Wire | int | None=None, srcr_type_v: Wire | int | None=None, shamt_v: Wire | int | None=None, srcp_v: Wire | int | None=None, imm_v: Wire | int | None=None) -> None:
        nonlocal op, len_bytes, regdst, srcl, srcr, srcr_type, shamt, srcp, imm
        cond = m.wire(cond)
        if op_v is not None:
            op = aw(op_v, 12) if cond else op
        if len_v is not None:
            len_bytes = aw(len_v, 3) if cond else len_bytes
        if regdst_v is not None:
            regdst = aw(regdst_v, 6) if cond else regdst
        if srcl_v is not None:
            srcl = aw(srcl_v, 6) if cond else srcl
        if srcr_v is not None:
            srcr = aw(srcr_v, 6) if cond else srcr
        if srcr_type_v is not None:
            srcr_type = aw(srcr_type_v, 2) if cond else srcr_type
        if shamt_v is not None:
            shamt = aw(shamt_v, 6) if cond else shamt
        if srcp_v is not None:
            srcp = aw(srcp_v, 6) if cond else srcp
        if imm_v is not None:
            imm = aw(imm_v, 64) if cond else imm
    set_if(cond, op_v=OP_C_ADDI, len_v=2, regdst_v=31, srcl_v=rs16, imm_v=simm5_11_s64)
    cond = in16 & masked_eq(insn16, mask=63, match=8)
    set_if(cond, op_v=OP_C_ADD, len_v=2, regdst_v=31, srcl_v=rs16, srcr_v=rd16)
    cond = in16 & masked_eq(insn16, mask=63, match=24)
    set_if(cond, op_v=OP_C_SUB, len_v=2, regdst_v=31, srcl_v=rs16, srcr_v=rd16)
    cond = in16 & masked_eq(insn16, mask=63, match=40)
    set_if(cond, op_v=OP_C_AND, len_v=2, regdst_v=31, srcl_v=rs16, srcr_v=rd16)
    cond = in16 & masked_eq(insn16, mask=63, match=22)
    set_if(cond, op_v=OP_C_MOVI, len_v=2, regdst_v=rd16, imm_v=simm5_6_s64)
    cond = in16 & masked_eq(insn16, mask=63551, match=20502)
    set_if(cond, op_v=OP_C_SETRET, len_v=2, regdst_v=10, imm_v=unsigned(uimm5).shl(amount=1))
    cond = in16 & masked_eq(insn16, mask=63, match=42)
    set_if(cond, op_v=OP_C_SWI, len_v=2, srcl_v=rs16, srcr_v=24, imm_v=simm5_11_s64)
    cond = in16 & masked_eq(insn16, mask=63, match=58)
    set_if(cond, op_v=OP_C_SDI, len_v=2, srcl_v=rs16, srcr_v=24, imm_v=simm5_11_s64)
    cond = in16 & masked_eq(insn16, mask=63, match=10)
    set_if(cond, op_v=OP_C_LWI, len_v=2, srcl_v=rs16, imm_v=simm5_11_s64)
    cond = in16 & masked_eq(insn16, mask=63, match=26)
    set_if(cond, op_v=OP_C_LDI, len_v=2, regdst_v=31, srcl_v=rs16, imm_v=simm5_11_s64)
    cond = in16 & masked_eq(insn16, mask=63, match=6)
    set_if(cond, op_v=OP_C_MOVR, len_v=2, regdst_v=rd16, srcl_v=rs16)
    cond = in16 & masked_eq(insn16, mask=63, match=38)
    set_if(cond, op_v=OP_C_SETC_EQ, len_v=2, srcl_v=rs16, srcr_v=rd16)
    cond = in16 & masked_eq(insn16, mask=63, match=54)
    set_if(cond, op_v=OP_C_SETC_NE, len_v=2, srcl_v=rs16, srcr_v=rd16)
    cond = in16 & masked_eq(insn16, mask=63551, match=28)
    set_if(cond, op_v=OP_C_SETC_TGT, len_v=2, srcl_v=rs16)
    cond = in16 & masked_eq(insn16, mask=63, match=56)
    set_if(cond, op_v=OP_C_OR, len_v=2, regdst_v=31, srcl_v=rs16, srcr_v=rd16)
    cond = in16 & masked_eq(insn16, mask=63551, match=20508)
    set_if(cond, op_v=OP_C_SEXT_W, len_v=2, regdst_v=31, srcl_v=rs16)
    cond = in16 & masked_eq(insn16, mask=63551, match=26652)
    set_if(cond, op_v=OP_C_ZEXT_W, len_v=2, regdst_v=31, srcl_v=rs16)
    cond = in16 & masked_eq(insn16, mask=63551, match=44)
    set_if(cond, op_v=OP_CMP_EQI, len_v=2, regdst_v=31, srcl_v=24, imm_v=simm5_6_s64)
    cond = in16 & masked_eq(insn16, mask=63551, match=2092)
    set_if(cond, op_v=OP_CMP_NEI, len_v=2, regdst_v=31, srcl_v=24, imm_v=simm5_6_s64)
    cond = in16 & masked_eq(insn16, mask=15, match=2)
    set_if(cond, op_v=OP_C_BSTART_DIRECT, len_v=2, imm_v=simm12_s64_c.shl(amount=1))
    cond = in16 & masked_eq(insn16, mask=15, match=4)
    set_if(cond, op_v=OP_C_BSTART_COND, len_v=2, imm_v=simm12_s64_c.shl(amount=1))
    cond = in16 & masked_eq(insn16, mask=51199, match=0)
    set_if(cond, op_v=OP_C_BSTART_STD, len_v=2, imm_v=brtype)
    cond = in16 & masked_eq(insn16, mask=65535, match=0)
    set_if(cond, op_v=OP_C_BSTOP, len_v=2)
    cond = in32 & masked_eq(insn32, mask=28799, match=65)
    uimm_hi = unsigned(insn32[7:12])
    uimm_lo = unsigned(insn32[25:32])
    macro_imm = uimm_hi.shl(amount=10) | uimm_lo.shl(amount=3)
    set_if(cond, op_v=OP_FENTRY, len_v=4, srcl_v=insn32[15:20], srcr_v=insn32[20:25], imm_v=macro_imm)
    cond = in32 & masked_eq(insn32, mask=28799, match=4161)
    set_if(cond, op_v=OP_FEXIT, len_v=4, srcl_v=insn32[15:20], srcr_v=insn32[20:25], imm_v=macro_imm)
    cond = in32 & masked_eq(insn32, mask=28799, match=8257)
    set_if(cond, op_v=OP_FRET_RA, len_v=4, srcl_v=insn32[15:20], srcr_v=insn32[20:25], imm_v=macro_imm)
    cond = in32 & masked_eq(insn32, mask=28799, match=12353)
    set_if(cond, op_v=OP_FRET_STK, len_v=4, srcl_v=insn32[15:20], srcr_v=insn32[20:25], imm_v=macro_imm)
    cond = in32 & masked_eq(insn32, mask=127, match=23)
    set_if(cond, op_v=OP_LUI, len_v=4, regdst_v=rd32, imm_v=imm20_s64.shl(amount=12))
    cond = in32 & masked_eq(insn32, mask=28799, match=5)
    set_if(cond, op_v=OP_ADD, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=shamt5_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=4101)
    set_if(cond, op_v=OP_SUB, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=shamt5_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=8197)
    set_if(cond, op_v=OP_AND, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=shamt5_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=12293)
    set_if(cond, op_v=OP_OR, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=shamt5_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=16389)
    set_if(cond, op_v=OP_XOR, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=shamt5_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=8213)
    set_if(cond, op_v=OP_ANDI, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=8245)
    set_if(cond, op_v=OP_ANDIW, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=12309)
    set_if(cond, op_v=OP_ORI, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=12341)
    set_if(cond, op_v=OP_ORIW, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=16437)
    set_if(cond, op_v=OP_XORIW, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=4261441663, match=71)
    set_if(cond, op_v=OP_MUL, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32)
    cond = in32 & masked_eq(insn32, mask=4261441663, match=8263)
    set_if(cond, op_v=OP_MULW, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32)
    cond = in32 & masked_eq(insn32, mask=100692095, match=24647)
    set_if(cond, op_v=OP_MADD, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcp_v=srcp_32)
    cond = in32 & masked_eq(insn32, mask=100692095, match=28743)
    set_if(cond, op_v=OP_MADDW, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcp_v=srcp_32)
    cond = in32 & masked_eq(insn32, mask=4261441663, match=87)
    set_if(cond, op_v=OP_DIV, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32)
    cond = in32 & masked_eq(insn32, mask=4261441663, match=4183)
    set_if(cond, op_v=OP_DIVU, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32)
    cond = in32 & masked_eq(insn32, mask=4261441663, match=8279)
    set_if(cond, op_v=OP_DIVW, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32)
    cond = in32 & masked_eq(insn32, mask=4261441663, match=12375)
    set_if(cond, op_v=OP_DIVUW, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32)
    cond = in32 & masked_eq(insn32, mask=4261441663, match=16471)
    set_if(cond, op_v=OP_REM, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32)
    cond = in32 & masked_eq(insn32, mask=4261441663, match=20567)
    set_if(cond, op_v=OP_REMU, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32)
    cond = in32 & masked_eq(insn32, mask=4261441663, match=24663)
    set_if(cond, op_v=OP_REMW, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32)
    cond = in32 & masked_eq(insn32, mask=4261441663, match=28759)
    set_if(cond, op_v=OP_REMUW, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32)
    cond = in32 & masked_eq(insn32, mask=4261441663, match=28677)
    set_if(cond, op_v=OP_SLL, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32)
    cond = in32 & masked_eq(insn32, mask=4261441663, match=20485)
    set_if(cond, op_v=OP_SRL, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32)
    cond = in32 & masked_eq(insn32, mask=4261441663, match=24581)
    set_if(cond, op_v=OP_SRA, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32)
    cond = in32 & masked_eq(insn32, mask=4227887231, match=28693)
    set_if(cond, op_v=OP_SLLI, len_v=4, regdst_v=rd32, srcl_v=rs1_32, shamt_v=shamt6_32)
    cond = in32 & masked_eq(insn32, mask=4261441663, match=20533)
    set_if(cond, op_v=OP_SRLIW, len_v=4, regdst_v=rd32, srcl_v=rs1_32, shamt_v=rs2_32)
    cond = in32 & masked_eq(insn32, mask=4261441663, match=24629)
    set_if(cond, op_v=OP_SRAIW, len_v=4, regdst_v=rd32, srcl_v=rs1_32, shamt_v=rs2_32)
    cond = in32 & masked_eq(insn32, mask=4261441663, match=28725)
    set_if(cond, op_v=OP_SLLIW, len_v=4, regdst_v=rd32, srcl_v=rs1_32, shamt_v=rs2_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=103)
    set_if(cond, op_v=OP_BXS, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=insn32[26:32], srcp_v=insn32[20:26])
    cond = in32 & masked_eq(insn32, mask=28799, match=4199)
    set_if(cond, op_v=OP_BXU, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=insn32[26:32], srcp_v=insn32[20:26])
    cond = in32 & masked_eq(insn32, mask=4160778367, match=24645)
    set_if(cond, op_v=OP_CMP_LTU, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=85)
    set_if(cond, op_v=OP_CMP_EQI, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=4181)
    set_if(cond, op_v=OP_CMP_NEI, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=8277)
    set_if(cond, op_v=OP_CMP_ANDI, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=12373)
    set_if(cond, op_v=OP_CMP_ORI, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=16469)
    set_if(cond, op_v=OP_CMP_LTI, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=20565)
    set_if(cond, op_v=OP_CMP_GEI, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=24661)
    set_if(cond, op_v=OP_CMP_LTUI, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_u64)
    cond = in32 & masked_eq(insn32, mask=28799, match=28757)
    set_if(cond, op_v=OP_CMP_GEUI, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_u64)
    cond = in32 & masked_eq(insn32, mask=28799, match=117)
    set_if(cond, op_v=OP_SETC_EQI, len_v=4, srcl_v=rs1_32, shamt_v=rd32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=4213)
    set_if(cond, op_v=OP_SETC_NEI, len_v=4, srcl_v=rs1_32, shamt_v=rd32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=8309)
    set_if(cond, op_v=OP_SETC_ANDI, len_v=4, srcl_v=rs1_32, shamt_v=rd32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=12405)
    set_if(cond, op_v=OP_SETC_ORI, len_v=4, srcl_v=rs1_32, shamt_v=rd32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=16501)
    set_if(cond, op_v=OP_SETC_LTI, len_v=4, srcl_v=rs1_32, shamt_v=rd32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=20597)
    set_if(cond, op_v=OP_SETC_GEI, len_v=4, srcl_v=rs1_32, shamt_v=rd32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=24693)
    set_if(cond, op_v=OP_SETC_LTUI, len_v=4, srcl_v=rs1_32, shamt_v=rd32, imm_v=imm12_u64)
    cond = in32 & masked_eq(insn32, mask=28799, match=28789)
    set_if(cond, op_v=OP_SETC_GEUI, len_v=4, srcl_v=rs1_32, shamt_v=rd32, imm_v=imm12_u64)
    cond = in32 & masked_eq(insn32, mask=4160778367, match=101)
    set_if(cond, op_v=OP_SETC_EQ, len_v=4, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32)
    cond = in32 & masked_eq(insn32, mask=4160778367, match=4197)
    set_if(cond, op_v=OP_SETC_NE, len_v=4, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32)
    cond = in32 & masked_eq(insn32, mask=4160778367, match=8293)
    set_if(cond, op_v=OP_SETC_AND, len_v=4, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32)
    cond = in32 & masked_eq(insn32, mask=4160778367, match=12389)
    set_if(cond, op_v=OP_SETC_OR, len_v=4, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32)
    cond = in32 & masked_eq(insn32, mask=4160778367, match=16485)
    set_if(cond, op_v=OP_SETC_LT, len_v=4, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32)
    cond = in32 & masked_eq(insn32, mask=4160778367, match=24677)
    set_if(cond, op_v=OP_SETC_LTU, len_v=4, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32)
    cond = in32 & masked_eq(insn32, mask=4160778367, match=20581)
    set_if(cond, op_v=OP_SETC_GE, len_v=4, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32)
    cond = in32 & masked_eq(insn32, mask=4160778367, match=28773)
    set_if(cond, op_v=OP_SETC_GEU, len_v=4, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32)
    cond = in32 & masked_eq(insn32, mask=4293951487, match=16443)
    set_if(cond, op_v=OP_C_SETC_TGT, len_v=4, srcl_v=rs1_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=16409)
    set_if(cond, op_v=OP_LBUI, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=25)
    set_if(cond, op_v=OP_LBI, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=4121)
    set_if(cond, op_v=OP_LHI, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=20505)
    set_if(cond, op_v=OP_LHUI, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=24601)
    set_if(cond, op_v=OP_LWUI, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=9)
    set_if(cond, op_v=OP_LB, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=shamt5_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=16393)
    set_if(cond, op_v=OP_LBU, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=shamt5_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=4105)
    set_if(cond, op_v=OP_LH, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=shamt5_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=20489)
    set_if(cond, op_v=OP_LHU, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=shamt5_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=8201)
    set_if(cond, op_v=OP_LW, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=shamt5_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=24585)
    set_if(cond, op_v=OP_LWU, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=shamt5_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=12297)
    set_if(cond, op_v=OP_LD, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=shamt5_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=12313)
    set_if(cond, op_v=OP_LDI, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=32767, match=4097)
    set_if(cond, op_v=OP_BSTART_STD_FALL, len_v=4)
    cond = in32 & masked_eq(insn32, mask=32767, match=8193)
    set_if(cond, op_v=OP_BSTART_STD_DIRECT, len_v=4, imm_v=simm17_s64.shl(amount=1))
    cond = in32 & masked_eq(insn32, mask=32767, match=12289)
    set_if(cond, op_v=OP_BSTART_STD_COND, len_v=4, imm_v=simm17_s64.shl(amount=1))
    cond = in32 & masked_eq(insn32, mask=32767, match=20481)
    set_if(cond, op_v=OP_C_BSTART_STD, len_v=4, imm_v=5)
    cond = in32 & masked_eq(insn32, mask=32767, match=24577)
    set_if(cond, op_v=OP_C_BSTART_STD, len_v=4, imm_v=6)
    cond = in32 & masked_eq(insn32, mask=32767, match=28673)
    set_if(cond, op_v=OP_C_BSTART_STD, len_v=4, imm_v=7)
    cond = in32 & masked_eq(insn32, mask=28799, match=16421)
    set_if(cond, op_v=OP_XORW, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=shamt5_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=8229)
    set_if(cond, op_v=OP_ANDW, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=shamt5_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=12325)
    set_if(cond, op_v=OP_ORW, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=shamt5_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=37)
    set_if(cond, op_v=OP_ADDW, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=shamt5_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=4133)
    set_if(cond, op_v=OP_SUBW, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=shamt5_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=119)
    set_if(cond, op_v=OP_CSEL, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, srcp_v=srcp_32)
    cond = in32 & masked_eq(insn32, mask=28799, match=89)
    set_if(cond, op_v=OP_SBI, len_v=4, srcl_v=rs1_32, srcr_v=rs2_32, imm_v=simm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=4185)
    set_if(cond, op_v=OP_SHI, len_v=4, srcl_v=rs1_32, srcr_v=rs2_32, imm_v=simm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=8281)
    set_if(cond, op_v=OP_SWI, len_v=4, srcl_v=rs1_32, srcr_v=rs2_32, imm_v=simm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=12377)
    set_if(cond, op_v=OP_SDI, len_v=4, srcl_v=rs1_32, srcr_v=rs2_32, imm_v=simm12_s64)
    cond = in32 & masked_eq(insn32, mask=32767, match=73)
    set_if(cond, op_v=OP_SB, len_v=4, srcp_v=insn32[27:32], srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=0)
    cond = in32 & masked_eq(insn32, mask=32767, match=4169)
    set_if(cond, op_v=OP_SH, len_v=4, srcp_v=insn32[27:32], srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=1)
    cond = in32 & masked_eq(insn32, mask=32767, match=8265)
    set_if(cond, op_v=OP_SW, len_v=4, srcp_v=insn32[27:32], srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=2)
    cond = in32 & masked_eq(insn32, mask=32767, match=12361)
    set_if(cond, op_v=OP_SD, len_v=4, srcp_v=insn32[27:32], srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32, shamt_v=3)
    cond = in32 & masked_eq(insn32, mask=28799, match=8217)
    set_if(cond, op_v=OP_LWI, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_s64)
    cond = in32 & masked_eq(insn32, mask=28799, match=53)
    set_if(cond, op_v=OP_ADDIW, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_u64)
    cond = in32 & masked_eq(insn32, mask=28799, match=4149)
    set_if(cond, op_v=OP_SUBIW, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_u64)
    cond = in32 & masked_eq(insn32, mask=28799, match=21)
    set_if(cond, op_v=OP_ADDI, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_u64)
    cond = in32 & masked_eq(insn32, mask=28799, match=4117)
    set_if(cond, op_v=OP_SUBI, len_v=4, regdst_v=rd32, srcl_v=rs1_32, imm_v=imm12_u64)
    cond = in32 & masked_eq(insn32, mask=4160778367, match=69)
    set_if(cond, op_v=OP_CMP_EQ, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32)
    cond = in32 & masked_eq(insn32, mask=4160778367, match=4165)
    set_if(cond, op_v=OP_CMP_NE, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32)
    cond = in32 & masked_eq(insn32, mask=4160778367, match=16453)
    set_if(cond, op_v=OP_CMP_LT, len_v=4, regdst_v=rd32, srcl_v=rs1_32, srcr_v=rs2_32, srcr_type_v=srcr_type_32)
    cond = in32 & masked_eq(insn32, mask=4043309055, match=1052715)
    set_if(cond, op_v=OP_EBREAK, len_v=4)
    cond = in32 & masked_eq(insn32, mask=127, match=7)
    set_if(cond, op_v=OP_ADDTPC, len_v=4, regdst_v=rd32, imm_v=imm20_s64.shl(amount=12))
    set_if(cond & (rd32 == 10), op_v=OP_SETRET, len_v=4, regdst_v=10, imm_v=imm20_u64.shl(amount=1))
    cond = in32 & masked_eq(insn32, mask=32767, match=16385)
    set_if(cond, op_v=OP_BSTART_STD_CALL, len_v=4, imm_v=simm17_s64.shl(amount=1))
    hl_bstart_hi12 = unsigned(pfx16[4:16])
    hl_bstart_lo17 = unsigned(insn48[31:48])
    hl_bstart_simm_hw = cat(hl_bstart_hi12, hl_bstart_lo17, c(0, width=1)).as_signed()
    hl_bstart_off = hl_bstart_simm_hw
    cond = is_hl & masked_eq(insn48, mask=2147418127, match=268501006)
    set_if(cond, op_v=OP_BSTART_STD_FALL, len_v=6)
    cond = is_hl & masked_eq(insn48, mask=2147418127, match=536936462)
    set_if(cond, op_v=OP_BSTART_STD_DIRECT, len_v=6, imm_v=hl_bstart_off)
    cond = is_hl & masked_eq(insn48, mask=2147418127, match=805371918)
    set_if(cond, op_v=OP_BSTART_STD_COND, len_v=6, imm_v=hl_bstart_off)
    cond = is_hl & masked_eq(insn48, mask=2147418127, match=1073807374)
    set_if(cond, op_v=OP_BSTART_STD_CALL, len_v=6, imm_v=hl_bstart_off)
    cond = is_hl & masked_eq(insn48, mask=8323087, match=3735566)
    hl_load_regdst = insn48[23:28]
    hl_load_simm_hi12 = unsigned(pfx16[4:16])
    hl_load_simm_lo17 = unsigned(insn48[31:48])
    hl_load_simm29 = cat(hl_load_simm_hi12, hl_load_simm_lo17).as_signed()
    hl_load_funct3 = insn48[28:31]
    set_if(cond, len_v=6, regdst_v=hl_load_regdst, imm_v=hl_load_simm29)
    set_if(cond, op_v=OP_HL_LW_PCR)
    set_if(cond & (hl_load_funct3 == 0), op_v=OP_HL_LB_PCR)
    set_if(cond & (hl_load_funct3 == 1), op_v=OP_HL_LH_PCR)
    set_if(cond & (hl_load_funct3 == 2), op_v=OP_HL_LW_PCR)
    set_if(cond & (hl_load_funct3 == 3), op_v=OP_HL_LD_PCR)
    set_if(cond & (hl_load_funct3 == 4), op_v=OP_HL_LBU_PCR)
    set_if(cond & (hl_load_funct3 == 5), op_v=OP_HL_LHU_PCR)
    set_if(cond & (hl_load_funct3 == 6), op_v=OP_HL_LWU_PCR)
    cond = is_hl & masked_eq(insn48, mask=8323087, match=6881294)
    hl_store_srcl = insn48[31:36]
    hl_store_simm_hi12 = unsigned(pfx16[4:16])
    hl_store_simm_mid5 = unsigned(insn48[23:28])
    hl_store_simm_lo12 = unsigned(insn48[36:48])
    hl_store_simm29 = cat(hl_store_simm_hi12, hl_store_simm_mid5, hl_store_simm_lo12).as_signed()
    hl_store_funct3 = insn48[28:31]
    set_if(cond, len_v=6, srcl_v=hl_store_srcl, imm_v=hl_store_simm29)
    set_if(cond, op_v=OP_HL_SW_PCR)
    set_if(cond & (hl_store_funct3 == 0), op_v=OP_HL_SB_PCR)
    set_if(cond & (hl_store_funct3 == 1), op_v=OP_HL_SH_PCR)
    set_if(cond & (hl_store_funct3 == 2), op_v=OP_HL_SW_PCR)
    set_if(cond & (hl_store_funct3 == 3), op_v=OP_HL_SD_PCR)
    cond = is_hl & masked_eq(insn48, mask=1887371279, match=538247182)
    imm_hi12 = pfx16[4:16]
    imm_lo12 = main32[20:32]
    imm24 = unsigned(imm_hi12).shl(amount=12) | unsigned(imm_lo12)
    set_if(cond, op_v=OP_ANDI, len_v=6, regdst_v=rd_hl, srcl_v=main32[15:20], imm_v=imm24.as_signed())
    cond = is_hl & masked_eq(insn48, mask=8323087, match=1507342)
    set_if(cond, op_v=OP_HL_LUI, len_v=6, regdst_v=rd_hl, imm_v=imm_hl_lui)
    return Decode(op=op, len_bytes=len_bytes, regdst=regdst, srcl=srcl, srcr=srcr, srcr_type=srcr_type, shamt=shamt, srcp=srcp, imm=imm)

@jit_inline
def decode_bundle_8B(m: Circuit, window: Wire) -> DecodeBundle:
    """Decode up to 4 sequential instructions from an 8-byte fetch window.

    Returns per-slot byte offsets (from the window base) and a total length
    suitable for advancing the fetch PC.
    """
    c = m.const
    z4 = c(0, width=4)
    b8 = c(8, width=4)
    b2 = c(2, width=4)
    win0 = window
    dec0 = decode_window(m, win0)
    len0_4 = c(0, width=4) + unsigned(dec0.len_bytes)
    off0 = z4
    v0 = ~(len0_4 == z4)
    is_macro0 = (dec0.op == OP_FENTRY) | (dec0.op == OP_FEXIT) | (dec0.op == OP_FRET_RA) | (dec0.op == OP_FRET_STK)
    sh0 = (c(0, width=6) + unsigned(len0_4)).shl(amount=3)
    win1 = lshr_var(m, win0, sh0)
    dec1 = decode_window(m, win1)
    len1_4 = c(0, width=4) + unsigned(dec1.len_bytes)
    off1 = len0_4
    rem0 = b8 - len0_4
    v1 = v0 & ~is_macro0 & rem0.uge(b2) & ~(len1_4 == z4) & len1_4.ule(rem0)
    off2 = off1 + len1_4
    sh1 = (c(0, width=6) + unsigned(off2)).shl(amount=3)
    win2 = lshr_var(m, win0, sh1)
    dec2 = decode_window(m, win2)
    len2_4 = c(0, width=4) + unsigned(dec2.len_bytes)
    rem1 = rem0 - len1_4
    v2 = v1 & rem1.uge(b2) & ~(len2_4 == z4) & len2_4.ule(rem1)
    off3 = off2 + len2_4
    sh2 = (c(0, width=6) + unsigned(off3)).shl(amount=3)
    win3 = lshr_var(m, win0, sh2)
    dec3 = decode_window(m, win3)
    len3_4 = c(0, width=4) + unsigned(dec3.len_bytes)
    rem2 = rem1 - len2_4
    v3 = v2 & rem2.uge(b2) & ~(len3_4 == z4) & len3_4.ule(rem2)
    total = len0_4
    total = off2 if v1 else total
    total = off3 if v2 else total
    total = off3 + len3_4 if v3 else total
    return DecodeBundle(valid=[v0, v1, v2, v3], off_bytes=[off0, off1, off2, off3], dec=[dec0, dec1, dec2, dec3], total_len_bytes=total)
