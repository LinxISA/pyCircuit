from __future__ import annotations
from dataclasses import dataclass
from pycircuit import Circuit, Wire, jit_inline
from ..isa import OP_ADDTPC, OP_ADDI, OP_ADDIW, OP_ADDW, OP_ANDW, OP_BSTART_STD_CALL, OP_CMP_EQ, OP_C_BSTART_COND, OP_C_BSTART_STD, OP_CSEL, OP_C_LWI, OP_C_MOVI, OP_C_MOVR, OP_C_SETC_EQ, OP_C_SETC_TGT, OP_C_SETRET, OP_C_SWI, OP_HL_LUI, OP_LWI, OP_ORW, OP_SDI, OP_SUBI, OP_SWI, OP_XORW
from ..pipeline import ExMemRegs, IdExRegs
from ..util import Consts

@dataclass(frozen=True)
class ExBundle:
    alu: Wire
    is_load: Wire
    is_store: Wire
    size: Wire
    addr: Wire
    wdata: Wire

def _ex_apply(m: Circuit, ex: ExBundle, cond: Wire, *, alu: Wire, is_load: Wire, is_store: Wire, size: Wire, addr: Wire, wdata: Wire) -> ExBundle:
    return ExBundle(alu=alu if cond else ex.alu, is_load=is_load if cond else ex.is_load, is_store=is_store if cond else ex.is_store, size=size if cond else ex.size, addr=addr if cond else ex.addr, wdata=wdata if cond else ex.wdata)

