from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    ProbeBuilder,
    ProbeView,
    cas,
    probe,
    wire_of,
)


def build(m: CycleAwareCircuit, domain: CycleAwareDomain, width: int = 8) -> None:
    en = cas(domain, m.input("en", width=1), cycle=0)

    q = domain.state(width=width, reset_value=0, name="q")
    m.output("y", wire_of(q))

    domain.next()
    q.set(q + 1, when=en)


build.__pycircuit_name__ = "reset_invalidate_order_smoke"
build.__pycircuit_kind__ = "module"


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
    pass
