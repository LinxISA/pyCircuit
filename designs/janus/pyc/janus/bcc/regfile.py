from __future__ import annotations
from pycircuit import Circuit, Reg, Wire, u
from pycircuit.dsl import Signal
from .isa import REG_INVALID

def make_gpr(m: Circuit, clk: Signal, rst: Signal, *, boot_sp: Wire, en: Wire) -> list[Reg]:
    """24-entry GPR file (r0 forced to 0, r1 initialized to boot_sp)."""
    zero64 = u(64, 0)
    regs: list[Reg] = []
    for i in range(24):
        init = boot_sp if i == 1 else zero64
        regs.append(m.out(f'r{i}', clk=clk, rst=rst, width=64, init=init, en=en))
    return regs

def make_regs(m: Circuit, clk: Signal, rst: Signal, *, count: int, width: int, init: Wire, en: Wire) -> list[Reg]:
    regs: list[Reg] = []
    for i in range(count):
        regs.append(m.out(f'r{i}', clk=clk, rst=rst, width=width, init=init, en=en))
    return regs

def read_reg(m: Circuit, code: Wire, *, gpr: list[Reg], t: list[Reg], u: list[Reg], default: Wire) -> Wire:
    """Mux-based regfile read with strict defaulting (out-of-range -> default)."""
    c = m.const
    v: Wire = default
    for i in range(24):
        vv = default if i == 0 else gpr[i]
        v = vv if code == c(i, width=6) else v
    for i in range(4):
        v = t[i] if code == c(24 + i, width=6) else v
    for i in range(4):
        v = u[i] if code == c(28 + i, width=6) else v
    return v

def stack_next(m: Circuit, arr: list[Reg], *, do_push: Wire, do_clear: Wire, value: Wire) -> list[Wire]:
    zero64 = u(64, 0)
    n0 = zero64 if do_clear else value if do_push else arr[0]
    n1 = zero64 if do_clear else arr[0] if do_push else arr[1]
    n2 = zero64 if do_clear else arr[1] if do_push else arr[2]
    n3 = zero64 if do_clear else arr[2] if do_push else arr[3]
    return [n0, n1, n2, n3]

def commit_gpr(m: Circuit, gpr: list[Reg], *, do_reg_write: Wire, regdst: Reg, value: Reg) -> None:
    c = m.const
    zero64 = u(64, 0)
    for i in range(24):
        if i == 0:
            gpr[i].set(zero64)
            continue
        we = do_reg_write & (regdst == c(i, width=6))
        gpr[i].set(value, when=we)

def commit_stack(m: Circuit, arr: list[Reg], next_vals: list[Wire]) -> None:
    for i in range(4):
        arr[i].set(next_vals[i])
