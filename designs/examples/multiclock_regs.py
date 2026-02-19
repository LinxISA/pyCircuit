from __future__ import annotations

from pycircuit import Circuit, compile, module, u


@module
def build(m: Circuit) -> None:
    clk_a = m.clock("clk_a")
    rst_a = m.reset("rst_a")
    clk_b = m.clock("clk_b")
    rst_b = m.reset("rst_b")

    a = m.out("a_q", clk=clk_a, rst=rst_a, width=8, init=u(8, 0))
    b = m.out("b_q", clk=clk_b, rst=rst_b, width=8, init=u(8, 0))

    a.set(a.out() + 1)
    b.set(b.out() + 1)

    m.output("a_count", a)
    m.output("b_count", b)


if __name__ == "__main__":
    print(compile(build, name="multiclock_regs").emit_mlir())
