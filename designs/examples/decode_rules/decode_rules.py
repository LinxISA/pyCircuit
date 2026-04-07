from __future__ import annotations

from pycircuit import CycleAwareCircuit, CycleAwareDomain, cas, compile_cycle_aware, mux, wire_of

RULES = [
    {"mask": 0xF0, "match": 0x10, "op": 1, "len": 4},
    {"mask": 0xF0, "match": 0x20, "op": 2, "len": 4},
    {"mask": 0xF0, "match": 0x30, "op": 3, "len": 4},
]


def build(m: CycleAwareCircuit, domain: CycleAwareDomain) -> None:
    insn = cas(domain, m.input("insn", width=8), cycle=0)
    insn_w = wire_of(insn)
    op = cas(domain, m.const(0, width=4), cycle=0)
    ln = cas(domain, m.const(0, width=3), cycle=0)

    for r in RULES:
        mask = cas(domain, m.const(r["mask"], width=8), cycle=0)
        match = cas(domain, m.const(r["match"], width=8), cycle=0)
        hit = (insn_w & wire_of(mask)) == wire_of(match)
        op = mux(hit, cas(domain, m.const(r["op"], width=4), cycle=0), op)
        ln = mux(hit, cas(domain, m.const(r["len"], width=3), cycle=0), ln)

    m.output("op", wire_of(op))
    m.output("len", wire_of(ln))


build.__pycircuit_name__ = "decode_rules"

if __name__ == "__main__":
    print(compile_cycle_aware(build, name="decode_rules").emit_mlir())
