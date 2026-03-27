from __future__ import annotations

from pycircuit import Circuit, ProbeBuilder, ProbeView, compile_cycle_aware, CycleAwareCircuit, CycleAwareDomain, const, module, probe, spec


@const
def _probe_struct(m: Circuit):
    _ = m
    return (
        spec.struct("probe_struct")
        .field("a", width=8)
        .field("b.c", width=1)
        .build()
    )


def build(m: CycleAwareCircuit, domain: CycleAwareDomain) -> None:
    _clk = m.clock("clk")
    _rst = m.reset("rst")

    s = _probe_struct(m)
    inp = m.inputs(s, prefix="in_")

build.__pycircuit_name__ = "bundle_probe_expand"
build.__pycircuit_kind__ = "module"


@probe(target=build, name="pv")
def bundle_probe(p: ProbeBuilder, dut: ProbeView) -> None:
    p.emit(
        "in",
        {
            "a": dut.read("in_a"),
            "b": {"c": dut.read("in_b_c")},
        },
        at="tick",
        tags={"demo": "bundle", "family": "pv", "stage": "ex", "lane": 0},
    )


if __name__ == "__main__":
    print(compile_cycle_aware(build, name="bundle_probe_expand", eager=True).emit_mlir())
