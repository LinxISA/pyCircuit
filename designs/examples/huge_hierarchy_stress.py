from __future__ import annotations

from pycircuit import Cache, Circuit, Connector, compile, ct, function, meta, module, const, u


@function
def _mix3(m: Circuit, a, b, c):
    _ = m
    w = a.width
    x = (a ^ b) + c
    return x[0:w]


@const
def _lane_pipe_spec(m: Circuit, *, width: int):
    _ = m
    payload = meta.bundle("lane_payload").field("data", width=width).build()
    return meta.stage_pipe("lane_pipe", payload=payload, has_valid=True, has_ready=False)


@const
def _top_in_struct(m: Circuit, *, width: int):
    _ = m
    return (
        meta.struct("top_in")
        .field("seed", width=width)
        .field("enable", width=1)
        .build()
        .drop_fields(["enable"])
    )


@const
def _top_out_struct(m: Circuit, *, width: int):
    _ = m
    base = meta.struct("top_out").field("value", width=width).field("hit", width=1).build()
    return base.rename_field("value", "out").drop_fields(["hit"]).add_field("cache_hit", width=1)


@const
def _stress_cfg(
    m: Circuit,
    *,
    module_count: int,
    hierarchy_depth: int,
    fanout: int,
    cache_ways: int,
    cache_sets: int,
):
    _ = m
    spec = (
        meta.params()
        .add("module_count", default=16, min_value=1)
        .add("hierarchy_depth", default=2, min_value=0)
        .add("fanout", default=2, min_value=1)
        .add("cache_ways", default=4, min_value=1)
        .add("cache_sets", default=64, min_value=1)
    )
    return spec.build(
        {
            "module_count": int(module_count),
            "hierarchy_depth": int(hierarchy_depth),
            "fanout": int(fanout),
            "cache_ways": int(cache_ways),
            "cache_sets": int(cache_sets),
        }
    )


@module(structural=True)
def _leaf(m: Circuit, clk, rst, x, *, width: int = 64):
    clk_v = clk.read() if isinstance(clk, Connector) else clk
    rst_v = rst.read() if isinstance(rst, Connector) else rst
    x_v = x.read() if isinstance(x, Connector) else x

    clk_c = m.as_connector(clk_v, name="clk")
    rst_c = m.as_connector(rst_v, name="rst")
    ps = _lane_pipe_spec(m, width=width)
    staged = m.pipe(
        ps,
        m.bundle_connector(
            data=m.as_connector(x_v, name="data"),
            valid=m.as_connector(u(1, 1), name="valid"),
        ),
        clk=clk_c,
        rst=rst_c,
        prefix="leaf_stg_",
    )

    acc = m.out("acc", clk=clk_v, rst=rst_v, width=width, init=u(width, 0))
    nxt = _mix3(m, acc.out(), staged["data"].read(), acc.out().lshr(amount=1))
    acc.set(nxt, when=staged["valid"].read())
    m.output("y", acc)


@module(structural=True)
def _node(
    m: Circuit,
    clk,
    rst,
    x,
    *,
    width: int = 64,
    fanout: int = 2,
    depth: int = 2,
):
    x_v = x.read() if isinstance(x, Connector) else x
    clk_c = m.as_connector(clk, name="clk")
    rst_c = m.as_connector(rst, name="rst")

    fan = max(1, int(fanout))
    children = []
    for i in range(fan):
        xin = (x_v + u(width, i + 1))[0:width]
        xin_c = m.as_connector(xin, name=f"x{i}")
        if depth <= 0:
            child = _leaf(m, clk=clk_c, rst=rst_c, x=xin_c, width=width)
        else:
            child = _node(m, clk=clk_c, rst=rst_c, x=xin_c, width=width, fanout=fanout, depth=depth - 1)
        children.append(child)

    y = x_v
    for i, c in enumerate(children):
        y = _mix3(m, y, c.read(), y.lshr(amount=(i % max(1, width // 8)) + 1))

    m.output("y", y)


@module
def build(
    m: Circuit,
    *,
    width: int = 64,
    module_count: int = 32,
    hierarchy_depth: int = 2,
    fanout: int = 2,
    cache_ways: int = 4,
    cache_sets: int = 64,
):
    clk = m.clock("clk")
    rst = m.reset("rst")
    clk_c = m.as_connector(clk, name="clk")
    rst_c = m.as_connector(rst, name="rst")

    in_spec = _top_in_struct(m, width=width)
    top_in = m.inputs(in_spec, prefix="")

    cfg = _stress_cfg(
        m,
        module_count=module_count,
        hierarchy_depth=hierarchy_depth,
        fanout=fanout,
        cache_ways=cache_ways,
        cache_sets=cache_sets,
    )

    nmods = int(cfg["module_count"])
    depth_cfg = int(cfg["hierarchy_depth"])
    fan_cfg = int(cfg["fanout"])
    ways_cfg = int(cfg["cache_ways"])
    sets_cfg = int(cfg["cache_sets"])

    family = meta.module_family("stress_node", module=_node, params={"width": int(width), "fanout": fan_cfg, "depth": depth_cfg})
    node_list = family.list(max(1, nmods), name="stress_nodes")

    per_instance: dict[str, dict[str, object]] = {}
    seed = top_in["seed"].read()
    for key in node_list.keys():
        idx = int(key)
        x_i = m.as_connector((seed + u(width, idx + 1))[0:width], name=f"x_{idx}")
        per_instance[str(key)] = {"x": x_i}

    nodes = m.array(
        node_list,
        name="stress_node",
        bind={"clk": clk_c, "rst": rst_c},
        per=per_instance,
    )

    cur = seed
    for i in range(len(node_list.keys())):
        yi = nodes.output(str(i))
        cur = _mix3(m, cur, yi.read(), cur.lshr(amount=(i % max(1, width // 8)) + 1))

    req_wmask_w = max(1, width // 8)
    cache_req_wmask = m.as_connector(u(req_wmask_w, ct.bitmask(req_wmask_w)), name="wmask")
    cache_req_write = m.as_connector(u(1, 0), name="req_write")
    cache_req_valid = m.as_connector(u(1, 1), name="req_valid")
    cache = Cache(
        m,
        clk=clk_c,
        rst=rst_c,
        req_valid=cache_req_valid,
        req_addr=m.as_connector(cur, name="req_addr"),
        req_write=cache_req_write,
        req_wdata=m.as_connector(cur, name="req_wdata"),
        req_wmask=cache_req_wmask,
        ways=ways_cfg,
        sets=sets_cfg,
        addr_width=width,
        data_width=width,
    )

    hit_mask = cache["resp_hit"].read()
    out_v = _mix3(m, cur, cache["resp_data"].read(), hit_mask)

    out_spec = _top_out_struct(m, width=width)
    m.outputs(out_spec, {"out": out_v, "cache_hit": cache["resp_hit"]}, prefix="")


if __name__ == "__main__":
    print(
        compile(
            build,
            name="huge_hierarchy_stress",
            width=64,
            module_count=16,
            hierarchy_depth=2,
            fanout=2,
            cache_ways=4,
            cache_sets=64,
        ).emit_mlir()
    )
