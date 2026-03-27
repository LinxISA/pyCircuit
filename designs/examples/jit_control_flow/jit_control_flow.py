from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    compile_cycle_aware,
    mux,
    u,
)


def build(m: CycleAwareCircuit, domain: CycleAwareDomain, rounds: int = 4) -> None:
    a = cas(domain, m.input("a", width=8), cycle=0)
    b = cas(domain, m.input("b", width=8), cycle=0)
    op = cas(domain, m.input("op", width=2), cycle=0)

    op0 = cas(domain, m.const(0, width=2), cycle=0)
    op1 = cas(domain, m.const(1, width=2), cycle=0)
    op2 = cas(domain, m.const(2, width=2), cycle=0)

    acc = mux(op == op0, a + b,
          mux(op == op1, a - b,
          mux(op == op2, a ^ b,
                         a & b)))

    for _ in range(rounds):
        acc = acc + 1

    m.output("result", acc.wire)


build.__pycircuit_name__ = "jit_control_flow"


if __name__ == "__main__":
    print(compile_cycle_aware(build, name="jit_control_flow", eager=True, rounds=4).emit_mlir())
