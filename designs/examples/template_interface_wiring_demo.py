from __future__ import annotations

from pycircuit import Circuit, compile, meta, module, const


@const
def _pair_spec(m: Circuit, *, width: int):
    _ = m
    base = meta.struct("pair").field("left", width=width).field("right", width=width).field("drop", width=1).build()
    return base.remove_field("drop").rename_field("right", "rhs").select_fields(["left", "rhs"])


@module
def pair_add(m: Circuit, *, width: int = 16):
    spec = _pair_spec(m, width=width)
    ins = m.inputs(spec, prefix="in_")
    a = ins["left"].read()
    b = ins["rhs"].read()
    m.outputs(spec, {"left": a, "rhs": (a + b)[0:width]}, prefix="out_")


@module
def build(m: Circuit, *, width: int = 16):
    in_spec = _pair_spec(m, width=width)
    top_in = m.inputs(in_spec, prefix="top_in_")
    h = m.new(
        pair_add,
        name="pair_add0",
        params={"width": int(width)},
        bind={"in": meta.bind(in_spec, top_in)},
    )

    m.outputs(
        in_spec,
        {
            "left": h.outputs["out_left"],
            "rhs": h.outputs["out_rhs"],
        },
        prefix="top_out_",
    )


build.__pycircuit_name__ = "template_interface_wiring_demo"


if __name__ == "__main__":
    print(compile(build, name="template_interface_wiring_demo", width=16).emit_mlir())
