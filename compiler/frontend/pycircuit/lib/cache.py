from __future__ import annotations

from ..connectors import Connector, ConnectorBundle
from ..design import module
from ..dsl import Signal
from ..hw import Circuit
from ..literals import u


@module(structural=True)
def Cache(
    m: Circuit,
    clk: Connector,
    rst: Connector,
    req_valid: Connector,
    req_addr: Connector,
    req_write: Connector,
    req_wdata: Connector,
    req_wmask: Connector,
    *,
    ways: int = 4,
    sets: int = 64,
    line_bytes: int = 64,
    addr_width: int = 64,
    data_width: int = 64,
    write_back: bool = True,
    write_allocate: bool = True,
    replacement: str = "plru",
) -> ConnectorBundle:
    """Structural cache baseline.

    Default policy contract:
    - write_back=True
    - write_allocate=True
    - replacement="plru"

    This pyc4.0 baseline is intentionally compact and hierarchy-preserving; it keeps
    state visible to the compiler flow without flattening into primitive wires.
    """

    _ = (line_bytes, write_back, write_allocate, replacement)
    clk_v = clk.read() if isinstance(clk, Connector) else clk
    rst_v = rst.read() if isinstance(rst, Connector) else rst

    req_valid_v = req_valid.read() if isinstance(req_valid, Connector) else req_valid
    req_addr_v = req_addr.read() if isinstance(req_addr, Connector) else req_addr
    req_write_v = req_write.read() if isinstance(req_write, Connector) else req_write
    req_wdata_v = req_wdata.read() if isinstance(req_wdata, Connector) else req_wdata
    req_wmask_v = req_wmask.read() if isinstance(req_wmask, Connector) else req_wmask
    req_valid_w = m.wire(req_valid_v) if isinstance(req_valid_v, Signal) else req_valid_v
    req_addr_w = m.wire(req_addr_v) if isinstance(req_addr_v, Signal) else req_addr_v
    req_write_w = m.wire(req_write_v) if isinstance(req_write_v, Signal) else req_write_v
    req_wdata_w = m.wire(req_wdata_v) if isinstance(req_wdata_v, Signal) else req_wdata_v
    _req_wmask_w = m.wire(req_wmask_v) if isinstance(req_wmask_v, Signal) else req_wmask_v
    ways_i = max(1, int(ways))
    sets_i = max(1, int(sets))
    set_bits = max(1, (sets_i - 1).bit_length())
    tag_bits = max(1, int(addr_width) - set_bits)
    plru_bits = max(1, ways_i - 1)
    way_idx_bits = max(1, (ways_i - 1).bit_length())

    tags = [m.out(f"cache_tag_{i}", clk=clk_v, rst=rst_v, width=tag_bits, init=0) for i in range(ways_i)]
    valids = [m.out(f"cache_valid_{i}", clk=clk_v, rst=rst_v, width=1, init=0) for i in range(ways_i)]
    dirty = [m.out(f"cache_dirty_{i}", clk=clk_v, rst=rst_v, width=1, init=0) for i in range(ways_i)]
    data = [m.out(f"cache_data_{i}", clk=clk_v, rst=rst_v, width=int(data_width), init=0) for i in range(ways_i)]
    plru = m.out("cache_plru", clk=clk_v, rst=rst_v, width=plru_bits, init=0)

    req_tag = req_addr_w[set_bits : set_bits + tag_bits]

    hit = u(1, 0)
    hit_data = u(int(data_width), 0)
    hit_way = u(way_idx_bits, 0)

    for i in range(ways_i):
        way_hit = valids[i].out() & (tags[i].out() == req_tag)
        hit_data = data[i].out() if way_hit else hit_data
        hit_way = i if way_hit else hit_way
        hit = hit | way_hit

    victim_way = plru.out()[0:way_idx_bits]

    do_alloc = req_valid_w & (~hit)
    do_write_hit = req_valid_w & req_write_w & hit
    do_write_alloc = req_valid_w & req_write_w & do_alloc

    for i in range(ways_i):
        sel_hit = hit & (hit_way == i)
        sel_victim = do_alloc & (victim_way == i)

        tags[i].set(req_tag, when=sel_victim)
        valids[i].set(1, when=sel_victim)

        data[i].set(req_wdata_w, when=sel_hit & req_write_w)
        data[i].set(req_wdata_w, when=sel_victim & req_write_w)

        dirty[i].set(1, when=sel_hit & req_write_w)
        dirty[i].set(do_write_alloc, when=sel_victim)

    plru.set(plru.out() + 1, when=req_valid_w)

    resp_valid = req_valid_w
    resp_ready = req_valid_w
    resp_hit = hit
    resp_data = hit_data if hit else u(int(data_width), 0)
    miss = req_valid_w & (~hit)

    return m.bundle_connector(
        resp_valid=resp_valid,
        resp_ready=resp_ready,
        resp_hit=resp_hit,
        resp_data=resp_data,
        miss=miss,
    )