@jit_inline
def build_ex_stage(m: Circuit, *, do_ex: Wire, pc: Wire, idex: IdExRegs, exmem: ExMemRegs, consts: Consts) -> None:
    with m.scope('EX'):
        c = m.const
        pc = pc.out()
        op = idex.op.out()
        len_bytes = idex.len_bytes.out()
        regdst = idex.regdst.out()
        srcl_val = idex.srcl_val.out()
        srcr_val = idex.srcr_val.out()
        srcp_val = idex.srcp_val.out()
        imm = idex.imm.out()
        op_c_bstart_std = op == c(OP_C_BSTART_STD, width=6)
        op_c_bstart_cond = op == c(OP_C_BSTART_COND, width=6)
        op_bstart_std_call = op == c(OP_BSTART_STD_CALL, width=6)
        op_c_movr = op == c(OP_C_MOVR, width=6)
        op_c_movi = op == c(OP_C_MOVI, width=6)
        op_c_setret = op == c(OP_C_SETRET, width=6)
        op_c_setc_eq = op == c(OP_C_SETC_EQ, width=6)
        op_c_setc_tgt = op == c(OP_C_SETC_TGT, width=6)
        op_addtpc = op == c(OP_ADDTPC, width=6)
        op_addi = op == c(OP_ADDI, width=6)
        op_subi = op == c(OP_SUBI, width=6)
        op_addiw = op == c(OP_ADDIW, width=6)
        op_addw = op == c(OP_ADDW, width=6)
        op_orw = op == c(OP_ORW, width=6)
        op_andw = op == c(OP_ANDW, width=6)
        op_xorw = op == c(OP_XORW, width=6)
        op_cmp_eq = op == c(OP_CMP_EQ, width=6)
        op_csel = op == c(OP_CSEL, width=6)
        op_hl_lui = op == c(OP_HL_LUI, width=6)
        op_lwi = op == c(OP_LWI, width=6)
        op_c_lwi = op == c(OP_C_LWI, width=6)
        op_swi = op == c(OP_SWI, width=6)
        op_c_swi = op == c(OP_C_SWI, width=6)
        op_sdi = op == c(OP_SDI, width=6)
        off = imm.shl(amount=2)
        ex = ExBundle(alu=consts.zero64, is_load=consts.zero1, is_store=consts.zero1, size=consts.zero3, addr=consts.zero64, wdata=consts.zero64)
        ex = _ex_apply(m, ex, op_c_bstart_std | op_c_bstart_cond | op_bstart_std_call, alu=imm, is_load=consts.zero1, is_store=consts.zero1, size=consts.zero3, addr=consts.zero64, wdata=consts.zero64)
        ex = _ex_apply(m, ex, op_c_movr, alu=srcl_val, is_load=consts.zero1, is_store=consts.zero1, size=consts.zero3, addr=consts.zero64, wdata=consts.zero64)
        ex = _ex_apply(m, ex, op_c_movi, alu=imm, is_load=consts.zero1, is_store=consts.zero1, size=consts.zero3, addr=consts.zero64, wdata=consts.zero64)
        ex = _ex_apply(m, ex, op_c_setret, alu=pc + imm, is_load=consts.zero1, is_store=consts.zero1, size=consts.zero3, addr=consts.zero64, wdata=consts.zero64)
        setc_eq = consts.one64 if srcl_val == srcr_val else consts.zero64
        ex = _ex_apply(m, ex, op_c_setc_eq, alu=setc_eq, is_load=consts.zero1, is_store=consts.zero1, size=consts.zero3, addr=consts.zero64, wdata=consts.zero64)
        ex = _ex_apply(m, ex, op_c_setc_tgt, alu=srcl_val, is_load=consts.zero1, is_store=consts.zero1, size=consts.zero3, addr=consts.zero64, wdata=consts.zero64)
        pc_page = pc & c(18446744073709547520, width=64)
        ex = _ex_apply(m, ex, op_addtpc, alu=pc_page + imm, is_load=consts.zero1, is_store=consts.zero1, size=consts.zero3, addr=consts.zero64, wdata=consts.zero64)
        ex = _ex_apply(m, ex, op_addi, alu=srcl_val + imm, is_load=consts.zero1, is_store=consts.zero1, size=consts.zero3, addr=consts.zero64, wdata=consts.zero64)
        subi = srcl_val + (~imm + consts.one64)
        ex = _ex_apply(m, ex, op_subi, alu=subi, is_load=consts.zero1, is_store=consts.zero1, size=consts.zero3, addr=consts.zero64, wdata=consts.zero64)
        addiw = (srcl_val[0:32] + imm[0:32]).as_signed()
        ex = _ex_apply(m, ex, op_addiw, alu=addiw, is_load=consts.zero1, is_store=consts.zero1, size=consts.zero3, addr=consts.zero64, wdata=consts.zero64)
        addw = (srcl_val[0:32] + srcr_val[0:32]).as_signed()
        orw = (srcl_val[0:32] | srcr_val[0:32]).as_signed()
        andw = (srcl_val[0:32] & srcr_val[0:32]).as_signed()
        xorw = (srcl_val[0:32] ^ srcr_val[0:32]).as_signed()
        ex = _ex_apply(m, ex, op_addw, alu=addw, is_load=consts.zero1, is_store=consts.zero1, size=consts.zero3, addr=consts.zero64, wdata=consts.zero64)
        ex = _ex_apply(m, ex, op_orw, alu=orw, is_load=consts.zero1, is_store=consts.zero1, size=consts.zero3, addr=consts.zero64, wdata=consts.zero64)
        ex = _ex_apply(m, ex, op_andw, alu=andw, is_load=consts.zero1, is_store=consts.zero1, size=consts.zero3, addr=consts.zero64, wdata=consts.zero64)
        ex = _ex_apply(m, ex, op_xorw, alu=xorw, is_load=consts.zero1, is_store=consts.zero1, size=consts.zero3, addr=consts.zero64, wdata=consts.zero64)
        cmp = consts.one64 if srcl_val == srcr_val else consts.zero64
        ex = _ex_apply(m, ex, op_cmp_eq, alu=cmp, is_load=consts.zero1, is_store=consts.zero1, size=consts.zero3, addr=consts.zero64, wdata=consts.zero64)
        ex = _ex_apply(m, ex, op_hl_lui, alu=imm, is_load=consts.zero1, is_store=consts.zero1, size=consts.zero3, addr=consts.zero64, wdata=consts.zero64)
        srcp_nz = ~(srcp_val == consts.zero64)
        csel_val = srcr_val if srcp_nz else srcl_val
        ex = _ex_apply(m, ex, op_csel, alu=csel_val, is_load=consts.zero1, is_store=consts.zero1, size=consts.zero3, addr=consts.zero64, wdata=consts.zero64)
        is_lwi = op_lwi | op_c_lwi
        lwi_addr = srcl_val + off
        ex = _ex_apply(m, ex, is_lwi, alu=consts.zero64, is_load=consts.one1, is_store=consts.zero1, size=c(4, width=3), addr=lwi_addr, wdata=consts.zero64)
        store_addr = srcr_val + off if op_swi else srcl_val + off
        store_data = srcl_val if op_swi else srcr_val
        ex = _ex_apply(m, ex, op_swi | op_c_swi, alu=consts.zero64, is_load=consts.zero1, is_store=consts.one1, size=c(4, width=3), addr=store_addr, wdata=store_data)
        sdi_off = imm.shl(amount=3)
        sdi_addr = srcr_val + sdi_off
        ex = _ex_apply(m, ex, op_sdi, alu=consts.zero64, is_load=consts.zero1, is_store=consts.one1, size=c(8, width=3), addr=sdi_addr, wdata=srcl_val)
        exmem.op.set(op, when=do_ex)
        exmem.len_bytes.set(len_bytes, when=do_ex)
        exmem.regdst.set(regdst, when=do_ex)
        exmem.alu.set(ex.alu, when=do_ex)
        exmem.is_load.set(ex.is_load, when=do_ex)
        exmem.is_store.set(ex.is_store, when=do_ex)
        exmem.size.set(ex.size, when=do_ex)
        exmem.addr.set(ex.addr, when=do_ex)
        exmem.wdata.set(ex.wdata, when=do_ex)
