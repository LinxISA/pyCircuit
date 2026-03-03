from __future__ import annotations

from pycircuit import Circuit, compile, module


@module
def leaf(m: Circuit) -> None:
    clk = m.clock("clk")
    rst = m.reset("rst")

    x = m.input("in_x", width=8)
    r = m.out("r", clk=clk, rst=rst, width=8, init=0)
    r.set(x)

    m.output("out_y", r)

    # Decision 0140: probes must declare observation point + carry tags.
    m.probe({"q": r}, stage="leaf", lane=0, family="pv", at="tick")


@module
def build(m: Circuit) -> None:
    clk = m.clock("clk")
    rst = m.reset("rst")
    x = m.input("in_x", width=8)

    u0 = m.new(leaf, name="unit0_long_name", short_name="u0", bind={"clk": clk, "rst": rst, "in_x": x})
    u1 = m.new(leaf, name="unit1_long_name", short_name="u1", bind={"clk": clk, "rst": rst, "in_x": x})

    m.output("y0", u0.outputs["out_y"])
    m.output("y1", u1.outputs["out_y"])


build.__pycircuit_name__ = "trace_dsl_smoke"


if __name__ == "__main__":
    print(compile(build, name="trace_dsl_smoke").emit_mlir())
