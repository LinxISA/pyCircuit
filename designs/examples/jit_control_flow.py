from __future__ import annotations

from pycircuit import Circuit, compile, module, u


@module
def build(m: Circuit, rounds: int = 4) -> None:
    a = m.input("a", width=8)
    b = m.input("b", width=8)
    op = m.input("op", width=2)

    acc = a + u(8, 0)
    if op == u(2, 0):
        acc = a + b
    elif op == u(2, 1):
        acc = a - b
    elif op == u(2, 2):
        acc = a ^ b
    else:
        acc = a & b

    for _ in range(rounds):
        acc = acc + 1

    m.output("result", acc)


if __name__ == "__main__":
    print(compile(build, name="jit_control_flow", rounds=4).emit_mlir())
