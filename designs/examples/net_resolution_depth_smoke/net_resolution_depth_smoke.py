from __future__ import annotations

from pycircuit import Circuit, compile, module


@module
def build(m: Circuit, width: int = 8) -> None:
    clk = m.clock("clk")
    rst = m.reset("rst")
    in_x = m.input("in_x", width=width)

    d0 = in_x + 1
    d1 = d0 + 1
    d2 = d1 + 1
    d3 = d2 + 1

    q = m.out("q", clk=clk, rst=rst, width=width, init=0)
    q.set(d3)
    m.output("y", q)


build.__pycircuit_name__ = "net_resolution_depth_smoke"


if __name__ == "__main__":
    print(compile(build, name="net_resolution_depth_smoke", width=8).emit_mlir())
