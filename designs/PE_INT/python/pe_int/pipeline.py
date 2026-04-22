from __future__ import annotations

from pycircuit import mux

from .constants import MODE_2A, MODE_2B, MODE_2C


def select_mode(mode, v2a, v2b, v2c, v2d):
    return mux(mode == MODE_2A, v2a, mux(mode == MODE_2B, v2b, mux(mode == MODE_2C, v2c, v2d)))


def out1_hold_policy(domain, *, vld_aligned, out1_en_aligned, out1_aligned, prefix: str = "pe"):
    out1_hold = domain.state(width=16, reset_value=0, name=f"{prefix}_out1_hold")
    out1_next = mux(vld_aligned, mux(out1_en_aligned, out1_aligned, out1_hold), out1_hold)
    out1_hold.set(out1_next)
    return out1_next
