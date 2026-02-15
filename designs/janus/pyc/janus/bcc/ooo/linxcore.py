from __future__ import annotations
from dataclasses import dataclass
from pycircuit import Circuit, unsigned
from pycircuit.dsl import Signal
from .exec import exec_uop_comb
from ..isa import BK_CALL, BK_COND, BK_DIRECT, BK_FALL, BK_ICALL, BK_IND, BK_RET, OP_BSTART_STD_COND, OP_BSTART_STD_CALL, OP_BSTART_STD_DIRECT, OP_BSTART_STD_FALL, OP_C_BSTART_COND, OP_C_BSTART_DIRECT, OP_C_BSTART_STD, OP_C_BSTOP, OP_C_LDI, OP_C_LWI, OP_C_SETC_NE, OP_C_SDI, OP_C_SWI, OP_C_SETC_EQ, OP_C_SETC_TGT, OP_EBREAK, OP_FENTRY, OP_FEXIT, OP_FRET_RA, OP_FRET_STK, OP_INVALID, OP_HL_LB_PCR, OP_HL_LBU_PCR, OP_HL_LD_PCR, OP_HL_LH_PCR, OP_HL_LHU_PCR, OP_HL_LW_PCR, OP_HL_LWU_PCR, OP_HL_SB_PCR, OP_HL_SD_PCR, OP_HL_SH_PCR, OP_HL_SW_PCR, OP_LB, OP_LBI, OP_LBU, OP_LBUI, OP_LD, OP_LH, OP_LHI, OP_LHU, OP_LHUI, OP_LW, OP_LWU, OP_LWUI, OP_SB, OP_SBI, OP_SD, OP_SH, OP_SHI, OP_SW, OP_LWI, OP_LDI, OP_SETC_AND, OP_SETC_ANDI, OP_SETC_EQ, OP_SETC_EQI, OP_SETC_GE, OP_SETC_GEI, OP_SETC_GEU, OP_SETC_GEUI, OP_SETC_LT, OP_SETC_LTI, OP_SETC_LTU, OP_SETC_LTUI, OP_SETC_NE, OP_SETC_NEI, OP_SETC_OR, OP_SETC_ORI, OP_SDI, OP_SWI, REG_INVALID
from ..util import lshr_var, make_consts
from .dec1 import decode_f4_bundle
from .helpers import alloc_from_free_mask, mask_bit, mux_by_uindex, onehot_from_tag
from .params import OooParams
from .state import make_core_ctrl_regs, make_ifu_regs, make_iq_regs, make_prf, make_rename_regs, make_rob_regs

@dataclass(frozen=True)
class BccOooExports:
    clk: Signal
    rst: Signal
    block_cmd_valid: Signal
    block_cmd_kind: Signal
    block_cmd_payload: Signal
    block_cmd_tile: Signal
    block_cmd_tag: Signal
    cycles: Signal
    halted: Signal

