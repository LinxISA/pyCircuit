from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    u,
    wire_of,
)

RULES = [
    {"mask": 0xF0, "match": 0x10, "op": 1, "len": 4},
    {"mask": 0xF0, "match": 0x20, "op": 2, "len": 4},
    {"mask": 0xF0, "match": 0x30, "op": 3, "len": 4},
]


def build(m: CycleAwareCircuit, domain: CycleAwareDomain) -> None:
    insn = cas(domain, m.input("insn", width=8), cycle=0)
    insn_w = wire_of(insn)
    op = u(4, 0)
    ln = u(3, 0)

    for r in RULES:
        mask = u(8, r["mask"])
        match = u(8, r["match"])
        hit = (insn_w & mask) == match
        op = u(4, r["op"]) if hit else op
        ln = u(3, r["len"]) if hit else ln

    m.output("op", wire_of(op))
    m.output("len", wire_of(ln))


build.__pycircuit_name__ = "decode_rules"

if __name__ == "__main__":
    pass
