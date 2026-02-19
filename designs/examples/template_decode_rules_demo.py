from __future__ import annotations

from pycircuit import Circuit, compile, meta, module, const, u


@const
def _decode_rules(m: Circuit):
    _ = m
    return (
        meta.ruleset()
        .rule(name="add", mask=0xF0, match=0x10, updates={"op": 1, "len": 4}, priority=10)
        .rule(name="sub", mask=0xF0, match=0x20, updates={"op": 2, "len": 4}, priority=9)
        .rule(name="xor", mask=0xF0, match=0x30, updates={"op": 3, "len": 4}, priority=8)
        .build()
    )


@module
def build(m: Circuit):
    insn = m.input("insn", width=8)
    op = u(4, 0)
    ln = u(3, 0)

    for r in _decode_rules(m):
        hit = (insn & u(8, int(r.mask))) == u(8, int(r.match))
        op = u(4, int(dict(r.updates)["op"])) if hit else op
        ln = u(3, int(dict(r.updates)["len"])) if hit else ln

    m.output("op", op)
    m.output("len", ln)


build.__pycircuit_name__ = "template_decode_rules_demo"


if __name__ == "__main__":
    print(compile(build, name="template_decode_rules_demo").emit_mlir())
