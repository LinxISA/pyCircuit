"""Testbench for TRegFile-4K — calendar rotation, read/write, multi-port,
and write-to-read bypass (same-cycle forwarding).

Uses test-size parameters: 64 banks × 8-deep × 8-bit = 512 bytes total.
Port data width = 8 banks × 8 bits = 64 bits per port per cycle; 8-cycle epoch.
"""

from __future__ import annotations

import sys
from pathlib import Path

from pycircuit import CycleAwareTb, Tb, compile_cycle_aware, testbench

_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from .parameters import (  # noqa: E402
    BANKS_PER_GROUP,
    CALENDAR_LEN,
    NUM_BANKS,
    NUM_GROUPS,
    NUM_READ_PORTS,
    NUM_WRITE_PORTS,
    TEST_BANK_DEPTH,
    TEST_BANK_WIDTH,
    port_data_width,
    tile_idx_width,
)
from .tregfile import tregfile  # noqa: E402

BANK_DEPTH = TEST_BANK_DEPTH
BANK_WIDTH = TEST_BANK_WIDTH
TIDX_W = tile_idx_width(BANK_DEPTH)
PDATA_W = port_data_width(BANK_WIDTH)
PDATA_MASK = (1 << PDATA_W) - 1
BW_MASK = (1 << BANK_WIDTH) - 1

PREFIX = "trf"


def _make_tile_data(tile_idx: int) -> list[int]:
    """Generate deterministic per-bank test data for a tile (64 banks)."""
    return [(tile_idx * NUM_BANKS + b + 1) & BW_MASK for b in range(NUM_BANKS)]


def _group_banks(data: list[int], group: int) -> int:
    """Pack 8 bank values from a group into a pdata_w-bit word."""
    base = group * BANKS_PER_GROUP
    word = 0
    for i in range(BANKS_PER_GROUP):
        word |= (data[base + i] & BW_MASK) << (i * BANK_WIDTH)
    return word & PDATA_MASK


# Calendar: port p accesses group (p + epoch) % NUM_GROUPS (tregfile4k.md §4).
def _port_group(port: int, epoch: int) -> int:
    return (port + epoch) % NUM_GROUPS


