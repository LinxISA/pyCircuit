from __future__ import annotations

from dataclasses import dataclass

from pycircuit import Circuit, Reg, Wire
from pycircuit.hw import cat
from pycircuit.dsl import Signal

from janus.bcc.ooo.helpers import mux_by_uindex


@dataclass(frozen=True)
class TileRegFile:
    cells: list[Reg]
    read_data: Wire


def build_tilereg(
    m: Circuit,
    *,
    clk: Signal,
    rst: Signal,
    read_idx: Wire,
    write_valid: Wire,
    write_idx: Wire,
    write_data: Wire,
    num_tiles: int = 64,
    name: str = "tilereg",
    use_byte_mem: bool = False,
) -> TileRegFile:
    if num_tiles <= 0:
        raise ValueError("num_tiles must be > 0")
    if (num_tiles & (num_tiles - 1)) != 0 and (not use_byte_mem):
        raise ValueError("num_tiles must be a positive power of two for reg storage")

    c = m.const
    idx_w = (num_tiles - 1).bit_length()
    read_idx = m.wire(read_idx)
    write_valid = m.wire(write_valid)
    write_idx = m.wire(write_idx)
    write_data = m.wire(write_data)

    ridx = read_idx
    if ridx.width < idx_w:
        ridx = ridx.zext(width=idx_w)
    elif ridx.width > idx_w:
        ridx = ridx.trunc(width=idx_w)

    widx = write_idx
    if widx.width < idx_w:
        widx = widx.zext(width=idx_w)
    elif widx.width > idx_w:
        widx = widx.trunc(width=idx_w)

    if use_byte_mem:
        byte_addr = cat(ridx, c(0, width=3))
        byte_waddr = cat(widx, c(0, width=3))
        depth_bytes = num_tiles * 8
        read_data = m.byte_mem(
            clk=clk,
            rst=rst,
            raddr=byte_addr,
            wvalid=write_valid,
            waddr=byte_waddr,
            wdata=write_data,
            wstrb=c(0xFF, width=8),
            depth=depth_bytes,
            name=name,
        )
        cells: list[Reg] = []
    else:
        with m.scope(name):
            cells = []
            for i in range(num_tiles):
                cells.append(m.out(f"t{i}", clk=clk, rst=rst, width=64, init=0, en=1))

        for i in range(num_tiles):
            hit = write_valid & widx.eq(c(i, width=widx.width))
            cells[i].set(write_data, when=hit)

        read_data = mux_by_uindex(m, idx=ridx, items=cells, default=c(0, width=64))

    return TileRegFile(cells=cells, read_data=read_data)
