from __future__ import annotations
from dataclasses import dataclass
from pycircuit import Circuit, Wire, jit_inline, u, unsigned
from ..isa import OP_ADDTPC, OP_ADDI, OP_ADDIW, OP_ADD, OP_ADDW, OP_AND, OP_ANDI, OP_ANDIW, OP_ANDW, OP_BSTART_STD_CALL, OP_BSTART_STD_COND, OP_BSTART_STD_DIRECT, OP_BSTART_STD_FALL, OP_BXS, OP_BXU, OP_CMP_EQ, OP_CMP_EQI, OP_CMP_NE, OP_CMP_NEI, OP_CMP_ANDI, OP_CMP_ORI, OP_CMP_LT, OP_CMP_LTI, OP_CMP_LTUI, OP_CMP_LTU, OP_CMP_GEI, OP_CMP_GEUI, OP_C_ADD, OP_C_ADDI, OP_C_AND, OP_C_OR, OP_C_SUB, OP_CSEL, OP_C_BSTART_DIRECT, OP_C_BSTART_COND, OP_C_BSTART_STD, OP_C_LDI, OP_C_LWI, OP_C_MOVI, OP_C_MOVR, OP_C_SETC_EQ, OP_C_SETC_NE, OP_C_SETC_TGT, OP_C_SDI, OP_C_SEXT_W, OP_C_SETRET, OP_C_SWI, OP_C_ZEXT_W, OP_FENTRY, OP_FEXIT, OP_FRET_RA, OP_FRET_STK, OP_HL_LB_PCR, OP_HL_LBU_PCR, OP_HL_LD_PCR, OP_HL_LH_PCR, OP_HL_LHU_PCR, OP_HL_LW_PCR, OP_HL_LUI, OP_HL_LWU_PCR, OP_HL_SB_PCR, OP_HL_SD_PCR, OP_HL_SH_PCR, OP_HL_SW_PCR, OP_LB, OP_LBI, OP_LBU, OP_LBUI, OP_LD, OP_LH, OP_LHI, OP_LHU, OP_LHUI, OP_LDI, OP_LUI, OP_LW, OP_LWI, OP_LWU, OP_LWUI, OP_MADD, OP_MADDW, OP_MUL, OP_MULW, OP_OR, OP_ORI, OP_ORIW, OP_ORW, OP_XOR, OP_XORIW, OP_DIV, OP_DIVU, OP_DIVW, OP_DIVUW, OP_REM, OP_REMU, OP_REMW, OP_REMUW, OP_SB, OP_SETC_AND, OP_SETC_ANDI, OP_SETC_EQ, OP_SETC_EQI, OP_SETC_GE, OP_SETC_GEI, OP_SETC_GEU, OP_SETC_GEUI, OP_SETC_LT, OP_SETC_LTI, OP_SETC_LTU, OP_SETC_LTUI, OP_SETC_NE, OP_SETC_NEI, OP_SETC_OR, OP_SETC_ORI, OP_SETRET, OP_SBI, OP_SD, OP_SH, OP_SHI, OP_SDI, OP_SLL, OP_SLLI, OP_SLLIW, OP_SRL, OP_SRA, OP_SRAIW, OP_SRLIW, OP_SW, OP_SUB, OP_SUBI, OP_SUBIW, OP_SUBW, OP_SWI, OP_XORW
from ..util import Consts, ashr_var, lshr_var, shl_var

@dataclass(frozen=True)
class ExecOut:
    alu: Wire
    is_load: Wire
    is_store: Wire
    size: Wire
    addr: Wire
    wdata: Wire

