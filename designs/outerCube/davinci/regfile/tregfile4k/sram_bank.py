"""1R1W SRAM bank — functional simulation model.

This module implements a single-port-read, single-port-write SRAM bank using
domain.signal() flip-flops.  It is intended as a **drop-in replaceable**
wrapper: for synthesis, swap this file with one that instantiates a foundry
SRAM macro or a blackbox module reference.

Interface (per bank, per cycle):
    rd_addr  [addr_w]  →  rd_data [width]     combinational read
    wr_addr  [addr_w]  +  wr_en [1]  +  wr_data [width]  →  registered write

Usage from parent module (V5 two-phase pattern):
    # Phase 1 — Cycle 0: create bank, get combinational read data
    rd_data, commit = sram_bank(m, domain, ..., rd_addr=..., wr_addr=...,
                                       wr_en=..., wr_data=...)
    # Phase 2 — after domain.next(): commit the write
    domain.next()
    commit()
"""

from __future__ import annotations

from collections.abc import Callable

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    CycleAwareSignal,
    cas,
    compile_cycle_aware,
    mux,
    wire_of,
)


def sram_bank(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    depth: int,
    width: int,
    prefix: str,
    rd_addr: CycleAwareSignal,
    wr_addr: CycleAwareSignal,
    wr_en: CycleAwareSignal,
    wr_data: CycleAwareSignal,
) -> tuple[CycleAwareSignal, Callable[[], None]]:
    """Build one 1R1W SRAM bank (functional model).

    Returns
    -------
    rd_data : CycleAwareSignal
        Combinational read output (available at current cycle).
    commit_write : callable
        Call **after** ``domain.next()`` to register the write.
    """
    addr_w = max(1, (depth - 1).bit_length())

    # ── Storage array (flip-flop model) ──
    storage = [
        domain.signal(width=width, reset_value=0, name=f"{prefix}_{d}")
        for d in range(depth)
    ]

    addr_consts = [cas(domain, m.const(d, width=addr_w), cycle=0) for d in range(depth)]

    # ── Combinational read (cycle 0) ──
    zero = cas(domain, m.const(0, width=width), cycle=0)
    rd_data: CycleAwareSignal = zero
    for d in range(depth):
        hit = rd_addr.eq(addr_consts[d])
        rd_data = mux(hit, storage[d], rd_data)

    # ── Deferred write (call after domain.next()) ──
    def commit_write() -> None:
        for d in range(depth):
            hit = wr_en & wr_addr.eq(addr_consts[d])
            storage[d].assign(wr_data, when=hit)

    return rd_data, commit_write


# ── Standalone compilation (for independent testing) ──


def _sram_bank_top(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    depth: int = 8,
    width: int = 8,
) -> None:
    """Top-level wrapper so the bank can be compiled and inspected alone."""
    addr_w = max(1, (depth - 1).bit_length())

    rd_addr = cas(domain, m.input("rd_addr", width=addr_w), cycle=0)
    wr_addr = cas(domain, m.input("wr_addr", width=addr_w), cycle=0)
    wr_en = cas(domain, m.input("wr_en", width=1), cycle=0)
    wr_data = cas(domain, m.input("wr_data", width=width), cycle=0)

    rd_data, commit = sram_bank(
        m,
        domain,
        depth=depth,
        width=width,
        prefix="mem",
        rd_addr=rd_addr,
        wr_addr=wr_addr,
        wr_en=wr_en,
        wr_data=wr_data,
    )

    m.output("rd_data", wire_of(rd_data))

    domain.next()
    commit()


_sram_bank_top.__pycircuit_name__ = "sram_bank"


if __name__ == "__main__":
    circuit = compile_cycle_aware(
        _sram_bank_top,
        name="sram_bank",
        eager=True,
        depth=8,
        width=8,
    )
