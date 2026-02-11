"""Cube v2 MMIO Interface.

Handles memory-mapped I/O for control registers and data transfers.
Supports 2048-bit bandwidth per cycle for data ports.
"""

from __future__ import annotations

from pycircuit import Circuit, Wire, jit_inline

from janus.cube.cube_v2_consts import (
    ADDR_ACC_DATA,
    ADDR_ACC_STATUS,
    ADDR_ADDR_A,
    ADDR_ADDR_B,
    ADDR_ADDR_C,
    ADDR_CONTROL,
    ADDR_L0A_DATA,
    ADDR_L0A_STATUS,
    ADDR_L0B_DATA,
    ADDR_L0B_STATUS,
    ADDR_LOAD_L0A_CMD,
    ADDR_LOAD_L0B_CMD,
    ADDR_MATMUL_INST,
    ADDR_QUEUE_STATUS,
    ADDR_STATUS,
    ADDR_STORE_ACC_CMD,
    CTRL_LOAD_L0A,
    CTRL_LOAD_L0B,
    CTRL_RESET,
    CTRL_START,
    CTRL_STORE_ACC,
    L0_IDX_WIDTH,
    MMIO_WIDTH,
    STAT_ACC_BUSY,
    STAT_BUSY,
    STAT_DONE,
    STAT_L0A_BUSY,
    STAT_L0B_BUSY,
    STAT_QUEUE_EMPTY,
    STAT_QUEUE_FULL,
)
from janus.cube.cube_v2_types import MmioWriteResult
from janus.cube.util import Consts


@jit_inline
def build_mmio_write(
    m: Circuit,
    *,
    clk: Wire,
    rst: Wire,
    consts: Consts,
    base_addr: int,
    # Write interface
    mem_wvalid: Wire,        # Write valid
    mem_waddr: Wire,         # Write address (64-bit)
    mem_wdata: Wire,         # Write data (64-bit for control, 2048-bit for data)
    mem_wdata_wide: Wire,    # Wide write data (2048-bit)
) -> MmioWriteResult:
    """Build MMIO write logic.

    Returns:
        MmioWriteResult with control signals
    """
    c = m.const

    with m.scope("MMIO_WR"):
        # Control register decode
        ctrl_match = mem_waddr.eq(c(base_addr + ADDR_CONTROL, width=64)) & mem_wvalid

        start = ctrl_match & mem_wdata[CTRL_START]
        reset_cube = ctrl_match & mem_wdata[CTRL_RESET]
        load_l0a = ctrl_match & mem_wdata[CTRL_LOAD_L0A]
        load_l0b = ctrl_match & mem_wdata[CTRL_LOAD_L0B]
        store_acc = ctrl_match & mem_wdata[CTRL_STORE_ACC]

        # Entry index from control register bits 15:8
        entry_idx = mem_wdata[8:15]

        return MmioWriteResult(
            start=start,
            reset_cube=reset_cube,
            load_l0a=load_l0a,
            load_l0b=load_l0b,
            store_acc=store_acc,
            entry_idx=entry_idx,
        )


