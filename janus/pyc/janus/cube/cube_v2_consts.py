"""Cube v2 Constants and Memory Map Definitions."""

from __future__ import annotations

# =============================================================================
# Array Dimensions
# =============================================================================
ARRAY_SIZE = 8  # 8×8 systolic array

# =============================================================================
# Buffer Sizes
# =============================================================================
L0A_ENTRIES = 64   # L0A buffer entries
L0B_ENTRIES = 64   # L0B buffer entries
ACC_ENTRIES = 64   # ACC buffer entries
ISSUE_QUEUE_SIZE = 64  # Issue queue entries

# Entry sizes in bits
L0_ENTRY_BITS = ARRAY_SIZE * ARRAY_SIZE * 16  # 4096 bits (16×16×16-bit)
ACC_ENTRY_BITS = ARRAY_SIZE * ARRAY_SIZE * 32  # 8192 bits (16×16×32-bit)

# =============================================================================
# Data Widths
# =============================================================================
INPUT_WIDTH = 16   # Input element width (16-bit)
OUTPUT_WIDTH = 32  # Output element width (32-bit)
MMIO_WIDTH = 2048  # MMIO bandwidth per cycle

# =============================================================================
# FSM States
# =============================================================================
ST_IDLE = 0
ST_DECODE = 1        # Decoding MATMUL instruction
ST_GENERATE_UOPS = 2 # Generating uops
ST_EXECUTE = 3       # Executing uops
ST_DRAIN = 4         # Draining pipeline
ST_DONE = 5

# =============================================================================
# Load/Store States
# =============================================================================
LS_IDLE = 0
LS_LOAD_L0A_0 = 1   # Loading L0A first half
LS_LOAD_L0A_1 = 2   # Loading L0A second half
LS_LOAD_L0B_0 = 3   # Loading L0B first half
LS_LOAD_L0B_1 = 4   # Loading L0B second half
LS_STORE_ACC_0 = 5  # Storing ACC quarter 0
LS_STORE_ACC_1 = 6  # Storing ACC quarter 1
LS_STORE_ACC_2 = 7  # Storing ACC quarter 2
LS_STORE_ACC_3 = 8  # Storing ACC quarter 3

# =============================================================================
# Memory Map (relative to base address)
# =============================================================================
ADDR_CONTROL = 0x0000       # Control register
ADDR_STATUS = 0x0008        # Status register
ADDR_MATMUL_INST = 0x0010   # MATMUL instruction (M, K, N)
ADDR_ADDR_A = 0x0018        # Matrix A base address
ADDR_ADDR_B = 0x0020        # Matrix B base address
ADDR_ADDR_C = 0x0028        # Matrix C base address
ADDR_LOAD_L0A_CMD = 0x0030  # Load L0A command
ADDR_LOAD_L0B_CMD = 0x0038  # Load L0B command
ADDR_STORE_ACC_CMD = 0x0040 # Store ACC command
ADDR_QUEUE_STATUS = 0x0048  # Queue status
ADDR_L0A_STATUS = 0x0050    # L0A valid bitmap
ADDR_L0B_STATUS = 0x0058    # L0B valid bitmap
ADDR_ACC_STATUS = 0x0060    # ACC ready bitmap

# Data ports (2048-bit aligned)
ADDR_L0A_DATA = 0x1000      # L0A data port
ADDR_L0B_DATA = 0x2000      # L0B data port
ADDR_ACC_DATA = 0x3000      # ACC data port

# =============================================================================
# Control Register Bits
# =============================================================================
CTRL_START = 0       # Bit 0: Start MATMUL execution
CTRL_RESET = 1       # Bit 1: Reset accelerator
CTRL_LOAD_L0A = 2    # Bit 2: Trigger L0A load
CTRL_LOAD_L0B = 3    # Bit 3: Trigger L0B load
CTRL_STORE_ACC = 4   # Bit 4: Trigger ACC store
# Bits 7:5 reserved
# Bits 15:8: Entry index for LOAD/STORE

# =============================================================================
# Status Register Bits
# =============================================================================
STAT_DONE = 0        # Bit 0: MATMUL complete
STAT_BUSY = 1        # Bit 1: Computation in progress
STAT_L0A_BUSY = 2    # Bit 2: L0A load in progress
STAT_L0B_BUSY = 3    # Bit 3: L0B load in progress
STAT_ACC_BUSY = 4    # Bit 4: ACC store in progress
STAT_QUEUE_FULL = 5  # Bit 5: Issue queue full
STAT_QUEUE_EMPTY = 6 # Bit 6: Issue queue empty
# Bits 15:7 reserved
# Bits 23:16: Queue entries used
# Bits 31:24: Queue entries free
# Bits 63:32: Cycle counter

# =============================================================================
# Timing Constants
# =============================================================================
CYCLES_LOAD_L0 = 2   # Cycles to load L0A or L0B (4096 bits / 2048 bits)
CYCLES_STORE_ACC = 4 # Cycles to store ACC (8192 bits / 2048 bits)
CYCLES_COMPUTE = 32  # Cycles for one tile computation (load + compute + drain)

# =============================================================================
# Index Widths
# =============================================================================
L0_IDX_WIDTH = 6     # log2(64) = 6 bits
ACC_IDX_WIDTH = 6    # log2(64) = 6 bits
QUEUE_IDX_WIDTH = 6  # log2(64) = 6 bits
TILE_IDX_WIDTH = 8   # Max 256 tiles per dimension
