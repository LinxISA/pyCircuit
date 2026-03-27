from __future__ import annotations

from pycircuit import Circuit, compile_cycle_aware, CycleAwareCircuit, CycleAwareDomain, const, module, spec, u, wiring


@const
def _lane_in_spec(m: Circuit, *, width: int):
    _ = m
    return spec.struct("lane_in").field("payload.data", width=width).field("meta.bias", width=width).build()


@const
def _lane_out_spec(m: Circuit, *, width: int):
    _ = m
    return _lane_in_spec(m, width=width).rename_field("payload.data", "sum").add_field("meta.idx", width=8)


def _lane(m: Circuit, *, width: int = 32):
    in_spec = _lane_in_spec(m, width=width)
    out_spec = _lane_out_spec(m, width=width)

    ins = m.inputs(in_spec, prefix="in_")
    data = ins["payload.data"].read()
    bias = ins["meta.bias"].read()
    y = (data + bias)[0:width]

    m.outputs(
        out_spec,
        {
            "payload.sum": y,
            "meta.bias": bias,
            "meta.idx": bias[0:8],
        },
        prefix="out_",
    )


def build(m: CycleAwareCircuit, domain: CycleAwareDomain, *, width: int = 32, lanes: int = 8):
    seed = m.input("seed", width=width)

    in_spec = _lane_in_spec(m, width=width)
    family = spec.module_family("lane_family", module=_lane, params={"width": int(width)})
    lane_vec = family.vector(max(1, int(lanes)), name="lane_vec")

    per_lane: dict[str, dict[str, object]] = {}
    for key in lane_vec.keys():
        i = int(key)
        lane_in = {
            "payload.data": (seed + u(width, i))[0:width],
            "meta.bias": u(width, i + 1),
        }
        per_lane[key] = {"in": wiring.bind(in_spec, lane_in)}

    insts = m.array(
        lane_vec,
        name="lane",
        bind={},
        per=per_lane,
    )

    acc = seed
    for key in lane_vec.keys():
        out = insts.output(key)
        lane_sum = out["out_payload_sum"].read()
        lane_idx = out["out_meta_idx"].read()
        acc = (acc + lane_sum + lane_idx)[0:width]

    m.output("acc", acc)


build.__pycircuit_name__ = "module_collection"
if __name__ == "__main__":
    print(compile_cycle_aware(build, name="module_collection", width=32, lanes=8).emit_mlir())
