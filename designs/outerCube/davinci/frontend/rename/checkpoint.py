"""Checkpoint Store — 8 slots for dual-RAT (Scalar + Tile) snapshots.

Each checkpoint stores:
  - Scalar RAT snapshot (32 × 7-bit mapping + 32 × 1-bit ready = 256 bits)
  - Tile RAT snapshot (32 × 8-bit mapping + 32 × 1-bit ready = 288 bits)
  - Scalar free-list head pointer (7 bits)
  - Tile free-list head pointer (8 bits)
  - RAS top pointer (4 bits)
  Total: ~563 bits per slot

Operations:
  - Allocate: save both RATs + pointers on branch decode (round-robin)
  - Deallocate: release slot on correct branch resolution
  - Restore: flash-copy checkpoint → active RATs on mispredict
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
    ARCH_GREGS,
    ARCH_TREGS,
    CHECKPOINT_SLOTS,
    CHECKPOINT_W,
    PHYS_GREG_W,
    PHYS_TREG_W,
)


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def checkpoint(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    n_slots: int = CHECKPOINT_SLOTS,
    slot_w: int = CHECKPOINT_W,
    n_sarch: int = ARCH_GREGS,
    n_tarch: int = ARCH_TREGS,
    sphys_w: int = PHYS_GREG_W,
    tphys_w: int = PHYS_TREG_W,
    prefix: str = "ckpt",
    inputs: dict | None = None,
) -> dict:
    sfl_ptr_w = sphys_w
    tfl_ptr_w = tphys_w
    ras_ptr_w = 4

    # ── Cycle 0: Inputs ──────────────────────────────────────────────
    save_valid = _in(inputs, "save_valid", m, domain, prefix, 1)

    # Scalar RAT snapshot to save
    save_smap = [
        _in(inputs, f"save_smap{i}", m, domain, prefix, sphys_w) for i in range(n_sarch)
    ]
    save_srdy = [
        _in(inputs, f"save_srdy{i}", m, domain, prefix, 1) for i in range(n_sarch)
    ]
    # Tile RAT snapshot to save
    save_tmap = [
        _in(inputs, f"save_tmap{i}", m, domain, prefix, tphys_w) for i in range(n_tarch)
    ]
    save_trdy = [
        _in(inputs, f"save_trdy{i}", m, domain, prefix, 1) for i in range(n_tarch)
    ]
    # Pointers
    save_sfl_head = _in(inputs, "save_sfl", m, domain, prefix, sfl_ptr_w)
    save_tfl_head = _in(inputs, "save_tfl", m, domain, prefix, tfl_ptr_w)
    save_ras_ptr = _in(inputs, "save_ras", m, domain, prefix, ras_ptr_w)

    # Dealloc (branch resolved correctly)
    dealloc_valid = _in(inputs, "dealloc_valid", m, domain, prefix, 1)
    dealloc_id = _in(inputs, "dealloc_id", m, domain, prefix, slot_w)

    # Restore (mispredict)
    _in(inputs, "restore_valid", m, domain, prefix, 1)
    restore_id = _in(inputs, "restore_id", m, domain, prefix, slot_w)

    # ── State ────────────────────────────────────────────────────────
    alloc_ptr = domain.signal(width=slot_w, reset_value=0, name=f"{prefix}_alloc_ptr")
    occupied = [
        domain.signal(width=1, reset_value=0, name=f"{prefix}_occ_{s}")
        for s in range(n_slots)
    ]

    # Checkpoint storage (scalar RAT)
    ckpt_smap = [
        [
            domain.signal(width=sphys_w, reset_value=a, name=f"{prefix}_smap_{s}_{a}")
            for a in range(n_sarch)
        ]
        for s in range(n_slots)
    ]
    ckpt_srdy = [
        [
            domain.signal(width=1, reset_value=1, name=f"{prefix}_srdy_{s}_{a}")
            for a in range(n_sarch)
        ]
        for s in range(n_slots)
    ]

    # Checkpoint storage (tile RAT)
    ckpt_tmap = [
        [
            domain.signal(width=tphys_w, reset_value=a, name=f"{prefix}_tmap_{s}_{a}")
            for a in range(n_tarch)
        ]
        for s in range(n_slots)
    ]
    ckpt_trdy = [
        [
            domain.signal(width=1, reset_value=1, name=f"{prefix}_trdy_{s}_{a}")
            for a in range(n_tarch)
        ]
        for s in range(n_slots)
    ]

    # Pointer storage
    ckpt_sfl = [
        domain.signal(width=sfl_ptr_w, reset_value=0, name=f"{prefix}_sfl_{s}")
        for s in range(n_slots)
    ]
    ckpt_tfl = [
        domain.signal(width=tfl_ptr_w, reset_value=0, name=f"{prefix}_tfl_{s}")
        for s in range(n_slots)
    ]
    ckpt_ras = [
        domain.signal(width=ras_ptr_w, reset_value=0, name=f"{prefix}_ras_{s}")
        for s in range(n_slots)
    ]

    # ── Outputs: allocated checkpoint ID ─────────────────────────────
    outs: dict = {}
    outs["alloc_id"] = alloc_ptr
    if inputs is None:
        m.output(f"{prefix}_alloc_id", wire_of(alloc_ptr))

    all_full = occupied[0]
    for s in range(1, n_slots):
        all_full = all_full & occupied[s]
    outs["full"] = all_full
    if inputs is None:
        m.output(f"{prefix}_full", wire_of(all_full))

    # ── Restore output: read selected checkpoint ─────────────────────
    for a in range(n_sarch):
        sel_smap = ckpt_smap[0][a]
        sel_srdy = ckpt_srdy[0][a]
        for s in range(n_slots):
            hit = restore_id == cas(domain, m.const(s, width=slot_w), cycle=0)
            sel_smap = mux(hit, ckpt_smap[s][a], sel_smap)
            sel_srdy = mux(hit, ckpt_srdy[s][a], sel_srdy)
        outs[f"rst_smap{a}"] = sel_smap
        outs[f"rst_srdy{a}"] = sel_srdy
        if inputs is None:
            m.output(f"{prefix}_rst_smap{a}", wire_of(sel_smap))
            m.output(f"{prefix}_rst_srdy{a}", wire_of(sel_srdy))

    for a in range(n_tarch):
        sel_tmap = ckpt_tmap[0][a]
        sel_trdy = ckpt_trdy[0][a]
        for s in range(n_slots):
            hit = restore_id == cas(domain, m.const(s, width=slot_w), cycle=0)
            sel_tmap = mux(hit, ckpt_tmap[s][a], sel_tmap)
            sel_trdy = mux(hit, ckpt_trdy[s][a], sel_trdy)
        outs[f"rst_tmap{a}"] = sel_tmap
        outs[f"rst_trdy{a}"] = sel_trdy
        if inputs is None:
            m.output(f"{prefix}_rst_tmap{a}", wire_of(sel_tmap))
            m.output(f"{prefix}_rst_trdy{a}", wire_of(sel_trdy))

    # Pointer restore
    sel_sfl = ckpt_sfl[0]
    sel_tfl = ckpt_tfl[0]
    sel_ras = ckpt_ras[0]
    for s in range(n_slots):
        hit = restore_id == cas(domain, m.const(s, width=slot_w), cycle=0)
        sel_sfl = mux(hit, ckpt_sfl[s], sel_sfl)
        sel_tfl = mux(hit, ckpt_tfl[s], sel_tfl)
        sel_ras = mux(hit, ckpt_ras[s], sel_ras)
    outs["rst_sfl"] = sel_sfl
    outs["rst_tfl"] = sel_tfl
    outs["rst_ras"] = sel_ras
    if inputs is None:
        m.output(f"{prefix}_rst_sfl", wire_of(sel_sfl))
        m.output(f"{prefix}_rst_tfl", wire_of(sel_tfl))
        m.output(f"{prefix}_rst_ras", wire_of(sel_ras))

    # ── Cycle 1: Sequential update ───────────────────────────────────
    domain.next()

    # Save to current alloc slot
    for s in range(n_slots):
        stag = cas(domain, m.const(s, width=slot_w), cycle=0)
        is_alloc = save_valid & (alloc_ptr == stag)
        for a in range(n_sarch):
            ckpt_smap[s][a].assign(save_smap[a], when=is_alloc)
            ckpt_srdy[s][a].assign(save_srdy[a], when=is_alloc)
        for a in range(n_tarch):
            ckpt_tmap[s][a].assign(save_tmap[a], when=is_alloc)
            ckpt_trdy[s][a].assign(save_trdy[a], when=is_alloc)
        ckpt_sfl[s].assign(save_sfl_head, when=is_alloc)
        ckpt_tfl[s].assign(save_tfl_head, when=is_alloc)
        ckpt_ras[s].assign(save_ras_ptr, when=is_alloc)

        is_dealloc = dealloc_valid & (dealloc_id == stag)
        occupied[s] <<= mux(
            is_alloc,
            cas(domain, m.const(1, width=1), cycle=0),
            mux(is_dealloc, cas(domain, m.const(0, width=1), cycle=0), occupied[s]),
        )

    # Advance alloc pointer
    next_ptr = (alloc_ptr + cas(domain, m.const(1, width=slot_w), cycle=0)).trunc(
        slot_w
    )
    alloc_ptr <<= mux(save_valid, next_ptr, alloc_ptr)

    return outs


checkpoint.__pycircuit_name__ = "checkpoint"


if __name__ == "__main__":
    pass
