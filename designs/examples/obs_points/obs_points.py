from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    compile_cycle_aware,
)


def build(m: CycleAwareCircuit, domain: CycleAwareDomain, width: int = 8) -> None:
    x = cas(domain, m.input("x", width=width), cycle=0)
    y = x + 1

    q = domain.state(width=width, reset_value=0, name="q")

    m.output("y", y.wire)
    m.output("q", q.wire)

    domain.next()
    q.set(y)


build.__pycircuit_name__ = "obs_points"


if __name__ == "__main__":
    print(compile_cycle_aware(build, name="obs_points", eager=True, width=8).emit_mlir())
