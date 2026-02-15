from __future__ import annotations
from dataclasses import dataclass
from pycircuit import Circuit, Wire, u
from pycircuit.dsl import Signal

@dataclass(frozen=True)
class ICacheOut:
    rdata: Wire

def build_icache(m: Circuit, *, clk: Signal, rst: Signal, raddr: Wire, mem: Wire | None=None, depth_bytes: int=1 << 20) -> ICacheOut:
    raddr = m.wire(raddr)
    if mem is not None:
        return ICacheOut(rdata=m.wire(mem))
    rdata = m.byte_mem(clk, rst, raddr=raddr, wvalid=u(1, 0), waddr=u(64, 0), wdata=u(64, 0), wstrb=u(8, 0), depth=depth_bytes, name='icache_mem')
    return ICacheOut(rdata=rdata)
