# -*- coding: utf-8 -*-
"""16-digit Calculator with decimal support — pyCircuit unified signal model.

Values are stored as unsigned magnitude + decimal-place count + sign bit.
  e.g. 1.5 → magnitude=15, dp=1, neg=0

Arithmetic alignment:
  add/sub : scale the operand with fewer decimal places up by 10^(dp_diff)
  mul     : result_dp = dp_a + dp_b  (trim to 8 if needed)
  div     : extend dividend by 10^8 for precision

Key codes (5-bit):
  0-9=digit, 10=+, 11=-, 12=*, 13=/, 14==, 15=AC,
  16=+/- (sign), 17=BS (backspace), 18=% (percentage), 19=. (dot)
"""
from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    CycleAwareSignal,
    compile_cycle_aware,
    mux,
)

# ── Key codes (5-bit) ────────────────────────────────────────
KEY_0 = 0; KEY_1 = 1; KEY_2 = 2; KEY_3 = 3; KEY_4 = 4
KEY_5 = 5; KEY_6 = 6; KEY_7 = 7; KEY_8 = 8; KEY_9 = 9
KEY_ADD = 10; KEY_SUB = 11; KEY_MUL = 12; KEY_DIV = 13
KEY_EQ = 14; KEY_AC = 15; KEY_SGN = 16; KEY_BS = 17
KEY_PCT = 18; KEY_DOT = 19

# ── State / Operator codes ───────────────────────────────────
ST_INPUT_A = 0; ST_OP_WAIT = 1; ST_INPUT_B = 2; ST_RESULT = 3
OP_NONE = 0; OP_ADD = 1; OP_SUB = 2; OP_MUL = 3; OP_DIV = 4

MAX_DISPLAY = 9_999_999_999_999_999
MAX_DP = 8


# ── Helper: power-of-10 lookup (4-bit index → 64-bit) ───────

def _pow10(domain, idx, name):
    """Combinational mux: 10^idx for idx in 0..8."""
    c = lambda v, w: domain.const(v, width=w)
    p = domain.signal(name, width=64)
    p.set(c(1, 64))
    p.set(c(10, 64),          when=idx.eq(c(1, 4)))
    p.set(c(100, 64),         when=idx.eq(c(2, 4)))
    p.set(c(1_000, 64),       when=idx.eq(c(3, 4)))
    p.set(c(10_000, 64),      when=idx.eq(c(4, 4)))
    p.set(c(100_000, 64),     when=idx.eq(c(5, 4)))
    p.set(c(1_000_000, 64),   when=idx.eq(c(6, 4)))
    p.set(c(10_000_000, 64),  when=idx.eq(c(7, 4)))
    p.set(c(100_000_000, 64), when=idx.eq(c(8, 4)))
    return p


