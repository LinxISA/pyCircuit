from __future__ import annotations

from pycircuit import Circuit, ProbeBuilder, ProbeView, compile, module, probe


@module
def build(m: Circuit, width: int = 8) -> None:
    clk = m.clock("clk")
    rst = m.reset("rst")
    en = m.input("en", width=1)

    q = m.out("q", clk=clk, rst=rst, width=width, init=0)
    q.set(q.out() + 1, when=en)
    m.output("y", q)


build.__pycircuit_name__ = "reset_invalidate_order_smoke"


@probe(target=build, name="reset")
def reset_probe(p: ProbeBuilder, dut: ProbeView, width: int = 8) -> None:
    _ = width
    p.emit(
        "q",
        dut.read("q"),
        at="tick",
        tags={"family": "reset", "stage": "order", "lane": 0},
    )


if __name__ == "__main__":
    print(compile(build, name="reset_invalidate_order_smoke", width=8).emit_mlir())
