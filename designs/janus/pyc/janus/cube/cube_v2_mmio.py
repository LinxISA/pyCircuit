"""Cube v2 MMIO Interface.

Handles memory-mapped I/O for control registers and data transfers.
Supports 2048-bit bandwidth per cycle for data ports.
"""
from __future__ import annotations
from pycircuit import Circuit, Wire, unsigned
from janus.cube.cube_v2_consts import ADDR_ACC_DATA, ADDR_ACC_STATUS, ADDR_ADDR_A, ADDR_ADDR_B, ADDR_ADDR_C, ADDR_CONTROL, ADDR_L0A_DATA, ADDR_L0A_STATUS, ADDR_L0B_DATA, ADDR_L0B_STATUS, ADDR_LOAD_L0A_CMD, ADDR_LOAD_L0B_CMD, ADDR_MATMUL_INST, ADDR_QUEUE_STATUS, ADDR_STATUS, ADDR_STORE_ACC_CMD, CTRL_LOAD_L0A, CTRL_LOAD_L0B, CTRL_RESET, CTRL_START, CTRL_STORE_ACC, L0_IDX_WIDTH, MMIO_WIDTH, STAT_ACC_BUSY, STAT_BUSY, STAT_DONE, STAT_L0A_BUSY, STAT_L0B_BUSY, STAT_QUEUE_EMPTY, STAT_QUEUE_FULL
from janus.cube.cube_v2_types import MmioWriteResult
from janus.cube.util import Consts

def build_mmio_write(m: Circuit, *, clk: Wire, rst: Wire, consts: Consts, base_addr: int, mem_wvalid: Wire, mem_waddr: Wire, mem_wdata: Wire, mem_wdata_wide: Wire) -> MmioWriteResult:
    """Build MMIO write logic.

    Returns:
        MmioWriteResult with control signals
    """
    c = m.const
    with m.scope('MMIO_WR'):
        ctrl_match = (mem_waddr == c(base_addr + ADDR_CONTROL, width=64)) & mem_wvalid
        start = ctrl_match & mem_wdata[CTRL_START]
        reset_cube = ctrl_match & mem_wdata[CTRL_RESET]
        load_l0a = ctrl_match & mem_wdata[CTRL_LOAD_L0A]
        load_l0b = ctrl_match & mem_wdata[CTRL_LOAD_L0B]
        store_acc = ctrl_match & mem_wdata[CTRL_STORE_ACC]
        entry_idx = mem_wdata[8:15]
        return MmioWriteResult(start=start, reset_cube=reset_cube, load_l0a=load_l0a, load_l0b=load_l0b, store_acc=store_acc, entry_idx=entry_idx)

def build_mmio_read(m: Circuit, *, consts: Consts, base_addr: int, mem_raddr: Wire, done: Wire, busy: Wire, l0a_busy: Wire, l0b_busy: Wire, acc_busy: Wire, queue_full: Wire, queue_empty: Wire, queue_entries_used: Wire, l0a_valid_bitmap: Wire, l0b_valid_bitmap: Wire, acc_ready_bitmap: Wire, cycle_count: Wire, acc_store_data: Wire) -> tuple[Wire, Wire]:
    """Build MMIO read logic.

    Returns:
        (rdata_64, rdata_wide): 64-bit read data (rdata_wide is placeholder)
    """
    c = m.const
    with m.scope('MMIO_RD'):
        rdata_64 = c(0, width=64)
        status_match = mem_raddr == c(base_addr + ADDR_STATUS, width=64)
        status_val = unsigned(done) | unsigned(busy) << STAT_BUSY | unsigned(l0a_busy) << STAT_L0A_BUSY | unsigned(l0b_busy) << STAT_L0B_BUSY | unsigned(acc_busy) << STAT_ACC_BUSY | unsigned(queue_full) << STAT_QUEUE_FULL | unsigned(queue_empty) << STAT_QUEUE_EMPTY | unsigned(queue_entries_used) << 16 | unsigned(cycle_count) << 32
        rdata_64 = status_val if status_match else rdata_64
        queue_status_match = mem_raddr == c(base_addr + ADDR_QUEUE_STATUS, width=64)
        queue_status_val = unsigned(queue_entries_used)
        rdata_64 = queue_status_val if queue_status_match else rdata_64
        l0a_status_match = mem_raddr == c(base_addr + ADDR_L0A_STATUS, width=64)
        rdata_64 = l0a_valid_bitmap if l0a_status_match else rdata_64
        l0b_status_match = mem_raddr == c(base_addr + ADDR_L0B_STATUS, width=64)
        rdata_64 = l0b_valid_bitmap if l0b_status_match else rdata_64
        acc_status_match = mem_raddr == c(base_addr + ADDR_ACC_STATUS, width=64)
        rdata_64 = acc_ready_bitmap if acc_status_match else rdata_64
        acc_data_match = mem_raddr == c(base_addr + ADDR_ACC_DATA, width=64)
        rdata_64 = acc_store_data if acc_data_match else rdata_64
        rdata_wide = c(0, width=64)
        return (rdata_64, rdata_wide)

