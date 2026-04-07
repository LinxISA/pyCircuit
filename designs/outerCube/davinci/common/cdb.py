"""Common Data Bus (CDB) — 6-port scalar result broadcast network.

Each port broadcasts a (tag, data, valid) tuple. All scalar/LSU RS entries
snoop every port and set their source ready bits on tag match.

Port mapping (§10.4):
  CDB0–CDB3: 4× ALU results
  CDB4:      MUL/DIV or LSU result (shared)
  CDB5:      TILE.GET scalar result
"""

from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    mux,
    wire_of,
)

from .parameters import CDB_PORTS, PHYS_GREG_W, SCALAR_DATA_W


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def cdb(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    n_ports: int = CDB_PORTS,
    tag_w: int = PHYS_GREG_W,
    data_w: int = SCALAR_DATA_W,
    n_snoop: int = 4,
    prefix: str = "cdb",
    inputs: dict | None = None,
) -> dict:
    """CDB crossbar: n_ports write ports, n_snoop query ports.

    Each snoop port provides a tag to match; the output indicates whether
    any CDB port matched and, if so, the forwarded data.
    """

    # ── Broadcast inputs (from execution units) ──────────────────────
    bc_valid = [
        _in(inputs, f"bc_valid{i}", m, domain, prefix, 1) for i in range(n_ports)
    ]
    bc_tag = [
        _in(inputs, f"bc_tag{i}", m, domain, prefix, tag_w) for i in range(n_ports)
    ]
    bc_data = [
        _in(inputs, f"bc_data{i}", m, domain, prefix, data_w) for i in range(n_ports)
    ]

    # ── Snoop query ports (from RS entries / RF) ─────────────────────
    snoop_tag = [
        _in(inputs, f"snoop_tag{i}", m, domain, prefix, tag_w) for i in range(n_snoop)
    ]

    zero_data = cas(domain, m.const(0, width=data_w), cycle=0)

    outs: dict = {}
    for s in range(n_snoop):
        hit = cas(domain, m.const(0, width=1), cycle=0)
        fwd = zero_data
        for p in range(n_ports):
            match = bc_valid[p] & (bc_tag[p] == snoop_tag[s])
            fwd = mux(match, bc_data[p], fwd)
            hit = hit | match
        outs[f"snoop_hit{s}"] = hit
        outs[f"snoop_data{s}"] = fwd
        if inputs is None:
            m.output(f"{prefix}_snoop_hit{s}", wire_of(hit))
            m.output(f"{prefix}_snoop_data{s}", wire_of(fwd))
    return outs


cdb.__pycircuit_name__ = "cdb"


if __name__ == "__main__":
    pass
