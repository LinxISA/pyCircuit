from __future__ import annotations

from typing import Sequence

from pycircuit import mux, wire_of


def booth_mul_signed(lhs, rhs):
    """
    DS §3.1 multiplier structural policy entry.
    RTL generation is expected to map this operation into the
    shared booth+compressor+BK template tracked by depth-probe flow.
    """
    return lhs * rhs


def dot8_reduce(products: Sequence[object]):
    if len(products) != 8:
        raise ValueError("dot8_reduce expects exactly 8 products")
    acc = products[0]
    for idx in range(1, 8):
        acc = acc + products[idx]
    return acc


def dot16_split8_reduce(products: Sequence[object]):
    if len(products) != 16:
        raise ValueError("dot16_split8_reduce expects exactly 16 products")
    lo = dot8_reduce(products[0:8])
    hi = dot8_reduce(products[8:16])
    return lo, hi


def shift_scale_x1_x2_x4(value, shift2b):
    """
    DS §3.3: use 2-level muxed shift (x1/x2/x4), avoid barrel shifter.
    """
    return mux(shift2b == 0, value, mux(shift2b == 1, value * 2, value * 4))


def select_one_hot4(sel0, sel1, sel2, sel3, cand0, cand1, cand2, cand3):
    """
    One-hot selection network to avoid deep priority mux chain.
    """
    return (cand0 * sel0) + (cand1 * sel1) + (cand2 * sel2) + (cand3 * sel3)


def sum_shift_pair(lo, hi, e1_a, e1_b):
    sh_lo = wire_of(e1_a)[0:1] + wire_of(e1_b)[0:1]
    sh_hi = wire_of(e1_a)[1:2] + wire_of(e1_b)[1:2]
    return shift_scale_x1_x2_x4(lo, sh_lo) + shift_scale_x1_x2_x4(hi, sh_hi)
