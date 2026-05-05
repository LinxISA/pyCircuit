from __future__ import annotations

from pycircuit import mux, wire_of

from .decode import decode_s4_from_40, decode_s4_hi_from_80, decode_s5, decode_s5_hi_from_80, decode_s8_from_lane_word
from .lane_mac import booth_mul_signed, dot8_reduce, dot16_split8_reduce, sum_shift_pair


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


def comb3_mode_merge(reduced, e1_a, e1_b0, e1_b1, *, is_2a, is_2b, is_2c, is_2d):
    """
    DS §5.7:
    - 2c post-scale x1/x2/x4
    - one-hot mode merge for out0_raw/out1_raw
    """
    s2c0 = sum_shift_pair(reduced["s2c0_lo"], reduced["s2c0_hi"], e1_a, e1_b0)
    s2c1 = sum_shift_pair(reduced["s2c1_lo"], reduced["s2c1_hi"], e1_a, e1_b1)

    # DS hard rule: comb3 mode merge uses registered one-hot directly; no mode==const compare here.
    # Keep comb3 as raw-wire combinational logic so cycle-aware balancing does not add flops here.
    is_2a_w = wire_of(is_2a)
    is_2b_w = wire_of(is_2b)
    is_2c_w = wire_of(is_2c)
    s2a_w = wire_of(reduced["s2a"])
    s2b0_w = wire_of(reduced["s2b0"])
    s2b1_w = wire_of(reduced["s2b1"])
    s2d0_w = wire_of(reduced["s2d0"])
    s2d1_w = wire_of(reduced["s2d1"])

    out0_raw = mux(is_2a_w, s2a_w, mux(is_2b_w, s2b0_w, mux(is_2c_w, s2c0, s2d0_w)))
    # 2a does not commit out1; use a real datapath branch instead of `* 0`
    # so pyCircuit does not insert multiplier balance registers in comb3.
    out1_raw = mux(is_2a_w, s2b1_w, mux(is_2b_w, s2b1_w, mux(is_2c_w, s2c1, s2d1_w)))
    return out0_raw, out1_raw
