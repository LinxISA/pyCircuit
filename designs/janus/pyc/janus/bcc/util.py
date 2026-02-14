from __future__ import annotations
from dataclasses import dataclass
from pycircuit import Circuit, Wire, cat, jit_inline, s, u

@dataclass(frozen=True)
class Consts:
    one1: Wire
    zero1: Wire
    zero3: Wire
    zero4: Wire
    zero6: Wire
    zero8: Wire
    zero32: Wire
    zero64: Wire
    one64: Wire

def make_consts(m: Circuit) -> Consts:
    c = m.const
    return Consts(one1=c(1, width=1), zero1=c(0, width=1), zero3=c(0, width=3), zero4=c(0, width=4), zero6=c(0, width=6), zero8=c(0, width=8), zero32=c(0, width=32), zero64=c(0, width=64), one64=c(1, width=64))

def masked_eq(x: Wire, *, mask: int, match: int) -> Wire:
    return x & int(mask) == int(match)

@jit_inline
def shl_var(m: Circuit, value: Wire, shamt: Wire) -> Wire:
    """Variable shift-left by `shamt` (uses low 6 bits)."""
    _ = m
    s = shamt[0:6] if shamt.width >= 6 else cat(u(6 - shamt.width, 0), shamt)
    out = value
    out = out.shl(amount=1) if s[0] else out
    out = out.shl(amount=2) if s[1] else out
    out = out.shl(amount=4) if s[2] else out
    out = out.shl(amount=8) if s[3] else out
    out = out.shl(amount=16) if s[4] else out
    out = out.shl(amount=32) if s[5] else out
    return out

@jit_inline
def lshr_var(m: Circuit, value: Wire, shamt: Wire) -> Wire:
    """Variable logical shift-right by `shamt` (uses low 6 bits)."""
    _ = m
    s = shamt[0:6] if shamt.width >= 6 else cat(u(6 - shamt.width, 0), shamt)
    out = value
    out = out.lshr(amount=1) if s[0] else out
    out = out.lshr(amount=2) if s[1] else out
    out = out.lshr(amount=4) if s[2] else out
    out = out.lshr(amount=8) if s[3] else out
    out = out.lshr(amount=16) if s[4] else out
    out = out.lshr(amount=32) if s[5] else out
    return out

@jit_inline
def ashr_var(m: Circuit, value: Wire, shamt: Wire) -> Wire:
    """Variable arithmetic shift-right by `shamt` (uses low 6 bits)."""
    _ = m
    s = shamt[0:6] if shamt.width >= 6 else cat(u(6 - shamt.width, 0), shamt)
    out = value.as_signed()
    out = out.ashr(amount=1) if s[0] else out
    out = out.ashr(amount=2) if s[1] else out
    out = out.ashr(amount=4) if s[2] else out
    out = out.ashr(amount=8) if s[3] else out
    out = out.ashr(amount=16) if s[4] else out
    out = out.ashr(amount=32) if s[5] else out
    return out
