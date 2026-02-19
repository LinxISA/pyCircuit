from __future__ import annotations

from pycircuit import Circuit, compile, module


@module
def build(m: Circuit, depth: int = 2) -> None:
    clk = m.clock("clk")
    rst = m.reset("rst")

    in_valid = m.input("in_valid", width=1)
    in_data = m.input("in_data", width=8)
    out_ready = m.input("out_ready", width=1)

    q = m.queue("q", clk=clk, rst=rst, width=8, depth=depth)
    q.push(in_data, when=in_valid)
    p = q.pop(when=out_ready)

    m.output("in_ready", q.in_ready)
    m.output("out_valid", p.valid)
    m.output("out_data", p.data)


if __name__ == "__main__":
    print(compile(build, name="fifo_loopback", depth=2).emit_mlir())
