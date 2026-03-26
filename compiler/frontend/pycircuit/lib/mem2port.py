from __future__ import annotations

from ..connectors import Connector, ConnectorBundle, ConnectorError
from ..design import module
from ..dsl import Signal
from ..hw import Circuit


@module(structural=True)
def Mem2Port(
    m: Circuit,
    clk: Connector,
    rst: Connector,
    ren0: Connector,
    raddr0: Connector,
    ren1: Connector,
    raddr1: Connector,
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
        raise ConnectorError("Mem2Port.clk must be !pyc.clock")
    if not isinstance(rst_v, Signal) or rst_v.ty != "!pyc.reset":
        raise ConnectorError("Mem2Port.rst must be !pyc.reset")

    def wire_of(v):
        vv = v.read() if isinstance(v, Connector) else v
        if isinstance(vv, Signal):
            return m.wire(vv)
        return vv

    ren0_w = wire_of(ren0)
    ren1_w = wire_of(ren1)
    wvalid_w = wire_of(wvalid)
    raddr0_w = wire_of(raddr0)
    raddr1_w = wire_of(raddr1)
    waddr_w = wire_of(waddr)
    wdata_w = wire_of(wdata)
    wstrb_w = wire_of(wstrb)
    if ren0_w.ty != "i1" or ren1_w.ty != "i1" or wvalid_w.ty != "i1":
        raise ConnectorError("Mem2Port ren0/ren1/wvalid must be i1")

    rdata0, rdata1 = m.sync_mem_dp(
        clk_v,
        rst_v,
        ren0=ren0_w,
        raddr0=raddr0_w,
        ren1=ren1_w,
        raddr1=raddr1_w,
        wvalid=wvalid_w,
        waddr=waddr_w,
        wdata=wdata_w,
        wstrb=wstrb_w,
        depth=int(depth),
        name="mem",
    )

    return m.bundle_connector(
        rdata0=rdata0,
        rdata1=rdata1,
    )
