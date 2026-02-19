from __future__ import annotations

from pycircuit import Circuit, compile, module, unsigned, u

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


@module
def build(m: Circuit) -> None:
    clk = m.clock("clk")
    rst = m.reset("rst")
    key = m.input("key", width=5)
    key_press = m.input("key_press", width=1)

    lhs = m.out("lhs", clk=clk, rst=rst, width=64, init=u(64, 0))
    rhs = m.out("rhs", clk=clk, rst=rst, width=64, init=u(64, 0))
    op = m.out("op", clk=clk, rst=rst, width=2, init=u(2, 0))
    in_rhs = m.out("in_rhs", clk=clk, rst=rst, width=1, init=u(1, 0))
    display = m.out("display_r", clk=clk, rst=rst, width=64, init=u(64, 0))

    digit = unsigned(key[0:4]) + u(64, 0)
    is_digit = key_press & (key <= u(5, 9))
    is_add = key_press & (key == u(5, KEY_ADD))
    is_sub = key_press & (key == u(5, KEY_SUB))
    is_mul = key_press & (key == u(5, KEY_MUL))
    is_div = key_press & (key == u(5, KEY_DIV))
    is_eq = key_press & (key == u(5, KEY_EQ))
    is_ac = key_press & (key == u(5, KEY_AC))

    lhs_n = lhs.out()
    rhs_n = rhs.out()
    op_n = op.out()
    in_rhs_n = in_rhs.out()
    disp_n = display.out()

    if is_digit:
        if in_rhs_n:
            rhs_n = rhs_n * u(64, 10) + digit
            disp_n = rhs_n
        else:
            lhs_n = lhs_n * u(64, 10) + digit
            disp_n = lhs_n

    if is_add | is_sub | is_mul | is_div:
        in_rhs_n = u(1, 1)
        rhs_n = u(64, 0)
        op_n = u(2, OP_ADD) if is_add else op_n
        op_n = u(2, OP_SUB) if is_sub else op_n
        op_n = u(2, OP_MUL) if is_mul else op_n
        op_n = u(2, OP_DIV) if is_div else op_n

    if is_eq:
        result = lhs_n
        if op_n == u(2, OP_ADD):
            result = lhs_n + rhs_n
        elif op_n == u(2, OP_SUB):
            result = lhs_n - rhs_n
        elif op_n == u(2, OP_MUL):
            result = lhs_n * rhs_n
        elif op_n == u(2, OP_DIV):
            result = lhs_n // (rhs_n if rhs_n != u(64, 0) else u(64, 1))
        lhs_n = result
        rhs_n = u(64, 0)
        in_rhs_n = u(1, 0)
        disp_n = result

    if is_ac:
        lhs_n = u(64, 0)
        rhs_n = u(64, 0)
        op_n = u(2, 0)
        in_rhs_n = u(1, 0)
        disp_n = u(64, 0)

    lhs.set(lhs_n)
    rhs.set(rhs_n)
    op.set(op_n)
    in_rhs.set(in_rhs_n)
    display.set(disp_n)

    m.output("display", display)
    m.output("op_pending", op)


if __name__ == "__main__":
    print(compile(build, name="calculator").emit_mlir())
