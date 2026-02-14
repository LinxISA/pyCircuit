from __future__ import annotations
from pycircuit import Circuit, Wire, u

def scb_merge_word(m: Circuit, *, old_data: Wire, new_data: Wire, wstrb: Wire) -> Wire:
    old_data = m.wire(old_data)
    new_data = m.wire(new_data)
    wstrb = m.wire(wstrb)
    merged = old_data
    for i in range(8):
        mask = u(64, 255 << i * 8)
        merged = merged & u(64, ~(255 << i * 8)) | new_data & mask if wstrb[i] else merged
    return merged
