from __future__ import annotations

from pycircuit import wire_of

from .decode import decode_s4_from_40, decode_s4_hi_from_80, decode_s5, decode_s5_hi_from_80, decode_s8_from_lane_word
from .lane_mac import booth_mul_signed, dot8_reduce, dot16_split8_reduce, select_one_hot4, sum_shift_pair


def _mode_one_hot(mode):
    is_2a = mode == 0
    is_2b = mode == 1
    is_2c = mode == 2
    is_2d = mode == 3
    return is_2a, is_2b, is_2c, is_2d


def comb1_generate_products(a, b, b1):
    """
    DS §5.3:
    - per-lane decode (S8/S4/S5)
    - per-lane multiplier generation (M8x8/M8x4/M8x5/M5x5)
    """
    b_lo40 = wire_of(b)[0:40]

    p2a = []
    p2b0 = []
    p2b1 = []
    p2d0 = []
    p2d1 = []
    p2c0 = []
    p2c1 = []

    for lane_idx in range(8):
        a8_i = decode_s8_from_lane_word(a, lane_idx)
        b8_i = decode_s8_from_lane_word(b, lane_idx)
        b4_0_i = decode_s4_from_40(b_lo40, lane_idx)
        b4_1_i = decode_s4_hi_from_80(b, lane_idx)
        b5_0_i = decode_s5(b_lo40, lane_idx)
        b5_1_i = decode_s5_hi_from_80(b, lane_idx)
        p2a.append(booth_mul_signed(a8_i, b8_i))
        p2b0.append(booth_mul_signed(a8_i, b4_0_i))
        p2b1.append(booth_mul_signed(a8_i, b4_1_i))
        p2d0.append(booth_mul_signed(a8_i, b5_0_i))
        p2d1.append(booth_mul_signed(a8_i, b5_1_i))

    for lane_idx in range(16):
        a5_i = decode_s5(a, lane_idx)
        b0_5_i = decode_s5(b, lane_idx)
        b1_5_i = decode_s5(b1, lane_idx)
        p2c0.append(booth_mul_signed(a5_i, b0_5_i))
        p2c1.append(booth_mul_signed(a5_i, b1_5_i))

    return {
        "p2a": p2a,
        "p2b0": p2b0,
        "p2b1": p2b1,
        "p2d0": p2d0,
        "p2d1": p2d1,
        "p2c0": p2c0,
        "p2c1": p2c1,
    }


def comb2_reduce_products(products):
    """
    DS §5.5:
    - dot8 reduction for 2a/2b/2d
    - dot16 split8 partial reduction for 2c
    """
    p2a = products["p2a"]
    p2b0 = products["p2b0"]
    p2b1 = products["p2b1"]
    p2d0 = products["p2d0"]
    p2d1 = products["p2d1"]
    p2c0 = products["p2c0"]
    p2c1 = products["p2c1"]

    s2a = dot8_reduce(p2a)
    s2b0 = dot8_reduce(p2b0)
    s2b1 = dot8_reduce(p2b1)
    s2d0 = dot8_reduce(p2d0)
    s2d1 = dot8_reduce(p2d1)
    s2c0_lo, s2c0_hi = dot16_split8_reduce(p2c0)
    s2c1_lo, s2c1_hi = dot16_split8_reduce(p2c1)
    return {
        "s2a": s2a,
        "s2b0": s2b0,
        "s2b1": s2b1,
        "s2d0": s2d0,
        "s2d1": s2d1,
        "s2c0_lo": s2c0_lo,
        "s2c0_hi": s2c0_hi,
        "s2c1_lo": s2c1_lo,
        "s2c1_hi": s2c1_hi,
    }


def comb3_mode_merge(reduced, mode, e1_a, e1_b0, e1_b1):
    """
    DS §5.7:
    - 2c post-scale x1/x2/x4
    - one-hot mode merge for out0_raw/out1_raw
    """
    is_2a, is_2b, is_2c, is_2d = _mode_one_hot(mode)
    s2c0 = sum_shift_pair(reduced["s2c0_lo"], reduced["s2c0_hi"], e1_a, e1_b0)
    s2c1 = sum_shift_pair(reduced["s2c1_lo"], reduced["s2c1_hi"], e1_a, e1_b1)

    out0_raw = select_one_hot4(
        is_2a,
        is_2b,
        is_2c,
        is_2d,
        reduced["s2a"],
        reduced["s2b0"],
        s2c0,
        reduced["s2d0"],
    )
    out1_raw = select_one_hot4(
        is_2a,
        is_2b,
        is_2c,
        is_2d,
        reduced["s2a"] * 0,
        reduced["s2b1"],
        s2c1,
        reduced["s2d1"],
    )
    return out0_raw, out1_raw
