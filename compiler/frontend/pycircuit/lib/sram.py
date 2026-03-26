from __future__ import annotations

from ..connectors import Connector, ConnectorBundle, ConnectorError
from ..design import module
from ..dsl import Signal
from ..hw import Circuit


@module(structural=True)
def SRAM(
    m: Circuit,
    clk: Connector,
    rst: Connector,
    ren: Connector,
    raddr: Connector,
    wvalid: Connector,
    waddr: Connector,
    wdata: Connector,
    wstrb: Connector,
    *,
    depth: int,
) -> ConnectorBundle:
    clk_v = clk.read() if isinstance(clk, Connector) else clk
    rst_v = rst.read() if isinstance(rst, Connector) else rst
    if not isinstance(clk_v, Signal) or clk_v.ty != "!pyc.clock":
        raise ConnectorError("SRAM.clk must be !pyc.clock")
    if not isinstance(rst_v, Signal) or rst_v.ty != "!pyc.reset":
        raise ConnectorError("SRAM.rst must be !pyc.reset")

    def wire_of(v):
        vv = v.read() if isinstance(v, Connector) else v
        if isinstance(vv, Signal):
            return m.wire(vv)
        return vv

    ren_w = wire_of(ren)
    wvalid_w = wire_of(wvalid)
    raddr_w = wire_of(raddr)
    waddr_w = wire_of(waddr)
    wdata_w = wire_of(wdata)
    wstrb_w = wire_of(wstrb)
    if ren_w.ty != "i1" or wvalid_w.ty != "i1":
        raise ConnectorError("SRAM ren/wvalid must be i1")

    rdata = m.sync_mem(
        clk_v,
        rst_v,
        ren=ren_w,
        raddr=raddr_w,
        wvalid=wvalid_w,
        waddr=waddr_w,
        wdata=wdata_w,
        wstrb=wstrb_w,
        depth=int(depth),
        name="mem",
    )

    return m.bundle_connector(
        rdata=rdata,
    )
