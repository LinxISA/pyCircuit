from __future__ import annotations

from typing import Sequence

from pycircuit import cas, cat, mux, wire_of

from .constants import OUT0_W, OUT1_W
from .decode import zext


REDUCE_W = 32


def booth_mul_signed(lhs, rhs):
    """
    Current functional signed multiplier entry point.

    Explicit radix-4 Booth remains a documented deferred topology optimization;
    this implementation keeps natural-width signed shift/add/sub behavior.
    """
    lhs_w = wire_of(lhs)
    rhs_w = wire_of(rhs)
    lhs_bits = lhs_w.width
    rhs_bits = rhs_w.width
    if lhs_bits <= 8 and rhs_bits <= 8:
        return _mul_signed_twos_complement(lhs_w, rhs_w)
    return lhs_w * rhs_w


def _mul_unsigned_rows(lhs, rhs, *, width: int):
    lhs_w = wire_of(lhs)
    rhs_w = wire_of(rhs)
    zero = zext(lhs_w[0:1], width) & 0
    acc = zero
    for bit_idx in range(rhs_w.width):
        row = wire_of(zext(lhs_w, width) << bit_idx)[0:width]
        acc = acc + wire_of(rhs_w[bit_idx : bit_idx + 1]).select(row, zero)
    return wire_of(acc)[0:width]


def _mul_signed_twos_complement(lhs, rhs):
    lhs_w = wire_of(lhs)
    rhs_w = wire_of(rhs)
    lhs_bits = lhs_w.width
    rhs_bits = rhs_w.width
    product_w = lhs_bits + rhs_bits
    zero = zext(lhs_w[0:1], product_w) & 0

    unsigned_product = _mul_unsigned_rows(lhs_w, rhs_w, width=product_w)
    lhs_correction = wire_of(rhs_w[rhs_bits - 1 : rhs_bits]).select(
        wire_of(zext(lhs_w, product_w) << rhs_bits)[0:product_w],
        zero,
    )
    rhs_correction = wire_of(lhs_w[lhs_bits - 1 : lhs_bits]).select(
        wire_of(zext(rhs_w, product_w) << lhs_bits)[0:product_w],
        zero,
    )
    return wire_of(unsigned_product - lhs_correction - rhs_correction)[0:product_w].as_signed()


def _pack_lsb_bits(bits: Sequence[object]):
    return cat(*reversed(bits))


def _bit(value, idx: int):
    return wire_of(value)[idx : idx + 1]


def _shift_carry_bits(carry_bits: Sequence[object], *, width: int = REDUCE_W):
    return cat(*reversed(carry_bits[: width - 1]), 0)


def ha(lhs, rhs):
    lhs_w = wire_of(lhs)[0:1]
    rhs_w = wire_of(rhs)[0:1]
    return lhs_w ^ rhs_w, lhs_w & rhs_w


def fa(lhs, rhs, carry_in):
    lhs_w = wire_of(lhs)[0:1]
    rhs_w = wire_of(rhs)[0:1]
    carry_in_w = wire_of(carry_in)[0:1]

    sum_bit = lhs_w ^ rhs_w ^ carry_in_w
    carry_bit = (lhs_w & rhs_w) | (lhs_w & carry_in_w) | (rhs_w & carry_in_w)
    return sum_bit, carry_bit


def cmpe42(in0, in1, in2, in3, cix, *, width: int = REDUCE_W):
    in0_w = wire_of(in0)[0:width]
    in1_w = wire_of(in1)[0:width]
    in2_w = wire_of(in2)[0:width]
    in3_w = wire_of(in3)[0:width]
    cix_bit = wire_of(cix)[0:1]

    sum_bits = []
    carry_bits = []
    cox_bits = []
    for bit_idx in range(width):
        pre_sum, cox_bit = fa(_bit(in0_w, bit_idx), _bit(in1_w, bit_idx), _bit(in2_w, bit_idx))
        sum_bit, carry_bit = fa(pre_sum, _bit(in3_w, bit_idx), cix_bit)
        sum_bits.append(sum_bit)
        carry_bits.append(carry_bit)
        cox_bits.append(cox_bit)
        cix_bit = cox_bit

    return _pack_lsb_bits(sum_bits), _shift_carry_bits(carry_bits, width=width), cox_bits[-1]


