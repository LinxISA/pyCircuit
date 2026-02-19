from __future__ import annotations

from pycircuit import Circuit, compile_design, meta, module, template


@template
def _pair_spec(m: Circuit, *, width: int):
    _ = m
    return meta.bundle("pair").field("a", width=width).field("b", width=width).build()


@module
def pair_add(m: Circuit, *, width: int = 16):
    spec = _pair_spec(m, width=width)
    ins = m.io_in(spec, prefix="in_")
    a = ins["a"].read()
    b = ins["b"].read()
    m.io_out(spec, {"a": a, "b": (a + b)[0:width]}, prefix="out_")


@module
def build(m: Circuit, *, width: int = 16):
    in_spec = _pair_spec(m, width=width)
    top_in = m.io_in(in_spec, prefix="top_in_")
    h = m.instance_bind(
        pair_add,
        name="pair_add0",
        params={"width": int(width)},
        spec_bindings={"in": meta.bind(in_spec, top_in)},
    )

    out_spec = _pair_spec(m, width=width)
    m.io_out(
        out_spec,
        {
            "a": h.outputs["out_a"],
            "b": h.outputs["out_b"],
        },
        prefix="top_out_",
    )


build.__pycircuit_name__ = "template_interface_wiring_demo"


if __name__ == "__main__":
    print(compile_design(build, name="template_interface_wiring_demo", width=16).emit_mlir())
