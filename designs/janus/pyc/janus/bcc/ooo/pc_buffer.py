from __future__ import annotations
from pycircuit import Circuit, Wire, unsigned

def next_pc(m: Circuit, *, pc: Wire, len_bytes: Wire, redirect_valid: Wire, redirect_pc: Wire) -> Wire:
    pc = m.wire(pc)
    len_bytes = m.wire(len_bytes)
    redirect_valid = m.wire(redirect_valid)
    redirect_pc = m.wire(redirect_pc)
    fallthrough = pc + unsigned(len_bytes)
    return redirect_pc if redirect_valid else fallthrough
