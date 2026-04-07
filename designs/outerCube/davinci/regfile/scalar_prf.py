"""Scalar Physical Register File — 128 × 64-bit, 12R + 6W.

Implements a multi-ported flip-flop-based register file with 6-source bypass
network. P0 is always hardwired to zero (maps to architectural X0).
"""

from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    mux,
    wire_of,
)

from ..common.parameters import (
    PHYS_GREG_W,
    PHYS_GREGS,
    SCALAR_DATA_W,
    SCALAR_RF_RPORTS,
    SCALAR_RF_WPORTS,
)


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def scalar_prf(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    entries: int = PHYS_GREGS,
    tag_w: int = PHYS_GREG_W,
    data_w: int = SCALAR_DATA_W,
    n_rd: int = SCALAR_RF_RPORTS,
    n_wr: int = SCALAR_RF_WPORTS,
    prefix: str = "prf",
    inputs: dict | None = None,
) -> dict:
    # ── Cycle 0: Inputs ──────────────────────────────────────────────
    raddr = [_in(inputs, f"raddr{i}", m, domain, prefix, tag_w) for i in range(n_rd)]
    wen = [_in(inputs, f"wen{i}", m, domain, prefix, 1) for i in range(n_wr)]
    waddr = [_in(inputs, f"waddr{i}", m, domain, prefix, tag_w) for i in range(n_wr)]
    wdata = [_in(inputs, f"wdata{i}", m, domain, prefix, data_w) for i in range(n_wr)]

    # ── State: register storage ──────────────────────────────────────
    regs = [
        domain.signal(width=data_w, reset_value=0, name=f"{prefix}_reg_{i}")
        for i in range(entries)
    ]

    zero = cas(domain, m.const(0, width=data_w), cycle=0)

    # ── Combinational read with bypass ───────────────────────────────
    outs = {}
    for r in range(n_rd):
        rd = zero
        for e in range(entries):
            hit = raddr[r] == cas(domain, m.const(e, width=tag_w), cycle=0)
            rd = mux(hit, regs[e], rd)

        for w in range(n_wr):
            bypass_hit = wen[w] & (waddr[w] == raddr[r])
            rd = mux(bypass_hit, wdata[w], rd)

        is_zero_reg = raddr[r] == cas(domain, m.const(0, width=tag_w), cycle=0)
        rd = mux(is_zero_reg, zero, rd)

        outs[f"rdata{r}"] = rd

    if inputs is None:
        for k in outs:
            m.output(f"{prefix}_{k}", wire_of(outs[k]))

    # ── Cycle 1: Sequential write ────────────────────────────────────
    domain.next()

    for e in range(1, entries):  # skip P0 (hardwired zero)
        etag = cas(domain, m.const(e, width=tag_w), cycle=0)
        for w in range(n_wr):
            hit = wen[w] & (waddr[w] == etag)
            regs[e].assign(wdata[w], when=hit)

    return outs


scalar_prf.__pycircuit_name__ = "scalar_prf"


if __name__ == "__main__":
    pass
