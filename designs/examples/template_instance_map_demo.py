from __future__ import annotations

from pycircuit import Circuit, compile, meta, module, const, u


@const
def _unit_in_spec(m: Circuit, *, width: int):
    _ = m
    return meta.struct("unit_in").field("x", width=width).build()


@const
def _unit_out_spec(m: Circuit, *, width: int):
    _ = m
    return meta.struct("unit_out").field("y", width=width).field("valid", width=1).build()


@module(structural=True)
def _unit(m: Circuit, *, width: int = 32, gain: int = 1):
    in_spec = _unit_in_spec(m, width=width)
    out_spec = _unit_out_spec(m, width=width)

    ins = m.inputs(in_spec, prefix="in_")
    x = ins["x"].read()
    y = (x + u(width, int(gain)))[0:width]

    m.outputs(out_spec, {"y": y, "valid": u(1, 1)}, prefix="out_")


@const
def _top_struct(m: Circuit, *, width: int):
    _ = m
    s = meta.struct("top_units").field("alu", width=width).field("bru", width=width).build()
    return s.add_field("lsu", width=width).rename_field("bru", "branch").select_fields(["alu", "branch", "lsu"])


@module
def build(m: Circuit, *, width: int = 32):
    top_spec = _top_struct(m, width=width)
    top_in = m.inputs(top_spec, prefix="in_")

    family = meta.module_family("unit_family", module=_unit, params={"width": int(width)})
    dict_spec = family.dict(
        {
            "alu": {"gain": 1},
            "branch": {"gain": 2},
            "lsu": {"gain": 3},
        },
        name="unit_dict",
    )

    unit_in = _unit_in_spec(m, width=width)
    per_unit: dict[str, dict[str, object]] = {}
    for key in dict_spec.keys():
        per_unit[key] = {
            "in": meta.bind(unit_in, {"x": top_in[key]}),
        }

    insts = m.array(
        dict_spec,
        name="unit",
        bind={},
        per=per_unit,
    )

    acc = u(width, 0)
    for key in dict_spec.keys():
        out = insts.output(key)
        y = out["out_y"].read()
        m.output(f"{key}_y", y)
        acc = (acc + y)[0:width]

    m.output("acc", acc)


build.__pycircuit_name__ = "template_instance_map_demo"


if __name__ == "__main__":
    print(compile(build, name="template_instance_map_demo", width=32).emit_mlir())
