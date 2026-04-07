"""TRegFile-4K design parameters.

Full-size: 64 banks × 256-deep × 512-bit SRAM = 1 MB, 8R8W, 8-cycle calendar.
256 tiles × 4 KB; tile_idx[7:0] → SRAM row. See designs/outerCube/tregfile4k.md.
Test-size: same 64 banks and calendar logic, reduced depth/width for fast sim.
"""
from __future__ import annotations

# ---------- Fixed architectural constants ----------
NUM_BANKS = 64
BANKS_PER_GROUP = 8
NUM_GROUPS = NUM_BANKS // BANKS_PER_GROUP  # 8
NUM_READ_PORTS = 8
NUM_WRITE_PORTS = 8
CALENDAR_LEN = 8

# ---------- Full-size parameters ----------
FULL_BANK_DEPTH = 256    # rows = tile count (tile_idx[7:0])
FULL_BANK_WIDTH = 512    # bits per bank (64 B)

# ---------- Test-size parameters ----------
TEST_BANK_DEPTH = 8      # 8 tiles
TEST_BANK_WIDTH = 8      # 8 bits per bank (1 byte)

# ---------- Derived ----------
def tile_idx_width(depth: int) -> int:
    return max(1, (depth - 1).bit_length())

def port_data_width(bank_width: int) -> int:
    return BANKS_PER_GROUP * bank_width
