"""Cube v2 Top-Level Module with Module Reuse.

Matrix Multiplication Accelerator with MATMUL Block Instruction Support.
Uses m.instance() for PE and L0 entry module reuse to reduce generated code size.

Features:
- MATMUL block instruction decomposition into uops
- 64-entry L0A and L0B input buffers (with module reuse)
- 64-entry ACC output buffer
- 64-entry issue queue with out-of-order execution
- 4-stage pipelined systolic array (4 PE Clusters × 64 PEs each)
- Pipeline throughput: 1 uop/cycle after 4-cycle fill
- Peak: 4096 MACs/cycle
- Module reuse for PE instances (256 PEs share one module definition)
- Module reuse for L0 entries (128 entries share one module definition)
"""
from __future__ import annotations
from pycircuit import Circuit, Wire, unsigned
from janus.cube.cube_v2_acc import build_acc_buffer
from janus.cube.cube_v2_consts import ACC_ENTRIES, ARRAY_SIZE, L0A_ENTRIES, L0B_ENTRIES, L0_IDX_WIDTH, MMIO_WIDTH, OUTPUT_WIDTH, ST_DONE, ST_EXECUTE, ST_IDLE
from janus.cube.cube_v2_decoder import build_matmul_decoder
from janus.cube.cube_v2_issue_queue import build_issue_queue
from janus.cube.cube_v2_l0_reuse import build_l0a_reuse, build_l0b_reuse
from janus.cube.cube_v2_mmio import build_mmio_inst_write, build_mmio_read, build_mmio_write
from janus.cube.cube_v2_systolic_reuse import build_pipelined_systolic_array_reuse
from janus.cube.cube_v2_types import CubeV2State
from janus.cube.util import Consts, make_consts

def _make_cube_v2_state(m: Circuit, clk: Wire, rst: Wire, consts: Consts) -> CubeV2State:
    """Create main FSM state registers."""
    with m.scope('MAIN_STATE'):
        return CubeV2State(state=m.out('state', clk=clk, rst=rst, width=3, init=ST_IDLE, en=consts.one1), cycle_count=m.out('cycle_count', clk=clk, rst=rst, width=32, init=0, en=consts.one1), done=m.out('done', clk=clk, rst=rst, width=1, init=0, en=consts.one1), busy=m.out('busy', clk=clk, rst=rst, width=1, init=0, en=consts.one1))

def _build_l0_bitmaps(m: Circuit, consts: Consts, l0a_status: list, l0b_status: list) -> tuple[Wire, Wire]:
    """Build valid bitmaps for L0A and L0B buffers (64-bit each)."""
    c = m.const
    with m.scope('L0_BITMAPS'):
        l0a_bitmap = c(0, width=64)
        for i in range(L0A_ENTRIES):
            bit = unsigned(l0a_status[i].valid.out()) << i
            l0a_bitmap = l0a_bitmap | bit
        l0b_bitmap = c(0, width=64)
        for i in range(L0B_ENTRIES):
            bit = unsigned(l0b_status[i].valid.out()) << i
            l0b_bitmap = l0b_bitmap | bit
        return (l0a_bitmap, l0b_bitmap)

