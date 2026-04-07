from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    wire_of,
)


def build(m: CycleAwareCircuit, domain: CycleAwareDomain, width: int = 8) -> None:
    enable = cas(domain, m.input("enable", width=1), cycle=0)
    count = domain.state(width=width, reset_value=0, name="count")

    m.output("count", wire_of(count))

    domain.next()
    count.set(count + 1, when=enable)


build.__pycircuit_name__ = "counter"


if __name__ == "__main__":
    pass
