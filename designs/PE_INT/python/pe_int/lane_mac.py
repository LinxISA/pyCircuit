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
    scale2 = value + value
    scale4 = scale2 + scale2
    return mux(shift2b == 0, value, mux(shift2b == 1, scale2, scale4))


def sum_shift_pair(lo, hi, e1_a, e1_b):
    lo_w = wire_of(lo)
    hi_w = wire_of(hi)
    e1_a_w = wire_of(e1_a)
    e1_b_w = wire_of(e1_b)
    sh_lo = e1_a_w[0:1]._zext(width=2) + e1_b_w[0:1]._zext(width=2)
    sh_hi = e1_a_w[1:2]._zext(width=2) + e1_b_w[1:2]._zext(width=2)
    return shift_scale_x1_x2_x4(lo_w, sh_lo) + shift_scale_x1_x2_x4(hi_w, sh_hi)
