# -*- coding: utf-8 -*-
"""LinxISA CPU — true 5-stage parallel pipeline with forwarding & stalling.

所有 5 级（IF/ID/EX/MEM/WB）每个时钟周期同时运行，每级处理不同指令。
- fetch_pc_reg 每拍按 quick_len 推进。
- 分支误预测在 WB 级检测，full flush + redirect。
- 数据前递：WB → ID（同拍），MEM → ID（同拍），EX → ID（非 load 时同拍）。
- Load-use 互锁：EX 级为 load 且 ID 需要其结果时，stall 1 拍。
- 所有流水线寄存器用 ca_reg，不依赖 domain.next() / domain.cycle()。
"""
from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    CycleAwareSignal,
    compile_cycle_aware,
    mux,
)

from .isa import (
    BK_FALL,
    OP_BSTART_STD_CALL,
    OP_C_BSTART_COND,
    OP_C_BSTART_STD,
    OP_C_LWI,
    OP_EBREAK,
    OP_INVALID,
    OP_LWI,
    REG_INVALID,
)
from .memory import build_byte_mem
from .pipeline import CoreState, RegFiles
from .regfile import make_gpr, make_regs, read_reg
from .util import make_consts
from .decode import decode_window
from .stages.ex_stage import ex_stage_logic
from .stages.mem_stage import mem_stage_logic
from .stages.wb_stage import wb_stage_updates


