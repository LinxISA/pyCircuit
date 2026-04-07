"""Scalar Register Alias Table (RAT) — 32 → 128, 4-wide rename.

Maps 32 architectural GPRs (X0–X31) to 128 physical GPRs (P0–P127).
Each entry: physical tag (7 bits) + ready bit (1 bit).

Supports:
  - 4-wide rename with intra-group bypass (§6.2.3)
  - Flash-copy to checkpoint (1-cycle)
  - Flash-restore from checkpoint (1-cycle)
"""

from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    mux,
    wire_of,
)

from ...common.parameters import (
    ARCH_GREG_W,
    ARCH_GREGS,
    PHYS_GREG_W,
    RENAME_WIDTH,
)


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def scalar_rat(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    n_arch: int = ARCH_GREGS,
    arch_w: int = ARCH_GREG_W,
    phys_w: int = PHYS_GREG_W,
    width: int = RENAME_WIDTH,
    prefix: str = "srat",
    inputs: dict | None = None,
) -> dict:
    # ── Cycle 0: Inputs ──────────────────────────────────────────────
    # Source lookups: 2 per slot
    src1_arch = [
        _in(inputs, f"src1_arch{i}", m, domain, prefix, arch_w) for i in range(width)
    ]
    src2_arch = [
        _in(inputs, f"src2_arch{i}", m, domain, prefix, arch_w) for i in range(width)
    ]

    # Destination writes: 1 per slot
    dst_valid = [
        _in(inputs, f"dst_valid{i}", m, domain, prefix, 1) for i in range(width)
    ]
    dst_arch = [
        _in(inputs, f"dst_arch{i}", m, domain, prefix, arch_w) for i in range(width)
    ]
    dst_phys = [
        _in(inputs, f"dst_phys{i}", m, domain, prefix, phys_w) for i in range(width)
    ]

    # Writeback (set ready)
    wb_valid = [
        _in(inputs, f"wb_valid{i}", m, domain, prefix, 1) for i in range(6)
    ]  # CDB ports
    wb_tag = [_in(inputs, f"wb_tag{i}", m, domain, prefix, phys_w) for i in range(6)]

    # Checkpoint restore
    restore = _in(inputs, "restore", m, domain, prefix, 1)
    restore_map = [
        _in(inputs, f"restore_map{i}", m, domain, prefix, phys_w) for i in range(n_arch)
    ]
    restore_rdy = [
        _in(inputs, f"restore_rdy{i}", m, domain, prefix, 1) for i in range(n_arch)
    ]

    # ── State: mapping table ─────────────────────────────────────────
    # P0 hardwired to X0; initial mapping: X[i] → P[i]
    mapping = [
        domain.signal(width=phys_w, reset_value=i, name=f"{prefix}_map_{i}")
        for i in range(n_arch)
    ]
    ready = [
        domain.signal(width=1, reset_value=1, name=f"{prefix}_rdy_{i}")
        for i in range(n_arch)
    ]

    # ── Combinational: 4-wide source lookup with intra-group bypass ──
    outs: dict = {}
    for slot in range(width):
        for src_idx, src_arch_sig in enumerate([src1_arch[slot], src2_arch[slot]]):
            # Base lookup from RAT
            phys_out = mapping[0]
            rdy_out = ready[0]
            for a in range(n_arch):
                hit = src_arch_sig == cas(domain, m.const(a, width=arch_w), cycle=0)
                phys_out = mux(hit, mapping[a], phys_out)
                rdy_out = mux(hit, ready[a], rdy_out)

            # Intra-group bypass: check older slots in this rename group
            for older in range(slot):
                match = dst_valid[older] & (dst_arch[older] == src_arch_sig)
                phys_out = mux(match, dst_phys[older], phys_out)
                rdy_out = mux(match, cas(domain, m.const(0, width=1), cycle=0), rdy_out)

            sn = f"src{src_idx + 1}"
            outs[f"{sn}_phys{slot}"] = phys_out
            outs[f"{sn}_rdy{slot}"] = rdy_out
            if inputs is None:
                m.output(f"{prefix}_{sn}_phys{slot}", wire_of(phys_out))
                m.output(f"{prefix}_{sn}_rdy{slot}", wire_of(rdy_out))

    # ── Old physical tag output (for orphan marking) ─────────────────
    for slot in range(width):
        old_phys = mapping[0]
        for a in range(n_arch):
            hit = dst_arch[slot] == cas(domain, m.const(a, width=arch_w), cycle=0)
            old_phys = mux(hit, mapping[a], old_phys)
        for older in range(slot):
            match = dst_valid[older] & (dst_arch[older] == dst_arch[slot])
            old_phys = mux(match, dst_phys[older], old_phys)
        outs[f"old_phys{slot}"] = old_phys
        if inputs is None:
            m.output(f"{prefix}_old_phys{slot}", wire_of(old_phys))

    # ── Checkpoint snapshot output ───────────────────────────────────
    for a in range(n_arch):
        outs[f"snap_map{a}"] = mapping[a]
        outs[f"snap_rdy{a}"] = ready[a]
        if inputs is None:
            m.output(f"{prefix}_snap_map{a}", wire_of(mapping[a]))
            m.output(f"{prefix}_snap_rdy{a}", wire_of(ready[a]))

    # ── Cycle 1: Sequential update ───────────────────────────────────
    domain.next()

    for a in range(n_arch):
        atag = cas(domain, m.const(a, width=arch_w), cycle=0)

        new_map = mapping[a]
        new_rdy = ready[a]

        # Destination writes (last writer wins within the group)
        for slot in range(width):
            hit = dst_valid[slot] & (dst_arch[slot] == atag)
            new_map = mux(hit, dst_phys[slot], new_map)
            new_rdy = mux(hit, cas(domain, m.const(0, width=1), cycle=0), new_rdy)

        # Writeback: set ready for matching physical tags
        for w in range(6):
            wb_match = wb_valid[w] & (new_map == wb_tag[w])
            new_rdy = mux(wb_match, cas(domain, m.const(1, width=1), cycle=0), new_rdy)

        if a == 0:
            # X0 → P0 hardwired
            new_map = cas(domain, m.const(0, width=phys_w), cycle=0)
            new_rdy = cas(domain, m.const(1, width=1), cycle=0)

        mapping[a] <<= mux(restore, restore_map[a], new_map)
        ready[a] <<= mux(restore, restore_rdy[a], new_rdy)

    return outs


scalar_rat.__pycircuit_name__ = "scalar_rat"


if __name__ == "__main__":
    pass
