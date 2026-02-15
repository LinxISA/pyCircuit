from __future__ import annotations

from pycircuit import Circuit, compile_design, u


def build(m: Circuit) -> None:
    clk = m.clock("clk")
    rst = m.reset("rst")

    a = m.input("a", width=8)
    b = m.input("b", width=8)
    sel = m.input("sel", width=1)

    y = a & b if sel else a ^ b
    y_q = m.out("y_q", clk=clk, rst=rst, width=8, init=u(8, 0))
    y_q.set(y)

    m.output("y", y_q)


if __name__ == "__main__":
    print(compile_design(build, name="wire_ops").emit_mlir())