def _linx_cpu_impl(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    mem_bytes: int,
) -> None:
    # ---------- 输入与常量 ----------
    boot_pc = domain.create_signal("boot_pc", width=64)
    boot_sp = domain.create_signal("boot_sp", width=64)
    consts = make_consts(m, domain)
    zero64 = consts.zero64
    zero8 = consts.zero8
    zero1 = consts.zero1
    c = lambda v, w: m.ca_const(v, width=w, domain=domain)

    # ---------- 架构状态 ----------
    state = CoreState(
        stage=m.ca_reg("state_stage", domain=domain, width=3, init=0),
        pc=m.ca_reg("state_pc", domain=domain, width=64, init=0),
        br_kind=m.ca_reg("state_br_kind", domain=domain, width=2, init=BK_FALL),
        br_base_pc=m.ca_reg("state_br_base_pc", domain=domain, width=64, init=0),
        br_off=m.ca_reg("state_br_off", domain=domain, width=64, init=0),
        commit_cond=m.ca_reg("state_commit_cond", domain=domain, width=1, init=0),
        commit_tgt=m.ca_reg("state_commit_tgt", domain=domain, width=64, init=0),
        cycles=m.ca_reg("state_cycles", domain=domain, width=64, init=0),
        halted=m.ca_reg("state_halted", domain=domain, width=1, init=0),
    )
    is_first = state.cycles.out().eq(0)
    state.pc.set(boot_pc, when=is_first)
    state.br_base_pc.set(boot_pc, when=is_first)

    fetch_pc_reg = m.ca_reg("fetch_pc", domain=domain, width=64, init=0)

    # ---------- 反馈信号（后级 → 前级，预声明）----------
    # 预先声明需要从后级流水线反馈到前级的信号。
    # 语义：该信号在产生的流水线级通过 m.assign 赋值（LHS），
    #       在更早的流水线级作为 RHS 直接引用，不触发自动周期平衡。
    # 实现：named_wire 创建占位信号，包装为当前周期的 CycleAwareSignal，
    #       后续 m.assign 驱动该 wire，所有引用处获得组合连接。
    _fb_flush_w = m.named_wire("fb_flush", width=1)
    _fb_redirect_pc_w = m.named_wire("fb_redirect_pc", width=64)
    # flush:       WB 级产生 → IF/ID/EX/MEM 级引用（分支冲刷）
    flush = CycleAwareSignal(
        m=m, sig=_fb_flush_w.sig, cycle=domain.current_cycle,
        domain=domain, name="fb_flush")
    # redirect_pc: WB 级产生 → IF 级引用（分支目标地址）
    redirect_pc = CycleAwareSignal(
        m=m, sig=_fb_redirect_pc_w.sig, cycle=domain.current_cycle,
        domain=domain, name="fb_redirect_pc")

    # ---------- 累积停顿反馈信号（后级 → 所有前级）----------
    # 各级（ID / EX / MEM / WB）均可产生 stall 条件，向前传播冻结所有前序流水级：
    #   stall_wb  → 冻结 MEM, EX, ID, IF
    #   stall_mem → 冻结 EX, ID, IF
    #   stall_ex  → 冻结 ID, IF
    #   stall_id  → 冻结 IF
    # freeze_XX = OR(该级之后所有级产生的 stall) & ~flush（flush 优先于 stall）。
    _fb_freeze_if_w  = m.named_wire("fb_freeze_if",  width=1)
    _fb_freeze_id_w  = m.named_wire("fb_freeze_id",  width=1)
    _fb_freeze_ex_w  = m.named_wire("fb_freeze_ex",  width=1)
    _fb_freeze_mem_w = m.named_wire("fb_freeze_mem", width=1)
    freeze_if = CycleAwareSignal(
        m=m, sig=_fb_freeze_if_w.sig, cycle=domain.current_cycle,
        domain=domain, name="fb_freeze_if")
    freeze_id = CycleAwareSignal(
        m=m, sig=_fb_freeze_id_w.sig, cycle=domain.current_cycle,
        domain=domain, name="fb_freeze_id")
    freeze_ex = CycleAwareSignal(
        m=m, sig=_fb_freeze_ex_w.sig, cycle=domain.current_cycle,
        domain=domain, name="fb_freeze_ex")
    freeze_mem = CycleAwareSignal(
        m=m, sig=_fb_freeze_mem_w.sig, cycle=domain.current_cycle,
        domain=domain, name="fb_freeze_mem")

    # ---------- 寄存器堆 ----------
    gpr = make_gpr(m, domain, boot_sp=boot_sp)
    gpr[1].set(boot_sp, when=is_first)
    t = make_regs(m, domain, count=4, width=64, init=0)
    u = make_regs(m, domain, count=4, width=64, init=0)
    rf = RegFiles(gpr=gpr, t=t, u=u)

    # ================================================================
    # 所有流水线寄存器（ca_reg）——DFF 输出在本拍开始时立即可读
    # ================================================================
    # IF/ID
    ifid_window_r = m.ca_reg("ifid_window", domain=domain, width=64, init=0)
    ifid_pc_r = m.ca_reg("ifid_pc", domain=domain, width=64, init=0)
    valid_id_r = m.ca_reg("valid_id", domain=domain, width=1, init=0)
    # ID/EX
    idex_pc_r = m.ca_reg("idex_pc", domain=domain, width=64, init=0)
    idex_op_r = m.ca_reg("idex_op", domain=domain, width=6, init=OP_INVALID)
    idex_len_bytes_r = m.ca_reg("idex_len_bytes", domain=domain, width=3, init=0)
    idex_regdst_r = m.ca_reg("idex_regdst", domain=domain, width=6, init=REG_INVALID)
    idex_srcl_val_r = m.ca_reg("idex_srcl_val", domain=domain, width=64, init=0)
    idex_srcr_val_r = m.ca_reg("idex_srcr_val", domain=domain, width=64, init=0)
    idex_srcp_val_r = m.ca_reg("idex_srcp_val", domain=domain, width=64, init=0)
    idex_imm_r = m.ca_reg("idex_imm", domain=domain, width=64, init=0)
    valid_ex_r = m.ca_reg("valid_ex", domain=domain, width=1, init=0)
    # EX/MEM
    exmem_op_r = m.ca_reg("exmem_op", domain=domain, width=6, init=OP_INVALID)
    exmem_len_bytes_r = m.ca_reg("exmem_len_bytes", domain=domain, width=3, init=0)
    exmem_pc_r = m.ca_reg("exmem_pc", domain=domain, width=64, init=0)
    exmem_regdst_r = m.ca_reg("exmem_regdst", domain=domain, width=6, init=REG_INVALID)
    exmem_alu_r = m.ca_reg("exmem_alu", domain=domain, width=64, init=0)
    exmem_is_load_r = m.ca_reg("exmem_is_load", domain=domain, width=1, init=0)
    exmem_is_store_r = m.ca_reg("exmem_is_store", domain=domain, width=1, init=0)
    exmem_size_r = m.ca_reg("exmem_size", domain=domain, width=3, init=0)
    exmem_addr_r = m.ca_reg("exmem_addr", domain=domain, width=64, init=0)
    exmem_wdata_r = m.ca_reg("exmem_wdata", domain=domain, width=64, init=0)
    valid_mem_r = m.ca_reg("valid_mem", domain=domain, width=1, init=0)
    # MEM/WB
    memwb_op_r = m.ca_reg("memwb_op", domain=domain, width=6, init=OP_INVALID)
    memwb_len_bytes_r = m.ca_reg("memwb_len_bytes", domain=domain, width=3, init=0)
    memwb_pc_r = m.ca_reg("memwb_pc", domain=domain, width=64, init=0)
    memwb_regdst_r = m.ca_reg("memwb_regdst", domain=domain, width=6, init=REG_INVALID)
    memwb_value_r = m.ca_reg("memwb_value", domain=domain, width=64, init=0)
    valid_wb_r = m.ca_reg("valid_wb", domain=domain, width=1, init=0)

    # ================================================================
    # 读取所有 DFF Q 输出（本拍开始时值）
    # ================================================================
    ifid_window = ifid_window_r.out()
    ifid_pc = ifid_pc_r.out()
    valid_id_raw = valid_id_r.out()

    idex_pc = idex_pc_r.out()
    idex_op = idex_op_r.out()
    idex_len_bytes = idex_len_bytes_r.out()
    idex_regdst = idex_regdst_r.out()
    idex_srcl_val = idex_srcl_val_r.out()
    idex_srcr_val = idex_srcr_val_r.out()
    idex_srcp_val = idex_srcp_val_r.out()
    idex_imm = idex_imm_r.out()
    valid_ex_raw = valid_ex_r.out()

    exmem_op = exmem_op_r.out()
    exmem_len_bytes = exmem_len_bytes_r.out()
    exmem_pc = exmem_pc_r.out()
    exmem_regdst = exmem_regdst_r.out()
    exmem_alu = exmem_alu_r.out()
    exmem_is_load = exmem_is_load_r.out()
    exmem_is_store = exmem_is_store_r.out()
    exmem_size = exmem_size_r.out()
    exmem_addr = exmem_addr_r.out()
    exmem_wdata = exmem_wdata_r.out()
    valid_mem_raw = valid_mem_r.out()

    wb_op = memwb_op_r.out()
    wb_len_bytes = memwb_len_bytes_r.out()
    wb_pc = memwb_pc_r.out()
    wb_regdst = memwb_regdst_r.out()
    wb_value = memwb_value_r.out()
    valid_wb = valid_wb_r.out()

    # ================================================================
    # WB 级（最先计算，得到 flush / redirect_pc）
    # ================================================================
    wb_valid = wb_op.ne(c(OP_INVALID, 6)) & wb_pc.ne(c(0, 64))
    do_wb_arch = valid_wb & wb_valid

    halt_set = (~state.halted.out()) & do_wb_arch & wb_op.eq(c(OP_EBREAK, 6))
    state.halted.set(c(1, 1), when=halt_set)

    wb_result = wb_stage_updates(
        m, state=state, rf=rf, domain=domain,
        op=wb_op, len_bytes=wb_len_bytes, pc=wb_pc,
        regdst=wb_regdst, value=wb_value,
        do_wb_arch=do_wb_arch,
    )
    # 驱动反馈信号（WB 级 → 前级）
    m.assign(_fb_flush_w, wb_result["flush"].sig)
    m.assign(_fb_redirect_pc_w, wb_result["redirect_pc"].sig)

    # WB 级 stall 源（当前无，预留扩展）
    stall_wb = zero1

    stop = state.halted.out() | halt_set

    # flush 后的有效 valid 位
    valid_id = valid_id_raw & ~flush
    valid_ex = valid_ex_raw & ~flush
    valid_mem = valid_mem_raw & ~flush

    # ================================================================
    # MEM 级（用 exmem DFF 输出）
    # ================================================================
    ex_out_d = {
        "op": exmem_op, "len_bytes": exmem_len_bytes, "pc": exmem_pc,
        "regdst": exmem_regdst, "alu": exmem_alu,
        "is_load": exmem_is_load, "is_store": exmem_is_store,
        "size": exmem_size, "addr": exmem_addr, "wdata": exmem_wdata,
    }
    dmem_raddr = mux(exmem_is_load, exmem_addr, zero64)
    dmem_wvalid = exmem_is_store & valid_mem
    wstrb = mux(exmem_size.eq(8), c(0xFF, 8), zero8)
    wstrb = mux(exmem_size.eq(4), c(0x0F, 8), wstrb)
    mem_rdata = build_byte_mem(
        m, domain,
        raddr=dmem_raddr, wvalid=dmem_wvalid, waddr=exmem_addr,
        wdata=exmem_wdata, wstrb=wstrb,
        depth_bytes=mem_bytes, name="mem",
    )
    mem_out = mem_stage_logic(m, ex_out_d, mem_rdata)

    # MEM 级 stall 源（当前无，未来可添加 cache miss 等）
    stall_mem = zero1

    # ================================================================
    # EX 级（用 idex DFF 输出）
    # ================================================================
    ex_out = ex_stage_logic(
        m, domain,
        pc=idex_pc, op=idex_op, len_bytes=idex_len_bytes, regdst=idex_regdst,
        srcl_val=idex_srcl_val, srcr_val=idex_srcr_val, srcp_val=idex_srcp_val,
        imm=idex_imm, consts=consts,
    )
    ex_is_load = idex_op.eq(c(OP_LWI, 6)) | idex_op.eq(c(OP_C_LWI, 6))

    # EX 级 stall 源（当前无，未来可添加多周期 ALU 等）
    stall_ex = zero1

    # ================================================================
    # ID 级（用 ifid DFF 输出 + 前递 + 互锁）
    # ================================================================
    dec = decode_window(m, ifid_window)
    op_id = dec.op
    len_bytes_id = dec.len_bytes
    regdst_id = dec.regdst
    srcl, srcr, srcp = dec.srcl, dec.srcr, dec.srcp
    imm_id = dec.imm

    # 寄存器堆读取（基线值）
    srcl_val_rf = read_reg(m, srcl, gpr=rf.gpr, t=rf.t, u=rf.u, default=zero64)
    srcr_val_rf = read_reg(m, srcr, gpr=rf.gpr, t=rf.t, u=rf.u, default=zero64)
    srcp_val_rf = read_reg(m, srcp, gpr=rf.gpr, t=rf.t, u=rf.u, default=zero64)

    # --- 前递（优先级：EX > MEM > WB > RF）---
    # 注意：前递仅对 GPR (0-23) 有效。T/U 栈 (24-31) 因 push/shift/clear
    # 语义无法简单用 regdst 匹配前递，通过 tu_hazard stall 保证正确性。
    fwd_ex_ok = valid_ex & idex_regdst.ne(c(REG_INVALID, 6)) & (~ex_is_load)
    fwd_mem_ok = valid_mem & exmem_regdst.ne(c(REG_INVALID, 6))
    fwd_wb_ok = do_wb_arch & wb_regdst.ne(c(REG_INVALID, 6))

    def fwd(src, rf_val):
        """对单个源寄存器应用前递链。"""
        v = rf_val
        v = mux(fwd_wb_ok & src.eq(wb_regdst), wb_value, v)
        v = mux(fwd_mem_ok & src.eq(exmem_regdst), mem_out["value"], v)
        v = mux(fwd_ex_ok & src.eq(idex_regdst), ex_out["alu"], v)
        return v

    srcl_val = fwd(srcl, srcl_val_rf)
    srcr_val = fwd(srcr, srcr_val_rf)
    srcp_val = fwd(srcp, srcp_val_rf)

    # --- Load-use 互锁：EX 为 load 且 ID 需要其结果 → stall 1 拍 ---
    load_use = valid_ex & ex_is_load & idex_regdst.ne(c(REG_INVALID, 6)) & (
        srcl.eq(idex_regdst) | srcr.eq(idex_regdst) | srcp.eq(idex_regdst))

    # --- T/U 栈 hazard ---
    # T/U 栈操作（push / clear）改变的实际寄存器与 regdst 编码不同：
    #   regdst=31 或 C_LWI → push t（实际写 t[0]=reg24，同时 shift t[1..3]）
    #   regdst=30          → push u（实际写 u[0]=reg28，同时 shift u[1..3]）
    #   block start marker → clear 所有 t/u 为 0
    # 当 EX/MEM/WB 有此类操作且 ID 读 t/u (reg 24-31) 时必须 stall。
    def _writes_tu(rd, op):
        """判断该指令是否修改 t/u 栈。"""
        return (rd.eq(c(30, 6)) | rd.eq(c(31, 6)) |
                op.eq(c(OP_C_LWI, 6)) |
                op.eq(c(OP_C_BSTART_STD, 6)) |
                op.eq(c(OP_C_BSTART_COND, 6)) |
                op.eq(c(OP_BSTART_STD_CALL, 6)))

    inflight_tu_write = (
        (valid_ex & _writes_tu(idex_regdst, idex_op)) |
        (valid_mem & _writes_tu(exmem_regdst, exmem_op)) |
        (do_wb_arch & _writes_tu(wb_regdst, wb_op))
    )

    def _is_tu(s):
        """判断寄存器编号是否属于 t/u 栈 (24-31)。"""
        return (s.eq(c(24, 6)) | s.eq(c(25, 6)) |
                s.eq(c(26, 6)) | s.eq(c(27, 6)) |
                s.eq(c(28, 6)) | s.eq(c(29, 6)) |
                s.eq(c(30, 6)) | s.eq(c(31, 6)))

    id_reads_tu = _is_tu(srcl) | _is_tu(srcr) | _is_tu(srcp)
    tu_hazard = inflight_tu_write & id_reads_tu

    stall_id = (load_use | tu_hazard) & valid_id

    # ---- 驱动累积停顿反馈信号（flush 优先于 stall）----
    # 累积 OR 链从后往前：
    #   freeze_mem = stall_wb
    #   freeze_ex  = stall_mem | freeze_mem
    #   freeze_id  = stall_ex  | freeze_ex
    #   freeze_if  = stall_id  | freeze_id
    cum_stall_mem = stall_wb
    cum_stall_ex  = stall_mem | cum_stall_mem
    cum_stall_id  = stall_ex  | cum_stall_ex
    cum_stall_if  = stall_id  | cum_stall_id
    m.assign(_fb_freeze_if_w,  (cum_stall_if  & ~flush).sig)
    m.assign(_fb_freeze_id_w,  (cum_stall_id  & ~flush).sig)
    m.assign(_fb_freeze_ex_w,  (cum_stall_ex  & ~flush).sig)
    m.assign(_fb_freeze_mem_w, (cum_stall_mem & ~flush).sig)

    # ================================================================
    # IF 级
    # ================================================================
    current_fetch_pc = mux(is_first, boot_pc, fetch_pc_reg.out())
    imem_rdata = build_byte_mem(
        m, domain,
        raddr=current_fetch_pc,
        wvalid=zero1, waddr=zero64, wdata=zero64, wstrb=zero8,
        depth_bytes=mem_bytes, name="imem",
    )
    window = imem_rdata

    # 快速指令长度解码
    low4 = window.trunc(width=4)
    is_hl = low4.eq(0xE)
    bit0 = window[0]
    quick_len = mux(is_hl, c(6, 3), mux(bit0 & ~is_hl, c(4, 3), c(2, 3)))

    next_pc_seq = current_fetch_pc + quick_len.zext(width=64)
    next_pc = mux(flush, redirect_pc, next_pc_seq)
    valid_if = ~stop & ~flush & ~freeze_if

    # ================================================================
    # 写入所有 DFF D 输入
    # ================================================================
    # fetch_pc
    fetch_pc_reg.set(fetch_pc_reg.out())                          # 默认：保持
    fetch_pc_reg.set(next_pc, when=~stop & ~freeze_if)            # 正常推进

    # IF/ID：freeze_if 时保持，否则推进
    ifid_window_r.set(ifid_window)                                # 默认：保持
    ifid_window_r.set(window, when=~freeze_if)
    ifid_pc_r.set(ifid_pc)
    ifid_pc_r.set(current_fetch_pc, when=~freeze_if)
    valid_id_r.set(valid_id_raw)
    valid_id_r.set(valid_if, when=~freeze_if)

    # ID/EX：freeze_id 时保持；未冻结时 stall_id 或 flush → 插入气泡
    id_to_ex_valid = valid_id & ~stall_id & ~flush
    idex_pc_r.set(idex_pc)                                        # 默认：保持
    idex_pc_r.set(ifid_pc, when=~freeze_id)
    idex_op_r.set(idex_op)                                        # 默认：保持
    idex_op_r.set(mux(id_to_ex_valid, op_id, c(OP_INVALID, 6)), when=~freeze_id)
    idex_len_bytes_r.set(idex_len_bytes)                          # 默认：保持
    idex_len_bytes_r.set(mux(id_to_ex_valid, len_bytes_id, c(0, 3)), when=~freeze_id)
    idex_regdst_r.set(idex_regdst)                                # 默认：保持
    idex_regdst_r.set(mux(id_to_ex_valid, regdst_id, c(REG_INVALID, 6)), when=~freeze_id)
    idex_srcl_val_r.set(idex_srcl_val)                            # 默认：保持
    idex_srcl_val_r.set(srcl_val, when=~freeze_id)
    idex_srcr_val_r.set(idex_srcr_val)                            # 默认：保持
    idex_srcr_val_r.set(srcr_val, when=~freeze_id)
    idex_srcp_val_r.set(idex_srcp_val)                            # 默认：保持
    idex_srcp_val_r.set(srcp_val, when=~freeze_id)
    idex_imm_r.set(idex_imm)                                      # 默认：保持
    idex_imm_r.set(imm_id, when=~freeze_id)
    valid_ex_r.set(valid_ex_raw)                                   # 默认：保持
    valid_ex_r.set(id_to_ex_valid, when=~freeze_id)

    # EX/MEM：freeze_ex 时保持；未冻结时 stall_ex 或 flush → 气泡
    ex_to_mem_valid = valid_ex & ~stall_ex & ~flush
    exmem_op_r.set(exmem_op)                                      # 默认：保持
    exmem_op_r.set(ex_out["op"], when=~freeze_ex)
    exmem_len_bytes_r.set(exmem_len_bytes)                        # 默认：保持
    exmem_len_bytes_r.set(ex_out["len_bytes"], when=~freeze_ex)
    exmem_pc_r.set(exmem_pc)                                      # 默认：保持
    exmem_pc_r.set(ex_out["pc"], when=~freeze_ex)
    exmem_regdst_r.set(exmem_regdst)                              # 默认：保持
    exmem_regdst_r.set(ex_out["regdst"], when=~freeze_ex)
    exmem_alu_r.set(exmem_alu)                                    # 默认：保持
    exmem_alu_r.set(ex_out["alu"], when=~freeze_ex)
    exmem_is_load_r.set(exmem_is_load)                            # 默认：保持
    exmem_is_load_r.set(ex_out["is_load"], when=~freeze_ex)
    exmem_is_store_r.set(exmem_is_store)                          # 默认：保持
    exmem_is_store_r.set(ex_out["is_store"], when=~freeze_ex)
    exmem_size_r.set(exmem_size)                                   # 默认：保持
    exmem_size_r.set(ex_out["size"], when=~freeze_ex)
    exmem_addr_r.set(exmem_addr)                                   # 默认：保持
    exmem_addr_r.set(ex_out["addr"], when=~freeze_ex)
    exmem_wdata_r.set(exmem_wdata)                                 # 默认：保持
    exmem_wdata_r.set(ex_out["wdata"], when=~freeze_ex)
    valid_mem_r.set(valid_mem_raw)                                 # 默认：保持
    valid_mem_r.set(ex_to_mem_valid, when=~freeze_ex)

    # MEM/WB：freeze_mem 时保持；未冻结时 stall_mem 或 flush → 气泡
    mem_to_wb_valid = valid_mem & ~stall_mem
    memwb_op_r.set(wb_op)                                         # 默认：保持
    memwb_op_r.set(mem_out["op"], when=~freeze_mem)
    memwb_len_bytes_r.set(wb_len_bytes)                           # 默认：保持
    memwb_len_bytes_r.set(mem_out["len_bytes"], when=~freeze_mem)
    memwb_pc_r.set(wb_pc)                                         # 默认：保持
    memwb_pc_r.set(mem_out["pc"], when=~freeze_mem)
    memwb_regdst_r.set(wb_regdst)                                 # 默认：保持
    memwb_regdst_r.set(mem_out["regdst"], when=~freeze_mem)
    memwb_value_r.set(wb_value)                                   # 默认：保持
    memwb_value_r.set(mem_out["value"], when=~freeze_mem)
    valid_wb_r.set(valid_wb)                                      # 默认：保持
    valid_wb_r.set(mem_to_wb_valid, when=~freeze_mem)

    # 周期计数器
    state.cycles.set(state.cycles.out() + 1)

    # ---------- 输出 ----------
    active = ~stop
    m.output("halted", state.halted.out().sig)
    m.output("pc", state.pc.out().sig)
    m.output("stage", state.stage.out().sig)
    m.output("cycles", state.cycles.out().sig)
    m.output("br_kind", state.br_kind.out().sig)
    m.output("br_base_pc", state.br_base_pc.out().sig)
    m.output("br_off", state.br_off.out().sig)
    m.output("commit_cond", state.commit_cond.out().sig)
    m.output("commit_tgt", state.commit_tgt.out().sig)
    m.output("active", active.sig)
    m.output("pc_if", current_fetch_pc.sig)
    m.output("pc_id", ifid_pc.sig)
    m.output("pc_ex", idex_pc.sig)
    m.output("pc_mem", exmem_pc.sig)
    m.output("pc_wb", wb_pc.sig)
    m.output("if_window", ifid_window.sig)
    m.output("a0", rf.gpr[2].out().sig)
    m.output("a1", rf.gpr[3].out().sig)
    m.output("ra", rf.gpr[10].out().sig)
    m.output("sp", rf.gpr[1].out().sig)
    m.output("flush", flush.sig)
    m.output("valid_wb", valid_wb.sig)
    # DEBUG: pipeline internals
    m.output("dbg_idex_srcl", idex_srcl_val.sig)
    m.output("dbg_idex_op", idex_op.sig)
    m.output("dbg_ex_alu", ex_out["alu"].sig)
    m.output("dbg_stall", stall_id.sig)
    m.output("dbg_valid_id", valid_id.sig)
    m.output("dbg_valid_ex", valid_ex.sig)
    m.output("dbg_valid_mem", valid_mem.sig)


def linx_cpu_pyc(m: CycleAwareCircuit, domain: CycleAwareDomain) -> None:
    _linx_cpu_impl(m, domain, mem_bytes=(1 << 20))


if __name__ == "__main__":
    circuit = compile_cycle_aware(linx_cpu_pyc, name="linx_cpu_pyc")
    print(circuit.emit_mlir())
