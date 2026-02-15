from __future__ import annotations
from pycircuit import Circuit, Wire

def s2_pick_first_ready(m: Circuit, *, ready_vec: list[Wire], idx_width: int) -> tuple[Wire, Wire]:
    c = m.const
    valid = c(0, width=1)
    idx = c(0, width=idx_width)
    for i, rdy in enumerate(ready_vec):
        rdy = m.wire(rdy)
        take = rdy & ~valid
        valid = c(1, width=1) if take else valid
        idx = c(i, width=idx_width) if take else idx
    return (valid, idx)