@testbench
def tb(t: Tb) -> None:
    tb = CycleAwareTb(t)
    tb.clock("clk")
    tb.reset("rst", cycles_asserted=2, cycles_deasserted=1)
    tb.timeout(900)

    # Reference storage:  storage[bank][row] = value
    storage: list[list[int]] = [[0] * BANK_DEPTH for _ in range(NUM_BANKS)]

    def idle_ports() -> None:
        for p in range(NUM_READ_PORTS):
            tb.drive(f"{PREFIX}_r{p}_tile_idx", 0)
        for p in range(NUM_WRITE_PORTS):
            tb.drive(f"{PREFIX}_w{p}_tile_idx", 0)
            tb.drive(f"{PREFIX}_w{p}_en", 0)
            tb.drive(f"{PREFIX}_w{p}_data", 0)

    def write_tile_via_port(port: int, tile_idx: int, bank_data: list[int]) -> None:
        """Drive a full tile write over CALENDAR_LEN cycles through one write port.

        Must be called when epoch == 0 so the tile_idx is latched.
        """
        tb.drive(f"{PREFIX}_w{port}_tile_idx", tile_idx)
        for cy in range(CALENDAR_LEN):
            grp = _port_group(port, cy)
            word = _group_banks(bank_data, grp)
            tb.drive(f"{PREFIX}_w{port}_en", 1)
            tb.drive(f"{PREFIX}_w{port}_data", word)
            if cy < CALENDAR_LEN - 1:
                tb.next()
        # Update reference model
        for b in range(NUM_BANKS):
            storage[b][tile_idx] = bank_data[b]

    def read_tile_via_port(port: int, tile_idx: int, label: str) -> None:
        """Drive a full tile read over CALENDAR_LEN cycles and verify outputs.

        Must be called when epoch == 0.
        """
        tb.drive(f"{PREFIX}_r{port}_tile_idx", tile_idx)
        for cy in range(CALENDAR_LEN):
            grp = _port_group(port, cy)
            expected = 0
            for i in range(BANKS_PER_GROUP):
                b = grp * BANKS_PER_GROUP + i
                expected |= (storage[b][tile_idx] & BW_MASK) << (i * BANK_WIDTH)
            expected &= PDATA_MASK
            tb.expect(
                f"{PREFIX}_r{port}_data",
                expected,
                msg=f"{label} port={port} tile={tile_idx} cy={cy} grp={grp}",
            )
            if cy < CALENDAR_LEN - 1:
                tb.next()

    # ------------------------------------------------------------------
    # Test 1: Write tile 0 via W0, then read back via R0
    # ------------------------------------------------------------------
    idle_ports()

    tile0_data = _make_tile_data(0)
    write_tile_via_port(0, 0, tile0_data)

    # Advance past the write epoch (1 extra cycle for write latency)
    tb.next()
    idle_ports()

    # Wait for epoch == 0 so a new tile_idx can be latched.
    for _ in range(CALENDAR_LEN - 1):
        tb.next()

    read_tile_via_port(0, 0, "T1-read-tile0")

    # ------------------------------------------------------------------
    # Test 2: Write tile 1 via W1, read via R1
    # ------------------------------------------------------------------
    tb.next()
    idle_ports()
    for _ in range(CALENDAR_LEN - 1):
        tb.next()

    tile1_data = _make_tile_data(1)
    write_tile_via_port(1, 1, tile1_data)

    tb.next()
    idle_ports()
    for _ in range(CALENDAR_LEN - 1):
        tb.next()

    read_tile_via_port(1, 1, "T2-read-tile1")

    # ------------------------------------------------------------------
    # Test 3: Simultaneous write via W0 (tile 2) + W1 (tile 3)
    # ------------------------------------------------------------------
    tb.next()
    idle_ports()
    for _ in range(CALENDAR_LEN - 1):
        tb.next()

    tile2_data = _make_tile_data(2)
    tile3_data = _make_tile_data(3)

    tb.drive(f"{PREFIX}_w0_tile_idx", 2)
    tb.drive(f"{PREFIX}_w1_tile_idx", 3)
    for cy in range(CALENDAR_LEN):
        g0 = _port_group(0, cy)
        g1 = _port_group(1, cy)
        tb.drive(f"{PREFIX}_w0_en", 1)
        tb.drive(f"{PREFIX}_w0_data", _group_banks(tile2_data, g0))
        tb.drive(f"{PREFIX}_w1_en", 1)
        tb.drive(f"{PREFIX}_w1_data", _group_banks(tile3_data, g1))
        if cy < CALENDAR_LEN - 1:
            tb.next()
    for b in range(NUM_BANKS):
        storage[b][2] = tile2_data[b]
        storage[b][3] = tile3_data[b]

    # Wait + read both tiles
    tb.next()
    idle_ports()
    for _ in range(CALENDAR_LEN - 1):
        tb.next()

    # Read tile 2 via R0, tile 3 via R1 (simultaneously)
    tb.drive(f"{PREFIX}_r0_tile_idx", 2)
    tb.drive(f"{PREFIX}_r1_tile_idx", 3)
    for cy in range(CALENDAR_LEN):
        g0 = _port_group(0, cy)
        g1 = _port_group(1, cy)

        exp0 = 0
        for i in range(BANKS_PER_GROUP):
            b = g0 * BANKS_PER_GROUP + i
            exp0 |= (storage[b][2] & BW_MASK) << (i * BANK_WIDTH)
        exp0 &= PDATA_MASK

        exp1 = 0
        for i in range(BANKS_PER_GROUP):
            b = g1 * BANKS_PER_GROUP + i
            exp1 |= (storage[b][3] & BW_MASK) << (i * BANK_WIDTH)
        exp1 &= PDATA_MASK

        tb.expect(f"{PREFIX}_r0_data", exp0, msg=f"T3-read-tile2 cy={cy}")
        tb.expect(f"{PREFIX}_r1_data", exp1, msg=f"T3-read-tile3 cy={cy}")
        if cy < CALENDAR_LEN - 1:
            tb.next()

    # ------------------------------------------------------------------
    # Test 4: Overwrite tile 0, verify old data in tile 1 is still intact
    # ------------------------------------------------------------------
    tb.next()
    idle_ports()
    for _ in range(CALENDAR_LEN - 1):
        tb.next()

    tile0_v2 = [(0xA0 + b) & BW_MASK for b in range(NUM_BANKS)]
    write_tile_via_port(0, 0, tile0_v2)

    tb.next()
    idle_ports()
    for _ in range(CALENDAR_LEN - 1):
        tb.next()

    read_tile_via_port(0, 0, "T4-read-tile0-v2")

    tb.next()
    idle_ports()
    for _ in range(CALENDAR_LEN - 1):
        tb.next()

    read_tile_via_port(1, 1, "T4-read-tile1-intact")

    # ------------------------------------------------------------------
    # Test 5: Write-to-read bypass — simultaneous read+write, same tile
    # ------------------------------------------------------------------
    # Write tile 4 via W0 while simultaneously reading tile 4 via R0.
    # Same-phase ports access the same group each cycle,
    # the bypass mux should forward write data to read output on every
    # cycle of the epoch — even though the SRAM hasn't committed yet.
    tb.next()
    idle_ports()
    for _ in range(CALENDAR_LEN - 1):
        tb.next()

    tile4_data = _make_tile_data(4)

    tb.drive(f"{PREFIX}_r0_tile_idx", 4)
    tb.drive(f"{PREFIX}_w0_tile_idx", 4)
    for cy in range(CALENDAR_LEN):
        grp_r = _port_group(0, cy)  # R0 group this cycle
        grp_w = _port_group(0, cy)  # W0 group (same phase as R0)

        w_word = _group_banks(tile4_data, grp_w)
        tb.drive(f"{PREFIX}_w0_en", 1)
        tb.drive(f"{PREFIX}_w0_data", w_word)

        # Bypass should forward write data → read output immediately
        tb.expect(
            f"{PREFIX}_r0_data",
            w_word,
            msg=f"T5-bypass cy={cy} grp={grp_r}",
        )
        if cy < CALENDAR_LEN - 1:
            tb.next()

    for b in range(NUM_BANKS):
        storage[b][4] = tile4_data[b]

    # ------------------------------------------------------------------
    # Test 6: Bypass only when addresses match — different tiles, no bypass
    # ------------------------------------------------------------------
    # Write tile 5 via W0 while reading tile 4 via R0 (different address).
    # Bypass must NOT fire; read should return the previously written tile 4.
    tb.next()
    idle_ports()
    for _ in range(CALENDAR_LEN - 1):
        tb.next()

    tile5_data = _make_tile_data(5)

    tb.drive(f"{PREFIX}_r0_tile_idx", 4)  # read tile 4 (already stored)
    tb.drive(f"{PREFIX}_w0_tile_idx", 5)  # write tile 5 (different)
    for cy in range(CALENDAR_LEN):
        grp = _port_group(0, cy)
        w_word = _group_banks(tile5_data, grp)
        tb.drive(f"{PREFIX}_w0_en", 1)
        tb.drive(f"{PREFIX}_w0_data", w_word)

        # Read should return tile 4's committed data, NOT tile 5's write data
        exp = _group_banks(tile4_data, grp)
        tb.expect(
            f"{PREFIX}_r0_data",
            exp,
            msg=f"T6-no-bypass cy={cy} grp={grp}",
        )
        if cy < CALENDAR_LEN - 1:
            tb.next()

    for b in range(NUM_BANKS):
        storage[b][5] = tile5_data[b]

    # ------------------------------------------------------------------
    # Test 7: Bypass only when write-enable is high
    # ------------------------------------------------------------------
    # Read + write same tile 4 via R0/W0, but w_en=0 → no bypass,
    # read returns committed storage.
    tb.next()
    idle_ports()
    for _ in range(CALENDAR_LEN - 1):
        tb.next()

    bogus_data = [(0xFF - b) & BW_MASK for b in range(NUM_BANKS)]

    tb.drive(f"{PREFIX}_r0_tile_idx", 4)
    tb.drive(f"{PREFIX}_w0_tile_idx", 4)
    for cy in range(CALENDAR_LEN):
        grp = _port_group(0, cy)
        tb.drive(f"{PREFIX}_w0_en", 0)  # write DISABLED
        tb.drive(f"{PREFIX}_w0_data", _group_banks(bogus_data, grp))

        # Read should return tile 4's stored data (bypass inactive)
        exp = _group_banks(tile4_data, grp)
        tb.expect(
            f"{PREFIX}_r0_data",
            exp,
            msg=f"T7-no-bypass-wen0 cy={cy} grp={grp}",
        )
        if cy < CALENDAR_LEN - 1:
            tb.next()

    tb.finish()


if __name__ == "__main__":
    circuit = compile_cycle_aware(
        tregfile,
        name="tb_tregfile_top",
        eager=True,
        bank_depth=BANK_DEPTH,
        bank_width=BANK_WIDTH,
    )
