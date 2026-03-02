from __future__ import annotations

from pycircuit import Circuit, compile, const, module, spec


@const
def _probe_struct(m: Circuit):
    _ = m
    return (
        spec.struct("probe_struct")
        .field("a", width=8)
        .field("b.c", width=1)
        .build()
    )


@module
def build(m: Circuit) -> None:
    _clk = m.clock("clk")
    _rst = m.reset("rst")

    s = _probe_struct(m)
    inp = m.inputs(s, prefix="in_")

    # Decision 0138: one-shot probe expansion for Bundle/Struct values.
    m.probe(inp, stage="ex", lane=0, family="pv", prefix="in", at="tick", tags={"demo": "bundle"})


build.__pycircuit_name__ = "bundle_probe_expand"


if __name__ == "__main__":
    print(compile(build, name="bundle_probe_expand").emit_mlir())
