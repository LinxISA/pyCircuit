from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    wire_of,
)


def build(m: CycleAwareCircuit, domain: CycleAwareDomain, rounds: int = 4) -> None:
    a = cas(domain, m.input("a", width=8), cycle=0)
    b = cas(domain, m.input("b", width=8), cycle=0)
    op = cas(domain, m.input("op", width=2), cycle=0)

    acc = (
        (a + b)
        if (op == 0)
        else ((a - b) if (op == 1) else ((a ^ b) if (op == 2) else (a & b)))
    )

    for _ in range(rounds):
        acc = acc + 1

    m.output("result", wire_of(acc))


build.__pycircuit_name__ = "jit_control_flow"


if __name__ == "__main__":
    pass