def exec_uop_comb(m: Circuit, *, op: Wire, pc: Wire, imm: Wire, srcl_val: Wire, srcr_val: Wire, srcr_type: Wire, shamt: Wire, srcp_val: Wire, consts: Consts) -> ExecOut:
    """Combinational exec for python-mode builders (no `if Wire:` / no `==` on Wire).

    The OOO core builder (`ooo/linxcore.py`) executes as a normal Python helper
    during JIT compilation. That means any use of `if <Wire>:` will raise, and
    `wire == const` is Python dataclass equality (not a hardware compare).

    This function is written in "select-chain" style so it can be called from
    such helpers. It is still safe to inline in JIT mode if desired.
    """
    c = m.const
    z1 = consts.zero1
    z4 = consts.zero4
    z64 = consts.zero64
    one1 = consts.one1
    one64 = consts.one64
    sz1 = c(1, width=4)
    sz2 = c(2, width=4)
    sz4 = c(4, width=4)
    sz8 = c(8, width=4)
    pc = pc.out()
    op = op.out()
    imm = imm.out()
    srcl_val = srcl_val.out()
    srcr_val = srcr_val.out()
    srcr_type = srcr_type.out()
    shamt = shamt.out()
    srcp_val = srcp_val.out()
    op_c_bstart_std = op == OP_C_BSTART_STD
    op_c_bstart_cond = op == OP_C_BSTART_COND
    op_c_bstart_direct = op == OP_C_BSTART_DIRECT
    op_bstart_std_fall = op == OP_BSTART_STD_FALL
    op_bstart_std_direct = op == OP_BSTART_STD_DIRECT
    op_bstart_std_cond = op == OP_BSTART_STD_COND
    op_bstart_std_call = op == OP_BSTART_STD_CALL
    op_fentry = op == OP_FENTRY
    op_fexit = op == OP_FEXIT
    op_fret_ra = op == OP_FRET_RA
    op_fret_stk = op == OP_FRET_STK
    op_c_movr = op == OP_C_MOVR
    op_c_movi = op == OP_C_MOVI
    op_c_setret = op == OP_C_SETRET
    op_c_setc_eq = op == OP_C_SETC_EQ
    op_c_setc_ne = op == OP_C_SETC_NE
    op_c_setc_tgt = op == OP_C_SETC_TGT
    op_setret = op == OP_SETRET
    op_addtpc = op == OP_ADDTPC
    op_lui = op == OP_LUI
    op_add = op == OP_ADD
    op_sub = op == OP_SUB
    op_and = op == OP_AND
    op_or = op == OP_OR
    op_xor = op == OP_XOR
    op_addi = op == OP_ADDI
    op_subi = op == OP_SUBI
    op_andi = op == OP_ANDI
    op_ori = op == OP_ORI
    op_addiw = op == OP_ADDIW
    op_subiw = op == OP_SUBIW
    op_andiw = op == OP_ANDIW
    op_oriw = op == OP_ORIW
    op_xoriw = op == OP_XORIW
    op_mul = op == OP_MUL
    op_mulw = op == OP_MULW
    op_madd = op == OP_MADD
    op_maddw = op == OP_MADDW
    op_div = op == OP_DIV
    op_divu = op == OP_DIVU
    op_divw = op == OP_DIVW
    op_divuw = op == OP_DIVUW
    op_rem = op == OP_REM
    op_remu = op == OP_REMU
    op_remw = op == OP_REMW
    op_remuw = op == OP_REMUW
    op_sll = op == OP_SLL
    op_srl = op == OP_SRL
    op_sra = op == OP_SRA
    op_slli = op == OP_SLLI
    op_slliw = op == OP_SLLIW
    op_sraiw = op == OP_SRAIW
    op_srliw = op == OP_SRLIW
    op_bxs = op == OP_BXS
    op_bxu = op == OP_BXU
    op_addw = op == OP_ADDW
    op_subw = op == OP_SUBW
    op_orw = op == OP_ORW
    op_andw = op == OP_ANDW
    op_xorw = op == OP_XORW
    op_cmp_eq = op == OP_CMP_EQ
    op_cmp_ne = op == OP_CMP_NE
    op_cmp_lt = op == OP_CMP_LT
    op_cmp_eqi = op == OP_CMP_EQI
    op_cmp_nei = op == OP_CMP_NEI
    op_cmp_andi = op == OP_CMP_ANDI
    op_cmp_ori = op == OP_CMP_ORI
    op_cmp_lti = op == OP_CMP_LTI
    op_cmp_ltu = op == OP_CMP_LTU
    op_cmp_ltui = op == OP_CMP_LTUI
    op_cmp_gei = op == OP_CMP_GEI
    op_cmp_geui = op == OP_CMP_GEUI
    op_setc_geui = op == OP_SETC_GEUI
    op_setc_eq = op == OP_SETC_EQ
    op_setc_ne = op == OP_SETC_NE
    op_setc_and = op == OP_SETC_AND
    op_setc_or = op == OP_SETC_OR
    op_setc_lt = op == OP_SETC_LT
    op_setc_ltu = op == OP_SETC_LTU
    op_setc_ge = op == OP_SETC_GE
    op_setc_geu = op == OP_SETC_GEU
    op_setc_eqi = op == OP_SETC_EQI
    op_setc_nei = op == OP_SETC_NEI
    op_setc_andi = op == OP_SETC_ANDI
    op_setc_ori = op == OP_SETC_ORI
    op_setc_lti = op == OP_SETC_LTI
    op_setc_gei = op == OP_SETC_GEI
    op_setc_ltui = op == OP_SETC_LTUI
    op_csel = op == OP_CSEL
    op_hl_lui = op == OP_HL_LUI
    op_hl_lb_pcr = op == OP_HL_LB_PCR
    op_hl_lbu_pcr = op == OP_HL_LBU_PCR
    op_hl_lh_pcr = op == OP_HL_LH_PCR
    op_hl_lhu_pcr = op == OP_HL_LHU_PCR
    op_hl_lw_pcr = op == OP_HL_LW_PCR
    op_hl_lwu_pcr = op == OP_HL_LWU_PCR
    op_hl_ld_pcr = op == OP_HL_LD_PCR
    op_hl_sb_pcr = op == OP_HL_SB_PCR
    op_hl_sh_pcr = op == OP_HL_SH_PCR
    op_hl_sw_pcr = op == OP_HL_SW_PCR
    op_hl_sd_pcr = op == OP_HL_SD_PCR
    op_lwi = op == OP_LWI
    op_c_lwi = op == OP_C_LWI
    op_lbi = op == OP_LBI
    op_lbui = op == OP_LBUI
    op_lhi = op == OP_LHI
    op_lhui = op == OP_LHUI
    op_lwui = op == OP_LWUI
    op_lb = op == OP_LB
    op_lbu = op == OP_LBU
    op_lh = op == OP_LH
    op_lhu = op == OP_LHU
    op_lw = op == OP_LW
    op_lwu = op == OP_LWU
    op_ld = op == OP_LD
    op_ldi = op == OP_LDI
    op_c_add = op == OP_C_ADD
    op_c_addi = op == OP_C_ADDI
    op_c_sub = op == OP_C_SUB
    op_c_and = op == OP_C_AND
    op_c_or = op == OP_C_OR
    op_c_ldi = op == OP_C_LDI
    op_sbi = op == OP_SBI
    op_shi = op == OP_SHI
    op_swi = op == OP_SWI
    op_c_swi = op == OP_C_SWI
    op_c_sdi = op == OP_C_SDI
    op_sb = op == OP_SB
    op_sh = op == OP_SH
    op_sw = op == OP_SW
    op_sd = op == OP_SD
    op_c_sext_w = op == OP_C_SEXT_W
    op_c_zext_w = op == OP_C_ZEXT_W
    op_sdi = op == OP_SDI
    alu = z64
    is_load = z1
    is_store = z1
    size = z4
    addr = z64
    wdata = z64
    st0 = srcr_type == 0
    st1 = srcr_type == 1
    st2 = srcr_type == 2
    srcr_addsub = srcr_val
    srcr_addsub = srcr_val[0:32].as_signed() if st0 else srcr_addsub
    srcr_addsub = unsigned(srcr_val[0:32]) if st1 else srcr_addsub
    srcr_addsub = ~srcr_val + 1 if st2 else srcr_addsub
    srcr_logic = srcr_val
    srcr_logic = srcr_val[0:32].as_signed() if st0 else srcr_logic
    srcr_logic = unsigned(srcr_val[0:32]) if st1 else srcr_logic
    srcr_logic = ~srcr_val if st2 else srcr_logic
    srcr_addsub_nosh = srcr_addsub
    srcr_addsub_shl = shl_var(m, srcr_addsub, shamt)
    srcr_logic_shl = shl_var(m, srcr_logic, shamt)
    idx_mod = unsigned(srcr_val[0:32])
    idx_mod = srcr_val[0:32].as_signed() if st0 else idx_mod
    idx_mod_shl = shl_var(m, idx_mod, shamt)
    off_w = imm.shl(amount=2)
    h_off = imm.shl(amount=1)
    ldi_off = imm.shl(amount=3)
    is_marker = op_c_bstart_std | op_c_bstart_cond | op_c_bstart_direct | op_bstart_std_fall | op_bstart_std_direct | op_bstart_std_cond | op_bstart_std_call | op_fentry | op_fexit | op_fret_ra | op_fret_stk
    alu = imm if is_marker else alu
    alu = srcl_val if op_c_movr else alu
    alu = imm if op_c_movi else alu
    alu = pc + imm if op_c_setret else alu
    setc_eq = one64 if srcl_val == srcr_val else z64
    alu = setc_eq if op_c_setc_eq else alu
    alu = (one64 if ~(srcl_val == srcr_val) else z64) if op_c_setc_ne else alu
    alu = srcl_val if op_c_setc_tgt else alu
    pc_page = pc & 18446744073709547520
    alu = pc_page + imm if op_addtpc else alu
    alu = srcl_val + imm if op_addi else alu
    alu = srcl_val - imm if op_subi else alu
    addiw = (srcl_val[0:32] + imm[0:32]).as_signed()
    subiw = (srcl_val[0:32] - imm[0:32]).as_signed()
    alu = addiw if op_addiw else alu
    alu = subiw if op_subiw else alu
    alu = imm if op_lui else alu
    alu = pc + imm if op_setret else alu
    alu = srcl_val + srcr_addsub_shl if op_add else alu
    alu = srcl_val - srcr_addsub_shl if op_sub else alu
    alu = srcl_val & srcr_logic_shl if op_and else alu
    alu = srcl_val | srcr_logic_shl if op_or else alu
    alu = srcl_val ^ srcr_logic_shl if op_xor else alu
    alu = srcl_val & imm if op_andi else alu
    alu = srcl_val | imm if op_ori else alu
    alu = (srcl_val & imm)[0:32].as_signed() if op_andiw else alu
    alu = (srcl_val | imm)[0:32].as_signed() if op_oriw else alu
    alu = (srcl_val ^ imm)[0:32].as_signed() if op_xoriw else alu
    alu = srcl_val * srcr_val if op_mul else alu
    alu = (srcl_val * srcr_val)[0:32].as_signed() if op_mulw else alu
    alu = srcp_val + srcl_val * srcr_val if op_madd else alu
    alu = (srcp_val + srcl_val * srcr_val)[0:32].as_signed() if op_maddw else alu
    alu = srcl_val.as_signed() // srcr_val.as_signed() if op_div else alu
    alu = unsigned(srcl_val) // unsigned(srcr_val) if op_divu else alu
    divw_l32 = srcl_val[0:32].as_signed().as_signed()
    divw_r32 = srcr_val[0:32].as_signed().as_signed()
    alu = (divw_l32 // divw_r32)[0:32].as_signed() if op_divw else alu
    divuw_l32 = unsigned(unsigned(srcl_val[0:32]))
    divuw_r32 = unsigned(unsigned(srcr_val[0:32]))
    alu = (divuw_l32 // divuw_r32)[0:32].as_signed() if op_divuw else alu
    alu = srcl_val.as_signed() % srcr_val.as_signed() if op_rem else alu
    alu = unsigned(srcl_val) % unsigned(srcr_val) if op_remu else alu
    remw_l32 = srcl_val[0:32].as_signed().as_signed()
    remw_r32 = srcr_val[0:32].as_signed().as_signed()
    alu = (remw_l32 % remw_r32)[0:32].as_signed() if op_remw else alu
    remuw_l32 = unsigned(unsigned(srcl_val[0:32]))
    remuw_r32 = unsigned(unsigned(srcr_val[0:32]))
    alu = (remuw_l32 % remuw_r32)[0:32].as_signed() if op_remuw else alu
    alu = shl_var(m, srcl_val, srcr_val) if op_sll else alu
    alu = lshr_var(m, srcl_val, srcr_val) if op_srl else alu
    alu = ashr_var(m, srcl_val, srcr_val) if op_sra else alu
    alu = shl_var(m, srcl_val, shamt) if op_slli else alu
    sh5 = shamt & 31
    slliw_val = shl_var(m, unsigned(srcl_val[0:32]), sh5)[0:32].as_signed()
    sraiw_val = ashr_var(m, srcl_val[0:32].as_signed(), sh5)[0:32].as_signed()
    srliw_val = lshr_var(m, unsigned(srcl_val[0:32]), sh5)[0:32].as_signed()
    alu = slliw_val if op_slliw else alu
    alu = sraiw_val if op_sraiw else alu
    alu = srliw_val if op_srliw else alu
    imms = srcr_val
    imml = srcp_val
    shifted = lshr_var(m, srcl_val, imms)
    sh_mask_amt = c(63, width=64) - unsigned(imml)
    mask = lshr_var(m, c(18446744073709551615, width=64), sh_mask_amt)
    extracted = shifted & mask
    valid_bx = (unsigned(imms) + unsigned(imml)).ule(63)
    sext_bxs = ashr_var(m, shl_var(m, extracted, sh_mask_amt), sh_mask_amt)
    alu = (sext_bxs if valid_bx else z64) if op_bxs else alu
    alu = (extracted if valid_bx else z64) if op_bxu else alu
    addw = (srcl_val + srcr_addsub_shl)[0:32].as_signed()
    subw = (srcl_val - srcr_addsub_shl)[0:32].as_signed()
    orw = (srcl_val | srcr_logic_shl)[0:32].as_signed()
    andw = (srcl_val & srcr_logic_shl)[0:32].as_signed()
    xorw = (srcl_val ^ srcr_logic_shl)[0:32].as_signed()
    alu = addw if op_addw else alu
    alu = subw if op_subw else alu
    alu = orw if op_orw else alu
    alu = andw if op_andw else alu
    alu = xorw if op_xorw else alu
    alu = (one64 if srcl_val == srcr_addsub_nosh else z64) if op_cmp_eq else alu
    alu = (one64 if ~(srcl_val == srcr_addsub_nosh) else z64) if op_cmp_ne else alu
    alu = (one64 if srcl_val.slt(srcr_addsub_nosh) else z64) if op_cmp_lt else alu
    alu = (one64 if srcl_val == imm else z64) if op_cmp_eqi else alu
    alu = (one64 if ~(srcl_val == imm) else z64) if op_cmp_nei else alu
    alu = (one64 if ~(srcl_val & imm == 0) else z64) if op_cmp_andi else alu
    alu = (one64 if ~(srcl_val | imm == 0) else z64) if op_cmp_ori else alu
    alu = (one64 if srcl_val.slt(imm) else z64) if op_cmp_lti else alu
    alu = (one64 if ~srcl_val.slt(imm) else z64) if op_cmp_gei else alu
    alu = (one64 if srcl_val.ult(srcr_addsub_nosh) else z64) if op_cmp_ltu else alu
    alu = (one64 if srcl_val.ult(imm) else z64) if op_cmp_ltui else alu
    alu = (one64 if srcl_val.uge(imm) else z64) if op_cmp_geui else alu
    uimm_sh = shl_var(m, imm, shamt)
    simm_sh = uimm_sh
    alu = (one64 if srcl_val.uge(uimm_sh) else z64) if op_setc_geui else alu
    alu = (one64 if srcl_val == simm_sh else z64) if op_setc_eqi else alu
    alu = (one64 if ~(srcl_val == simm_sh) else z64) if op_setc_nei else alu
    alu = (one64 if ~(srcl_val & simm_sh == 0) else z64) if op_setc_andi else alu
    alu = (one64 if ~(srcl_val | simm_sh == 0) else z64) if op_setc_ori else alu
    alu = (one64 if srcl_val.slt(simm_sh) else z64) if op_setc_lti else alu
    alu = (one64 if ~srcl_val.slt(simm_sh) else z64) if op_setc_gei else alu
    alu = (one64 if srcl_val.ult(uimm_sh) else z64) if op_setc_ltui else alu
    alu = (one64 if srcl_val == srcr_addsub_nosh else z64) if op_setc_eq else alu
    alu = (one64 if ~(srcl_val == srcr_addsub_nosh) else z64) if op_setc_ne else alu
    alu = (one64 if ~(srcl_val & srcr_logic == 0) else z64) if op_setc_and else alu
    alu = (one64 if ~(srcl_val | srcr_logic == 0) else z64) if op_setc_or else alu
    alu = (one64 if srcl_val.slt(srcr_addsub_nosh) else z64) if op_setc_lt else alu
    alu = (one64 if srcl_val.ult(srcr_addsub_nosh) else z64) if op_setc_ltu else alu
    alu = (one64 if ~srcl_val.slt(srcr_addsub_nosh) else z64) if op_setc_ge else alu
    alu = (one64 if srcl_val.uge(srcr_addsub_nosh) else z64) if op_setc_geu else alu
    alu = imm if op_hl_lui else alu
    alu = (srcr_addsub_nosh if ~(srcp_val == 0) else srcl_val) if op_csel else alu
    is_lwi = op_lwi | op_c_lwi
    lwi_addr = srcl_val + off_w
    is_load = one1 if is_lwi else is_load
    size = sz4 if is_lwi else size
    addr = lwi_addr if is_lwi else addr
    is_load = one1 if op_lwui else is_load
    size = sz4 if op_lwui else size
    addr = lwi_addr if op_lwui else addr
    is_lbi_any = op_lbi | op_lbui
    is_load = one1 if is_lbi_any else is_load
    size = sz1 if is_lbi_any else size
    addr = srcl_val + imm if is_lbi_any else addr
    is_lhi_any = op_lhi | op_lhui
    is_load = one1 if is_lhi_any else is_load
    size = sz2 if is_lhi_any else size
    addr = srcl_val + h_off if is_lhi_any else addr
    idx_addr = srcl_val + idx_mod_shl
    is_load = one1 if op_lb | op_lbu else is_load
    size = sz1 if op_lb | op_lbu else size
    addr = idx_addr if op_lb | op_lbu else addr
    is_load = one1 if op_lh | op_lhu else is_load
    size = sz2 if op_lh | op_lhu else size
    addr = idx_addr if op_lh | op_lhu else addr
    is_load = one1 if op_lw | op_lwu else is_load
    size = sz4 if op_lw | op_lwu else size
    addr = idx_addr if op_lw | op_lwu else addr
    is_load = one1 if op_ld else is_load
    size = sz8 if op_ld else size
    addr = idx_addr if op_ld else addr
    is_load = one1 if op_ldi | op_c_ldi else is_load
    size = sz8 if op_ldi | op_c_ldi else size
    addr = srcl_val + ldi_off if op_ldi | op_c_ldi else addr
    is_store = one1 if op_sbi else is_store
    size = sz1 if op_sbi else size
    addr = srcr_val + imm if op_sbi else addr
    wdata = srcl_val if op_sbi else wdata
    is_store = one1 if op_shi else is_store
    size = sz2 if op_shi else size
    addr = srcr_val + h_off if op_shi else addr
    wdata = srcl_val if op_shi else wdata
    store_addr_def = srcl_val + off_w
    store_data_def = srcr_val
    store_addr = srcr_val + off_w if op_swi else store_addr_def
    store_data = srcl_val if op_swi else store_data_def
    op_swi_any = op_swi | op_c_swi
    is_store = one1 if op_swi_any else is_store
    size = sz4 if op_swi_any else size
    addr = store_addr if op_swi_any else addr
    wdata = store_data if op_swi_any else wdata
    is_store = one1 if op_sb else is_store
    size = sz1 if op_sb else size
    addr = idx_addr if op_sb else addr
    wdata = srcp_val if op_sb else wdata
    is_store = one1 if op_sh else is_store
    size = sz2 if op_sh else size
    addr = idx_addr if op_sh else addr
    wdata = srcp_val if op_sh else wdata
    is_store = one1 if op_sw else is_store
    size = sz4 if op_sw else size
    addr = idx_addr if op_sw else addr
    wdata = srcp_val if op_sw else wdata
    is_store = one1 if op_sd else is_store
    size = sz8 if op_sd else size
    addr = idx_addr if op_sd else addr
    wdata = srcp_val if op_sd else wdata
    sdi_off = imm.shl(amount=3)
    is_store = one1 if op_c_sdi else is_store
    size = sz8 if op_c_sdi else size
    addr = srcl_val + sdi_off if op_c_sdi else addr
    wdata = srcr_val if op_c_sdi else wdata
    sdi_addr = srcr_val + sdi_off
    is_store = one1 if op_sdi else is_store
    size = sz8 if op_sdi else size
    addr = sdi_addr if op_sdi else addr
    wdata = srcl_val if op_sdi else wdata
    hl_addr = pc + imm
    hl_load_b = op_hl_lb_pcr | op_hl_lbu_pcr
    hl_load_h = op_hl_lh_pcr | op_hl_lhu_pcr
    hl_load_w = op_hl_lw_pcr | op_hl_lwu_pcr
    is_load = one1 if hl_load_b else is_load
    size = sz1 if hl_load_b else size
    addr = hl_addr if hl_load_b else addr
    is_load = one1 if hl_load_h else is_load
    size = sz2 if hl_load_h else size
    addr = hl_addr if hl_load_h else addr
    is_load = one1 if hl_load_w else is_load
    size = sz4 if hl_load_w else size
    addr = hl_addr if hl_load_w else addr
    is_load = one1 if op_hl_ld_pcr else is_load
    size = sz8 if op_hl_ld_pcr else size
    addr = hl_addr if op_hl_ld_pcr else addr
    is_store = one1 if op_hl_sb_pcr else is_store
    size = sz1 if op_hl_sb_pcr else size
    addr = hl_addr if op_hl_sb_pcr else addr
    wdata = srcl_val if op_hl_sb_pcr else wdata
    is_store = one1 if op_hl_sh_pcr else is_store
    size = sz2 if op_hl_sh_pcr else size
    addr = hl_addr if op_hl_sh_pcr else addr
    wdata = srcl_val if op_hl_sh_pcr else wdata
    is_store = one1 if op_hl_sw_pcr else is_store
    size = sz4 if op_hl_sw_pcr else size
    addr = hl_addr if op_hl_sw_pcr else addr
    wdata = srcl_val if op_hl_sw_pcr else wdata
    is_store = one1 if op_hl_sd_pcr else is_store
    size = sz8 if op_hl_sd_pcr else size
    addr = hl_addr if op_hl_sd_pcr else addr
    wdata = srcl_val if op_hl_sd_pcr else wdata
    alu = srcl_val + imm if op_c_addi else alu
    alu = srcl_val + srcr_val if op_c_add else alu
    alu = srcl_val - srcr_val if op_c_sub else alu
    alu = srcl_val & srcr_val if op_c_and else alu
    alu = srcl_val | srcr_val if op_c_or else alu
    alu = srcl_val[0:32].as_signed() if op_c_sext_w else alu
    alu = unsigned(srcl_val[0:32]) if op_c_zext_w else alu
    return ExecOut(alu=alu, is_load=is_load, is_store=is_store, size=size, addr=addr, wdata=wdata)

@jit_inline
def exec_uop(m: Circuit, *, op: Wire, pc: Wire, imm: Wire, srcl_val: Wire, srcr_val: Wire, srcr_type: Wire, shamt: Wire, srcp_val: Wire, consts: Consts) -> ExecOut:
    with m.scope('exec'):
        z1 = consts.zero1
        z4 = consts.zero4
        z64 = consts.zero64
        pc = pc.out()
        op = op.out()
        imm = imm.out()
        srcl_val = srcl_val.out()
        srcr_val = srcr_val.out()
        srcr_type = srcr_type.out()
        shamt = shamt.out()
        srcp_val = srcp_val.out()
        op_c_bstart_std = op == OP_C_BSTART_STD
        op_c_bstart_cond = op == OP_C_BSTART_COND
        op_c_bstart_direct = op == OP_C_BSTART_DIRECT
        op_bstart_std_fall = op == OP_BSTART_STD_FALL
        op_bstart_std_direct = op == OP_BSTART_STD_DIRECT
        op_bstart_std_cond = op == OP_BSTART_STD_COND
        op_bstart_std_call = op == OP_BSTART_STD_CALL
        op_fentry = op == OP_FENTRY
        op_fexit = op == OP_FEXIT
        op_fret_ra = op == OP_FRET_RA
        op_fret_stk = op == OP_FRET_STK
        op_c_movr = op == OP_C_MOVR
        op_c_movi = op == OP_C_MOVI
        op_c_setret = op == OP_C_SETRET
        op_c_setc_eq = op == OP_C_SETC_EQ
        op_c_setc_ne = op == OP_C_SETC_NE
        op_c_setc_tgt = op == OP_C_SETC_TGT
        op_setret = op == OP_SETRET
        op_addtpc = op == OP_ADDTPC
        op_lui = op == OP_LUI
        op_add = op == OP_ADD
        op_sub = op == OP_SUB
        op_and = op == OP_AND
        op_or = op == OP_OR
        op_xor = op == OP_XOR
        op_addi = op == OP_ADDI
        op_subi = op == OP_SUBI
        op_andi = op == OP_ANDI
        op_ori = op == OP_ORI
        op_addiw = op == OP_ADDIW
        op_subiw = op == OP_SUBIW
        op_andiw = op == OP_ANDIW
        op_oriw = op == OP_ORIW
        op_xoriw = op == OP_XORIW
        op_mul = op == OP_MUL
        op_mulw = op == OP_MULW
        op_madd = op == OP_MADD
        op_maddw = op == OP_MADDW
        op_div = op == OP_DIV
        op_divu = op == OP_DIVU
        op_divw = op == OP_DIVW
        op_divuw = op == OP_DIVUW
        op_rem = op == OP_REM
        op_remu = op == OP_REMU
        op_remw = op == OP_REMW
        op_remuw = op == OP_REMUW
        op_sll = op == OP_SLL
        op_srl = op == OP_SRL
        op_sra = op == OP_SRA
        op_slli = op == OP_SLLI
        op_slliw = op == OP_SLLIW
        op_sraiw = op == OP_SRAIW
        op_srliw = op == OP_SRLIW
        op_bxs = op == OP_BXS
        op_bxu = op == OP_BXU
        op_addw = op == OP_ADDW
        op_subw = op == OP_SUBW
        op_orw = op == OP_ORW
        op_andw = op == OP_ANDW
        op_xorw = op == OP_XORW
        op_cmp_eq = op == OP_CMP_EQ
        op_cmp_ne = op == OP_CMP_NE
        op_cmp_lt = op == OP_CMP_LT
        op_cmp_eqi = op == OP_CMP_EQI
        op_cmp_nei = op == OP_CMP_NEI
        op_cmp_andi = op == OP_CMP_ANDI
        op_cmp_ori = op == OP_CMP_ORI
        op_cmp_lti = op == OP_CMP_LTI
        op_cmp_ltu = op == OP_CMP_LTU
        op_cmp_ltui = op == OP_CMP_LTUI
        op_cmp_gei = op == OP_CMP_GEI
        op_cmp_geui = op == OP_CMP_GEUI
        op_setc_geui = op == OP_SETC_GEUI
        op_setc_eq = op == OP_SETC_EQ
        op_setc_ne = op == OP_SETC_NE
        op_setc_and = op == OP_SETC_AND
        op_setc_or = op == OP_SETC_OR
        op_setc_lt = op == OP_SETC_LT
        op_setc_ltu = op == OP_SETC_LTU
        op_setc_ge = op == OP_SETC_GE
        op_setc_geu = op == OP_SETC_GEU
        op_setc_eqi = op == OP_SETC_EQI
        op_setc_nei = op == OP_SETC_NEI
        op_setc_andi = op == OP_SETC_ANDI
        op_setc_ori = op == OP_SETC_ORI
        op_setc_lti = op == OP_SETC_LTI
        op_setc_gei = op == OP_SETC_GEI
        op_setc_ltui = op == OP_SETC_LTUI
        op_csel = op == OP_CSEL
        op_hl_lui = op == OP_HL_LUI
        op_hl_lb_pcr = op == OP_HL_LB_PCR
        op_hl_lbu_pcr = op == OP_HL_LBU_PCR
        op_hl_lh_pcr = op == OP_HL_LH_PCR
        op_hl_lhu_pcr = op == OP_HL_LHU_PCR
        op_hl_lw_pcr = op == OP_HL_LW_PCR
        op_hl_lwu_pcr = op == OP_HL_LWU_PCR
        op_hl_ld_pcr = op == OP_HL_LD_PCR
        op_hl_sb_pcr = op == OP_HL_SB_PCR
        op_hl_sh_pcr = op == OP_HL_SH_PCR
        op_hl_sw_pcr = op == OP_HL_SW_PCR
        op_hl_sd_pcr = op == OP_HL_SD_PCR
        op_lwi = op == OP_LWI
        op_c_lwi = op == OP_C_LWI
        op_lbi = op == OP_LBI
        op_lbui = op == OP_LBUI
        op_lhi = op == OP_LHI
        op_lhui = op == OP_LHUI
        op_lwui = op == OP_LWUI
        op_lb = op == OP_LB
        op_lbu = op == OP_LBU
        op_lh = op == OP_LH
        op_lhu = op == OP_LHU
        op_lw = op == OP_LW
        op_lwu = op == OP_LWU
        op_ld = op == OP_LD
        op_ldi = op == OP_LDI
        op_c_add = op == OP_C_ADD
        op_c_addi = op == OP_C_ADDI
        op_c_sub = op == OP_C_SUB
        op_c_and = op == OP_C_AND
        op_c_or = op == OP_C_OR
        op_c_ldi = op == OP_C_LDI
        op_sbi = op == OP_SBI
        op_shi = op == OP_SHI
        op_swi = op == OP_SWI
        op_c_swi = op == OP_C_SWI
        op_c_sdi = op == OP_C_SDI
        op_sb = op == OP_SB
        op_sh = op == OP_SH
        op_sw = op == OP_SW
        op_sd = op == OP_SD
        op_c_sext_w = op == OP_C_SEXT_W
        op_c_zext_w = op == OP_C_ZEXT_W
        op_sdi = op == OP_SDI
        off = imm.shl(amount=2)
        alu = z64
        is_load = z1
        is_store = z1
        size = z4
        addr = z64
        wdata = z64
        srcr_addsub = srcr_val
        if srcr_type == 0:
            srcr_addsub = srcr_val[0:32].as_signed()
        if srcr_type == 1:
            srcr_addsub = unsigned(srcr_val[0:32])
        if srcr_type == 2:
            srcr_addsub = ~srcr_val + 1
        srcr_logic = srcr_val
        if srcr_type == 0:
            srcr_logic = srcr_val[0:32].as_signed()
        if srcr_type == 1:
            srcr_logic = unsigned(srcr_val[0:32])
        if srcr_type == 2:
            srcr_logic = ~srcr_val
        srcr_addsub_shl = shl_var(m, srcr_addsub, shamt)
        srcr_logic_shl = shl_var(m, srcr_logic, shamt)
        idx_mod = unsigned(srcr_val[0:32])
        if srcr_type == 0:
            idx_mod = srcr_val[0:32].as_signed()
        idx_mod_shl = shl_var(m, idx_mod, shamt)
        if op_c_bstart_std | op_c_bstart_cond | op_c_bstart_direct | op_bstart_std_fall | op_bstart_std_direct | op_bstart_std_cond | op_bstart_std_call | op_fentry | op_fexit | op_fret_ra | op_fret_stk:
            alu = imm
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_c_movr:
            alu = srcl_val
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_c_movi:
            alu = imm
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_c_setret:
            alu = pc + imm
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        setc_eq = z64
        if srcl_val == srcr_val:
            setc_eq = 1
        if op_c_setc_eq:
            alu = setc_eq
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_c_setc_tgt:
            alu = srcl_val
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        pc_page = pc & 18446744073709547520
        if op_addtpc:
            alu = pc_page + imm
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_addi:
            alu = srcl_val + imm
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        subi = srcl_val + (~imm + 1)
        if op_subi:
            alu = subi
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        addiw = (srcl_val[0:32] + imm[0:32]).as_signed()
        if op_addiw:
            alu = addiw
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        subiw = (srcl_val[0:32] - imm[0:32]).as_signed()
        if op_subiw:
            alu = subiw
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_lui:
            alu = imm
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_setret:
            alu = pc + imm
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_add:
            alu = srcl_val + srcr_addsub_shl
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_sub:
            alu = srcl_val - srcr_addsub_shl
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_and:
            alu = srcl_val & srcr_logic_shl
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_or:
            alu = srcl_val | srcr_logic_shl
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_xor:
            alu = srcl_val ^ srcr_logic_shl
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_andi:
            alu = srcl_val & imm
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_ori:
            alu = srcl_val | imm
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_andiw:
            alu = (srcl_val & imm)[0:32].as_signed()
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_oriw:
            alu = (srcl_val | imm)[0:32].as_signed()
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_xoriw:
            alu = (srcl_val ^ imm)[0:32].as_signed()
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_mul:
            alu = srcl_val * srcr_val
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_mulw:
            alu = (srcl_val * srcr_val)[0:32].as_signed()
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_madd:
            alu = srcp_val + srcl_val * srcr_val
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_maddw:
            alu = (srcp_val + srcl_val * srcr_val)[0:32].as_signed()
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_div:
            alu = srcl_val.as_signed() // srcr_val.as_signed()
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_divu:
            alu = unsigned(srcl_val) // unsigned(srcr_val)
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_divw:
            l32 = srcl_val[0:32].as_signed().as_signed()
            r32 = srcr_val[0:32].as_signed().as_signed()
            alu = (l32 // r32)[0:32].as_signed()
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_divuw:
            l32 = unsigned(unsigned(srcl_val[0:32]))
            r32 = unsigned(unsigned(srcr_val[0:32]))
            alu = (l32 // r32)[0:32].as_signed()
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_rem:
            alu = srcl_val.as_signed() % srcr_val.as_signed()
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_remu:
            alu = unsigned(srcl_val) % unsigned(srcr_val)
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_remw:
            l32 = srcl_val[0:32].as_signed().as_signed()
            r32 = srcr_val[0:32].as_signed().as_signed()
            alu = (l32 % r32)[0:32].as_signed()
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_remuw:
            l32 = unsigned(unsigned(srcl_val[0:32]))
            r32 = unsigned(unsigned(srcr_val[0:32]))
            alu = (l32 % r32)[0:32].as_signed()
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_sll:
            alu = shl_var(m, srcl_val, srcr_val)
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_srl:
            alu = lshr_var(m, srcl_val, srcr_val)
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_sra:
            alu = ashr_var(m, srcl_val, srcr_val)
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_slli:
            alu = shl_var(m, srcl_val, shamt)
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_slliw:
            l32 = unsigned(srcl_val[0:32])
            sh5 = shamt & 31
            shifted = shl_var(m, l32, sh5)
            alu = shifted[0:32].as_signed()
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_sraiw:
            l32 = srcl_val[0:32].as_signed()
            sh5 = shamt & 31
            shifted = ashr_var(m, l32, sh5)
            alu = shifted[0:32].as_signed()
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_srliw:
            l32 = unsigned(srcl_val[0:32])
            sh5 = shamt & 31
            shifted = lshr_var(m, l32, sh5)
            alu = shifted[0:32].as_signed()
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_bxs:
            imms = srcr_val
            imml = srcp_val
            shifted = lshr_var(m, srcl_val, imms)
            sh_mask_amt = u(64, 63) - unsigned(imml)
            mask = lshr_var(m, u(64, 18446744073709551615), sh_mask_amt)
            extracted = shifted & mask
            valid = (unsigned(imms) + unsigned(imml)).ule(63)
            sh_ext = sh_mask_amt
            sext = ashr_var(m, shl_var(m, extracted, sh_ext), sh_ext)
            alu = sext if valid else z64
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_bxu:
            imms = srcr_val
            imml = srcp_val
            shifted = lshr_var(m, srcl_val, imms)
            sh_mask_amt = u(64, 63) - unsigned(imml)
            mask = lshr_var(m, u(64, 18446744073709551615), sh_mask_amt)
            extracted = shifted & mask
            valid = (unsigned(imms) + unsigned(imml)).ule(63)
            alu = extracted if valid else z64
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        addw = (srcl_val + srcr_addsub_shl)[0:32].as_signed()
        subw = (srcl_val - srcr_addsub_shl)[0:32].as_signed()
        orw = (srcl_val | srcr_logic_shl)[0:32].as_signed()
        andw = (srcl_val & srcr_logic_shl)[0:32].as_signed()
        xorw = (srcl_val ^ srcr_logic_shl)[0:32].as_signed()
        if op_addw:
            alu = addw
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_subw:
            alu = subw
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_orw:
            alu = orw
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_andw:
            alu = andw
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_xorw:
            alu = xorw
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        srcr_addsub_nosh = srcr_addsub
        cmp = z64
        if srcl_val == srcr_addsub_nosh:
            cmp = 1
        if op_cmp_eq:
            alu = cmp
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        cmp_ne = z64
        if srcl_val != srcr_addsub_nosh:
            cmp_ne = 1
        if op_cmp_ne:
            alu = cmp_ne
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        cmp_lt = z64
        if srcl_val.slt(srcr_addsub_nosh):
            cmp_lt = 1
        if op_cmp_lt:
            alu = cmp_lt
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        cmp_eqi = z64
        if srcl_val == imm:
            cmp_eqi = 1
        if op_cmp_eqi:
            alu = cmp_eqi
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        cmp_nei = z64
        if srcl_val != imm:
            cmp_nei = 1
        if op_cmp_nei:
            alu = cmp_nei
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        cmp_andi = z64
        if srcl_val & imm != 0:
            cmp_andi = 1
        if op_cmp_andi:
            alu = cmp_andi
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        cmp_ori = z64
        if srcl_val | imm != 0:
            cmp_ori = 1
        if op_cmp_ori:
            alu = cmp_ori
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        cmp_lti = z64
        if srcl_val.slt(imm):
            cmp_lti = 1
        if op_cmp_lti:
            alu = cmp_lti
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        cmp_gei = z64
        if ~srcl_val.slt(imm):
            cmp_gei = 1
        if op_cmp_gei:
            alu = cmp_gei
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        cmp_ltu = z64
        if srcl_val.ult(srcr_addsub_nosh):
            cmp_ltu = 1
        if op_cmp_ltu:
            alu = cmp_ltu
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        cmp_ltui = z64
        if srcl_val.ult(imm):
            cmp_ltui = 1
        if op_cmp_ltui:
            alu = cmp_ltui
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        cmp_geui = z64
        if srcl_val.uge(imm):
            cmp_geui = 1
        if op_cmp_geui:
            alu = cmp_geui
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_setc_geui:
            setc_bit = z64
            uimm = shl_var(m, imm, shamt)
            if srcl_val.uge(uimm):
                setc_bit = 1
            alu = setc_bit
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_setc_eqi:
            setc_bit = z64
            simm = shl_var(m, imm, shamt)
            if srcl_val == simm:
                setc_bit = 1
            alu = setc_bit
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_setc_nei:
            setc_bit = z64
            simm = shl_var(m, imm, shamt)
            if srcl_val != simm:
                setc_bit = 1
            alu = setc_bit
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_setc_andi:
            setc_bit = z64
            simm = shl_var(m, imm, shamt)
            if srcl_val & simm != 0:
                setc_bit = 1
            alu = setc_bit
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_setc_ori:
            setc_bit = z64
            simm = shl_var(m, imm, shamt)
            if srcl_val | simm != 0:
                setc_bit = 1
            alu = setc_bit
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_setc_lti:
            setc_bit = z64
            simm = shl_var(m, imm, shamt)
            if srcl_val.slt(simm):
                setc_bit = 1
            alu = setc_bit
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_setc_gei:
            setc_bit = z64
            simm = shl_var(m, imm, shamt)
            if ~srcl_val.slt(simm):
                setc_bit = 1
            alu = setc_bit
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_setc_ltui:
            setc_bit = z64
            uimm = shl_var(m, imm, shamt)
            if srcl_val.ult(uimm):
                setc_bit = 1
            alu = setc_bit
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_setc_eq:
            setc_bit = z64
            if srcl_val == srcr_addsub_nosh:
                setc_bit = 1
            alu = setc_bit
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_setc_ne:
            setc_bit = z64
            if srcl_val != srcr_addsub_nosh:
                setc_bit = 1
            alu = setc_bit
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_setc_and:
            setc_bit = z64
            if srcl_val & srcr_logic != 0:
                setc_bit = 1
            alu = setc_bit
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_setc_or:
            setc_bit = z64
            if srcl_val | srcr_logic != 0:
                setc_bit = 1
            alu = setc_bit
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_setc_lt:
            setc_bit = z64
            if srcl_val.slt(srcr_addsub_nosh):
                setc_bit = 1
            alu = setc_bit
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_setc_ltu:
            setc_bit = z64
            if srcl_val.ult(srcr_addsub_nosh):
                setc_bit = 1
            alu = setc_bit
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_setc_ge:
            setc_bit = z64
            if ~srcl_val.slt(srcr_addsub_nosh):
                setc_bit = 1
            alu = setc_bit
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_setc_geu:
            setc_bit = z64
            if srcl_val.uge(srcr_addsub_nosh):
                setc_bit = 1
            alu = setc_bit
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_c_setc_ne:
            setc_bit = z64
            if srcl_val != srcr_val:
                setc_bit = 1
            alu = setc_bit
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_hl_lui:
            alu = imm
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        csel_srcr = srcr_addsub_nosh
        csel_val = srcl_val
        if srcp_val != 0:
            csel_val = csel_srcr
        if op_csel:
            alu = csel_val
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        is_lwi = op_lwi | op_c_lwi
        lwi_addr = srcl_val + off
        if is_lwi:
            alu = z64
            is_load = 1
            is_store = z1
            size = 4
            addr = lwi_addr
            wdata = z64
        if op_lwui:
            alu = z64
            is_load = 1
            is_store = z1
            size = 4
            addr = lwi_addr
            wdata = z64
        if op_lbi | op_lbui:
            alu = z64
            is_load = 1
            is_store = z1
            size = 1
            addr = srcl_val + imm
            wdata = z64
        h_off = imm.shl(amount=1)
        if op_lhi | op_lhui:
            alu = z64
            is_load = 1
            is_store = z1
            size = 2
            addr = srcl_val + h_off
            wdata = z64
        idx_addr = srcl_val + idx_mod_shl
        if op_lb:
            alu = z64
            is_load = 1
            is_store = z1
            size = 1
            addr = idx_addr
            wdata = z64
        if op_lbu:
            alu = z64
            is_load = 1
            is_store = z1
            size = 1
            addr = idx_addr
            wdata = z64
        if op_lh:
            alu = z64
            is_load = 1
            is_store = z1
            size = 2
            addr = idx_addr
            wdata = z64
        if op_lhu:
            alu = z64
            is_load = 1
            is_store = z1
            size = 2
            addr = idx_addr
            wdata = z64
        if op_lw:
            alu = z64
            is_load = 1
            is_store = z1
            size = 4
            addr = idx_addr
            wdata = z64
        if op_lwu:
            alu = z64
            is_load = 1
            is_store = z1
            size = 4
            addr = idx_addr
            wdata = z64
        if op_ld:
            alu = z64
            is_load = 1
            is_store = z1
            size = 8
            addr = idx_addr
            wdata = z64
        ldi_off = imm.shl(amount=3)
        if op_ldi | op_c_ldi:
            alu = z64
            is_load = 1
            is_store = z1
            size = 8
            addr = srcl_val + ldi_off
            wdata = z64
        if op_sbi:
            alu = z64
            is_load = z1
            is_store = 1
            size = 1
            addr = srcr_val + imm
            wdata = srcl_val
        if op_shi:
            alu = z64
            is_load = z1
            is_store = 1
            size = 2
            addr = srcr_val + h_off
            wdata = srcl_val
        store_addr = srcl_val + off
        store_data = srcr_val
        if op_swi:
            store_addr = srcr_val + off
            store_data = srcl_val
        if op_swi | op_c_swi:
            alu = z64
            is_load = z1
            is_store = 1
            size = 4
            addr = store_addr
            wdata = store_data
        if op_sb:
            alu = z64
            is_load = z1
            is_store = 1
            size = 1
            addr = idx_addr
            wdata = srcp_val
        if op_sh:
            alu = z64
            is_load = z1
            is_store = 1
            size = 2
            addr = idx_addr
            wdata = srcp_val
        if op_sw:
            alu = z64
            is_load = z1
            is_store = 1
            size = 4
            addr = idx_addr
            wdata = srcp_val
        if op_sd:
            alu = z64
            is_load = z1
            is_store = 1
            size = 8
            addr = idx_addr
            wdata = srcp_val
        sdi_off = imm.shl(amount=3)
        if op_c_sdi:
            alu = z64
            is_load = z1
            is_store = 1
            size = 8
            addr = srcl_val + sdi_off
            wdata = srcr_val
        sdi_addr = srcr_val + sdi_off
        if op_sdi:
            alu = z64
            is_load = z1
            is_store = 1
            size = 8
            addr = sdi_addr
            wdata = srcl_val
        if op_hl_lb_pcr | op_hl_lbu_pcr:
            alu = z64
            is_load = 1
            is_store = z1
            size = 1
            addr = pc + imm
            wdata = z64
        if op_hl_lh_pcr | op_hl_lhu_pcr:
            alu = z64
            is_load = 1
            is_store = z1
            size = 2
            addr = pc + imm
            wdata = z64
        if op_hl_lw_pcr | op_hl_lwu_pcr:
            alu = z64
            is_load = 1
            is_store = z1
            size = 4
            addr = pc + imm
            wdata = z64
        if op_hl_ld_pcr:
            alu = z64
            is_load = 1
            is_store = z1
            size = 8
            addr = pc + imm
            wdata = z64
        if op_hl_sb_pcr:
            alu = z64
            is_load = z1
            is_store = 1
            size = 1
            addr = pc + imm
            wdata = srcl_val
        if op_hl_sh_pcr:
            alu = z64
            is_load = z1
            is_store = 1
            size = 2
            addr = pc + imm
            wdata = srcl_val
        if op_hl_sw_pcr:
            alu = z64
            is_load = z1
            is_store = 1
            size = 4
            addr = pc + imm
            wdata = srcl_val
        if op_hl_sd_pcr:
            alu = z64
            is_load = z1
            is_store = 1
            size = 8
            addr = pc + imm
            wdata = srcl_val
        if op_c_addi:
            alu = srcl_val + imm
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_c_add:
            alu = srcl_val + srcr_val
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_c_sub:
            alu = srcl_val - srcr_val
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_c_and:
            alu = srcl_val & srcr_val
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_c_or:
            alu = srcl_val | srcr_val
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_c_sext_w:
            alu = srcl_val[0:32].as_signed()
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        if op_c_zext_w:
            alu = unsigned(srcl_val[0:32])
            is_load = z1
            is_store = z1
            size = z4
            addr = z64
            wdata = z64
        return ExecOut(alu=alu, is_load=is_load, is_store=is_store, size=size, addr=addr, wdata=wdata)
