from __future__ import annotations

from pycircuit import Circuit, ProbeBuilder, ProbeView, compile, module, probe


@module
def build(m: Circuit, width: int = 8) -> None:
    clk = m.clock("clk")
    rst = m.reset("rst")
    in_a = m.input("in_a", width=width)

    q = m.out("q", clk=clk, rst=rst, width=width, init=0)
    q.set(in_a)

    m.output("y", q)


build.__pycircuit_name__ = "xz_value_model_smoke"


@probe(target=build, name="value")
def value_probe(p: ProbeBuilder, dut: ProbeView, width: int = 8) -> None:
    _ = width
    p.emit(
        "q",
        dut.read("q"),
        at="tick",
        tags={"family": "value", "stage": "smoke", "lane": 0},
    )


if __name__ == "__main__":
    print(compile(build, name="xz_value_model_smoke", width=8).emit_mlir())
