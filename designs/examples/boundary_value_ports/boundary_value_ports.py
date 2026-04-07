from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    wire_of,
)


def _lane(domain, m, x, gain, bias, enable, *, width: int = 32):
    _ = domain, m
    y_add = (x + gain + bias)[0:width]
    return y_add if enable else x


def _sum3(a, b, c, *, width: int):
    return (a + b + c)[0:width]


def build(m: CycleAwareCircuit, domain: CycleAwareDomain, *, width: int = 32):
    seed = cas(domain, m.input("seed", width=width), cycle=0)

    g0 = 1
    b0 = 5
    e0 = 1
    lane0 = _lane(domain, m, seed, g0, b0, e0, width=width)

    g1 = 3
    b1 = 9
    e1 = 1
    lane1 = _lane(domain, m, seed, g1, b1, e1, width=width)

    g2 = 7
    b2 = 11
    e2 = 0
    lane2 = _lane(domain, m, seed, g2, b2, e2, width=width)

    acc = _sum3(lane0, lane1, lane2, width=width)
    m.output("acc", wire_of(acc))


build.__pycircuit_name__ = "boundary_value_ports"

if __name__ == "__main__":
    pass
