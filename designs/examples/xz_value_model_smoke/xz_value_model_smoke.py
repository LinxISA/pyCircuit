from __future__ import annotations

from pycircuit import Circuit, compile, module


@module
def build(m: Circuit, width: int = 8) -> None:
    clk = m.clock("clk")
    rst = m.reset("rst")
    in_a = m.input("in_a", width=width)

    q = m.out("q", clk=clk, rst=rst, width=width, init=0)
    q.set(in_a)

    m.probe({"q": q}, family="value", stage="smoke", lane=0, at="tick")
    m.output("y", q)


build.__pycircuit_name__ = "xz_value_model_smoke"


if __name__ == "__main__":
    print(compile(build, name="xz_value_model_smoke", width=8).emit_mlir())