@jit_inline
def build_mmio_read(
    m: Circuit,
    *,
    consts: Consts,
    base_addr: int,
    # Read interface
    mem_raddr: Wire,         # Read address (64-bit)
    # Status inputs
    done: Wire,              # Computation done
    busy: Wire,              # Computation busy
    l0a_busy: Wire,          # L0A load busy
    l0b_busy: Wire,          # L0B load busy
    acc_busy: Wire,          # ACC store busy
    queue_full: Wire,        # Issue queue full
    queue_empty: Wire,       # Issue queue empty
    queue_entries_used: Wire,  # Number of queue entries used
    l0a_valid_bitmap: Wire,  # L0A valid bitmap (64-bit)
    l0b_valid_bitmap: Wire,  # L0B valid bitmap (64-bit)
    acc_ready_bitmap: Wire,  # ACC ready bitmap (64-bit)
    cycle_count: Wire,       # Cycle counter (32-bit)
    # Data outputs from buffers (simplified - just return 64-bit)
    acc_store_data: Wire,    # ACC data for store (64-bit placeholder)
) -> tuple[Wire, Wire]:
    """Build MMIO read logic.

    Returns:
        (rdata_64, rdata_wide): 64-bit read data (rdata_wide is placeholder)
    """
    c = m.const

    with m.scope("MMIO_RD"):
        rdata_64 = c(0, width=64)

        # Status register
        status_match = mem_raddr.eq(c(base_addr + ADDR_STATUS, width=64))
        status_val = (
            done.zext(width=64)
            | (busy.zext(width=64) << STAT_BUSY)
            | (l0a_busy.zext(width=64) << STAT_L0A_BUSY)
            | (l0b_busy.zext(width=64) << STAT_L0B_BUSY)
            | (acc_busy.zext(width=64) << STAT_ACC_BUSY)
            | (queue_full.zext(width=64) << STAT_QUEUE_FULL)
            | (queue_empty.zext(width=64) << STAT_QUEUE_EMPTY)
            | (queue_entries_used.zext(width=64) << 16)
            | (cycle_count.zext(width=64) << 32)
        )
        rdata_64 = status_match.select(status_val, rdata_64)

        # Queue status
        queue_status_match = mem_raddr.eq(c(base_addr + ADDR_QUEUE_STATUS, width=64))
        queue_status_val = queue_entries_used.zext(width=64)
        rdata_64 = queue_status_match.select(queue_status_val, rdata_64)

        # L0A status (64-bit bitmap)
        l0a_status_match = mem_raddr.eq(c(base_addr + ADDR_L0A_STATUS, width=64))
        rdata_64 = l0a_status_match.select(l0a_valid_bitmap, rdata_64)

        # L0B status (64-bit bitmap)
        l0b_status_match = mem_raddr.eq(c(base_addr + ADDR_L0B_STATUS, width=64))
        rdata_64 = l0b_status_match.select(l0b_valid_bitmap, rdata_64)

        # ACC status (64-bit bitmap)
        acc_status_match = mem_raddr.eq(c(base_addr + ADDR_ACC_STATUS, width=64))
        rdata_64 = acc_status_match.select(acc_ready_bitmap, rdata_64)

        # ACC data port (simplified - just return placeholder)
        acc_data_match = mem_raddr.eq(c(base_addr + ADDR_ACC_DATA, width=64))
        rdata_64 = acc_data_match.select(acc_store_data, rdata_64)

        # Return 64-bit data and a placeholder for wide data
        rdata_wide = c(0, width=64)  # Placeholder - not used

        return rdata_64, rdata_wide


@jit_inline
def build_mmio_inst_write(
    m: Circuit,
    *,
    consts: Consts,
    base_addr: int,
    mem_wvalid: Wire,
    mem_waddr: Wire,
    mem_wdata: Wire,
) -> tuple[Wire, Wire, Wire, Wire, Wire, Wire, Wire]:
    """Build MMIO write logic for MATMUL instruction registers.

    Returns:
        (inst_write, inst_m, inst_k, inst_n, addr_a, addr_b, addr_c)
    """
    c = m.const

    with m.scope("MMIO_INST"):
        # MATMUL instruction register (M, K, N packed)
        # Format: [15:0] = M, [31:16] = K, [47:32] = N
        inst_match = mem_waddr.eq(c(base_addr + ADDR_MATMUL_INST, width=64)) & mem_wvalid
        inst_m = mem_wdata[0:16]
        inst_k = mem_wdata[16:32]
        inst_n = mem_wdata[32:48]

        # Address registers
        addr_a_match = mem_waddr.eq(c(base_addr + ADDR_ADDR_A, width=64)) & mem_wvalid
        addr_b_match = mem_waddr.eq(c(base_addr + ADDR_ADDR_B, width=64)) & mem_wvalid
        addr_c_match = mem_waddr.eq(c(base_addr + ADDR_ADDR_C, width=64)) & mem_wvalid

        # Select which address is being written
        addr_a = addr_a_match.select(mem_wdata, c(0, width=64))
        addr_b = addr_b_match.select(mem_wdata, c(0, width=64))
        addr_c = addr_c_match.select(mem_wdata, c(0, width=64))

        inst_write = inst_match

        return inst_write, inst_m, inst_k, inst_n, addr_a, addr_b, addr_c


