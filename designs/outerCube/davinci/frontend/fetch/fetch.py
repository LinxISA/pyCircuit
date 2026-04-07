"""Fetch Unit — F1/F2 stages, sequential PC, redirect support.

F1: Send PC to I-cache and branch predictor.
F2: Receive 4 instructions; push into instruction buffer.

Simplified model:
  - No actual I-cache; instructions provided externally (testbench drives)
  - PC increments by 16 each cycle (4 × 32-bit instructions)
  - Redirect support: mispredict and taken-branch redirects
"""

from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    mux,
    wire_of,
)

from ...common.parameters import FETCH_WIDTH

ADDR_W = 64
FETCH_INCR = FETCH_WIDTH * 4  # 16 bytes


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def fetch(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    addr_w: int = ADDR_W,
    fetch_w: int = FETCH_WIDTH,
    prefix: str = "fe",
    inputs: dict | None = None,
) -> dict:
    # ── Cycle 0: Inputs ──────────────────────────────────────────────
    stall = _in(inputs, "stall", m, domain, prefix, 1)

    # Redirect from BRU (mispredict) — highest priority
    redirect_valid = _in(inputs, "redirect_valid", m, domain, prefix, 1)
    redirect_target = _in(inputs, "redirect_target", m, domain, prefix, addr_w)

    # Redirect from BPU (predicted taken)
    bpu_taken = _in(inputs, "bpu_taken", m, domain, prefix, 1)
    bpu_target = _in(inputs, "bpu_target", m, domain, prefix, addr_w)

    # ── State ────────────────────────────────────────────────────────
    pc = domain.signal(width=addr_w, reset_value=0, name=f"{prefix}_pc")

    # ── Outputs ──────────────────────────────────────────────────────
    fetch_valid = ~stall & ~redirect_valid

    # Per-instruction PCs within the fetch block
    fetch_pc_list = []
    for i in range(fetch_w):
        ipc = (pc + cas(domain, m.const(i * 4, width=addr_w), cycle=0)).trunc(addr_w)
        fetch_pc_list.append(ipc)

    if inputs is None:
        m.output(f"{prefix}_pc", wire_of(pc))
        m.output(f"{prefix}_valid", wire_of(fetch_valid))
        for i, ipc in enumerate(fetch_pc_list):
            m.output(f"{prefix}_ipc{i}", wire_of(ipc))

    outs = {"pc": pc, "valid": fetch_valid, "ipc": fetch_pc_list}

    # ── Cycle 1: PC update ───────────────────────────────────────────
    domain.next()

    incr = cas(domain, m.const(FETCH_INCR, width=addr_w), cycle=0)
    next_seq_pc = (pc + incr).trunc(addr_w)

    # Priority: mispredict redirect > BPU taken > sequential
    next_pc = next_seq_pc
    next_pc = mux(bpu_taken, bpu_target, next_pc)
    next_pc = mux(redirect_valid, redirect_target, next_pc)
    next_pc = mux(stall, pc, next_pc)

    pc <<= next_pc

    return outs


fetch.__pycircuit_name__ = "fetch"


if __name__ == "__main__":
    pass
