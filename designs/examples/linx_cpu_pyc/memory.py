from __future__ import annotations

from pycircuit import Circuit, Reg, Wire, jit_inline
from pycircuit.dsl import Signal


@jit_inline
def build_byte_mem(
    m: Circuit,
    clk: Signal,
    rst: Signal,
    *,
    raddr: Wire | Reg | Signal,
    wvalid: Wire | Reg | Signal,
    waddr: Wire | Reg | Signal,
    wdata: Wire | Reg | Signal,
    wstrb: Wire | Reg | Signal,
    depth_bytes: int,
    name: str,
) -> Wire:
    return m.byte_mem(clk, rst, raddr=raddr, wvalid=wvalid, waddr=waddr, wdata=wdata, wstrb=wstrb, depth=depth_bytes, name=name)
