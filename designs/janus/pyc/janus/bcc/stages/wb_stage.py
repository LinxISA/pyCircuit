from __future__ import annotations
from pycircuit import Circuit, Wire, jit_inline, unsigned
from ..isa import BK_CALL, BK_COND, BK_FALL, BK_RET, OP_BSTART_STD_CALL, OP_C_BSTART_COND, OP_C_BSTART_STD, OP_C_LWI, OP_C_SETC_EQ, OP_C_SETC_TGT, OP_C_SWI, OP_C_BSTOP, OP_SWI, REG_INVALID, ST_EX, ST_ID, ST_IF, ST_MEM, ST_WB
from ..pipeline import CoreState, MemWbRegs, RegFiles
from ..regfile import commit_gpr, commit_stack, stack_next
from ..util import Consts

@jit_inline
def build_wb_stage(m: Circuit, *, do_wb: Wire, stage_is_if: Wire, stage_is_id: Wire, stage_is_ex: Wire, stage_is_mem: Wire, stage_is_wb: Wire, stop: Wire, halt_set: Wire, state: CoreState, memwb: MemWbRegs, rf: RegFiles, consts: Consts) -> None:
    with m.scope('WB'):
        c = m.const
        stage = state.stage.out()
        pc = state.pc.out()
        br_kind = state.br_kind.out()
        br_base_pc = state.br_base_pc.out()
        br_off = state.br_off.out()
        commit_cond = state.commit_cond.out()
        commit_tgt = state.commit_tgt.out()
        op = memwb.op.out()
        len_bytes = memwb.len_bytes.out()
        regdst = memwb.regdst.out()
        value = memwb.value.out()
        state.halted.set(consts.one1, when=halt_set)
        op_c_bstart_std = op == c(OP_C_BSTART_STD, width=6)
        op_c_bstart_cond = op == c(OP_C_BSTART_COND, width=6)
        op_bstart_call = op == c(OP_BSTART_STD_CALL, width=6)
        op_c_bstop = op == c(OP_C_BSTOP, width=6)
        op_is_start_marker = op_c_bstart_std | op_c_bstart_cond | op_bstart_call
        op_is_boundary = op_is_start_marker | op_c_bstop
        br_is_fall = br_kind == c(BK_FALL, width=2)
        br_is_cond = br_kind == c(BK_COND, width=2)
        br_is_call = br_kind == c(BK_CALL, width=2)
        br_is_ret = br_kind == c(BK_RET, width=2)
        br_target_pc = br_base_pc + br_off
        br_target_pc = commit_tgt if br_is_ret else br_target_pc
        br_take = br_is_call | br_is_ret | br_is_cond & commit_cond
        pc_inc = pc + unsigned(len_bytes)
        pc_next = (br_target_pc if br_take else pc_inc) if op_is_boundary else pc_inc
        state.pc.set(pc_next, when=do_wb)
        stage_seq = c(ST_ID, width=3) if stage_is_if else stage
        stage_seq = c(ST_EX, width=3) if stage_is_id else stage_seq
        stage_seq = c(ST_MEM, width=3) if stage_is_ex else stage_seq
        stage_seq = c(ST_WB, width=3) if stage_is_mem else stage_seq
        stage_seq = c(ST_IF, width=3) if stage_is_wb else stage_seq
        state.stage.set(stage_seq, when=~stop)
        state.cycles.set(state.cycles.out() + consts.one64)
        op_c_setc_eq = op == c(OP_C_SETC_EQ, width=6)
        op_c_setc_tgt = op == c(OP_C_SETC_TGT, width=6)
        commit_cond_next = commit_cond
        commit_tgt_next = commit_tgt
        commit_cond_next = consts.zero1 if do_wb & op_is_boundary else commit_cond_next
        commit_tgt_next = consts.zero64 if do_wb & op_is_boundary else commit_tgt_next
        commit_cond_next = value[0:1] if do_wb & op_c_setc_eq else commit_cond_next
        commit_tgt_next = value if do_wb & op_c_setc_tgt else commit_tgt_next
        state.commit_cond.set(commit_cond_next)
        state.commit_tgt.set(commit_tgt_next)
        br_kind_next = br_kind
        br_base_next = br_base_pc
        br_off_next = br_off
        br_kind_next = c(BK_FALL, width=2) if do_wb & op_is_boundary & br_take else br_kind_next
        br_base_next = pc if do_wb & op_is_boundary & br_take else br_base_next
        br_off_next = consts.zero64 if do_wb & op_is_boundary & br_take else br_off_next
        enter_new_block = do_wb & op_is_start_marker & ~br_take
        br_kind_next = c(BK_COND, width=2) if enter_new_block & op_c_bstart_cond else br_kind_next
        br_base_next = pc if enter_new_block & op_c_bstart_cond else br_base_next
        br_off_next = value if enter_new_block & op_c_bstart_cond else br_off_next
        br_kind_next = c(BK_CALL, width=2) if enter_new_block & op_bstart_call else br_kind_next
        br_base_next = pc if enter_new_block & op_bstart_call else br_base_next
        br_off_next = value if enter_new_block & op_bstart_call else br_off_next
        brtype = value[0:3]
        kind_from_brtype = c(BK_RET, width=2) if brtype == c(7, width=3) else c(BK_FALL, width=2)
        br_kind_next = kind_from_brtype if enter_new_block & op_c_bstart_std else br_kind_next
        br_base_next = pc if enter_new_block & op_c_bstart_std else br_base_next
        br_off_next = consts.zero64 if enter_new_block & op_c_bstart_std else br_off_next
        br_kind_next = c(BK_FALL, width=2) if do_wb & op_c_bstop else br_kind_next
        br_base_next = pc if do_wb & op_c_bstop else br_base_next
        br_off_next = consts.zero64 if do_wb & op_c_bstop else br_off_next
        state.br_kind.set(br_kind_next)
        state.br_base_pc.set(br_base_next)
        state.br_off.set(br_off_next)
        wb_is_store = (op == c(OP_SWI, width=6)) | (op == c(OP_C_SWI, width=6))
        do_reg_write = do_wb & ~wb_is_store & ~(regdst == c(REG_INVALID, width=6))
        do_clear_hands = do_wb & op_is_start_marker
        do_push_t = do_wb & (op == c(OP_C_LWI, width=6))
        do_push_t = do_push_t | do_reg_write & (regdst == c(31, width=6))
        do_push_u = do_reg_write & (regdst == c(30, width=6))
        commit_gpr(m, rf.gpr, do_reg_write=do_reg_write, regdst=memwb.regdst, value=memwb.value)
        t_next = stack_next(m, rf.t, do_push=do_push_t, do_clear=do_clear_hands, value=memwb.value)
        u_next = stack_next(m, rf.u, do_push=do_push_u, do_clear=do_clear_hands, value=memwb.value)
        commit_stack(m, rf.t, t_next)
        commit_stack(m, rf.u, u_next)
