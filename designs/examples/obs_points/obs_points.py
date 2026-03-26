from __future__ import annotations

from pycircuit import Circuit, compile, module, u


@module
def build(m: Circuit, width: int = 8) -> None:
    clk = m.clock("clk")
    rst = m.reset("rst")

    x = m.input("x", width=width)
    y = x + 1

    # Sample-and-hold: capture combinational `y` into state `q` each cycle.
    q = m.out("q_q", clk=clk, rst=rst, width=width, init=u(width, 0))
    q.set(y)

    m.output("y", y)
    m.output("q", q)


build.__pycircuit_name__ = "obs_points"


if __name__ == "__main__":
    print(compile(build, name="obs_points", width=8).emit_mlir())