def brent_kung_cpa_truncated(lhs, rhs, *, width: int):
    lhs_w = wire_of(lhs)[0:width]
    rhs_w = wire_of(rhs)[0:width]
    p_init = [_bit(lhs_w, bit_idx) ^ _bit(rhs_w, bit_idx) for bit_idx in range(width)]
    g = [_bit(lhs_w, bit_idx) & _bit(rhs_w, bit_idx) for bit_idx in range(width)]
    p = list(p_init)

    step = 1
    while step < width:
        for bit_idx in range((2 * step) - 1, width, 2 * step):
            prev_idx = bit_idx - step
            g[bit_idx] = g[bit_idx] | (p[bit_idx] & g[prev_idx])
            p[bit_idx] = p[bit_idx] & p[prev_idx]
        step *= 2

    step //= 4
    while step >= 1:
        for bit_idx in range((3 * step) - 1, width, 2 * step):
            prev_idx = bit_idx - step
            g[bit_idx] = g[bit_idx] | (p[bit_idx] & g[prev_idx])
            p[bit_idx] = p[bit_idx] & p[prev_idx]
        step //= 2

    zero = _bit(lhs_w, 0) & 0
    sum_bits = []
    for bit_idx in range(width):
        carry_in = zero if bit_idx == 0 else g[bit_idx - 1]
        sum_bits.append(p_init[bit_idx] ^ carry_in)
    return _pack_lsb_bits(sum_bits)


def _wallace_dot8_reduce(products: Sequence[object], *, width: int = REDUCE_W):
    if len(products) != 8:
        raise ValueError("dot8_reduce expects exactly 8 products")
    terms = [wire_of(product)[0:width] for product in products]
    zero_cix = _bit(terms[0], 0) & 0

    # Wallace-style carry-save tree using explicit 4:2 compressors. The final
    # row merge is intentionally left as the only CPA in this reduction.
    lo_sum, lo_carry, _ = cmpe42(terms[0], terms[1], terms[2], terms[3], zero_cix, width=width)
    hi_sum, hi_carry, _ = cmpe42(terms[4], terms[5], terms[6], terms[7], zero_cix, width=width)
    final_sum, final_carry, terminal_cox = cmpe42(lo_sum, lo_carry, hi_sum, hi_carry, zero_cix, width=width)

    # Intentional truncation policy:
    # `terminal_cox` has weight 2^width, which is outside the fixed-width output
    # contract of this dot8 reducer (`sum[width-1:0]`). We intentionally drop
    # this out-of-range carry at the fixed-width final CPA boundary.
    _ = terminal_cox
    return brent_kung_cpa_truncated(final_sum, final_carry, width=width)


def wallace_dot8_tree(m, domain, *, inputs: dict | None = None, width: int = REDUCE_W, prefix: str = "wallace_dot8"):
    terms = []
    for idx in range(8):
        key = f"in{idx}"
        if inputs is not None and key in inputs:
            terms.append(inputs[key])
        else:
            terms.append(m.input(f"{prefix}_{key}", width=width))

    result = cas(domain, _wallace_dot8_reduce(terms, width=width), cycle=0)
    outs = {"sum": result}
    if inputs is None:
        m.output(f"{prefix}_sum", wire_of(result))
    return outs


wallace_dot8_tree.__pycircuit_name__ = "PE_INT_WALLACE_DOT8_TREE"


def wallace_dot8_tree_w19(m, domain, *, inputs: dict | None = None, prefix: str = "wallace_dot8_w19"):
    return wallace_dot8_tree(m, domain, inputs=inputs, width=OUT0_W, prefix=prefix)


wallace_dot8_tree_w19.__pycircuit_name__ = "PE_INT_WALLACE_DOT8_TREE_W19"


def wallace_dot8_tree_w16(m, domain, *, inputs: dict | None = None, prefix: str = "wallace_dot8_w16"):
    return wallace_dot8_tree(m, domain, inputs=inputs, width=OUT1_W, prefix=prefix)


wallace_dot8_tree_w16.__pycircuit_name__ = "PE_INT_WALLACE_DOT8_TREE_W16"


def dot8_reduce(products: Sequence[object]):
    return _wallace_dot8_reduce(products)


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
    value_w = wire_of(value)
    return wire_of(shift2b == 0).select(
        value_w,
        wire_of(shift2b == 1).select(value_w << 1, value_w << 2),
    )


def select_one_hot4(sel0, sel1, sel2, cand0, cand1, cand2, cand3):
    """
    One-hot mode selection. Use muxes instead of boolean-masked
    multiplication so PyCircuit does not insert balancing registers in comb3.
    """
    lo_pair = wire_of(sel0).select(wire_of(cand0), wire_of(cand1))
    hi_pair = wire_of(sel2).select(wire_of(cand2), wire_of(cand3))
    return wire_of(sel0 | sel1).select(lo_pair, hi_pair)


def sum_shift_pair(lo, hi, e1_a, e1_b, *, width: int = REDUCE_W):
    e1_a_w = wire_of(e1_a)
    e1_b_w = wire_of(e1_b)
    sh_lo = zext(e1_a_w[0:1], 2) + zext(e1_b_w[0:1], 2)
    sh_hi = zext(e1_a_w[1:2], 2) + zext(e1_b_w[1:2], 2)
    lo_scaled = wire_of(shift_scale_x1_x2_x4(wire_of(lo)[0:width], sh_lo))[0:width]
    hi_scaled = wire_of(shift_scale_x1_x2_x4(wire_of(hi)[0:width], sh_hi))[0:width]
    return brent_kung_cpa_truncated(lo_scaled, hi_scaled, width=width)
