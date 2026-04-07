from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    compile_cycle_aware,
    mux,
    wire_of,
)


def build(m: CycleAwareCircuit, domain: CycleAwareDomain, width: int = 8) -> None:
    enable = cas(domain, m.input("enable", width=1), cycle=0)
    count = domain.signal(width=width, reset_value=0, name="count")

    m.output("count", wire_of(count))

    domain.next()
    count.assign(count + 1, when=enable)


build.__pycircuit_name__ = "counter"


if __name__ == "__main__":
    print(compile_cycle_aware(build, name="counter", eager=True, width=8).emit_mlir())
