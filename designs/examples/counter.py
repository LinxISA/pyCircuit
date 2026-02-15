from __future__ import annotations

from pycircuit import Circuit, compile_design, u


def build(m: Circuit, width: int = 8) -> None:
    clk = m.clock("clk")
    rst = m.reset("rst")
    en = m.input("enable", width=1)

    count = m.out("count_q", clk=clk, rst=rst, width=width, init=u(width, 0))
    count.set(count.out() + 1, when=en)
    m.output("count", count)


if __name__ == "__main__":
    print(compile_design(build, name="counter", width=8).emit_mlir())