def build(m: Circuit, *, base_addr: int=2147483648) -> None:
    """Build Cube v2 matrix multiplication accelerator.

    Memory-mapped interface:
        base_addr + 0x0000: Control register
        base_addr + 0x0008: Status register
        base_addr + 0x0010: MATMUL instruction (M, K, N)
        base_addr + 0x0018: Matrix A address
        base_addr + 0x0020: Matrix B address
        base_addr + 0x0028: Matrix C address
        base_addr + 0x0100: L0A load (entry_idx in bits 13:8, row in bits 7:4, col in bits 3:0)
        base_addr + 0x0200: L0B load (entry_idx in bits 13:8, row in bits 7:4, col in bits 3:0)
    """
    c = m.const
    clk = m.clock('clk')
    rst = m.reset('rst')
    mem_wvalid = m.input('mem_wvalid', width=1)
    mem_waddr = m.input('mem_waddr', width=64)
    mem_wdata = m.input('mem_wdata', width=64)
    mem_raddr = m.input('mem_raddr', width=64)
    consts = make_consts(m)
    main_state = _make_cube_v2_state(m, clk, rst, consts)
    mmio_wr = build_mmio_write(m, clk=clk, rst=rst, consts=consts, base_addr=base_addr, mem_wvalid=mem_wvalid, mem_waddr=mem_waddr, mem_wdata=mem_wdata, mem_wdata_wide=c(0, width=MMIO_WIDTH))
    inst_write, inst_m, inst_k, inst_n, addr_a, addr_b, addr_c = build_mmio_inst_write(m, consts=consts, base_addr=base_addr, mem_wvalid=mem_wvalid, mem_waddr=mem_waddr, mem_wdata=mem_wdata)
    with m.scope('L0_LOAD_DECODE'):
        addr_offset = (mem_waddr - c(base_addr, width=64))[0:16]
        l0a_range = addr_offset[8:16] == c(1, width=8)
        l0a_load_valid = mem_wvalid & l0a_range
        l0a_entry_idx = addr_offset[8:14][0:L0_IDX_WIDTH]
        l0a_row = addr_offset[4:8]
        l0a_col = addr_offset[0:4]
        l0b_range = addr_offset[8:16] == c(2, width=8)
        l0b_load_valid = mem_wvalid & l0b_range
        l0b_entry_idx = addr_offset[8:14][0:L0_IDX_WIDTH]
        l0b_row = addr_offset[4:8]
        l0b_col = addr_offset[0:4]
    l0a_status, l0a_matrix, l0a_load_done = build_l0a_reuse(m, clk=clk, rst=rst, consts=consts, load_entry_idx=l0a_entry_idx, load_row=l0a_row, load_col=l0a_col, load_data=mem_wdata, load_valid=l0a_load_valid, read_entry_idx=c(0, width=L0_IDX_WIDTH))
    l0b_status, l0b_matrix, l0b_load_done = build_l0b_reuse(m, clk=clk, rst=rst, consts=consts, load_entry_idx=l0b_entry_idx, load_row=l0b_row, load_col=l0b_col, load_data=mem_wdata, load_valid=l0b_load_valid, read_entry_idx=c(0, width=L0_IDX_WIDTH))
    l0a_bitmap, l0b_bitmap = _build_l0_bitmaps(m, consts, l0a_status, l0b_status)
    matmul_inst, gen_state, uop_valid, uop_l0a_idx, uop_l0b_idx, uop_acc_idx, uop_is_first, uop_is_last, gen_done = build_matmul_decoder(m, clk=clk, rst=rst, consts=consts, start=mmio_wr.start, inst_m=inst_m, inst_k=inst_k, inst_n=inst_n, queue_full=consts.zero1, reset_decoder=mmio_wr.reset_cube)
    acc_bitmap_placeholder = c((1 << ACC_ENTRIES) - 1, width=64)
    iq_entries, issue_result, queue_full, queue_empty, entries_used = build_issue_queue(m, clk=clk, rst=rst, consts=consts, enqueue_valid=uop_valid, enqueue_l0a_idx=uop_l0a_idx, enqueue_l0b_idx=uop_l0b_idx, enqueue_acc_idx=uop_acc_idx, enqueue_is_first=uop_is_first, enqueue_is_last=uop_is_last, l0a_valid_bitmap=l0a_bitmap, l0b_valid_bitmap=l0b_bitmap, acc_available_bitmap=acc_bitmap_placeholder, issue_ack=consts.one1, flush=mmio_wr.reset_cube)
    pipe_regs, sa_write_valid, sa_results, sa_write_acc_idx, sa_write_is_first, sa_write_is_last, sa_busy = build_pipelined_systolic_array_reuse(m, clk=clk, rst=rst, consts=consts, issue_valid=issue_result.issue_valid, issue_uop=issue_result.uop, l0a_data=l0a_matrix, l0b_data=l0b_matrix, stall=consts.zero1)
    acc_status, acc_store_data, acc_store_done = build_acc_buffer(m, clk=clk, rst=rst, consts=consts, write_entry_idx=sa_write_acc_idx, write_data=sa_results, write_valid=sa_write_valid, write_is_first=sa_write_is_first, write_is_last=sa_write_is_last, store_start=consts.zero1, store_entry_idx=c(0, width=L0_IDX_WIDTH), store_quarter=c(0, width=2), store_ack=consts.zero1, query_entry_idx=c(0, width=L0_IDX_WIDTH))
    with m.scope('MAIN_FSM'):
        current_state = main_state.state.out()
        state_is_idle = current_state == c(ST_IDLE, width=3)
        state_is_execute = current_state == c(ST_EXECUTE, width=3)
        state_is_done = current_state == c(ST_DONE, width=3)
        next_state = current_state
        next_state = c(ST_EXECUTE, width=3) if state_is_idle & mmio_wr.start else next_state
        all_done = gen_done & queue_empty & ~sa_busy
        next_state = c(ST_DONE, width=3) if state_is_execute & all_done else next_state
        next_state = c(ST_IDLE, width=3) if state_is_done & mmio_wr.reset_cube else next_state
        main_state.state.set(next_state)
        main_state.done.set(state_is_done)
        main_state.busy.set(state_is_execute)
        next_cycle = main_state.cycle_count.out() + c(1, width=32)
        main_state.cycle_count.set(next_cycle)
    acc_ready_bitmap = c(0, width=64)
    for i in range(ACC_ENTRIES):
        bit = unsigned(acc_status[i].valid.out()) << i
        acc_ready_bitmap = acc_ready_bitmap | bit
    rdata_64, rdata_wide = build_mmio_read(m, consts=consts, base_addr=base_addr, mem_raddr=mem_raddr, done=main_state.done.out(), busy=main_state.busy.out(), l0a_busy=consts.zero1, l0b_busy=consts.zero1, acc_busy=consts.zero1, queue_full=queue_full, queue_empty=queue_empty, queue_entries_used=entries_used, l0a_valid_bitmap=l0a_bitmap, l0b_valid_bitmap=l0b_bitmap, acc_ready_bitmap=acc_ready_bitmap, cycle_count=main_state.cycle_count.out(), acc_store_data=acc_store_data)
    m.output('mem_rdata', rdata_64)
    m.output('done', main_state.done.out())
    m.output('busy', main_state.busy.out())
    m.output('queue_full', queue_full)
    m.output('queue_empty', queue_empty)
build.__pycircuit_name__ = 'janus_cube_pyc'
