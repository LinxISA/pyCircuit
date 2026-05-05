from __future__ import annotations

from typing import Sequence

from pycircuit import cas, cat, mux, wire_of

from .decode import zext


REDUCE_W = 32


def booth_mul_signed(lhs, rhs):
    """
    DS §3.1 multiplier structural policy entry.
    RTL generation is expected to map this operation into the
    shared booth+compressor+BK template tracked by depth-probe flow.
    """
    return lhs * rhs


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


def _wallace_dot8_reduce(products: Sequence[object], *, width: int = REDUCE_W):
    if len(products) != 8:
        raise ValueError("dot8_reduce expects exactly 8 products")
    terms = [wire_of(product)[0:width] for product in products]
    zero_cix = _bit(terms[0], 0) & 0

    # Wallace-style carry-save tree using explicit 4:2 compressors. The final
    # row merge is intentionally left as the only CPA in this reduction.
    lo_sum, lo_carry, _ = cmpe42(terms[0], terms[1], terms[2], terms[3], zero_cix, width=width)
    hi_sum, hi_carry, _ = cmpe42(terms[4], terms[5], terms[6], terms[7], zero_cix, width=width)
    final_sum, final_carry, _ = cmpe42(lo_sum, lo_carry, hi_sum, hi_carry, zero_cix, width=width)
    return final_sum + final_carry


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
    return wire_of(shift2b == 0)._select_internal(
        value_w,
        wire_of(shift2b == 1)._select_internal(value_w << 1, value_w << 2),
    )


def select_one_hot4(sel0, sel1, sel2, sel3, cand0, cand1, cand2, cand3):
    """
    One-hot mode selection. Use muxes instead of boolean-masked
    multiplication so PyCircuit does not insert balancing registers in comb3.
    """
    _ = sel3
    return wire_of(sel0)._select_internal(
        wire_of(cand0),
        wire_of(sel1)._select_internal(
            wire_of(cand1),
            wire_of(sel2)._select_internal(wire_of(cand2), wire_of(cand3)),
        ),
    )


def sum_shift_pair(lo, hi, e1_a, e1_b):
    e1_a_w = wire_of(e1_a)
    e1_b_w = wire_of(e1_b)
    sh_lo = zext(e1_a_w[0:1], 2) + zext(e1_b_w[0:1], 2)
    sh_hi = zext(e1_a_w[1:2], 2) + zext(e1_b_w[1:2], 2)
    return shift_scale_x1_x2_x4(lo, sh_lo) + shift_scale_x1_x2_x4(hi, sh_hi)