def _calculator_impl(m, domain):
    c = lambda v, w: domain.const(v, width=w)

    # ═══════════════════ Inputs ═══════════════════════════════
    key       = domain.input("key",       width=5)
    key_press = domain.input("key_press", width=1)

    # ═══════════════════ Registers ════════════════════════════
    display_r   = domain.signal("display",   width=64, reset=0)
    accum_r     = domain.signal("accum",     width=64, reset=0)
    state_r     = domain.signal("state",     width=2,  reset=ST_INPUT_A)
    op_r        = domain.signal("op",        width=3,  reset=OP_NONE)
    neg_r       = domain.signal("neg",       width=1,  reset=0)
    err_r       = domain.signal("err",       width=1,  reset=0)
    digit_cnt_r = domain.signal("digit_cnt", width=5,  reset=0)
    accum_neg_r = domain.signal("accum_neg", width=1,  reset=0)
    dp_r        = domain.signal("dp",        width=4,  reset=0)   # display decimal places
    accum_dp_r  = domain.signal("accum_dp",  width=4,  reset=0)   # accum decimal places
    has_dot_r   = domain.signal("has_dot",   width=1,  reset=0)   # dot entered

    # ═══════════════════ Key decoding ═════════════════════════
    is_digit = key.le(c(9, 5)) & key_press
    is_add   = key.eq(c(KEY_ADD, 5)) & key_press
    is_sub   = key.eq(c(KEY_SUB, 5)) & key_press
    is_mul   = key.eq(c(KEY_MUL, 5)) & key_press
    is_div   = key.eq(c(KEY_DIV, 5)) & key_press
    is_op    = is_add | is_sub | is_mul | is_div
    is_eq    = key.eq(c(KEY_EQ,  5)) & key_press
    is_ac    = key.eq(c(KEY_AC,  5)) & key_press
    is_sgn   = key.eq(c(KEY_SGN, 5)) & key_press
    is_bs    = key.eq(c(KEY_BS,  5)) & key_press
    is_pct   = key.eq(c(KEY_PCT, 5)) & key_press
    is_dot   = key.eq(c(KEY_DOT, 5)) & key_press

    new_op = domain.signal("new_op", width=3)
    new_op.set(c(OP_NONE, 3))
    new_op.set(c(OP_ADD, 3), when=is_add)
    new_op.set(c(OP_SUB, 3), when=is_sub)
    new_op.set(c(OP_MUL, 3), when=is_mul)
    new_op.set(c(OP_DIV, 3), when=is_div)

    in_a   = state_r.eq(c(ST_INPUT_A, 2))
    in_op  = state_r.eq(c(ST_OP_WAIT, 2))
    in_b   = state_r.eq(c(ST_INPUT_B, 2))
    in_res = state_r.eq(c(ST_RESULT,  2))

    # ═══════════════════ Digit / dot input logic ══════════════
    key_64 = key.trunc(width=4).zext(width=64)
    disp_x10 = (display_r << 3) + (display_r << 1)
    new_disp_digit = disp_x10 + key_64
    can_add_digit = digit_cnt_r.lt(c(16, 5))
    can_add_dp = dp_r.lt(c(MAX_DP, 4))

    # Backspace
    disp_div10 = display_r // c(10, 64)

    # ═══════════════════ ALU — alignment + compute ════════════

    # ---- Align decimal places for add/sub ----
    a_dp = accum_dp_r
    b_dp = dp_r
    a_more = a_dp.gt(b_dp)             # accum has more dp
    b_more = b_dp.gt(a_dp)             # display has more dp
    dp_diff = mux(a_more, a_dp - b_dp, b_dp - a_dp).trunc(width=4)
    dp_diff_cap = mux(dp_diff.gt(c(MAX_DP, 4)), c(MAX_DP, 4), dp_diff)
    align_factor = _pow10(domain, dp_diff_cap, "align_pow10")

    a_aligned = mux(b_more, accum_r * align_factor, accum_r)
    b_aligned = mux(a_more, display_r * align_factor, display_r)
    common_dp  = mux(a_more, a_dp, b_dp)

    # Signed values (for add/sub)
    a_s = mux(accum_neg_r, c(0, 64) - a_aligned, a_aligned).as_signed()
    b_s = mux(neg_r, c(0, 64) - b_aligned, b_aligned).as_signed()
    sum_res  = a_s + b_s
    diff_res = a_s - b_s

    # ---- Multiplication (raw magnitudes) ----
    prod_raw = accum_r * display_r
    # Signed: negate if signs differ
    prod_neg = accum_neg_r ^ neg_r
    mul_dp_5 = accum_dp_r.zext(width=5) + dp_r.zext(width=5)
    mul_dp_over = mul_dp_5.gt(c(MAX_DP, 5))
    trim_amt = (mul_dp_5 - c(MAX_DP, 5)).trunc(width=4)
    trim_factor = _pow10(domain, mux(mul_dp_over, trim_amt, c(0, 4)), "trim_pow10")
    prod_trimmed = mux(mul_dp_over, prod_raw // trim_factor, prod_raw)
    mul_dp = mux(mul_dp_over, c(MAX_DP, 4), mul_dp_5.trunc(width=4))

    # ---- Division (extend dividend for precision) ----
    div_scale = c(10**MAX_DP, 64)      # 10^8
    a_ext = accum_r * div_scale
    b_safe = mux(display_r.eq(c(0, 64)), c(1, 64), display_r)
    div_raw = a_ext // b_safe
    div_neg = accum_neg_r ^ neg_r
    # result_dp = accum_dp + 8 - display_dp
    div_dp_5 = accum_dp_r.zext(width=5) + c(MAX_DP, 5) - dp_r.zext(width=5)
    div_dp_ok = div_dp_5.le(c(MAX_DP, 5))
    div_dp = mux(div_dp_ok, div_dp_5.trunc(width=4), c(MAX_DP, 4))
    div_by_zero = display_r.eq(c(0, 64))

    # ---- Select ALU result by operator ----
    alu_val = domain.signal("alu_val", width=64)
    alu_neg = domain.signal("alu_neg", width=1)
    alu_dp  = domain.signal("alu_dp",  width=4)

    # Default: add result
    add_is_neg = sum_res.lt(c(0, 64).as_signed())
    add_mag = mux(add_is_neg, c(0, 64) - sum_res.as_unsigned(), sum_res.as_unsigned())
    sub_is_neg = diff_res.lt(c(0, 64).as_signed())
    sub_mag = mux(sub_is_neg, c(0, 64) - diff_res.as_unsigned(), diff_res.as_unsigned())

    alu_val.set(add_mag)
    alu_neg.set(add_is_neg)
    alu_dp.set(common_dp)
    alu_val.set(add_mag,       when=op_r.eq(c(OP_ADD, 3)))
    alu_neg.set(add_is_neg,    when=op_r.eq(c(OP_ADD, 3)))
    alu_dp.set(common_dp,      when=op_r.eq(c(OP_ADD, 3)))
    alu_val.set(sub_mag,       when=op_r.eq(c(OP_SUB, 3)))
    alu_neg.set(sub_is_neg,    when=op_r.eq(c(OP_SUB, 3)))
    alu_dp.set(common_dp,      when=op_r.eq(c(OP_SUB, 3)))
    alu_val.set(prod_trimmed,  when=op_r.eq(c(OP_MUL, 3)))
    alu_neg.set(prod_neg,      when=op_r.eq(c(OP_MUL, 3)))
    alu_dp.set(mul_dp,         when=op_r.eq(c(OP_MUL, 3)))
    alu_val.set(div_raw,       when=op_r.eq(c(OP_DIV, 3)))
    alu_neg.set(div_neg,       when=op_r.eq(c(OP_DIV, 3)))
    alu_dp.set(div_dp,         when=op_r.eq(c(OP_DIV, 3)))

    max_val = c(MAX_DISPLAY, 64)
    result_overflow = alu_val.gt(max_val)
    compute_err = result_overflow | (div_by_zero & op_r.eq(c(OP_DIV, 3)))

    # ---- Percentage: accum * display / 100 ----
    pct_raw = accum_r * display_r // c(100, 64)
    pct_dp_5 = accum_dp_r.zext(width=5) + dp_r.zext(width=5)
    pct_dp = mux(pct_dp_5.gt(c(MAX_DP, 5)), c(MAX_DP, 4), pct_dp_5.trunc(width=4))
    pct_overflow = pct_raw.gt(max_val)

    # ═══════════════════ DFF boundary ═════════════════════════
    domain.next()

    # ═══════════════════ Register updates (.set) ══════════════

    # Default: hold all
    display_r.set(display_r);   accum_r.set(accum_r)
    state_r.set(state_r);       op_r.set(op_r)
    neg_r.set(neg_r);           err_r.set(err_r)
    digit_cnt_r.set(digit_cnt_r); accum_neg_r.set(accum_neg_r)
    dp_r.set(dp_r);             accum_dp_r.set(accum_dp_r)
    has_dot_r.set(has_dot_r)

    # ── AC ──
    display_r.set(c(0, 64),       when=is_ac);  accum_r.set(c(0, 64), when=is_ac)
    state_r.set(c(ST_INPUT_A, 2), when=is_ac);  op_r.set(c(OP_NONE, 3), when=is_ac)
    neg_r.set(c(0, 1),            when=is_ac);  err_r.set(c(0, 1), when=is_ac)
    digit_cnt_r.set(c(0, 5),      when=is_ac);  accum_neg_r.set(c(0, 1), when=is_ac)
    dp_r.set(c(0, 4),             when=is_ac);  accum_dp_r.set(c(0, 4), when=is_ac)
    has_dot_r.set(c(0, 1),        when=is_ac)

    # ── Digit in INPUT_A or INPUT_B ──
    d_ab = is_digit & (in_a | in_b) & can_add_digit & (~err_r)
    display_r.set(new_disp_digit,    when=d_ab)
    digit_cnt_r.set(digit_cnt_r + 1, when=d_ab)
    dp_r.set(dp_r + 1,              when=d_ab & has_dot_r & can_add_dp)

    # ── Digit in OP_WAIT → start B ──
    d_op = is_digit & in_op & (~err_r)
    display_r.set(key_64,            when=d_op); digit_cnt_r.set(c(1, 5), when=d_op)
    neg_r.set(c(0, 1), when=d_op);  dp_r.set(c(0, 4), when=d_op)
    has_dot_r.set(c(0, 1), when=d_op); state_r.set(c(ST_INPUT_B, 2), when=d_op)

    # ── Digit in RESULT → new calc ──
    d_res = is_digit & in_res & (~err_r)
    display_r.set(key_64, when=d_res);  digit_cnt_r.set(c(1, 5), when=d_res)
    neg_r.set(c(0, 1), when=d_res);    dp_r.set(c(0, 4), when=d_res)
    has_dot_r.set(c(0, 1), when=d_res); accum_r.set(c(0, 64), when=d_res)
    accum_neg_r.set(c(0, 1), when=d_res); accum_dp_r.set(c(0, 4), when=d_res)
    op_r.set(c(OP_NONE, 3), when=d_res); state_r.set(c(ST_INPUT_A, 2), when=d_res)

    # ── Dot key ──
    dot_ok = is_dot & (~has_dot_r) & (~err_r) & (in_a | in_b)
    has_dot_r.set(c(1, 1), when=dot_ok)
    # If pressed with no digits yet, display stays 0 but dot is active
    digit_cnt_r.set(c(1, 5), when=dot_ok & digit_cnt_r.eq(c(0, 5)))

    # Dot in OP_WAIT → start B with "0."
    dot_op = is_dot & in_op & (~err_r)
    display_r.set(c(0, 64), when=dot_op);  has_dot_r.set(c(1, 1), when=dot_op)
    dp_r.set(c(0, 4), when=dot_op);        digit_cnt_r.set(c(1, 5), when=dot_op)
    neg_r.set(c(0, 1), when=dot_op);       state_r.set(c(ST_INPUT_B, 2), when=dot_op)

    # Dot in RESULT → new calc with "0."
    dot_res = is_dot & in_res & (~err_r)
    display_r.set(c(0, 64), when=dot_res);   has_dot_r.set(c(1, 1), when=dot_res)
    dp_r.set(c(0, 4), when=dot_res);         digit_cnt_r.set(c(1, 5), when=dot_res)
    neg_r.set(c(0, 1), when=dot_res);        accum_r.set(c(0, 64), when=dot_res)
    accum_neg_r.set(c(0, 1), when=dot_res);  accum_dp_r.set(c(0, 4), when=dot_res)
    op_r.set(c(OP_NONE, 3), when=dot_res);   state_r.set(c(ST_INPUT_A, 2), when=dot_res)

    # ── Operator ──
    op_a = is_op & in_a & (~err_r)
    accum_r.set(display_r, when=op_a);  accum_neg_r.set(neg_r, when=op_a)
    accum_dp_r.set(dp_r, when=op_a);    op_r.set(new_op, when=op_a)
    state_r.set(c(ST_OP_WAIT, 2), when=op_a); has_dot_r.set(c(0, 1), when=op_a)

    op_op = is_op & in_op & (~err_r)
    op_r.set(new_op, when=op_op)

    op_b = is_op & in_b & (~err_r)
    accum_r.set(alu_val, when=op_b);     accum_neg_r.set(alu_neg, when=op_b)
    accum_dp_r.set(alu_dp, when=op_b);   display_r.set(alu_val, when=op_b)
    neg_r.set(alu_neg, when=op_b);       dp_r.set(alu_dp, when=op_b)
    err_r.set(compute_err, when=op_b);   op_r.set(new_op, when=op_b)
    digit_cnt_r.set(c(0, 5), when=op_b); has_dot_r.set(c(0, 1), when=op_b)
    state_r.set(c(ST_OP_WAIT, 2), when=op_b)

    op_res = is_op & in_res & (~err_r)
    accum_r.set(display_r, when=op_res); accum_neg_r.set(neg_r, when=op_res)
    accum_dp_r.set(dp_r, when=op_res);  op_r.set(new_op, when=op_res)
    state_r.set(c(ST_OP_WAIT, 2), when=op_res); has_dot_r.set(c(0, 1), when=op_res)

    # ── Equals ──
    eq_b = is_eq & in_b & (~err_r)
    display_r.set(alu_val, when=eq_b);   neg_r.set(alu_neg, when=eq_b)
    dp_r.set(alu_dp, when=eq_b);         err_r.set(compute_err, when=eq_b)
    digit_cnt_r.set(c(0, 5), when=eq_b); has_dot_r.set(c(0, 1), when=eq_b)
    state_r.set(c(ST_RESULT, 2), when=eq_b)

    # ── +/- ──
    sgn_ok = is_sgn & (~err_r) & display_r.ne(c(0, 64))
    neg_r.set(~neg_r, when=sgn_ok)

    # ── Backspace ──
    bs_ok = is_bs & (~err_r) & (in_a | in_b) & digit_cnt_r.gt(c(0, 5))
    display_r.set(disp_div10, when=bs_ok)
    digit_cnt_r.set(digit_cnt_r - 1, when=bs_ok)
    # If removing decimal digit, decrement dp
    dp_r.set(dp_r - 1, when=bs_ok & dp_r.gt(c(0, 4)))
    # If dp reaches 0, clear has_dot when last decimal digit removed
    has_dot_r.set(c(0, 1), when=bs_ok & dp_r.eq(c(1, 4)))
    # If removing last digit
    neg_r.set(c(0, 1), when=bs_ok & digit_cnt_r.eq(c(1, 5)))

    # ── Percentage ──
    pct_ok = is_pct & in_b & (~err_r)
    display_r.set(pct_raw, when=pct_ok);  dp_r.set(pct_dp, when=pct_ok)
    neg_r.set(c(0, 1), when=pct_ok);     err_r.set(pct_overflow, when=pct_ok)
    digit_cnt_r.set(c(0, 5), when=pct_ok); has_dot_r.set(c(0, 1), when=pct_ok)

    # ═══════════════════ Outputs ══════════════════════════════
    m.output("display",     display_r)
    m.output("display_neg", neg_r)
    m.output("display_err", err_r)
    m.output("display_dp",  dp_r)
    m.output("op_pending",  op_r)


def calculator(m: CycleAwareCircuit, domain: CycleAwareDomain) -> None:
    _calculator_impl(m, domain)

def build():
    return compile_cycle_aware(calculator, name="calculator")

if __name__ == "__main__":
    print(build().emit_mlir())
