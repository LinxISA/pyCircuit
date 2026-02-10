# -*- coding: utf-8 -*-
"""Button debounce — counter-based with rising-edge output.

Each debouncer samples the raw button input every cycle.  If the input
differs from the previous sample the counter resets; otherwise it
increments.  When the counter reaches ``debounce_cycles - 1`` the
stable output is updated.  A one-cycle pulse is emitted on the rising
edge of the stable signal.

Usage (inside a pyCircuit design, at cycle 0 — before domain.next()):

    db = make_debouncer(domain, btn_raw, "set", debounce_cycles=1000)
    pulse = db["pulse"]           # 1-cycle high on button press

After domain.next(), call:

    update_debouncer(db)          # .set() the flop D-inputs
"""
from __future__ import annotations

from pycircuit import CycleAwareDomain, CycleAwareSignal, mux


def make_debouncer(
    domain: CycleAwareDomain,
    raw_input: CycleAwareSignal,
    name: str,
    debounce_cycles: int,
) -> dict[str, CycleAwareSignal]:
    """Create debounce state + combinational logic (all at current cycle).

    Returns a dict with:
        pulse         – 1-cycle rising-edge pulse (1-bit)
        cnt_r         – counter flop
        prev_r        – previous raw sample flop
        stable_r      – debounced stable value flop
        stable_prev_r – previous stable value (for edge detect) flop
        cnt_next      – next counter value (comb)
        stable_next   – next stable value (comb)
        raw           – the raw input (passed through for update)
    """
    c = lambda v, w: domain.const(v, width=w)

    DB_WIDTH = max((debounce_cycles - 1).bit_length(), 1)
    DB_MAX = c(debounce_cycles - 1, DB_WIDTH)

    # --- Flop definitions (Q outputs at current cycle) ---
    cnt_r         = domain.signal(f"db_{name}_cnt",         width=DB_WIDTH, reset=0)
    prev_r        = domain.signal(f"db_{name}_prev",        width=1, reset=0)
    stable_r      = domain.signal(f"db_{name}_stable",      width=1, reset=0)
    stable_prev_r = domain.signal(f"db_{name}_stable_prev", width=1, reset=0)

    # --- Combinational logic ---
    changed = raw_input.ne(prev_r)
    at_max  = cnt_r.eq(DB_MAX)

    # Counter: reset on change, freeze at max, else increment
    cnt_next = mux(changed, c(0, DB_WIDTH),
                   mux(at_max, cnt_r, cnt_r + 1))

    # Stable output: latch raw value when counter saturates
    stable_next = mux(at_max, raw_input, stable_r)

    # Rising-edge pulse on the stable signal
    pulse = stable_r & (~stable_prev_r)

    return {
        "pulse": pulse,
        "cnt_r": cnt_r, "prev_r": prev_r,
        "stable_r": stable_r, "stable_prev_r": stable_prev_r,
        "cnt_next": cnt_next, "stable_next": stable_next,
        "raw": raw_input,
    }


def update_debouncer(db: dict[str, CycleAwareSignal]) -> None:
    """Drive debouncer flop D-inputs.  Call after ``domain.next()``."""
    db["cnt_r"].set(db["cnt_next"])
    db["prev_r"].set(db["raw"])
    db["stable_r"].set(db["stable_next"])
    db["stable_prev_r"].set(db["stable_r"])
