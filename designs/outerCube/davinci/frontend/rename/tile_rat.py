"""Tile Register Alias Table — 32 → 256, 4-wide rename.

Maps 32 architectural tile registers (T0–T31) to 256 physical tile slots
(PT0–PT255) in TRegFile-4K. Each entry: physical tile tag (8 bits) + ready
bit (1 bit).

Supports:
  - 4-wide rename with intra-group bypass for up to 3 tile sources per slot
  - TILE.MOVE: alias destination to source PT (no free-list alloc)
  - Flash checkpoint / restore (parallel with Scalar RAT)
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
    ARCH_TREG_W,
    ARCH_TREGS,
    PHYS_TREG_W,
    RENAME_WIDTH,
    TCB_PORTS,
)


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def tile_rat(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    n_arch: int = ARCH_TREGS,
    arch_w: int = ARCH_TREG_W,
    phys_w: int = PHYS_TREG_W,
    width: int = RENAME_WIDTH,
    n_tile_src: int = 3,
    prefix: str = "trat",
    inputs: dict | None = None,
) -> dict:
    # ── Cycle 0: Inputs ──────────────────────────────────────────────
    # Up to 3 tile source lookups per slot
    src_arch = [
        [
            _in(inputs, f"src{s}_arch{i}", m, domain, prefix, arch_w)
            for i in range(width)
        ]
        for s in range(n_tile_src)
    ]

    dst_valid = [
        _in(inputs, f"dst_valid{i}", m, domain, prefix, 1) for i in range(width)
    ]
    dst_arch = [
        _in(inputs, f"dst_arch{i}", m, domain, prefix, arch_w) for i in range(width)
    ]
    dst_phys = [
        _in(inputs, f"dst_phys{i}", m, domain, prefix, phys_w) for i in range(width)
    ]

    # TILE.MOVE: dst inherits src's phys tag and ready bit (no alloc)
    is_tile_move = [
        _in(inputs, f"tile_move{i}", m, domain, prefix, 1) for i in range(width)
    ]
    move_src_arch = [
        _in(inputs, f"move_src{i}", m, domain, prefix, arch_w) for i in range(width)
    ]

    # TCB writeback (set ready)
    tcb_valid = [
        _in(inputs, f"tcb_valid{i}", m, domain, prefix, 1) for i in range(TCB_PORTS)
    ]
    tcb_tag = [
        _in(inputs, f"tcb_tag{i}", m, domain, prefix, phys_w) for i in range(TCB_PORTS)
    ]

    # Checkpoint restore
    restore = _in(inputs, "restore", m, domain, prefix, 1)
    restore_map = [
        _in(inputs, f"restore_map{i}", m, domain, prefix, phys_w) for i in range(n_arch)
    ]
    restore_rdy = [
        _in(inputs, f"restore_rdy{i}", m, domain, prefix, 1) for i in range(n_arch)
    ]

    # ── State ────────────────────────────────────────────────────────
    mapping = [
        domain.signal(width=phys_w, reset_value=i, name=f"{prefix}_map_{i}")
        for i in range(n_arch)
    ]
    ready = [
        domain.signal(width=1, reset_value=1, name=f"{prefix}_rdy_{i}")
        for i in range(n_arch)
    ]

    # ── Combinational: source lookups with intra-group bypass ────────
    outs: dict = {}
    for slot in range(width):
        for s in range(n_tile_src):
            sa = src_arch[s][slot]
            phys_out = mapping[0]
            rdy_out = ready[0]
            for a in range(n_arch):
                hit = sa == cas(domain, m.const(a, width=arch_w), cycle=0)
                phys_out = mux(hit, mapping[a], phys_out)
                rdy_out = mux(hit, ready[a], rdy_out)

            for older in range(slot):
                match = dst_valid[older] & (dst_arch[older] == sa)
                phys_out = mux(match, dst_phys[older], phys_out)
                rdy_out = mux(match, cas(domain, m.const(0, width=1), cycle=0), rdy_out)

            outs[f"src{s}_phys{slot}"] = phys_out
            outs[f"src{s}_rdy{slot}"] = rdy_out
            if inputs is None:
                m.output(f"{prefix}_src{s}_phys{slot}", wire_of(phys_out))
                m.output(f"{prefix}_src{s}_rdy{slot}", wire_of(rdy_out))

    # ── Old physical tag for orphan marking ──────────────────────────
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

    # ── Checkpoint snapshot ──────────────────────────────────────────
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

        for slot in range(width):
            hit = dst_valid[slot] & (dst_arch[slot] == atag)
            new_map = mux(hit, dst_phys[slot], new_map)
            new_rdy = mux(hit, cas(domain, m.const(0, width=1), cycle=0), new_rdy)

            # TILE.MOVE: dst inherits src's current phys tag + ready
            move_phys = mapping[0]
            move_rdy = ready[0]
            for a2 in range(n_arch):
                mhit = move_src_arch[slot] == cas(
                    domain, m.const(a2, width=arch_w), cycle=0
                )
                move_phys = mux(mhit, mapping[a2], move_phys)
                move_rdy = mux(mhit, ready[a2], move_rdy)
            move_hit = is_tile_move[slot] & (dst_arch[slot] == atag)
            new_map = mux(move_hit, move_phys, new_map)
            new_rdy = mux(move_hit, move_rdy, new_rdy)

        # TCB: set ready for matching physical tags
        for t in range(TCB_PORTS):
            tcb_match = tcb_valid[t] & (new_map == tcb_tag[t])
            new_rdy = mux(tcb_match, cas(domain, m.const(1, width=1), cycle=0), new_rdy)

        mapping[a] <<= mux(restore, restore_map[a], new_map)
        ready[a] <<= mux(restore, restore_rdy[a], new_rdy)

    return outs


tile_rat.__pycircuit_name__ = "tile_rat"


if __name__ == "__main__":
    pass
