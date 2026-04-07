"""Simplified Branch Prediction Unit — bimodal 2-bit counter table.

This is a P1-level stub. A full implementation would include:
  - TAGE predictor with 5 geometric history tables (§5.2.1)
  - BTB with 2048 entries, 4-way (§5.2.2)
  - RAS with 16-deep stack (§5.2.3)

Current model: simple bimodal counter indexed by PC[11:2].
"""

from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    mux,
    wire_of,
)


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def bpu(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    n_entries: int = 256,
    addr_w: int = 64,
    prefix: str = "bpu",
    inputs: dict | None = None,
) -> dict:
    idx_w = max(1, (n_entries - 1).bit_length())
    CTR_W = 2

    # ── Cycle 0: Inputs ──────────────────────────────────────────────
    pc = _in(inputs, "pc", m, domain, prefix, addr_w)

    # Update interface (from BRU resolve)
    update_valid = _in(inputs, "update_valid", m, domain, prefix, 1)
    update_pc = _in(inputs, "update_pc", m, domain, prefix, addr_w)
    update_taken = _in(inputs, "update_taken", m, domain, prefix, 1)
    update_target = _in(inputs, "update_target", m, domain, prefix, addr_w)

    # ── State: bimodal counter table ─────────────────────────────────
    counters = [
        domain.signal(width=CTR_W, reset_value=1, name=f"{prefix}_ctr_{i}")
        for i in range(n_entries)
    ]
    targets = [
        domain.signal(width=addr_w, reset_value=0, name=f"{prefix}_tgt_{i}")
        for i in range(n_entries)
    ]

    # ── Prediction (combinational) ───────────────────────────────────
    rd_idx = pc[2 : 2 + idx_w]

    pred_ctr = counters[0]
    pred_tgt = targets[0]
    for i in range(n_entries):
        hit = rd_idx == cas(domain, m.const(i, width=idx_w), cycle=0)
        pred_ctr = mux(hit, counters[i], pred_ctr)
        pred_tgt = mux(hit, targets[i], pred_tgt)

    taken = pred_ctr[1:2]  # MSB of 2-bit counter
    outs = {
        "taken": taken,
        "target": pred_tgt,
    }
    if inputs is None:
        for k, v in outs.items():
            m.output(f"{prefix}_{k}", wire_of(v))

    # ── Cycle 1: Update counter table ────────────────────────────────
    domain.next()

    wr_idx = update_pc[2 : 2 + idx_w]
    for i in range(n_entries):
        hit = update_valid & (wr_idx == cas(domain, m.const(i, width=idx_w), cycle=0))
        old_ctr = counters[i]
        one = cas(domain, m.const(1, width=CTR_W), cycle=0)
        zero = cas(domain, m.const(0, width=CTR_W), cycle=0)
        max_val = cas(domain, m.const(3, width=CTR_W), cycle=0)
        inc = mux(old_ctr == max_val, max_val, (old_ctr + one).trunc(CTR_W))
        dec = mux(old_ctr == zero, zero, (old_ctr - one).trunc(CTR_W))
        new_ctr = mux(update_taken, inc, dec)
        counters[i].assign(new_ctr, when=hit)
        targets[i].assign(update_target, when=hit & update_taken)

    return outs


bpu.__pycircuit_name__ = "bpu"


if __name__ == "__main__":
    pass
