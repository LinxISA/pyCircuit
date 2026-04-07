from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    unsigned,
    wire_of,
)

KEY_ADD = 10
KEY_SUB = 11
KEY_MUL = 12
KEY_DIV = 13
KEY_EQ = 14
KEY_AC = 15

OP_ADD = 0
OP_SUB = 1
OP_MUL = 2
OP_DIV = 3


def build(m: CycleAwareCircuit, domain: CycleAwareDomain) -> None:
    key = cas(domain, m.input("key", width=5), cycle=0)
    key_press = cas(domain, m.input("key_press", width=1), cycle=0)

    lhs = domain.signal(width=64, reset_value=0, name="lhs")
    rhs = domain.signal(width=64, reset_value=0, name="rhs")
    op = domain.signal(width=2, reset_value=0, name="op")
    in_rhs = domain.signal(width=1, reset_value=0, name="in_rhs")
    display = domain.signal(width=64, reset_value=0, name="display_r")

    digit_lo = unsigned(wire_of(key[0:4]))
    digit = cas(domain, digit_lo._zext(width=64), cycle=0)

    is_digit = key_press & (key <= 9)
    is_add = key_press & (key == KEY_ADD)
    is_sub = key_press & (key == KEY_SUB)
    is_mul = key_press & (key == KEY_MUL)
    is_div = key_press & (key == KEY_DIV)
    is_eq = key_press & (key == KEY_EQ)
    is_ac = key_press & (key == KEY_AC)

    lhs_n = lhs
    rhs_n = rhs
    op_n = op
    in_rhs_n = in_rhs
    disp_n = display

    rhs_new = rhs_n * 10 + digit
    lhs_new = lhs_n * 10 + digit
    rhs_a = rhs_new if (is_digit & in_rhs_n) else rhs_n
    disp_a = rhs_new if (is_digit & in_rhs_n) else disp_n
    lhs_a = lhs_new if (is_digit & ~in_rhs_n) else lhs_n
    disp_a = lhs_new if (is_digit & ~in_rhs_n) else disp_a

    op_sel = is_add | is_sub | is_mul | is_div
    in_rhs_b = 1 if op_sel else in_rhs_n
    rhs_b = 0 if op_sel else rhs_a
    lhs_b = lhs_a
    disp_b = disp_a

    op_b = op_n
    op_b = OP_ADD if is_add else op_b
    op_b = OP_SUB if is_sub else op_b
    op_b = OP_MUL if is_mul else op_b
    op_b = OP_DIV if is_div else op_b

    rhs_safe = rhs_b if (rhs_b != 0) else 1
    result = lhs_b
    result = (lhs_b + rhs_b) if (op_b == OP_ADD) else result
    result = (lhs_b - rhs_b) if (op_b == OP_SUB) else result
    result = (lhs_b * rhs_b) if (op_b == OP_MUL) else result
    result = (
        cas(domain, wire_of(lhs_b) // wire_of(rhs_safe), cycle=0)
        if (op_b == OP_DIV)
        else result
    )

    lhs_c = result if is_eq else lhs_b
    rhs_c = 0 if is_eq else rhs_b
    in_rhs_c = 0 if is_eq else in_rhs_b
    disp_c = result if is_eq else disp_b
    op_c = op_b

    lhs_next = 0 if is_ac else lhs_c
    rhs_next = 0 if is_ac else rhs_c
    op_next = 0 if is_ac else op_c
    in_rhs_next = 0 if is_ac else in_rhs_c
    disp_next = 0 if is_ac else disp_c

    m.output("display", wire_of(display))
    m.output("op_pending", wire_of(op))

    domain.next()

    lhs <<= lhs_next
    rhs <<= rhs_next
    op <<= op_next
    in_rhs <<= in_rhs_next
    display <<= disp_next


build.__pycircuit_name__ = "calculator"


if __name__ == "__main__":
    pass