def build_mmio_inst_write(m: Circuit, *, consts: Consts, base_addr: int, mem_wvalid: Wire, mem_waddr: Wire, mem_wdata: Wire) -> tuple[Wire, Wire, Wire, Wire, Wire, Wire, Wire]:
    """Build MMIO write logic for MATMUL instruction registers.

    Returns:
        (inst_write, inst_m, inst_k, inst_n, addr_a, addr_b, addr_c)
    """
    c = m.const
    with m.scope('MMIO_INST'):
        inst_match = (mem_waddr == c(base_addr + ADDR_MATMUL_INST, width=64)) & mem_wvalid
        inst_m = mem_wdata[0:16]
        inst_k = mem_wdata[16:32]
        inst_n = mem_wdata[32:48]
        addr_a_match = (mem_waddr == c(base_addr + ADDR_ADDR_A, width=64)) & mem_wvalid
        addr_b_match = (mem_waddr == c(base_addr + ADDR_ADDR_B, width=64)) & mem_wvalid
        addr_c_match = (mem_waddr == c(base_addr + ADDR_ADDR_C, width=64)) & mem_wvalid
        addr_a = mem_wdata if addr_a_match else c(0, width=64)
        addr_b = mem_wdata if addr_b_match else c(0, width=64)
        addr_c = mem_wdata if addr_c_match else c(0, width=64)
        inst_write = inst_match
        return (inst_write, inst_m, inst_k, inst_n, addr_a, addr_b, addr_c)

def build_load_store_controller(m: Circuit, *, clk: Wire, rst: Wire, consts: Consts, load_l0a_cmd: Wire, load_l0b_cmd: Wire, store_acc_cmd: Wire, entry_idx: Wire, data_in: Wire, data_in_valid: Wire) -> tuple[Wire, Wire, Wire, Wire, Wire, Wire, Wire, Wire, Wire, Wire]:
    """Build load/store controller.

    Returns:
        (l0a_load_start, l0a_load_data, l0a_load_half, l0a_load_valid,
         l0b_load_start, l0b_load_data, l0b_load_half, l0b_load_valid,
         acc_store_start, acc_store_quarter)
    """
    c = m.const
    with m.scope('LS_CTRL'):
        with m.scope('STATE'):
            ls_state = m.out('state', clk=clk, rst=rst, width=4, init=0, en=consts.one1)
            ls_entry = m.out('entry', clk=clk, rst=rst, width=L0_IDX_WIDTH, init=0, en=consts.one1)
            ls_count = m.out('count', clk=clk, rst=rst, width=3, init=0, en=consts.one1)
        current_state = ls_state.out()
        current_count = ls_count.out()
        is_idle = current_state == c(0, width=4)
        is_load_l0a = (current_state == c(1, width=4)) | (current_state == c(2, width=4))
        is_load_l0b = (current_state == c(3, width=4)) | (current_state == c(4, width=4))
        is_store_acc = (current_state == c(5, width=4)) | (current_state == c(6, width=4)) | (current_state == c(7, width=4)) | (current_state == c(8, width=4))
        l0a_load_start = load_l0a_cmd & is_idle
        l0b_load_start = load_l0b_cmd & is_idle
        acc_store_start = store_acc_cmd & is_idle
        next_state = current_state
        next_state = c(1, width=4) if l0a_load_start else next_state
        ls_entry.set(entry_idx, when=l0a_load_start)
        next_state = c(3, width=4) if l0b_load_start else next_state
        ls_entry.set(entry_idx, when=l0b_load_start)
        next_state = c(5, width=4) if acc_store_start else next_state
        ls_entry.set(entry_idx, when=acc_store_start)
        l0a_half_0_done = (current_state == c(1, width=4)) & data_in_valid
        next_state = c(2, width=4) if l0a_half_0_done else next_state
        l0a_half_1_done = (current_state == c(2, width=4)) & data_in_valid
        next_state = c(0, width=4) if l0a_half_1_done else next_state
        l0b_half_0_done = (current_state == c(3, width=4)) & data_in_valid
        next_state = c(4, width=4) if l0b_half_0_done else next_state
        l0b_half_1_done = (current_state == c(4, width=4)) & data_in_valid
        next_state = c(0, width=4) if l0b_half_1_done else next_state
        acc_q0_done = (current_state == c(5, width=4)) & data_in_valid
        next_state = c(6, width=4) if acc_q0_done else next_state
        acc_q1_done = (current_state == c(6, width=4)) & data_in_valid
        next_state = c(7, width=4) if acc_q1_done else next_state
        acc_q2_done = (current_state == c(7, width=4)) & data_in_valid
        next_state = c(8, width=4) if acc_q2_done else next_state
        acc_q3_done = (current_state == c(8, width=4)) & data_in_valid
        next_state = c(0, width=4) if acc_q3_done else next_state
        ls_state.set(next_state)
        l0a_load_data = data_in
        l0a_load_half = current_state == c(2, width=4)
        l0a_load_valid = is_load_l0a & data_in_valid
        l0b_load_data = data_in
        l0b_load_half = current_state == c(4, width=4)
        l0b_load_valid = is_load_l0b & data_in_valid
        acc_store_quarter = (current_state - c(5, width=4))[0:2]
        return (l0a_load_start, l0a_load_data, l0a_load_half, l0a_load_valid, l0b_load_start, l0b_load_data, l0b_load_half, l0b_load_valid, acc_store_start, acc_store_quarter)
