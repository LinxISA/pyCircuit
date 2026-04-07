"""Tile Completion Bus (TCB) — 4-port physical-tile-tag broadcast.

Lightweight broadcast: tag only (8-bit), no data payload. Tile-domain RS
entries snoop TCB ports to set their tile-source ready bits on tag match.

Port mapping (§10.4):
  TCB0: Vector unit tile completion
  TCB1: Cube unit (CUBE.DRAIN) tile completion
  TCB2: MTE tile completion (port 1)
  TCB3: MTE tile completion (port 2)
"""

from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    wire_of,
)

from .parameters import PHYS_TREG_W, TCB_PORTS


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def tcb(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    n_ports: int = TCB_PORTS,
    tag_w: int = PHYS_TREG_W,
    n_snoop: int = 4,
    prefix: str = "tcb",
    inputs: dict | None = None,
) -> dict:
    """TCB crossbar: n_ports broadcast, n_snoop query.

    Each snoop port provides a tag to check; output is a hit flag.
    """

    bc_valid = [
        _in(inputs, f"bc_valid{i}", m, domain, prefix, 1) for i in range(n_ports)
    ]
    bc_tag = [
        _in(inputs, f"bc_tag{i}", m, domain, prefix, tag_w) for i in range(n_ports)
    ]

    snoop_tag = [
        _in(inputs, f"snoop_tag{i}", m, domain, prefix, tag_w) for i in range(n_snoop)
    ]

    outs: dict = {}
    for s in range(n_snoop):
        hit = cas(domain, m.const(0, width=1), cycle=0)
        for p in range(n_ports):
            match = bc_valid[p] & (bc_tag[p] == snoop_tag[s])
            hit = hit | match
        outs[f"snoop_hit{s}"] = hit
        if inputs is None:
            m.output(f"{prefix}_snoop_hit{s}", wire_of(hit))
    return outs


tcb.__pycircuit_name__ = "tcb"


if __name__ == "__main__":
    pass
