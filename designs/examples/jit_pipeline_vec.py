from __future__ import annotations

from pycircuit import Circuit, compile, module, u


@module
def build(m: Circuit, stages: int = 3) -> None:
    clk = m.clock("clk")
    rst = m.reset("rst")

    a = m.input("a", width=16)
    b = m.input("b", width=16)
    sel = m.input("sel", width=1)

    tag = a == b
    data = a + b if sel else a ^ b

    for i in range(stages):
        tag_q = m.out(f"tag_s{i}", clk=clk, rst=rst, width=1, init=u(1, 0))
        data_q = m.out(f"data_s{i}", clk=clk, rst=rst, width=16, init=u(16, 0))
        tag_q.set(tag)
        data_q.set(data)
        tag = tag_q.out()
        data = data_q.out()

    m.output("tag", tag)
    m.output("data", data)
    m.output("lo8", data[0:8])


if __name__ == "__main__":
    print(compile(build, name="jit_pipeline_vec", stages=3).emit_mlir())
