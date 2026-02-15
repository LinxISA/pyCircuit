from __future__ import annotations

from pycircuit import Circuit, component, compile_design


@component
class Incrementer:
    width: int = 8

    def build(self, m: Circuit, x):
        return (x + 1)[0 : self.width]


def build(m: Circuit, width: int = 8, stages: int = 3) -> None:
    x = m.input("x", width=width)
    v = x
    for _ in range(stages):
        v = Incrementer(width=width)(m, x=v)
    m.output("y", v)


if __name__ == "__main__":
    print(compile_design(build, name="hier_modules", width=8, stages=3).emit_mlir())