def build_bcc_ooo(m: Circuit, *, mem_bytes: int, params: OooParams | None=None) -> BccOooExports:
    p = params or OooParams()
    clk = m.clock('clk')
    rst = m.reset('rst')
    boot_pc = m.input('boot_pc', width=64)
    boot_sp = m.input('boot_sp', width=64)
    host_wvalid = m.input('host_wvalid', width=1)
    host_waddr = m.input('host_waddr', width=64)
    host_wdata = m.input('host_wdata', width=64)
    host_wstrb = m.input('host_wstrb', width=8)
    c = m.const
    consts = make_consts(m)

    def op_is(op, *codes: int):
        v = consts.zero1
        for code in codes:
            v = v | (op == c(code, width=12))
        return v
    tag0 = c(0, width=p.ptag_w)
    state = make_core_ctrl_regs(m, clk, rst, boot_pc=boot_pc, consts=consts)
    base_can_run = ~state.halted.out() & ~state.flush_pending.out()
    do_flush = state.flush_pending.out()
    ifu = make_ifu_regs(m, clk, rst, boot_pc=boot_pc, consts=consts)
    prf = make_prf(m, clk, rst, boot_sp=boot_sp, consts=consts, p=p)
    ren = make_rename_regs(m, clk, rst, consts=consts, p=p)
    rob = make_rob_regs(m, clk, rst, consts=consts, p=p)
    iq_alu = make_iq_regs(m, clk, rst, consts=consts, p=p, name='iq_alu')
    iq_bru = make_iq_regs(m, clk, rst, consts=consts, p=p, name='iq_bru')
    iq_lsu = make_iq_regs(m, clk, rst, consts=consts, p=p, name='iq_lsu')
    commit_idxs = []
    rob_valids = []
    rob_dones = []
    rob_ops = []
    rob_lens = []
    rob_dst_kinds = []
    rob_dst_aregs = []
    rob_pdsts = []
    rob_values = []
    rob_is_stores = []
    rob_st_addrs = []
    rob_st_datas = []
    rob_st_sizes = []
    rob_is_loads = []
    rob_ld_addrs = []
    rob_ld_datas = []
    rob_ld_sizes = []
    rob_insn_raws = []
    rob_macro_begins = []
    rob_macro_ends = []
    for slot in range(p.commit_w):
        idx = rob.head.out() + c(slot, width=p.rob_w)
        commit_idxs.append(idx)
        rob_valids.append(mux_by_uindex(m, idx=idx, items=rob.valid, default=consts.zero1))
        rob_dones.append(mux_by_uindex(m, idx=idx, items=rob.done, default=consts.zero1))
        rob_ops.append(mux_by_uindex(m, idx=idx, items=rob.op, default=c(0, width=12)))
        rob_lens.append(mux_by_uindex(m, idx=idx, items=rob.len_bytes, default=consts.zero3))
        rob_dst_kinds.append(mux_by_uindex(m, idx=idx, items=rob.dst_kind, default=c(0, width=2)))
        rob_dst_aregs.append(mux_by_uindex(m, idx=idx, items=rob.dst_areg, default=c(REG_INVALID, width=6)))
        rob_pdsts.append(mux_by_uindex(m, idx=idx, items=rob.pdst, default=tag0))
        rob_values.append(mux_by_uindex(m, idx=idx, items=rob.value, default=consts.zero64))
        rob_is_stores.append(mux_by_uindex(m, idx=idx, items=rob.is_store, default=consts.zero1))
        rob_st_addrs.append(mux_by_uindex(m, idx=idx, items=rob.store_addr, default=consts.zero64))
        rob_st_datas.append(mux_by_uindex(m, idx=idx, items=rob.store_data, default=consts.zero64))
        rob_st_sizes.append(mux_by_uindex(m, idx=idx, items=rob.store_size, default=consts.zero4))
        rob_is_loads.append(mux_by_uindex(m, idx=idx, items=rob.is_load, default=consts.zero1))
        rob_ld_addrs.append(mux_by_uindex(m, idx=idx, items=rob.load_addr, default=consts.zero64))
        rob_ld_datas.append(mux_by_uindex(m, idx=idx, items=rob.load_data, default=consts.zero64))
        rob_ld_sizes.append(mux_by_uindex(m, idx=idx, items=rob.load_size, default=consts.zero4))
        rob_insn_raws.append(mux_by_uindex(m, idx=idx, items=rob.insn_raw, default=consts.zero64))
        rob_macro_begins.append(mux_by_uindex(m, idx=idx, items=rob.macro_begin, default=c(0, width=6)))
        rob_macro_ends.append(mux_by_uindex(m, idx=idx, items=rob.macro_end, default=c(0, width=6)))
    head_op = rob_ops[0]
    head_len = rob_lens[0]
    head_dst_kind = rob_dst_kinds[0]
    head_dst_areg = rob_dst_aregs[0]
    head_pdst = rob_pdsts[0]
    head_value = rob_values[0]
    head_is_store = rob_is_stores[0]
    head_st_addr = rob_st_addrs[0]
    head_st_data = rob_st_datas[0]
    head_st_size = rob_st_sizes[0]
    head_is_load = rob_is_loads[0]
    head_ld_addr = rob_ld_addrs[0]
    head_ld_data = rob_ld_datas[0]
    head_ld_size = rob_ld_sizes[0]
    head_insn_raw = rob_insn_raws[0]
    head_macro_begin = rob_macro_begins[0]
    head_macro_end = rob_macro_ends[0]
    head_is_macro = op_is(head_op, OP_FENTRY, OP_FEXIT, OP_FRET_RA, OP_FRET_STK)
    head_is_start_marker = op_is(head_op, OP_C_BSTART_STD, OP_C_BSTART_COND, OP_C_BSTART_DIRECT, OP_BSTART_STD_FALL, OP_BSTART_STD_DIRECT, OP_BSTART_STD_COND, OP_BSTART_STD_CALL) | head_is_macro
    head_is_boundary = head_is_start_marker | op_is(head_op, OP_C_BSTOP)
    br_kind_head = state.br_kind.out()
    br_is_cond_head = br_kind_head == c(BK_COND, width=3)
    br_is_call_head = br_kind_head == c(BK_CALL, width=3)
    br_is_ret_head = br_kind_head == c(BK_RET, width=3)
    br_is_direct_head = br_kind_head == c(BK_DIRECT, width=3)
    br_is_ind_head = br_kind_head == c(BK_IND, width=3)
    br_is_icall_head = br_kind_head == c(BK_ICALL, width=3)
    head_br_take = br_is_call_head | br_is_direct_head | br_is_ind_head | br_is_icall_head | br_is_cond_head & state.commit_cond.out() | br_is_ret_head & state.commit_cond.out()
    head_skip = head_is_boundary & head_br_take
    macro_start = base_can_run & ~state.macro_active.out() & ~state.macro_wait_commit.out() & head_is_macro & ~head_skip & rob_valids[0] & rob_dones[0]
    macro_block = state.macro_active.out() | macro_start
    can_run = base_can_run & ~macro_block
    ret_ra_tag = ren.cmap[10].out()
    ret_ra_val = mux_by_uindex(m, idx=ret_ra_tag, items=prf, default=consts.zero64)
    commit_allow = consts.one1
    commit_fires = []
    commit_pcs = []
    commit_next_pcs = []
    commit_enter_new_blocks = []
    commit_count = c(0, width=3)
    redirect_valid = consts.zero1
    redirect_pc = state.pc.out()
    commit_store_fire = consts.zero1
    commit_store_addr = consts.zero64
    commit_store_data = consts.zero64
    commit_store_size = consts.zero4
    ra_write_fire = consts.zero1
    ra_write_value = consts.zero64
    ra_tag_live = ren.cmap[10].out()
    ra_write_tag = ra_tag_live
    pc_live = state.pc.out()
    commit_cond_live = state.commit_cond.out()
    commit_tgt_live = state.commit_tgt.out()
    br_kind_live = state.br_kind.out()
    br_base_live = state.br_base_pc.out()
    br_off_live = state.br_off.out()
    for slot in range(p.commit_w):
        pc_this = pc_live
        commit_pcs.append(pc_this)
        op = rob_ops[slot]
        ln = rob_lens[slot]
        val = rob_values[slot]
        is_macro = op_is(op, OP_FENTRY, OP_FEXIT, OP_FRET_RA, OP_FRET_STK)
        is_start_marker = op_is(op, OP_C_BSTART_STD, OP_C_BSTART_COND, OP_C_BSTART_DIRECT, OP_BSTART_STD_FALL, OP_BSTART_STD_DIRECT, OP_BSTART_STD_COND, OP_BSTART_STD_CALL) | is_macro
        is_boundary = is_start_marker | op_is(op, OP_C_BSTOP)
        br_is_fall = br_kind_live == c(BK_FALL, width=3)
        br_is_cond = br_kind_live == c(BK_COND, width=3)
        br_is_call = br_kind_live == c(BK_CALL, width=3)
        br_is_ret = br_kind_live == c(BK_RET, width=3)
        br_is_direct = br_kind_live == c(BK_DIRECT, width=3)
        br_is_ind = br_kind_live == c(BK_IND, width=3)
        br_is_icall = br_kind_live == c(BK_ICALL, width=3)
        br_target = br_base_live + br_off_live
        br_target = commit_tgt_live if br_is_ret | br_is_ind | br_is_icall else br_target
        br_target = commit_tgt_live if ~(br_is_ret | br_is_ind | br_is_icall) & ~(commit_tgt_live == consts.zero64) else br_target
        br_take = br_is_call | br_is_direct | br_is_ind | br_is_icall | br_is_cond & commit_cond_live | br_is_ret & commit_cond_live
        pc_inc = pc_this + unsigned(ln)
        pc_next = (br_target if br_take else pc_inc) if is_boundary else pc_inc
        fire = can_run & commit_allow & rob_valids[slot] & rob_dones[slot]
        if slot != 0:
            fire = fire & ~is_macro
        is_halt = op_is(op, OP_EBREAK, OP_INVALID)
        redirect = fire & is_boundary & br_take
        is_fret = op_is(op, OP_FRET_RA, OP_FRET_STK)
        fret_redirect = fire & is_fret & ~redirect
        pc_next = ret_ra_val if fret_redirect else pc_next
        redirect = redirect | fret_redirect
        commit_next_pcs.append(pc_next)
        ra_fallthrough = pc_inc if op_is(op, OP_C_BSTOP) else pc_this
        ra_write = redirect & (br_is_call | br_is_icall)
        ra_write_fire = consts.one1 if ra_write else ra_write_fire
        ra_write_value = ra_fallthrough if ra_write else ra_write_value
        store_commit = fire & rob_is_stores[slot]
        stop = redirect | store_commit | fire & is_halt
        commit_fires.append(fire)
        commit_count = commit_count + unsigned(fire)
        ra_map_write = fire & (rob_dst_kinds[slot] == c(1, width=2)) & (rob_dst_aregs[slot] == c(10, width=6))
        ra_tag_live = rob_pdsts[slot] if ra_map_write else ra_tag_live
        redirect_valid = consts.one1 if redirect else redirect_valid
        redirect_pc = pc_next if redirect else redirect_pc
        commit_store_fire = consts.one1 if store_commit else commit_store_fire
        commit_store_addr = rob_st_addrs[slot] if store_commit else commit_store_addr
        commit_store_data = rob_st_datas[slot] if store_commit else commit_store_data
        commit_store_size = rob_st_sizes[slot] if store_commit else commit_store_size
        ra_write_tag = ra_tag_live if ra_write else ra_write_tag
        op_setc_any = op_is(op, OP_C_SETC_EQ, OP_C_SETC_NE, OP_SETC_GEUI, OP_SETC_EQ, OP_SETC_NE, OP_SETC_AND, OP_SETC_OR, OP_SETC_LT, OP_SETC_LTU, OP_SETC_GE, OP_SETC_GEU, OP_SETC_EQI, OP_SETC_NEI, OP_SETC_ANDI, OP_SETC_ORI, OP_SETC_LTI, OP_SETC_GEI, OP_SETC_LTUI)
        op_setc_tgt = op_is(op, OP_C_SETC_TGT)
        commit_cond_live = consts.zero1 if fire & is_boundary else commit_cond_live
        commit_tgt_live = consts.zero64 if fire & is_boundary else commit_tgt_live
        commit_cond_live = val[0:1] if fire & op_setc_any else commit_cond_live
        commit_tgt_live = val if fire & op_setc_tgt else commit_tgt_live
        commit_cond_live = consts.one1 if fire & op_setc_tgt else commit_cond_live
        br_kind_live = c(BK_FALL, width=3) if fire & is_boundary & br_take else br_kind_live
        br_base_live = pc_this if fire & is_boundary & br_take else br_base_live
        br_off_live = consts.zero64 if fire & is_boundary & br_take else br_off_live
        enter_new_block = fire & is_start_marker & ~br_take
        commit_enter_new_blocks.append(enter_new_block)
        is_bstart_cond = op_is(op, OP_C_BSTART_COND)
        br_kind_live = c(BK_COND, width=3) if enter_new_block & is_bstart_cond else br_kind_live
        br_base_live = pc_this if enter_new_block & is_bstart_cond else br_base_live
        br_off_live = val if enter_new_block & is_bstart_cond else br_off_live
        is_bstart_direct = op_is(op, OP_C_BSTART_DIRECT)
        br_kind_live = c(BK_DIRECT, width=3) if enter_new_block & is_bstart_direct else br_kind_live
        br_base_live = pc_this if enter_new_block & is_bstart_direct else br_base_live
        br_off_live = val if enter_new_block & is_bstart_direct else br_off_live
        is_bstart_std_fall = op_is(op, OP_BSTART_STD_FALL)
        br_kind_live = c(BK_FALL, width=3) if enter_new_block & is_bstart_std_fall else br_kind_live
        br_base_live = pc_this if enter_new_block & is_bstart_std_fall else br_base_live
        br_off_live = consts.zero64 if enter_new_block & is_bstart_std_fall else br_off_live
        is_bstart_std_direct = op_is(op, OP_BSTART_STD_DIRECT)
        br_kind_live = c(BK_DIRECT, width=3) if enter_new_block & is_bstart_std_direct else br_kind_live
        br_base_live = pc_this if enter_new_block & is_bstart_std_direct else br_base_live
        br_off_live = val if enter_new_block & is_bstart_std_direct else br_off_live
        is_bstart_std_cond = op_is(op, OP_BSTART_STD_COND)
        br_kind_live = c(BK_COND, width=3) if enter_new_block & is_bstart_std_cond else br_kind_live
        br_base_live = pc_this if enter_new_block & is_bstart_std_cond else br_base_live
        br_off_live = val if enter_new_block & is_bstart_std_cond else br_off_live
        is_bstart_call = op_is(op, OP_BSTART_STD_CALL)
        br_kind_live = c(BK_CALL, width=3) if enter_new_block & is_bstart_call else br_kind_live
        br_base_live = pc_this if enter_new_block & is_bstart_call else br_base_live
        br_off_live = val if enter_new_block & is_bstart_call else br_off_live
        brtype = val[0:3]
        kind_from_brtype = c(BK_FALL, width=3)
        kind_from_brtype = c(BK_DIRECT, width=3) if brtype == c(2, width=3) else kind_from_brtype
        kind_from_brtype = c(BK_COND, width=3) if brtype == c(3, width=3) else kind_from_brtype
        kind_from_brtype = c(BK_CALL, width=3) if brtype == c(4, width=3) else kind_from_brtype
        kind_from_brtype = c(BK_IND, width=3) if brtype == c(5, width=3) else kind_from_brtype
        kind_from_brtype = c(BK_ICALL, width=3) if brtype == c(6, width=3) else kind_from_brtype
        kind_from_brtype = c(BK_RET, width=3) if brtype == c(7, width=3) else kind_from_brtype
        is_bstart_std = op_is(op, OP_C_BSTART_STD)
        br_kind_live = kind_from_brtype if enter_new_block & is_bstart_std else br_kind_live
        br_base_live = pc_this if enter_new_block & is_bstart_std else br_base_live
        br_off_live = consts.zero64 if enter_new_block & is_bstart_std else br_off_live
        br_kind_live = c(BK_FALL, width=3) if enter_new_block & is_macro else br_kind_live
        br_base_live = pc_this if enter_new_block & is_macro else br_base_live
        br_off_live = consts.zero64 if enter_new_block & is_macro else br_off_live
        is_bstop = op_is(op, OP_C_BSTOP)
        br_kind_live = c(BK_FALL, width=3) if fire & is_bstop else br_kind_live
        br_base_live = pc_this if fire & is_bstop else br_base_live
        br_off_live = consts.zero64 if fire & is_bstop else br_off_live
        pc_live = pc_next if fire else pc_live
        commit_allow = commit_allow & fire & ~stop
    commit_fire = commit_fires[0]
    commit_redirect = redirect_valid
    store_pending = consts.zero1
    for i in range(p.rob_depth):
        store_pending = store_pending | rob.valid[i].out() & rob.is_store[i].out()
    sub_head = ~rob.head.out() + c(1, width=p.rob_w)
    alu_can_issue: list = []
    for i in range(p.iq_depth):
        v = iq_alu.valid[i].out()
        sl = iq_alu.srcl[i].out()
        sr = iq_alu.srcr[i].out()
        sp = iq_alu.srcp[i].out()
        sl_rdy = mask_bit(m, mask=ren.ready_mask.out(), idx=sl, width=p.pregs)
        sr_rdy = mask_bit(m, mask=ren.ready_mask.out(), idx=sr, width=p.pregs)
        sp_rdy = mask_bit(m, mask=ren.ready_mask.out(), idx=sp, width=p.pregs)
        alu_can_issue.append(v & sl_rdy & sr_rdy & sp_rdy)
    bru_can_issue: list = []
    for i in range(p.iq_depth):
        v = iq_bru.valid[i].out()
        sl = iq_bru.srcl[i].out()
        sr = iq_bru.srcr[i].out()
        sp = iq_bru.srcp[i].out()
        sl_rdy = mask_bit(m, mask=ren.ready_mask.out(), idx=sl, width=p.pregs)
        sr_rdy = mask_bit(m, mask=ren.ready_mask.out(), idx=sr, width=p.pregs)
        sp_rdy = mask_bit(m, mask=ren.ready_mask.out(), idx=sp, width=p.pregs)
        bru_can_issue.append(v & sl_rdy & sr_rdy & sp_rdy)
    lsu_is_load: list = []
    lsu_is_store: list = []
    lsu_older_store_pending: list = []
    lsu_can_issue: list = []
    for i in range(p.iq_depth):
        v = iq_lsu.valid[i].out()
        sl = iq_lsu.srcl[i].out()
        sr = iq_lsu.srcr[i].out()
        sp = iq_lsu.srcp[i].out()
        sl_rdy = mask_bit(m, mask=ren.ready_mask.out(), idx=sl, width=p.pregs)
        sr_rdy = mask_bit(m, mask=ren.ready_mask.out(), idx=sr, width=p.pregs)
        sp_rdy = mask_bit(m, mask=ren.ready_mask.out(), idx=sp, width=p.pregs)
        ready = v & sl_rdy & sr_rdy & sp_rdy
        op_i = iq_lsu.op[i].out()
        is_load_i = op_is(op_i, OP_LWI, OP_C_LWI, OP_LBI, OP_LBUI, OP_LHI, OP_LHUI, OP_LWUI, OP_LDI, OP_C_LDI, OP_LB, OP_LBU, OP_LH, OP_LHU, OP_LW, OP_LWU, OP_LD, OP_HL_LB_PCR, OP_HL_LBU_PCR, OP_HL_LH_PCR, OP_HL_LHU_PCR, OP_HL_LW_PCR, OP_HL_LWU_PCR, OP_HL_LD_PCR)
        is_store_i = op_is(op_i, OP_SBI, OP_SHI, OP_SWI, OP_C_SWI, OP_SDI, OP_C_SDI, OP_SB, OP_SH, OP_SW, OP_SD, OP_HL_SB_PCR, OP_HL_SH_PCR, OP_HL_SW_PCR, OP_HL_SD_PCR)
        uop_rob_i = iq_lsu.rob[i].out()
        uop_dist = uop_rob_i + sub_head
        older_store = consts.zero1
        for j in range(p.rob_depth):
            idx = c(j, width=p.rob_w)
            dist = idx + sub_head
            is_older = dist.ult(uop_dist)
            older_store = older_store | rob.valid[j].out() & rob.is_store[j].out() & is_older
        ok = ready & ~(is_load_i & older_store)
        lsu_is_load.append(is_load_i)
        lsu_is_store.append(is_store_i)
        lsu_older_store_pending.append(older_store)
        lsu_can_issue.append(ok)
    alu_issue_valids = []
    alu_issue_idxs = []
    for slot in range(p.alu_w):
        v = consts.zero1
        idx = c(0, width=p.iq_w)
        for i in range(p.iq_depth):
            cidx = c(i, width=p.iq_w)
            exclude = consts.zero1
            for prev in range(slot):
                exclude = exclude | alu_issue_valids[prev] & (alu_issue_idxs[prev] == cidx)
            cand = alu_can_issue[i] & ~exclude
            take = cand & ~v
            v = consts.one1 if take else v
            idx = cidx if take else idx
        alu_issue_valids.append(v)
        alu_issue_idxs.append(idx)
    bru_issue_valids = []
    bru_issue_idxs = []
    for slot in range(p.bru_w):
        v = consts.zero1
        idx = c(0, width=p.iq_w)
        for i in range(p.iq_depth):
            cidx = c(i, width=p.iq_w)
            exclude = consts.zero1
            for prev in range(slot):
                exclude = exclude | bru_issue_valids[prev] & (bru_issue_idxs[prev] == cidx)
            cand = bru_can_issue[i] & ~exclude
            take = cand & ~v
            v = consts.one1 if take else v
            idx = cidx if take else idx
        bru_issue_valids.append(v)
        bru_issue_idxs.append(idx)
    lsu_issue_valids = []
    lsu_issue_idxs = []
    for slot in range(p.lsu_w):
        v = consts.zero1
        idx = c(0, width=p.iq_w)
        for i in range(p.iq_depth):
            cidx = c(i, width=p.iq_w)
            exclude = consts.zero1
            for prev in range(slot):
                exclude = exclude | lsu_issue_valids[prev] & (lsu_issue_idxs[prev] == cidx)
            cand = lsu_can_issue[i] & ~exclude
            take = cand & ~v
            v = consts.one1 if take else v
            idx = cidx if take else idx
        lsu_issue_valids.append(v)
        lsu_issue_idxs.append(idx)
    issue_valids = lsu_issue_valids + bru_issue_valids + alu_issue_valids
    issue_idxs = lsu_issue_idxs + bru_issue_idxs + alu_issue_idxs
    issue_iqs = [iq_lsu] * p.lsu_w + [iq_bru] * p.bru_w + [iq_alu] * p.alu_w
    issue_fires = []
    for slot in range(p.issue_w):
        issue_fires.append(can_run & ~commit_redirect & issue_valids[slot])
    issue_fire = issue_fires[0]
    issue_idx = issue_idxs[0]
    uop_robs = []
    uop_ops = []
    uop_pcs = []
    uop_imms = []
    uop_sls = []
    uop_srs = []
    uop_srcr_types = []
    uop_shamts = []
    uop_sps = []
    uop_pdsts = []
    uop_has_dsts = []
    for slot in range(p.issue_w):
        iq = issue_iqs[slot]
        idx = issue_idxs[slot]
        uop_robs.append(mux_by_uindex(m, idx=idx, items=iq.rob, default=c(0, width=p.rob_w)))
        uop_ops.append(mux_by_uindex(m, idx=idx, items=iq.op, default=c(0, width=12)))
        uop_pcs.append(mux_by_uindex(m, idx=idx, items=iq.pc, default=consts.zero64))
        uop_imms.append(mux_by_uindex(m, idx=idx, items=iq.imm, default=consts.zero64))
        uop_sls.append(mux_by_uindex(m, idx=idx, items=iq.srcl, default=tag0))
        uop_srs.append(mux_by_uindex(m, idx=idx, items=iq.srcr, default=tag0))
        uop_srcr_types.append(mux_by_uindex(m, idx=idx, items=iq.srcr_type, default=c(0, width=2)))
        uop_shamts.append(mux_by_uindex(m, idx=idx, items=iq.shamt, default=consts.zero6))
        uop_sps.append(mux_by_uindex(m, idx=idx, items=iq.srcp, default=tag0))
        uop_pdsts.append(mux_by_uindex(m, idx=idx, items=iq.pdst, default=tag0))
        uop_has_dsts.append(mux_by_uindex(m, idx=idx, items=iq.has_dst, default=consts.zero1))
    uop_rob = uop_robs[0]
    uop_op = uop_ops[0]
    uop_pc = uop_pcs[0]
    uop_imm = uop_imms[0]
    uop_sl = uop_sls[0]
    uop_sr = uop_srs[0]
    uop_sp = uop_sps[0]
    uop_pdst = uop_pdsts[0]
    uop_has_dst = uop_has_dsts[0]
    sl_vals = []
    sr_vals = []
    sp_vals = []
    exs = []
    for slot in range(p.issue_w):
        sl_vals.append(mux_by_uindex(m, idx=uop_sls[slot], items=prf, default=consts.zero64))
        sr_vals.append(mux_by_uindex(m, idx=uop_srs[slot], items=prf, default=consts.zero64))
        sp_vals.append(mux_by_uindex(m, idx=uop_sps[slot], items=prf, default=consts.zero64))
        exs.append(exec_uop_comb(m, op=uop_ops[slot], pc=uop_pcs[slot], imm=uop_imms[slot], srcl_val=sl_vals[slot], srcr_val=sr_vals[slot], srcr_type=uop_srcr_types[slot], shamt=uop_shamts[slot], srcp_val=sp_vals[slot], consts=consts))
    sl_val = sl_vals[0]
    sr_val = sr_vals[0]
    sp_val = sp_vals[0]
    load_fires = []
    store_fires = []
    any_load_fire = consts.zero1
    load_addr = consts.zero64
    for slot in range(p.issue_w):
        ld = issue_fires[slot] & exs[slot].is_load
        st = issue_fires[slot] & exs[slot].is_store
        load_fires.append(ld)
        store_fires.append(st)
        any_load_fire = any_load_fire | ld
        load_addr = exs[slot].addr if ld else load_addr
    issued_is_load = load_fires[0]
    issued_is_store = store_fires[0]
    older_store_pending = mux_by_uindex(m, idx=issue_idx, items=lsu_older_store_pending, default=consts.zero1)
    macro_active = state.macro_active.out()
    macro_phase = state.macro_phase.out()
    macro_op = state.macro_op.out()
    macro_stacksize = state.macro_stacksize.out()
    macro_reg = state.macro_reg.out()
    macro_i = state.macro_i.out()
    macro_sp_base = state.macro_sp_base.out()
    macro_is_fentry = macro_op == c(OP_FENTRY, width=12)
    macro_phase_mem = macro_phase == c(1, width=2)
    macro_i1 = unsigned(macro_i + c(1, width=6))
    macro_bytes = macro_i1.shl(amount=3)
    macro_off_ok = macro_bytes.ule(macro_stacksize)
    macro_off = macro_stacksize - macro_bytes
    macro_addr = macro_sp_base + macro_off
    macro_mem_read = macro_active & macro_phase_mem & ~macro_is_fentry & macro_off_ok
    mem_raddr = macro_addr if macro_mem_read else load_addr if any_load_fire else state.fpc.out()
    cmap_now = [ren.cmap[i].out() for i in range(p.aregs)]
    macro_reg_tag = mux_by_uindex(m, idx=macro_reg, items=cmap_now, default=tag0)
    macro_reg_val = mux_by_uindex(m, idx=macro_reg_tag, items=prf, default=consts.zero64)
    macro_sp_tag = ren.cmap[1].out()
    macro_sp_val = mux_by_uindex(m, idx=macro_sp_tag, items=prf, default=consts.zero64)
    macro_reg_is_gpr = macro_reg.ult(c(24, width=6))
    macro_reg_not_zero = ~(macro_reg == c(0, width=6))
    macro_store_fire = macro_active & macro_phase_mem & macro_is_fentry & macro_off_ok & macro_reg_is_gpr & macro_reg_not_zero
    macro_store_addr = macro_addr
    macro_store_data = macro_reg_val
    macro_store_size = c(8, width=4)
    mmio_uart = commit_store_fire & (commit_store_addr == c(268435456, width=64))
    mmio_exit = commit_store_fire & (commit_store_addr == c(268435460, width=64))
    mmio_any = mmio_uart | mmio_exit
    mmio_uart_data = commit_store_data[0:8] if mmio_uart else c(0, width=8)
    mmio_exit_code = commit_store_data[0:32] if mmio_exit else c(0, width=32)
    mem_wvalid = commit_store_fire & ~mmio_any | macro_store_fire
    mem_waddr = macro_store_addr if macro_store_fire else commit_store_addr
    mem_wdata = macro_store_data if macro_store_fire else commit_store_data
    mem_wsize = macro_store_size if macro_store_fire else commit_store_size
    wstrb = consts.zero8
    wstrb = c(1, width=8) if mem_wsize == c(1, width=4) else wstrb
    wstrb = c(3, width=8) if mem_wsize == c(2, width=4) else wstrb
    wstrb = c(15, width=8) if mem_wsize == c(4, width=4) else wstrb
    wstrb = c(255, width=8) if mem_wsize == c(8, width=4) else wstrb
    mem_wvalid_eff = mem_wvalid | host_wvalid
    mem_waddr_eff = host_waddr if host_wvalid else mem_waddr
    mem_wdata_eff = host_wdata if host_wvalid else mem_wdata
    wstrb_eff = host_wstrb if host_wvalid else wstrb
    mem_rdata = m.byte_mem(clk, rst, raddr=mem_raddr, wvalid=mem_wvalid_eff, waddr=mem_waddr_eff, wdata=mem_wdata_eff, wstrb=wstrb_eff, depth=mem_bytes, name='mem')
    macro_phase_init = macro_phase == c(0, width=2)
    macro_phase_sp = macro_phase == c(2, width=2)
    macro_is_restore = macro_active & ~macro_is_fentry
    macro_reg_write = macro_active & macro_phase_mem & macro_is_restore & macro_off_ok & macro_reg_is_gpr & macro_reg_not_zero
    macro_sp_write_init = macro_active & macro_phase_init & macro_is_fentry
    macro_sp_write_restore = macro_active & macro_phase_sp & macro_is_restore
    macro_prf_we = macro_reg_write | macro_sp_write_init | macro_sp_write_restore
    macro_prf_tag = macro_sp_tag
    macro_prf_data = consts.zero64
    macro_prf_tag = macro_reg_tag if macro_reg_write else macro_prf_tag
    macro_prf_data = mem_rdata if macro_reg_write else macro_prf_data
    macro_prf_data = macro_sp_base + macro_stacksize if macro_sp_write_restore else macro_prf_data
    macro_prf_data = macro_sp_val - macro_stacksize if macro_sp_write_init else macro_prf_data
    load8 = mem_rdata[0:8]
    load16 = mem_rdata[0:16]
    load32 = mem_rdata[0:32]
    load_lb = load8.as_signed()
    load_lbu = unsigned(load8)
    load_lh = load16.as_signed()
    load_lhu = unsigned(load16)
    load_lw = load32.as_signed()
    load_lwu = unsigned(load32)
    load_ld = mem_rdata
    wb_fires = []
    wb_robs = []
    wb_pdsts = []
    wb_values = []
    wb_fire_has_dsts = []
    wb_onehots = []
    for slot in range(p.issue_w):
        wb_fire = issue_fires[slot]
        wb_rob = uop_robs[slot]
        wb_pdst = uop_pdsts[slot]
        op = uop_ops[slot]
        load_val = load_lw
        load_val = load_lb if op_is(op, OP_LB, OP_LBI, OP_HL_LB_PCR) else load_val
        load_val = load_lbu if op_is(op, OP_LBU, OP_LBUI, OP_HL_LBU_PCR) else load_val
        load_val = load_lh if op_is(op, OP_LH, OP_LHI, OP_HL_LH_PCR) else load_val
        load_val = load_lhu if op_is(op, OP_LHU, OP_LHUI, OP_HL_LHU_PCR) else load_val
        load_val = load_lw if op_is(op, OP_LWI, OP_C_LWI, OP_LW, OP_HL_LW_PCR) else load_val
        load_val = load_lwu if op_is(op, OP_LWU, OP_LWUI, OP_HL_LWU_PCR) else load_val
        load_val = load_ld if op_is(op, OP_LD, OP_LDI, OP_C_LDI, OP_HL_LD_PCR) else load_val
        wb_value = load_val if load_fires[slot] else exs[slot].alu
        wb_has_dst = uop_has_dsts[slot] & ~store_fires[slot]
        wb_fire_has_dst = wb_fire & wb_has_dst
        wb_fires.append(wb_fire)
        wb_robs.append(wb_rob)
        wb_pdsts.append(wb_pdst)
        wb_values.append(wb_value)
        wb_fire_has_dsts.append(wb_fire_has_dst)
        wb_onehots.append(onehot_from_tag(m, tag=wb_pdst, width=p.pregs, tag_width=p.ptag_w))
    f4_valid = ifu.f4_valid.out()
    f4_pc = ifu.f4_pc.out()
    f4_window = ifu.f4_window.out()
    f4_bundle = decode_f4_bundle(m, f4_window)
    disp_valids = []
    disp_pcs = []
    disp_ops = []
    disp_lens = []
    disp_regdsts = []
    disp_srcls = []
    disp_srcrs = []
    disp_srcr_types = []
    disp_shamts = []
    disp_srcps = []
    disp_imms = []
    disp_insn_raws = []
    disp_is_start_marker = []
    disp_push_t = []
    disp_push_u = []
    disp_is_store = []
    disp_dst_is_gpr = []
    disp_need_pdst = []
    disp_dst_kind = []
    for slot in range(p.dispatch_w):
        dec = f4_bundle.dec[slot]
        v = f4_valid & f4_bundle.valid[slot]
        off = f4_bundle.off_bytes[slot]
        pc = f4_pc + unsigned(off)
        op = dec.op
        ln = dec.len_bytes
        regdst = dec.regdst
        srcl = dec.srcl
        srcr = dec.srcr
        srcr_type = dec.srcr_type
        shamt = dec.shamt
        srcp = dec.srcp
        imm = dec.imm
        off_sh = unsigned(off).shl(amount=3)
        slot_window = lshr_var(m, f4_window, off_sh)
        insn_raw = slot_window
        insn_raw = slot_window & c(65535, width=64) if ln == c(2, width=3) else insn_raw
        insn_raw = slot_window & c(4294967295, width=64) if ln == c(4, width=3) else insn_raw
        insn_raw = slot_window & c(281474976710655, width=64) if ln == c(6, width=3) else insn_raw
        is_macro = op_is(op, OP_FENTRY, OP_FEXIT, OP_FRET_RA, OP_FRET_STK)
        is_start = op_is(op, OP_C_BSTART_STD, OP_C_BSTART_COND, OP_C_BSTART_DIRECT, OP_BSTART_STD_FALL, OP_BSTART_STD_DIRECT, OP_BSTART_STD_COND, OP_BSTART_STD_CALL) | is_macro
        push_t = (regdst == c(31, width=6)) | (op == c(OP_C_LWI, width=12))
        push_u = regdst == c(30, width=6)
        is_store = op_is(op, OP_SBI, OP_SHI, OP_SWI, OP_C_SWI, OP_SDI, OP_C_SDI, OP_SB, OP_SH, OP_SW, OP_SD, OP_HL_SB_PCR, OP_HL_SH_PCR, OP_HL_SW_PCR, OP_HL_SD_PCR)
        dst_is_invalid = regdst == c(REG_INVALID, width=6)
        dst_is_zero = regdst == c(0, width=6)
        dst_is_gpr_range = ~regdst[5] & ~(regdst[4] & regdst[3])
        dst_is_gpr = dst_is_gpr_range & ~dst_is_invalid & ~dst_is_zero & ~push_t & ~push_u
        need_pdst = dst_is_gpr | push_t | push_u
        dk = c(0, width=2)
        dk = c(1, width=2) if dst_is_gpr else dk
        dk = c(2, width=2) if push_t else dk
        dk = c(3, width=2) if push_u else dk
        disp_valids.append(v)
        disp_pcs.append(pc)
        disp_ops.append(op)
        disp_lens.append(ln)
        disp_regdsts.append(regdst)
        disp_srcls.append(srcl)
        disp_srcrs.append(srcr)
        disp_srcr_types.append(srcr_type)
        disp_shamts.append(shamt)
        disp_srcps.append(srcp)
        disp_imms.append(imm)
        disp_insn_raws.append(insn_raw)
        disp_is_start_marker.append(is_start)
        disp_push_t.append(push_t)
        disp_push_u.append(push_u)
        disp_is_store.append(is_store)
        disp_dst_is_gpr.append(dst_is_gpr)
        disp_need_pdst.append(need_pdst)
        disp_dst_kind.append(dk)
    dec_op = disp_ops[0]
    disp_count = c(0, width=3)
    for slot in range(p.dispatch_w):
        disp_count = disp_count + unsigned(disp_valids[slot])
    rob_cnt_after = rob.count.out() + unsigned(disp_count)
    rob_space_ok = rob_cnt_after.ult(c(p.rob_depth + 1, width=p.rob_w + 1))
    disp_to_alu = []
    disp_to_bru = []
    disp_to_lsu = []
    for slot in range(p.dispatch_w):
        op = disp_ops[slot]
        is_macro = op_is(op, OP_FENTRY, OP_FEXIT, OP_FRET_RA, OP_FRET_STK)
        is_load = op_is(op, OP_LWI, OP_C_LWI, OP_LBI, OP_LBUI, OP_LHI, OP_LHUI, OP_LWUI, OP_LB, OP_LBU, OP_LH, OP_LHU, OP_LW, OP_LWU, OP_LD, OP_LDI, OP_C_LDI, OP_HL_LB_PCR, OP_HL_LBU_PCR, OP_HL_LH_PCR, OP_HL_LHU_PCR, OP_HL_LW_PCR, OP_HL_LWU_PCR, OP_HL_LD_PCR)
        is_store = disp_is_store[slot]
        is_mem = is_load | is_store
        is_bru = op_is(op, OP_C_BSTART_STD, OP_C_BSTART_COND, OP_C_BSTART_DIRECT, OP_C_BSTOP, OP_BSTART_STD_FALL, OP_BSTART_STD_DIRECT, OP_BSTART_STD_COND, OP_BSTART_STD_CALL, OP_FENTRY, OP_FEXIT, OP_FRET_RA, OP_FRET_STK, OP_C_SETC_EQ, OP_C_SETC_NE, OP_C_SETC_TGT)
        to_lsu = is_mem
        to_bru = ~to_lsu & is_bru & ~is_macro
        to_alu = ~to_lsu & ~to_bru & ~is_macro
        disp_to_alu.append(to_alu)
        disp_to_bru.append(to_bru)
        disp_to_lsu.append(to_lsu)
    alu_alloc_valids = []
    alu_alloc_idxs = []
    bru_alloc_valids = []
    bru_alloc_idxs = []
    lsu_alloc_valids = []
    lsu_alloc_idxs = []
    for slot in range(p.dispatch_w):
        req_alu = disp_valids[slot] & disp_to_alu[slot]
        v = consts.zero1
        idx = c(0, width=p.iq_w)
        for i in range(p.iq_depth):
            cidx = c(i, width=p.iq_w)
            free = ~iq_alu.valid[i].out()
            exclude = consts.zero1
            for prev in range(slot):
                prev_req = disp_valids[prev] & disp_to_alu[prev]
                exclude = exclude | prev_req & alu_alloc_valids[prev] & (alu_alloc_idxs[prev] == cidx)
            cand = req_alu & free & ~exclude
            take = cand & ~v
            v = consts.one1 if take else v
            idx = cidx if take else idx
        alu_alloc_valids.append(v)
        alu_alloc_idxs.append(idx)
        req_bru = disp_valids[slot] & disp_to_bru[slot]
        v = consts.zero1
        idx = c(0, width=p.iq_w)
        for i in range(p.iq_depth):
            cidx = c(i, width=p.iq_w)
            free = ~iq_bru.valid[i].out()
            exclude = consts.zero1
            for prev in range(slot):
                prev_req = disp_valids[prev] & disp_to_bru[prev]
                exclude = exclude | prev_req & bru_alloc_valids[prev] & (bru_alloc_idxs[prev] == cidx)
            cand = req_bru & free & ~exclude
            take = cand & ~v
            v = consts.one1 if take else v
            idx = cidx if take else idx
        bru_alloc_valids.append(v)
        bru_alloc_idxs.append(idx)
        req_lsu = disp_valids[slot] & disp_to_lsu[slot]
        v = consts.zero1
        idx = c(0, width=p.iq_w)
        for i in range(p.iq_depth):
            cidx = c(i, width=p.iq_w)
            free = ~iq_lsu.valid[i].out()
            exclude = consts.zero1
            for prev in range(slot):
                prev_req = disp_valids[prev] & disp_to_lsu[prev]
                exclude = exclude | prev_req & lsu_alloc_valids[prev] & (lsu_alloc_idxs[prev] == cidx)
            cand = req_lsu & free & ~exclude
            take = cand & ~v
            v = consts.one1 if take else v
            idx = cidx if take else idx
        lsu_alloc_valids.append(v)
        lsu_alloc_idxs.append(idx)
    alu_alloc_ok = consts.one1
    bru_alloc_ok = consts.one1
    lsu_alloc_ok = consts.one1
    for slot in range(p.dispatch_w):
        req_alu = disp_valids[slot] & disp_to_alu[slot]
        req_bru = disp_valids[slot] & disp_to_bru[slot]
        req_lsu = disp_valids[slot] & disp_to_lsu[slot]
        alu_alloc_ok = alu_alloc_ok & (~req_alu | alu_alloc_valids[slot])
        bru_alloc_ok = bru_alloc_ok & (~req_bru | bru_alloc_valids[slot])
        lsu_alloc_ok = lsu_alloc_ok & (~req_lsu | lsu_alloc_valids[slot])
    iq_alloc_ok = alu_alloc_ok & bru_alloc_ok & lsu_alloc_ok
    preg_alloc_valids = []
    preg_alloc_tags = []
    preg_alloc_onehots = []
    free_mask_stage = ren.free_mask.out()
    for slot in range(p.dispatch_w):
        req = disp_valids[slot] & disp_need_pdst[slot]
        v, tag, oh = alloc_from_free_mask(m, free_mask=free_mask_stage, width=p.pregs, tag_width=p.ptag_w)
        free_mask_stage = free_mask_stage & ~oh if req else free_mask_stage
        preg_alloc_valids.append(v)
        preg_alloc_tags.append(tag)
        preg_alloc_onehots.append(oh)
    preg_alloc_ok = consts.one1
    for slot in range(p.dispatch_w):
        req = disp_valids[slot] & disp_need_pdst[slot]
        preg_alloc_ok = preg_alloc_ok & (~req | preg_alloc_valids[slot])
    disp_pdsts = []
    disp_alloc_mask = c(0, width=p.pregs)
    for slot in range(p.dispatch_w):
        req = disp_valids[slot] & disp_need_pdst[slot]
        pdst = preg_alloc_tags[slot] if req else tag0
        oh = preg_alloc_onehots[slot] if req else c(0, width=p.pregs)
        disp_pdsts.append(pdst)
        disp_alloc_mask = disp_alloc_mask | oh
    dispatch_fire = can_run & ~commit_redirect & f4_valid & rob_space_ok & iq_alloc_ok & preg_alloc_ok
    fetch_bundle = decode_f4_bundle(m, mem_rdata)
    fetch_len = fetch_bundle.total_len_bytes
    fetch_advance = unsigned(fetch_bundle.total_len_bytes)
    fetch_fire = can_run & ~commit_redirect & ~any_load_fire & (~f4_valid | dispatch_fire)
    f4_valid_next = f4_valid
    f4_valid_next = consts.zero1 if dispatch_fire & ~fetch_fire else f4_valid_next
    f4_valid_next = consts.one1 if fetch_fire else f4_valid_next
    f4_valid_next = consts.zero1 if commit_redirect else f4_valid_next
    f4_valid_next = consts.zero1 if do_flush else f4_valid_next
    ifu.f4_valid.set(f4_valid_next)
    ifu.f4_pc.set(state.fpc.out(), when=fetch_fire)
    ifu.f4_window.set(mem_rdata, when=fetch_fire)
    smap_live = [ren.smap[i].out() for i in range(p.aregs)]
    disp_srcl_tags = []
    disp_srcr_tags = []
    disp_srcp_tags = []
    for slot in range(p.dispatch_w):
        srcl_areg = disp_srcls[slot]
        srcr_areg = disp_srcrs[slot]
        srcp_areg = disp_srcps[slot]
        srcl_tag = mux_by_uindex(m, idx=srcl_areg, items=smap_live, default=tag0)
        srcr_tag = mux_by_uindex(m, idx=srcr_areg, items=smap_live, default=tag0)
        srcp_tag = mux_by_uindex(m, idx=srcp_areg, items=smap_live, default=tag0)
        srcl_tag = tag0 if srcl_areg == c(REG_INVALID, width=6) else srcl_tag
        srcr_tag = tag0 if srcr_areg == c(REG_INVALID, width=6) else srcr_tag
        srcp_tag = tag0 if srcp_areg == c(REG_INVALID, width=6) else srcp_tag
        disp_srcl_tags.append(srcl_tag)
        disp_srcr_tags.append(srcr_tag)
        disp_srcp_tags.append(srcp_tag)
        lane_fire = dispatch_fire & disp_valids[slot]
        t0_old = smap_live[24]
        t1_old = smap_live[25]
        t2_old = smap_live[26]
        u0_old = smap_live[28]
        u1_old = smap_live[29]
        u2_old = smap_live[30]
        smap_next = []
        for i in range(p.aregs):
            nxt = smap_live[i]
            if 24 <= i <= 31:
                nxt = tag0 if lane_fire & disp_is_start_marker[slot] else nxt
            if i == 24:
                nxt = disp_pdsts[slot] if lane_fire & disp_push_t[slot] else nxt
            if i == 25:
                nxt = t0_old if lane_fire & disp_push_t[slot] else nxt
            if i == 26:
                nxt = t1_old if lane_fire & disp_push_t[slot] else nxt
            if i == 27:
                nxt = t2_old if lane_fire & disp_push_t[slot] else nxt
            if i == 28:
                nxt = disp_pdsts[slot] if lane_fire & disp_push_u[slot] else nxt
            if i == 29:
                nxt = u0_old if lane_fire & disp_push_u[slot] else nxt
            if i == 30:
                nxt = u1_old if lane_fire & disp_push_u[slot] else nxt
            if i == 31:
                nxt = u2_old if lane_fire & disp_push_u[slot] else nxt
            if i < 24:
                dst_match = disp_regdsts[slot] == c(i, width=6)
                nxt = disp_pdsts[slot] if lane_fire & disp_dst_is_gpr[slot] & dst_match else nxt
            if i == 0:
                nxt = tag0
            smap_next.append(nxt)
        smap_live = smap_next
    ready_next = ren.ready_mask.out()
    ready_next = ready_next & ~disp_alloc_mask if dispatch_fire else ready_next
    wb_set_mask = c(0, width=p.pregs)
    for slot in range(p.issue_w):
        wb_set_mask = wb_set_mask | wb_onehots[slot] if wb_fire_has_dsts[slot] else wb_set_mask
    ready_next = ready_next | wb_set_mask
    ready_next = c((1 << p.pregs) - 1, width=p.pregs) if do_flush else ready_next
    ren.ready_mask.set(ready_next)
    ra_tag = ra_write_tag
    for i in range(p.pregs):
        we = consts.zero1
        wdata = consts.zero64
        for slot in range(p.issue_w):
            hit = wb_fire_has_dsts[slot] & (wb_pdsts[slot] == c(i, width=p.ptag_w))
            we = we | hit
            wdata = wb_values[slot] if hit else wdata
        hit_ra = ra_write_fire & (ra_tag == c(i, width=p.ptag_w))
        we = we | hit_ra
        wdata = ra_write_value if hit_ra else wdata
        hit_macro = macro_prf_we & (macro_prf_tag == c(i, width=p.ptag_w))
        we = we | hit_macro
        wdata = macro_prf_data if hit_macro else wdata
        prf[i].set(wdata, when=we)
    disp_rob_idxs = []
    disp_fires = []
    for slot in range(p.dispatch_w):
        disp_rob_idxs.append(rob.tail.out() + c(slot, width=p.rob_w))
        disp_fires.append(dispatch_fire & disp_valids[slot])
    for i in range(p.rob_depth):
        idx = c(i, width=p.rob_w)
        commit_hit = consts.zero1
        for slot in range(p.commit_w):
            commit_hit = commit_hit | commit_fires[slot] & (commit_idxs[slot] == idx)
        disp_hit = consts.zero1
        for slot in range(p.dispatch_w):
            disp_hit = disp_hit | disp_fires[slot] & (disp_rob_idxs[slot] == idx)
        wb_hit = consts.zero1
        for slot in range(p.issue_w):
            wb_hit = wb_hit | wb_fires[slot] & (wb_robs[slot] == idx)
        v_next = rob.valid[i].out()
        v_next = consts.zero1 if do_flush else v_next
        v_next = consts.zero1 if commit_hit else v_next
        v_next = consts.one1 if disp_hit else v_next
        rob.valid[i].set(v_next)
        done_next = rob.done[i].out()
        done_next = consts.zero1 if do_flush else done_next
        done_next = consts.zero1 if commit_hit else done_next
        done_next = consts.zero1 if disp_hit else done_next
        for slot in range(p.dispatch_w):
            hit = disp_fires[slot] & (disp_rob_idxs[slot] == idx)
            is_macro = op_is(disp_ops[slot], OP_FENTRY, OP_FEXIT, OP_FRET_RA, OP_FRET_STK)
            done_next = consts.one1 if hit & is_macro else done_next
        done_next = consts.one1 if wb_hit else done_next
        rob.done[i].set(done_next)
        op_next = rob.op[i].out()
        for slot in range(p.dispatch_w):
            hit = disp_fires[slot] & (disp_rob_idxs[slot] == idx)
            op_next = disp_ops[slot] if hit else op_next
        rob.op[i].set(op_next)
        ln_next = rob.len_bytes[i].out()
        for slot in range(p.dispatch_w):
            hit = disp_fires[slot] & (disp_rob_idxs[slot] == idx)
            ln_next = disp_lens[slot] if hit else ln_next
        rob.len_bytes[i].set(ln_next)
        insn_next = rob.insn_raw[i].out()
        for slot in range(p.dispatch_w):
            hit = disp_fires[slot] & (disp_rob_idxs[slot] == idx)
            insn_next = disp_insn_raws[slot] if hit else insn_next
        rob.insn_raw[i].set(insn_next)
        dk_next = rob.dst_kind[i].out()
        for slot in range(p.dispatch_w):
            hit = disp_fires[slot] & (disp_rob_idxs[slot] == idx)
            dk_next = disp_dst_kind[slot] if hit else dk_next
        rob.dst_kind[i].set(dk_next)
        da_next = rob.dst_areg[i].out()
        for slot in range(p.dispatch_w):
            hit = disp_fires[slot] & (disp_rob_idxs[slot] == idx)
            da_next = disp_regdsts[slot] if hit else da_next
        rob.dst_areg[i].set(da_next)
        pd_next = rob.pdst[i].out()
        for slot in range(p.dispatch_w):
            hit = disp_fires[slot] & (disp_rob_idxs[slot] == idx)
            pd_next = disp_pdsts[slot] if hit else pd_next
        rob.pdst[i].set(pd_next)
        val_next = rob.value[i].out()
        val_next = consts.zero64 if disp_hit else val_next
        for slot in range(p.dispatch_w):
            hit = disp_fires[slot] & (disp_rob_idxs[slot] == idx)
            is_macro = op_is(disp_ops[slot], OP_FENTRY, OP_FEXIT, OP_FRET_RA, OP_FRET_STK)
            val_next = disp_imms[slot] if hit & is_macro else val_next
        for slot in range(p.issue_w):
            hit = wb_fires[slot] & (wb_robs[slot] == idx)
            val_next = wb_values[slot] if hit else val_next
        rob.value[i].set(val_next)
        is_store_next = rob.is_store[i].out()
        is_load_next = rob.is_load[i].out()
        for slot in range(p.dispatch_w):
            hit = disp_fires[slot] & (disp_rob_idxs[slot] == idx)
            is_store_next = disp_is_store[slot] if hit else is_store_next
            is_load_next = consts.zero1 if hit else is_load_next
        st_addr_next = rob.store_addr[i].out()
        st_data_next = rob.store_data[i].out()
        st_size_next = rob.store_size[i].out()
        st_addr_next = consts.zero64 if disp_hit else st_addr_next
        st_data_next = consts.zero64 if disp_hit else st_data_next
        st_size_next = consts.zero4 if disp_hit else st_size_next
        for slot in range(p.issue_w):
            hit = store_fires[slot] & (wb_robs[slot] == idx)
            st_addr_next = exs[slot].addr if hit else st_addr_next
            st_data_next = exs[slot].wdata if hit else st_data_next
            st_size_next = exs[slot].size if hit else st_size_next
        rob.store_addr[i].set(st_addr_next)
        rob.store_data[i].set(st_data_next)
        rob.store_size[i].set(st_size_next)
        ld_addr_next = rob.load_addr[i].out()
        ld_data_next = rob.load_data[i].out()
        ld_size_next = rob.load_size[i].out()
        ld_addr_next = consts.zero64 if disp_hit else ld_addr_next
        ld_data_next = consts.zero64 if disp_hit else ld_data_next
        ld_size_next = consts.zero4 if disp_hit else ld_size_next
        for slot in range(p.issue_w):
            hit = load_fires[slot] & (wb_robs[slot] == idx)
            ld_addr_next = exs[slot].addr if hit else ld_addr_next
            ld_data_next = wb_values[slot] if hit else ld_data_next
            ld_size_next = exs[slot].size if hit else ld_size_next
            is_load_next = consts.one1 if hit else is_load_next
            is_store_next = consts.zero1 if hit else is_store_next
        rob.load_addr[i].set(ld_addr_next)
        rob.load_data[i].set(ld_data_next)
        rob.load_size[i].set(ld_size_next)
        rob.is_load[i].set(is_load_next)
        rob.is_store[i].set(is_store_next)
        mb_next = rob.macro_begin[i].out()
        me_next = rob.macro_end[i].out()
        for slot in range(p.dispatch_w):
            hit = disp_fires[slot] & (disp_rob_idxs[slot] == idx)
            mb_next = disp_srcls[slot] if hit else mb_next
            me_next = disp_srcrs[slot] if hit else me_next
        rob.macro_begin[i].set(mb_next)
        rob.macro_end[i].set(me_next)
    head_next = rob.head.out()
    tail_next = rob.tail.out()
    count_next = rob.count.out()
    head_next = c(0, width=p.rob_w) if do_flush else head_next
    tail_next = c(0, width=p.rob_w) if do_flush else tail_next
    count_next = c(0, width=p.rob_w + 1) if do_flush else count_next
    inc_head = commit_fire & ~do_flush
    inc_tail = dispatch_fire & ~do_flush
    head_inc = commit_count
    if p.rob_w > head_inc.width:
        head_inc = unsigned(head_inc)
    elif p.rob_w < head_inc.width:
        head_inc = head_inc[0:p.rob_w]
    head_next = rob.head.out() + head_inc if inc_head else head_next
    disp_tail_inc = disp_count
    if p.rob_w > disp_tail_inc.width:
        disp_tail_inc = unsigned(disp_tail_inc)
    elif p.rob_w < disp_tail_inc.width:
        disp_tail_inc = disp_tail_inc[0:p.rob_w]
    tail_next = rob.tail.out() + disp_tail_inc if inc_tail else tail_next
    commit_dec = unsigned(commit_count)
    commit_dec_neg = ~commit_dec + c(1, width=p.rob_w + 1)
    count_next = count_next + unsigned(disp_count) if inc_tail else count_next
    count_next = count_next + commit_dec_neg if inc_head else count_next
    rob.head.set(head_next)
    rob.tail.set(tail_next)
    rob.count.set(count_next)

    def update_iq(*, iq, disp_to: list, alloc_idxs: list, issue_fires_q: list, issue_idxs_q: list) -> None:
        for i in range(p.iq_depth):
            idx = c(i, width=p.iq_w)
            issue_clear = consts.zero1
            for slot in range(len(issue_fires_q)):
                issue_clear = issue_clear | issue_fires_q[slot] & (issue_idxs_q[slot] == idx)
            disp_alloc_hit = consts.zero1
            for slot in range(p.dispatch_w):
                disp_alloc_hit = disp_alloc_hit | disp_fires[slot] & disp_to[slot] & (alloc_idxs[slot] == idx)
            v_next = iq.valid[i].out()
            v_next = consts.zero1 if do_flush else v_next
            v_next = consts.zero1 if issue_clear else v_next
            v_next = consts.one1 if disp_alloc_hit else v_next
            iq.valid[i].set(v_next)
            robn = iq.rob[i].out()
            opn = iq.op[i].out()
            pcn = iq.pc[i].out()
            imn = iq.imm[i].out()
            sln = iq.srcl[i].out()
            srn = iq.srcr[i].out()
            stn = iq.srcr_type[i].out()
            shn = iq.shamt[i].out()
            spn = iq.srcp[i].out()
            pdn = iq.pdst[i].out()
            hdn = iq.has_dst[i].out()
            for slot in range(p.dispatch_w):
                hit = disp_fires[slot] & disp_to[slot] & (alloc_idxs[slot] == idx)
                robn = disp_rob_idxs[slot] if hit else robn
                opn = disp_ops[slot] if hit else opn
                pcn = disp_pcs[slot] if hit else pcn
                imn = disp_imms[slot] if hit else imn
                sln = disp_srcl_tags[slot] if hit else sln
                srn = disp_srcr_tags[slot] if hit else srn
                stn = disp_srcr_types[slot] if hit else stn
                shn = disp_shamts[slot] if hit else shn
                spn = disp_srcp_tags[slot] if hit else spn
                pdn = disp_pdsts[slot] if hit else pdn
                hdn = disp_need_pdst[slot] if hit else hdn
            iq.rob[i].set(robn)
            iq.op[i].set(opn)
            iq.pc[i].set(pcn)
            iq.imm[i].set(imn)
            iq.srcl[i].set(sln)
            iq.srcr[i].set(srn)
            iq.srcr_type[i].set(stn)
            iq.shamt[i].set(shn)
            iq.srcp[i].set(spn)
            iq.pdst[i].set(pdn)
            iq.has_dst[i].set(hdn)
    lsu_base = 0
    bru_base = p.lsu_w
    alu_base = p.lsu_w + p.bru_w
    update_iq(iq=iq_lsu, disp_to=disp_to_lsu, alloc_idxs=lsu_alloc_idxs, issue_fires_q=issue_fires[lsu_base:lsu_base + p.lsu_w], issue_idxs_q=lsu_issue_idxs)
    update_iq(iq=iq_bru, disp_to=disp_to_bru, alloc_idxs=bru_alloc_idxs, issue_fires_q=issue_fires[bru_base:bru_base + p.bru_w], issue_idxs_q=bru_issue_idxs)
    update_iq(iq=iq_alu, disp_to=disp_to_alu, alloc_idxs=alu_alloc_idxs, issue_fires_q=issue_fires[alu_base:alu_base + p.alu_w], issue_idxs_q=alu_issue_idxs)
    for i in range(p.aregs):
        nxt = smap_live[i]
        nxt = ren.cmap[i].out() if do_flush else nxt
        if i == 0:
            nxt = tag0
        ren.smap[i].set(nxt)
    cmap_live = [ren.cmap[i].out() for i in range(p.aregs)]
    free_after_dispatch = ren.free_mask.out() & ~disp_alloc_mask if dispatch_fire else ren.free_mask.out()
    free_live = free_after_dispatch
    for slot in range(p.commit_w):
        fire = commit_fires[slot]
        op = rob_ops[slot]
        dk = rob_dst_kinds[slot]
        areg = rob_dst_aregs[slot]
        pdst = rob_pdsts[slot]
        is_macro = op_is(op, OP_FENTRY, OP_FEXIT, OP_FRET_RA, OP_FRET_STK)
        is_start_marker = op_is(op, OP_C_BSTART_STD, OP_C_BSTART_COND, OP_C_BSTART_DIRECT, OP_BSTART_STD_FALL, OP_BSTART_STD_DIRECT, OP_BSTART_STD_COND, OP_BSTART_STD_CALL) | is_macro
        old_t0 = cmap_live[24]
        old_t1 = cmap_live[25]
        old_t2 = cmap_live[26]
        old_t3 = cmap_live[27]
        old_u0 = cmap_live[28]
        old_u1 = cmap_live[29]
        old_u2 = cmap_live[30]
        old_u3 = cmap_live[31]
        if_free = commit_enter_new_blocks[slot]
        for old in [old_t0, old_t1, old_t2, old_t3, old_u0, old_u1, old_u2, old_u3]:
            oh = onehot_from_tag(m, tag=old, width=p.pregs, tag_width=p.ptag_w)
            free_live = free_live | oh if if_free & ~(old == tag0) else free_live
        for i in range(24, 32):
            cmap_live[i] = tag0 if if_free else cmap_live[i]
        push_t = fire & (dk == c(2, width=2))
        t3_oh = onehot_from_tag(m, tag=old_t3, width=p.pregs, tag_width=p.ptag_w)
        free_live = free_live | t3_oh if push_t & ~(old_t3 == tag0) else free_live
        cmap_live[24] = pdst if push_t else cmap_live[24]
        cmap_live[25] = old_t0 if push_t else cmap_live[25]
        cmap_live[26] = old_t1 if push_t else cmap_live[26]
        cmap_live[27] = old_t2 if push_t else cmap_live[27]
        push_u = fire & (dk == c(3, width=2))
        u3_oh = onehot_from_tag(m, tag=old_u3, width=p.pregs, tag_width=p.ptag_w)
        free_live = free_live | u3_oh if push_u & ~(old_u3 == tag0) else free_live
        cmap_live[28] = pdst if push_u else cmap_live[28]
        cmap_live[29] = old_u0 if push_u else cmap_live[29]
        cmap_live[30] = old_u1 if push_u else cmap_live[30]
        cmap_live[31] = old_u2 if push_u else cmap_live[31]
        is_gpr = fire & (dk == c(1, width=2))
        for i in range(24):
            hit = is_gpr & (areg == c(i, width=6))
            old = cmap_live[i]
            old_oh = onehot_from_tag(m, tag=old, width=p.pregs, tag_width=p.ptag_w)
            free_live = free_live | old_oh if hit & ~(old == tag0) else free_live
            cmap_live[i] = pdst if hit else cmap_live[i]
        cmap_live[0] = tag0
    for i in range(p.aregs):
        ren.cmap[i].set(cmap_live[i])
    used = c(0, width=p.pregs)
    for i in range(p.aregs):
        used = used | onehot_from_tag(m, tag=ren.cmap[i].out(), width=p.pregs, tag_width=p.ptag_w)
    free_recomputed = ~used
    free_next = free_recomputed if do_flush else free_live
    ren.free_mask.set(free_next)
    state.pc.set(pc_live)
    fpc_next = state.fpc.out()
    fpc_next = state.fpc.out() + fetch_advance if fetch_fire else fpc_next
    fpc_next = redirect_pc if commit_redirect else fpc_next
    fpc_next = state.flush_pc.out() if do_flush else fpc_next
    state.fpc.set(fpc_next)
    state.flush_pc.set(redirect_pc if commit_redirect else state.flush_pc.out())
    flush_pend_next = state.flush_pending.out()
    flush_pend_next = consts.zero1 if do_flush else flush_pend_next
    flush_pend_next = consts.one1 if commit_redirect else flush_pend_next
    state.flush_pending.set(flush_pend_next)
    halt_set = consts.zero1
    for slot in range(p.commit_w):
        op = rob_ops[slot]
        is_halt = (op == c(OP_EBREAK, width=12)) | (op == c(OP_INVALID, width=12))
        halt_set = halt_set | commit_fires[slot] & is_halt
    halt_set = halt_set | mmio_exit
    state.halted.set(consts.one1, when=halt_set)
    state.cycles.set(state.cycles.out() + consts.one64)
    state.commit_cond.set(commit_cond_live)
    state.commit_tgt.set(commit_tgt_live)
    state.br_kind.set(br_kind_live)
    state.br_base_pc.set(br_base_live)
    state.br_off.set(br_off_live)
    ph_init = c(0, width=2)
    ph_mem = c(1, width=2)
    ph_sp = c(2, width=2)
    macro_active_n = macro_active
    macro_phase_n = macro_phase
    macro_op_n = macro_op
    macro_begin_n = state.macro_begin.out()
    macro_end_n = state.macro_end.out()
    macro_stack_n = macro_stacksize
    macro_reg_n = macro_reg
    macro_i_n = macro_i
    macro_sp_base_n = macro_sp_base
    macro_active_n = consts.zero1 if do_flush else macro_active_n
    macro_phase_n = ph_init if do_flush else macro_phase_n
    macro_active_n = consts.one1 if macro_start else macro_active_n
    macro_phase_n = ph_init if macro_start else macro_phase_n
    macro_op_n = head_op if macro_start else macro_op_n
    macro_begin_n = head_macro_begin if macro_start else macro_begin_n
    macro_end_n = head_macro_end if macro_start else macro_end_n
    macro_stack_n = head_value if macro_start else macro_stack_n
    macro_reg_n = head_macro_begin if macro_start else macro_reg_n
    macro_i_n = c(0, width=6) if macro_start else macro_i_n
    macro_phase_is_init = macro_phase == ph_init
    macro_phase_is_mem = macro_phase == ph_mem
    macro_phase_is_sp = macro_phase == ph_sp
    macro_is_restore = macro_active & ~macro_is_fentry
    init_fire = macro_active & macro_phase_is_init
    sp_new_init = macro_sp_val - macro_stacksize
    macro_sp_base_n = sp_new_init if init_fire & macro_is_fentry else macro_sp_base_n
    macro_sp_base_n = macro_sp_val if init_fire & macro_is_restore else macro_sp_base_n
    macro_reg_n = state.macro_begin.out() if init_fire else macro_reg_n
    macro_i_n = c(0, width=6) if init_fire else macro_i_n
    macro_phase_n = ph_mem if init_fire else macro_phase_n
    step_fire = macro_active & macro_phase_is_mem
    step_done = step_fire & (~macro_off_ok | (macro_reg == state.macro_end.out()))
    reg_plus = macro_reg + c(1, width=6)
    reg_wrap = reg_plus.ugt(c(23, width=6))
    reg_next = c(2, width=6) if reg_wrap else reg_plus
    macro_reg_n = reg_next if step_fire & ~step_done else macro_reg_n
    macro_i_n = macro_i + c(1, width=6) if step_fire & ~step_done else macro_i_n
    macro_phase_n = ph_sp if step_done & macro_is_restore else macro_phase_n
    macro_active_n = consts.zero1 if step_done & macro_is_fentry else macro_active_n
    macro_phase_n = ph_init if step_done & macro_is_fentry else macro_phase_n
    sp_fire = macro_active & macro_phase_is_sp
    macro_active_n = consts.zero1 if sp_fire else macro_active_n
    macro_phase_n = ph_init if sp_fire else macro_phase_n
    macro_wait_n = state.macro_wait_commit.out()
    macro_wait_n = consts.zero1 if do_flush else macro_wait_n
    macro_wait_n = consts.one1 if macro_start else macro_wait_n
    macro_committed = consts.zero1
    for slot in range(p.commit_w):
        op = rob_ops[slot]
        fire = commit_fires[slot]
        macro_committed = macro_committed | fire & op_is(op, OP_FENTRY, OP_FEXIT, OP_FRET_RA, OP_FRET_STK)
    macro_wait_n = consts.zero1 if macro_committed else macro_wait_n
    state.macro_active.set(macro_active_n)
    state.macro_wait_commit.set(macro_wait_n)
    state.macro_phase.set(macro_phase_n)
    state.macro_op.set(macro_op_n)
    state.macro_begin.set(macro_begin_n)
    state.macro_end.set(macro_end_n)
    state.macro_stacksize.set(macro_stack_n)
    state.macro_reg.set(macro_reg_n)
    state.macro_i.set(macro_i_n)
    state.macro_sp_base.set(macro_sp_base_n)
    a0_tag = ren.cmap[2].out()
    a1_tag = ren.cmap[3].out()
    ra_tag = ren.cmap[10].out()
    sp_tag = ren.cmap[1].out()
    m.output('halted', state.halted)
    m.output('cycles', state.cycles)
    m.output('pc', state.pc)
    m.output('fpc', state.fpc)
    m.output('a0', mux_by_uindex(m, idx=a0_tag, items=prf, default=consts.zero64))
    m.output('a1', mux_by_uindex(m, idx=a1_tag, items=prf, default=consts.zero64))
    m.output('ra', mux_by_uindex(m, idx=ra_tag, items=prf, default=consts.zero64))
    m.output('sp', mux_by_uindex(m, idx=sp_tag, items=prf, default=consts.zero64))
    m.output('commit_op', head_op)
    m.output('commit_fire', commit_fire)
    m.output('commit_value', head_value)
    m.output('commit_dst_kind', head_dst_kind)
    m.output('commit_dst_areg', head_dst_areg)
    m.output('commit_pdst', head_pdst)
    m.output('commit_cond', state.commit_cond)
    m.output('commit_tgt', state.commit_tgt)
    m.output('br_kind', state.br_kind)
    m.output('br_base_pc', state.br_base_pc)
    m.output('br_off', state.br_off)
    m.output('commit_store_fire', commit_store_fire)
    m.output('commit_store_addr', commit_store_addr)
    m.output('commit_store_data', commit_store_data)
    m.output('commit_store_size', commit_store_size)
    max_commit_slots = 4
    for slot in range(max_commit_slots):
        fire = consts.zero1
        pc = consts.zero64
        rob_idx = c(0, width=p.rob_w)
        op = c(0, width=12)
        val = consts.zero64
        ln = consts.zero3
        insn_raw = consts.zero64
        wb_valid = consts.zero1
        wb_rd = c(0, width=6)
        wb_data = consts.zero64
        mem_valid = consts.zero1
        mem_is_store = consts.zero1
        mem_addr = consts.zero64
        mem_wdata = consts.zero64
        mem_rdata = consts.zero64
        mem_size = consts.zero4
        trap_valid = consts.zero1
        trap_cause = c(0, width=32)
        next_pc = consts.zero64
        if slot < p.commit_w:
            fire = commit_fires[slot]
            pc = commit_pcs[slot]
            rob_idx = commit_idxs[slot]
            op = rob_ops[slot]
            val = rob_values[slot]
            ln = rob_lens[slot]
            insn_raw = rob_insn_raws[slot]
            is_gpr_dst = rob_dst_kinds[slot] == c(1, width=2)
            rd = rob_dst_aregs[slot]
            wb_valid = fire & is_gpr_dst & ~(rd == c(0, width=6)) & rd.ult(c(24, width=6))
            wb_rd = rd
            wb_data = rob_values[slot]
            is_store = rob_is_stores[slot]
            is_load = rob_is_loads[slot]
            mem_valid = fire & (is_store | is_load)
            mem_is_store = fire & is_store
            mem_addr = rob_st_addrs[slot] if is_store else rob_ld_addrs[slot]
            mem_wdata = rob_st_datas[slot] if is_store else consts.zero64
            mem_rdata = rob_ld_datas[slot] if is_load else consts.zero64
            mem_size = rob_st_sizes[slot] if is_store else rob_ld_sizes[slot]
            next_pc = commit_next_pcs[slot]
        m.output(f'commit_fire{slot}', fire)
        m.output(f'commit_pc{slot}', pc)
        m.output(f'commit_rob{slot}', rob_idx)
        m.output(f'commit_op{slot}', op)
        m.output(f'commit_value{slot}', val)
        m.output(f'commit_len{slot}', ln)
        m.output(f'commit_insn_raw{slot}', insn_raw)
        m.output(f'commit_wb_valid{slot}', wb_valid)
        m.output(f'commit_wb_rd{slot}', wb_rd)
        m.output(f'commit_wb_data{slot}', wb_data)
        m.output(f'commit_mem_valid{slot}', mem_valid)
        m.output(f'commit_mem_is_store{slot}', mem_is_store)
        m.output(f'commit_mem_addr{slot}', mem_addr)
        m.output(f'commit_mem_wdata{slot}', mem_wdata)
        m.output(f'commit_mem_rdata{slot}', mem_rdata)
        m.output(f'commit_mem_size{slot}', mem_size)
        m.output(f'commit_trap_valid{slot}', trap_valid)
        m.output(f'commit_trap_cause{slot}', trap_cause)
        m.output(f'commit_next_pc{slot}', next_pc)
    m.output('rob_count', rob.count)
    ct0_tag = ren.cmap[24].out()
    cu0_tag = ren.cmap[28].out()
    st0_tag = ren.smap[24].out()
    su0_tag = ren.smap[28].out()
    m.output('ct0', mux_by_uindex(m, idx=ct0_tag, items=prf, default=consts.zero64))
    m.output('cu0', mux_by_uindex(m, idx=cu0_tag, items=prf, default=consts.zero64))
    m.output('st0', mux_by_uindex(m, idx=st0_tag, items=prf, default=consts.zero64))
    m.output('su0', mux_by_uindex(m, idx=su0_tag, items=prf, default=consts.zero64))
    m.output('issue_fire', issue_fire)
    m.output('issue_op', uop_op)
    m.output('issue_pc', uop_pc)
    m.output('issue_rob', uop_rob)
    m.output('issue_sl', uop_sl)
    m.output('issue_sr', uop_sr)
    m.output('issue_sp', uop_sp)
    m.output('issue_pdst', uop_pdst)
    m.output('issue_sl_val', sl_val)
    m.output('issue_sr_val', sr_val)
    m.output('issue_sp_val', sp_val)
    m.output('issue_is_load', issued_is_load)
    m.output('issue_is_store', issued_is_store)
    m.output('store_pending', store_pending)
    m.output('store_pending_older', older_store_pending)
    m.output('mem_raddr', mem_raddr)
    m.output('dispatch_fire', dispatch_fire)
    m.output('dec_op', dec_op)
    max_disp_slots = 4
    for slot in range(max_disp_slots):
        fire = consts.zero1
        pc = consts.zero64
        rob_i = c(0, width=p.rob_w)
        op = c(0, width=12)
        if slot < p.dispatch_w:
            fire = disp_fires[slot]
            pc = disp_pcs[slot]
            rob_i = disp_rob_idxs[slot]
            op = disp_ops[slot]
        m.output(f'dispatch_fire{slot}', fire)
        m.output(f'dispatch_pc{slot}', pc)
        m.output(f'dispatch_rob{slot}', rob_i)
        m.output(f'dispatch_op{slot}', op)
    max_issue_slots = 4
    for slot in range(max_issue_slots):
        fire = consts.zero1
        pc = consts.zero64
        rob_i = c(0, width=p.rob_w)
        op = c(0, width=12)
        if slot < p.issue_w:
            fire = issue_fires[slot]
            pc = uop_pcs[slot]
            rob_i = uop_robs[slot]
            op = uop_ops[slot]
        m.output(f'issue_fire{slot}', fire)
        m.output(f'issue_pc{slot}', pc)
        m.output(f'issue_rob{slot}', rob_i)
        m.output(f'issue_op{slot}', op)
    m.output('mmio_uart_valid', mmio_uart)
    m.output('mmio_uart_data', mmio_uart_data)
    m.output('mmio_exit_valid', mmio_exit)
    m.output('mmio_exit_code', mmio_exit_code)
    block_cmd_valid = commit_fire & op_is(head_op, OP_C_BSTART_STD, OP_C_BSTART_COND, OP_BSTART_STD_CALL)
    block_cmd_kind = c(1, width=2) if head_op == c(OP_C_BSTART_COND, width=6) else c(0, width=2)
    block_cmd_kind = c(2, width=2) if head_op == c(OP_BSTART_STD_CALL, width=6) else block_cmd_kind
    block_cmd_payload = head_value
    block_cmd_tile = head_value[0:6]
    block_cmd_tag = state.cycles.out()[0:8]
    ooo_4wide = c(1 if p.fetch_w == 4 and p.dispatch_w == 4 and (p.issue_w == 4) and (p.commit_w == 4) else 0, width=1)
    m.output('ooo_4wide', ooo_4wide)
    m.output('block_cmd_valid', block_cmd_valid)
    m.output('block_cmd_kind', block_cmd_kind)
    m.output('block_cmd_payload', block_cmd_payload)
    m.output('block_cmd_tile', block_cmd_tile)
    m.output('block_cmd_tag', block_cmd_tag)
    return BccOooExports(clk=clk, rst=rst, block_cmd_valid=block_cmd_valid.sig, block_cmd_kind=block_cmd_kind.sig, block_cmd_payload=block_cmd_payload.sig, block_cmd_tile=block_cmd_tile.sig, block_cmd_tag=block_cmd_tag.sig, cycles=state.cycles.out().sig, halted=state.halted.out().sig)