@jit_inline
def build_load_store_controller(
    m: Circuit,
    *,
    clk: Wire,
    rst: Wire,
    consts: Consts,
    # Commands
    load_l0a_cmd: Wire,      # Start L0A load
    load_l0b_cmd: Wire,      # Start L0B load
    store_acc_cmd: Wire,     # Start ACC store
    entry_idx: Wire,         # Target entry index
    # Data interface
    data_in: Wire,           # Input data (2048-bit)
    data_in_valid: Wire,     # Input data valid
) -> tuple[Wire, Wire, Wire, Wire, Wire, Wire, Wire, Wire, Wire, Wire]:
    """Build load/store controller.

    Returns:
        (l0a_load_start, l0a_load_data, l0a_load_half, l0a_load_valid,
         l0b_load_start, l0b_load_data, l0b_load_half, l0b_load_valid,
         acc_store_start, acc_store_quarter)
    """
    c = m.const

    with m.scope("LS_CTRL"):
        # State registers
        with m.scope("STATE"):
            ls_state = m.out("state", clk=clk, rst=rst, width=4, init=0, en=consts.one1)
            ls_entry = m.out("entry", clk=clk, rst=rst, width=L0_IDX_WIDTH, init=0, en=consts.one1)
            ls_count = m.out("count", clk=clk, rst=rst, width=3, init=0, en=consts.one1)

        current_state = ls_state.out()
        current_count = ls_count.out()

        # State decode
        is_idle = current_state.eq(c(0, width=4))
        is_load_l0a = current_state.eq(c(1, width=4)) | current_state.eq(c(2, width=4))
        is_load_l0b = current_state.eq(c(3, width=4)) | current_state.eq(c(4, width=4))
        is_store_acc = (
            current_state.eq(c(5, width=4))
            | current_state.eq(c(6, width=4))
            | current_state.eq(c(7, width=4))
            | current_state.eq(c(8, width=4))
        )

        # Start commands
        l0a_load_start = load_l0a_cmd & is_idle
        l0b_load_start = load_l0b_cmd & is_idle
        acc_store_start = store_acc_cmd & is_idle

        # State transitions
        next_state = current_state

        # Start L0A load
        next_state = l0a_load_start.select(c(1, width=4), next_state)
        ls_entry.set(entry_idx, when=l0a_load_start)

        # Start L0B load
        next_state = l0b_load_start.select(c(3, width=4), next_state)
        ls_entry.set(entry_idx, when=l0b_load_start)

        # Start ACC store
        next_state = acc_store_start.select(c(5, width=4), next_state)
        ls_entry.set(entry_idx, when=acc_store_start)

        # L0A load progress (2 cycles)
        l0a_half_0_done = current_state.eq(c(1, width=4)) & data_in_valid
        next_state = l0a_half_0_done.select(c(2, width=4), next_state)

        l0a_half_1_done = current_state.eq(c(2, width=4)) & data_in_valid
        next_state = l0a_half_1_done.select(c(0, width=4), next_state)

        # L0B load progress (2 cycles)
        l0b_half_0_done = current_state.eq(c(3, width=4)) & data_in_valid
        next_state = l0b_half_0_done.select(c(4, width=4), next_state)

        l0b_half_1_done = current_state.eq(c(4, width=4)) & data_in_valid
        next_state = l0b_half_1_done.select(c(0, width=4), next_state)

        # ACC store progress (4 cycles)
        acc_q0_done = current_state.eq(c(5, width=4)) & data_in_valid
        next_state = acc_q0_done.select(c(6, width=4), next_state)

        acc_q1_done = current_state.eq(c(6, width=4)) & data_in_valid
        next_state = acc_q1_done.select(c(7, width=4), next_state)

        acc_q2_done = current_state.eq(c(7, width=4)) & data_in_valid
        next_state = acc_q2_done.select(c(8, width=4), next_state)

        acc_q3_done = current_state.eq(c(8, width=4)) & data_in_valid
        next_state = acc_q3_done.select(c(0, width=4), next_state)

        ls_state.set(next_state)

        # Output signals
        l0a_load_data = data_in
        l0a_load_half = current_state.eq(c(2, width=4))  # 0 for state 1, 1 for state 2
        l0a_load_valid = is_load_l0a & data_in_valid

        l0b_load_data = data_in
        l0b_load_half = current_state.eq(c(4, width=4))  # 0 for state 3, 1 for state 4
        l0b_load_valid = is_load_l0b & data_in_valid

        # ACC store quarter (0-3 based on state 5-8)
        acc_store_quarter = (current_state - c(5, width=4)).trunc(width=2)

        return (
            l0a_load_start,
            l0a_load_data,
            l0a_load_half,
            l0a_load_valid,
            l0b_load_start,
            l0b_load_data,
            l0b_load_half,
            l0b_load_valid,
            acc_store_start,
            acc_store_quarter,
        )
