"""TRegFile-4K — 8R8W tile register file with 8-cycle synchronized calendar.

64 physical 1R1W SRAM banks (8 groups × 8 banks). A rotating calendar ensures
each bank sees exactly 1R + 1W per cycle. Each port completes one full 4 KB
tile access over 8 consecutive cycles (512 B/cy × 8 groups).

Storage is delegated to ``sram_bank.sram_bank()`` — a separate module
that uses domain.signal() for functional simulation and can be swapped for a
foundry SRAM macro without touching this file.

See designs/outerCube/tregfile4k.md for the full architectural specification.
"""

from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    CycleAwareSignal,
    cas,
    compile_cycle_aware,
    mux,
    wire_of,
)

from .parameters import (
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
from .sram_bank import sram_bank

# Calendar (tregfile4k.md §4): global cy[2:0]; port p accesses group (p + cy) % 8.
# GROUP_PORT_MAP[e][g] = port that owns group g at epoch e → (g - e) % NUM_GROUPS.
GROUP_PORT_MAP: list[list[int]] = [
    [(g - e) % NUM_GROUPS for g in range(NUM_GROUPS)] for e in range(CALENDAR_LEN)
]

# PORT_GROUP_MAP[p][e] = group port p accesses at epoch e → (p + e) % NUM_GROUPS.
PORT_GROUP_MAP: list[list[int]] = [
    [(p + e) % NUM_GROUPS for e in range(CALENDAR_LEN)] for p in range(NUM_READ_PORTS)
]


def _epoch_bits() -> int:
    return max(1, (CALENDAR_LEN - 1).bit_length())


def _select_by_epoch(
    epoch_eq: list[CycleAwareSignal],
    per_epoch_values: list[CycleAwareSignal],
) -> CycleAwareSignal:
    """N-way mux driven by one-hot epoch comparisons (N = CALENDAR_LEN)."""
    assert len(epoch_eq) == len(per_epoch_values) == CALENDAR_LEN
    result = per_epoch_values[CALENDAR_LEN - 1]
    for k in range(CALENDAR_LEN - 2, -1, -1):
        result = mux(epoch_eq[k], per_epoch_values[k], result)
    return result


def tregfile(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    bank_depth: int = TEST_BANK_DEPTH,
    bank_width: int = TEST_BANK_WIDTH,
    prefix: str = "trf",
) -> None:
    BPG = BANKS_PER_GROUP
    NG = NUM_GROUPS
    NRP = NUM_READ_PORTS
    NWP = NUM_WRITE_PORTS

    tidx_w = tile_idx_width(bank_depth)
    pdata_w = port_data_width(bank_width)

    # ==================================================================
    # Cycle 0 — Inputs
    # ==================================================================
    r_tidx = [
        cas(domain, m.input(f"{prefix}_r{p}_tile_idx", width=tidx_w), cycle=0)
        for p in range(NRP)
    ]
    w_tidx = [
        cas(domain, m.input(f"{prefix}_w{p}_tile_idx", width=tidx_w), cycle=0)
        for p in range(NWP)
    ]
    w_en = [
        cas(domain, m.input(f"{prefix}_w{p}_en", width=1), cycle=0) for p in range(NWP)
    ]
    w_data = [
        cas(domain, m.input(f"{prefix}_w{p}_data", width=pdata_w), cycle=0)
        for p in range(NWP)
    ]

    # ==================================================================
    # Epoch counter (free-running 0 .. CALENDAR_LEN-1)
    # ==================================================================
    ew = _epoch_bits()
    epoch = domain.signal(width=ew, reset_value=0, name=f"{prefix}_epoch")

    CONST = [cas(domain, m.const(e, width=ew), cycle=0) for e in range(CALENDAR_LEN)]
    epoch_eq = [epoch.eq(CONST[e]) for e in range(CALENDAR_LEN)]

    m.output(f"{prefix}_epoch", wire_of(epoch))

    # ==================================================================
    # Tile-index latches (hold across CALENDAR_LEN-cycle epoch)
    # ==================================================================
    r_latch = [
        domain.signal(width=tidx_w, reset_value=0, name=f"{prefix}_r{p}_latch")
        for p in range(NRP)
    ]
    w_latch = [
        domain.signal(width=tidx_w, reset_value=0, name=f"{prefix}_w{p}_latch")
        for p in range(NWP)
    ]

    r_addr = [mux(epoch_eq[0], r_tidx[p], r_latch[p]) for p in range(NRP)]
    w_addr = [mux(epoch_eq[0], w_tidx[p], w_latch[p]) for p in range(NWP)]

    # ==================================================================
    # Per-group read / write signal selection via calendar
    # ==================================================================
    group_r_addr: list[CycleAwareSignal] = []
    for g in range(NG):
        vals = [r_addr[GROUP_PORT_MAP[e][g]] for e in range(CALENDAR_LEN)]
        group_r_addr.append(_select_by_epoch(epoch_eq, vals))

    group_w_addr: list[CycleAwareSignal] = []
    group_w_en: list[CycleAwareSignal] = []
    group_w_data: list[CycleAwareSignal] = []
    for g in range(NG):
        vals_a = [w_addr[GROUP_PORT_MAP[e][g]] for e in range(CALENDAR_LEN)]
        vals_e = [w_en[GROUP_PORT_MAP[e][g]] for e in range(CALENDAR_LEN)]
        vals_d = [w_data[GROUP_PORT_MAP[e][g]] for e in range(CALENDAR_LEN)]
        group_w_addr.append(_select_by_epoch(epoch_eq, vals_a))
        group_w_en.append(_select_by_epoch(epoch_eq, vals_e))
        group_w_data.append(_select_by_epoch(epoch_eq, vals_d))

    # ==================================================================
    # Instantiate NUM_BANKS SRAM banks (via sram_bank.py wrapper)
    # ==================================================================
    bank_rdata: list[CycleAwareSignal] = []
    bank_commits: list = []

    for b in range(NUM_BANKS):
        g = b // BPG
        local = b % BPG
        lo = local * bank_width
        hi = lo + bank_width

        rd, commit = sram_bank(
            m,
            domain,
            depth=bank_depth,
            width=bank_width,
            prefix=f"{prefix}_bank{b}",
            rd_addr=group_r_addr[g],
            wr_addr=group_w_addr[g],
            wr_en=group_w_en[g],
            wr_data=group_w_data[g][lo:hi],
        )
        bank_rdata.append(rd)
        bank_commits.append(commit)

    # ==================================================================
    # Assemble per-group SRAM read data
    # ==================================================================
    group_cat: list[CycleAwareSignal] = []
    for g in range(NG):
        w = wire_of(bank_rdata[g * BPG])
        for i in range(1, BPG):
            w = m.cat(wire_of(bank_rdata[g * BPG + i]), w)
        group_cat.append(cas(domain, w, cycle=0))

    # ==================================================================
    # Write-to-read bypass (same-phase ports only)
    # ==================================================================
    # The calendar guarantees that each group's reader and writer are
    # always the same-phase pair (R0/W0 … R7/W7).
    # SRAM writes are registered (1-cycle latency), so a simultaneous
    # read from the same-phase write port returns stale data.  Bypass
    # muxes forward write data combinationally when addresses match.
    #
    # ARCHITECTURAL CONSTRAINT — cross-phase RAW:
    #   For different-phase port pairs (e.g. R0 reading, W1 writing the
    #   same tile), one group per pair is read BEFORE it is written
    #   within the same epoch (the write data does not yet exist at the
    #   time of the read).  This hazard is NOT resolved in hardware.
    #   The upstream scheduler MUST guarantee that no two different-
    #   phase read/write ports operate on the same tile_idx within the
    #   same 8-cycle epoch.
    group_bypassed: list[CycleAwareSignal] = []
    for g in range(NG):
        bypass_hit = group_w_en[g] & group_r_addr[g].eq(group_w_addr[g])
        group_bypassed.append(mux(bypass_hit, group_w_data[g], group_cat[g]))

    # ==================================================================
    # Read-port output selection
    # ==================================================================
    for p in range(NRP):
        vals = [group_bypassed[PORT_GROUP_MAP[p][e]] for e in range(CALENDAR_LEN)]
        out = _select_by_epoch(epoch_eq, vals)
        m.output(f"{prefix}_r{p}_data", wire_of(out))

    # ==================================================================
    # Cycle 1 — Register updates (clocked)
    # ==================================================================
    domain.next()

    epoch <<= (epoch + 1).trunc(ew)

    for p in range(NRP):
        r_latch[p] <<= r_addr[p]
    for p in range(NWP):
        w_latch[p] <<= w_addr[p]

    # Commit all bank writes
    for commit in bank_commits:
        commit()


tregfile.__pycircuit_name__ = "tregfile"


if __name__ == "__main__":
    circuit = compile_cycle_aware(
        tregfile,
        name="tregfile",
        eager=True,
        bank_depth=TEST_BANK_DEPTH,
        bank_width=TEST_BANK_WIDTH,
    )
