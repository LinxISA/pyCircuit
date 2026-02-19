from __future__ import annotations

from contextlib import contextmanager
from dataclasses import dataclass
import inspect
from typing import Any, Iterable, Iterator, Mapping, Union, overload

from .connectors import (
    Connector,
    ConnectorBundle,
    ConnectorError,
    ModuleInstanceHandle,
    RegConnector,
    WireConnector,
    is_connector,
    is_connector_bundle,
)
from .dsl import Module, Signal
from .literals import LiteralValue, infer_literal_width


def _int_width(ty: str) -> int:
    if not ty.startswith("i"):
        raise TypeError(f"expected integer type iN, got {ty!r}")
    w = int(ty[1:])
    if w <= 0:
        raise ValueError(f"invalid integer width: {ty!r}")
    return w


def _removed_design_api(name: str, replacement: str) -> TypeError:
    return TypeError(f"{name} was removed from pyCircuit; use {replacement}")


def _coerce_literal_width(
    lit: LiteralValue,
    *,
    ctx_width: int | None,
    ctx_signed: bool | None,
) -> tuple[int, bool]:
    signed = bool(lit.signed) if lit.signed is not None else bool(ctx_signed)
    if lit.width is not None:
        return int(lit.width), signed
    if ctx_width is not None:
        return int(ctx_width), signed
    return infer_literal_width(int(lit.value), signed=signed), signed


@dataclass(frozen=True, eq=False)
class Wire:
    m: Module
    sig: Signal
    signed: bool = False
    # True if this Wire originates from `pyc.wire` and is intended to be driven
    # by `pyc.assign` (SSA backedge placeholder). JIT debug aliasing must not
    # wrap such wires in `pyc.alias`, because `pyc.assign` destinations must be
    # defined by `pyc.wire`.
    assignable: bool = False

    def __post_init__(self) -> None:
        _int_width(self.sig.ty)

    @property
    def ref(self) -> str:
        return self.sig.ref

    @property
    def ty(self) -> str:
        return self.sig.ty

    @property
    def width(self) -> int:
        return _int_width(self.sig.ty)

    def __str__(self) -> str:
        return self.sig.ref

    def __bool__(self) -> bool:
        raise TypeError(
            "Wire cannot be used as a Python boolean. "
            "Use `if` inside a JIT-compiled design function, or compare explicitly and return an i1 Wire."
        )

    def out(self) -> "Wire":
        """Stage-friendly sugar: a Wire's value is itself."""
        return self

    def _as_wire(self, v: Union["Wire", "Reg", Signal, int, LiteralValue], *, width: int | None) -> "Wire":
        if isinstance(v, Connector):
            v = v.read()
        if isinstance(v, Reg):
            v = v.q
        if isinstance(v, Wire):
            if v.m is not self.m:
                raise ValueError("cannot combine wires from different modules")
            return v
        if isinstance(v, Signal):
            return Wire(self.m, v)
        if isinstance(v, LiteralValue):
            lit_w, lit_signed = _coerce_literal_width(v, ctx_width=width, ctx_signed=v.signed)
            const_sig = Module.const(self.m, int(v.value), width=int(lit_w))
            return Wire(self.m, const_sig, signed=lit_signed)
        if isinstance(v, int):
            if width is None:
                width = self.width
            # Call the base `Module.const` even if `Circuit.const` is overridden
            # to return a `Wire`.
            const_sig = Module.const(self.m, int(v), width=int(width))
            return Wire(self.m, const_sig, signed=(int(v) < 0))
        raise TypeError(f"unsupported operand type: {type(v).__name__}")

    def _promote2(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> tuple["Wire", "Wire"]:
        """Promote operands to a common width (extend smaller operand)."""
        a = self._as_wire(self, width=None)
        if isinstance(other, int):
            b = self._as_wire(int(other), width=a.width)
        else:
            b = self._as_wire(other, width=None)
        out_w = max(a.width, b.width)
        if a.width != out_w:
            a = a._sext(width=out_w) if a.signed else a._zext(width=out_w)
        if b.width != out_w:
            b = b._sext(width=out_w) if b.signed else b._zext(width=out_w)
        return a, b

    def __add__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        a, b = self._promote2(other)
        return Wire(self.m, self.m.add(a.sig, b.sig), signed=(a.signed or b.signed))

    def __radd__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        return self.__add__(other)

    def __sub__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        a, b = self._promote2(other)
        return Wire(self.m, self.m.sub(a.sig, b.sig), signed=(a.signed or b.signed))

    def __rsub__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        b = self._as_wire(self, width=None)
        a = self._as_wire(other, width=b.width)
        aa, bb = a._promote2(b) if isinstance(a, Wire) else (a, b)
        return Wire(self.m, self.m.sub(aa.sig, bb.sig), signed=(aa.signed or bb.signed))

    def __mul__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        a, b = self._promote2(other)
        return Wire(self.m, self.m.mul(a.sig, b.sig), signed=(a.signed or b.signed))

    def __rmul__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        return self.__mul__(other)

    def __rfloordiv__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        num = self._as_wire(other, width=None)
        return num.__floordiv__(self)

    def __floordiv__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        a, b = self._promote2(other)
        if a.signed or b.signed:
            return Wire(self.m, self.m.sdiv(a.sig, b.sig), signed=True)
        return Wire(self.m, self.m.udiv(a.sig, b.sig), signed=False)

    def __rtruediv__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        return self.__rfloordiv__(other)

    def __truediv__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        # Treat `/` as integer division for hardware values.
        return self.__floordiv__(other)

    def __rmod__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        num = self._as_wire(other, width=None)
        return num.__mod__(self)

    def __mod__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        a, b = self._promote2(other)
        if a.signed or b.signed:
            return Wire(self.m, self.m.srem(a.sig, b.sig), signed=True)
        return Wire(self.m, self.m.urem(a.sig, b.sig), signed=False)

    def __and__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        a, b = self._promote2(other)
        return Wire(self.m, self.m.and_(a.sig, b.sig), signed=(a.signed or b.signed))

    def __rand__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        return self.__and__(other)

    def __or__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        a, b = self._promote2(other)
        return Wire(self.m, self.m.or_(a.sig, b.sig), signed=(a.signed or b.signed))

    def __ror__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        return self.__or__(other)

    def __xor__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        a, b = self._promote2(other)
        return Wire(self.m, self.m.xor(a.sig, b.sig), signed=(a.signed or b.signed))

    def __rxor__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        return self.__xor__(other)

    def __invert__(self) -> "Wire":
        return Wire(self.m, self.m.not_(self.sig), signed=self.signed)

    def __lshift__(self, other: int) -> "Wire":
        if not isinstance(other, int):
            raise TypeError("<< only supports constant integer shift amounts")
        return self.shl(amount=int(other))

    def lshr(self, *, amount: int) -> "Wire":
        """Logical shift right by a constant amount (zero-fill)."""
        amt = int(amount)
        if amt < 0:
            raise ValueError("lshr amount must be >= 0")
        return Wire(self.m, self.m.lshri(self.sig, amount=amt), signed=False)

    def ashr(self, *, amount: int) -> "Wire":
        """Arithmetic shift right by a constant amount (sign-fill)."""
        amt = int(amount)
        if amt < 0:
            raise ValueError("ashr amount must be >= 0")
        return Wire(self.m, self.m.ashri(self.sig, amount=amt), signed=True)

    def __rshift__(self, other: int) -> "Wire":
        if not isinstance(other, int):
            raise TypeError(">> only supports constant integer shift amounts")
        if self.signed:
            return self.ashr(amount=int(other))
        return self.lshr(amount=int(other))

    def __eq__(self, other: object) -> "Wire":  # type: ignore[override]
        if not isinstance(other, (Wire, Reg, Signal, Connector, int, LiteralValue)):
            return NotImplemented
        a, b = self._promote2(other)
        return Wire(self.m, self.m.eq(a.sig, b.sig))

    def __ne__(self, other: object) -> "Wire":  # type: ignore[override]
        if not isinstance(other, (Wire, Reg, Signal, Connector, int, LiteralValue)):
            return NotImplemented
        return ~(self == other)

    def eq(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        return self == other

    def ne(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        return self != other

    def ult(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        """Unsigned less-than compare (result is i1)."""
        a, b = self._promote2(other)
        return Wire(self.m, self.m.ult(a.sig, b.sig))

    def slt(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        """Signed less-than compare (result is i1)."""
        a, b = self._promote2(other)
        return Wire(self.m, self.m.slt(a.sig, b.sig))

    def __lt__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        """Less-than compare respecting signed intent (result is i1)."""
        a, b = self._promote2(other)
        if a.signed or b.signed:
            return Wire(self.m, self.m.slt(a.sig, b.sig))
        return Wire(self.m, self.m.ult(a.sig, b.sig))

    def __gt__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        """Greater-than compare respecting signed intent (result is i1)."""
        other_w = self._as_wire(other, width=None)
        return other_w < self

    def __le__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        """Less-than-or-equal compare respecting signed intent (result is i1)."""
        return ~(self > other)

    def __ge__(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        """Greater-than-or-equal compare respecting signed intent (result is i1)."""
        return ~(self < other)

    def ugt(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        """Unsigned greater-than compare (result is i1)."""
        other_w = self._as_wire(other, width=None)
        return other_w.ult(self)

    def ule(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        """Unsigned less-than-or-equal compare (result is i1)."""
        return ~self.ugt(other)

    def uge(self, other: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        """Unsigned greater-than-or-equal compare (result is i1)."""
        return ~self.ult(other)

    def _select_internal(self, a: Union["Wire", "Reg", Signal, int, LiteralValue], b: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        if self.ty != "i1":
            raise TypeError("conditional selection requires a 1-bit selector wire (i1)")

        # At least one operand must provide width.
        if isinstance(a, int) and isinstance(b, int):
            raise TypeError("conditional selection requires at least one Wire/Reg/Signal operand (cannot infer width from two ints)")

        aw: Wire | None = None
        bw: Wire | None = None
        if not isinstance(a, int):
            aw = self._as_wire(a, width=None)
        if not isinstance(b, int):
            bw = self._as_wire(b, width=None)

        if aw is None and bw is None:
            raise TypeError("conditional selection requires at least one Wire/Reg/Signal operand (cannot infer width)")

        out_w = max(aw.width if aw is not None else 0, bw.width if bw is not None else 0)
        if aw is None:
            aw = self._as_wire(int(a), width=out_w)
        if bw is None:
            bw = self._as_wire(int(b), width=out_w)

        if aw.width != out_w:
            aw = aw._sext(width=out_w) if aw.signed else aw._zext(width=out_w)
        if bw.width != out_w:
            bw = bw._sext(width=out_w) if bw.signed else bw._zext(width=out_w)
        return Wire(self.m, self.m.mux(self.sig, aw.sig, bw.sig), signed=(aw.signed or bw.signed))

    def select(self, a: Union["Wire", "Reg", Signal, int, LiteralValue], b: Union["Wire", "Reg", Signal, int, LiteralValue]) -> "Wire":
        return self._select_internal(a, b)

    def _trunc(self, *, width: int) -> "Wire":
        return Wire(self.m, self.m.trunc(self.sig, width=width), signed=self.signed)

    def _zext(self, *, width: int) -> "Wire":
        return Wire(self.m, self.m.zext(self.sig, width=width), signed=False)

    def _sext(self, *, width: int) -> "Wire":
        return Wire(self.m, self.m.sext(self.sig, width=width), signed=True)

    def trunc(self, *, width: int) -> "Wire":
        return self._trunc(width=width)

    def zext(self, *, width: int) -> "Wire":
        return self._zext(width=width)

    def sext(self, *, width: int) -> "Wire":
        return self._sext(width=width)

    def slice(self, *, lsb: int, width: int) -> "Wire":
        return Wire(self.m, self.m.extract(self.sig, lsb=lsb, width=width), signed=False)

    def shl(self, *, amount: int) -> "Wire":
        return Wire(self.m, self.m.shli(self.sig, amount=amount), signed=self.signed)

    def __getitem__(self, idx: int | slice) -> "Wire":
        if isinstance(idx, slice):
            if idx.step is not None:
                raise TypeError("wire slicing does not support step")
            lsb = 0 if idx.start is None else int(idx.start)
            stop = self.width if idx.stop is None else int(idx.stop)
            if lsb < 0 or stop < 0:
                raise ValueError("wire slice indices must be >= 0")
            if stop < lsb:
                raise ValueError("wire slice stop must be >= start")
            width = stop - lsb
            if width <= 0:
                raise ValueError("wire slice width must be > 0")
            if lsb + width > self.width:
                raise ValueError(f"wire slice out of range: [{lsb}:{stop}] on width {self.width}")
            return self.slice(lsb=lsb, width=width)

        bit = int(idx)
        if bit < 0:
            raise ValueError("wire bit index must be >= 0")
        if bit >= self.width:
            raise ValueError("wire bit index out of range")
        return self.slice(lsb=bit, width=1)

    def named(self, name: str) -> "Wire":
        """Attach a debug name via `pyc.alias` (pure)."""
        scoped = str(name)
        scoped_name = getattr(self.m, "scoped_name", None)
        if callable(scoped_name):
            scoped = scoped_name(scoped)
        return Wire(self.m, self.m.alias(self.sig, name=scoped), signed=self.signed)

    def as_signed(self) -> "Wire":
        """Mark this value as signed for shift/div/compare lowering."""
        return Wire(self.m, self.sig, signed=True)

    def as_unsigned(self) -> "Wire":
        """Mark this value as unsigned for shift/div/compare lowering."""
        return Wire(self.m, self.sig, signed=False)


@dataclass(frozen=True)
class ClockDomain:
    clk: Signal
    rst: Signal


@dataclass(frozen=True, eq=False)
class Reg:
    q: Wire
    clk: Signal
    rst: Signal
    en: Wire
    next: Wire
    init: Wire

    @property
    def ref(self) -> str:
        return self.q.ref

    @property
    def ty(self) -> str:
        return self.q.ty

    @property
    def width(self) -> int:
        return self.q.width

    def __str__(self) -> str:
        return self.q.ref

    def __bool__(self) -> bool:
        raise TypeError(
            "Reg cannot be used as a Python boolean. "
            "Use `if` inside a JIT-compiled design function, or compare explicitly and return an i1 Wire."
        )

    def out(self) -> Wire:
        """Read the current value of the register (q) as a Wire."""
        return self.q

    def __add__(self, other: Union[Wire, Signal, int]) -> Wire:
        return self.q + other

    def __and__(self, other: Union[Wire, Signal, int]) -> Wire:
        return self.q & other

    def __or__(self, other: Union[Wire, Signal, int]) -> Wire:
        return self.q | other

    def __xor__(self, other: Union[Wire, Signal, int]) -> Wire:
        return self.q ^ other

    def __invert__(self) -> Wire:
        return ~self.q

    def __lshift__(self, other: int) -> Wire:
        return self.q << other

    def __rshift__(self, other: int) -> Wire:
        return self.q >> other

    def lshr(self, *, amount: int) -> Wire:
        return self.q.lshr(amount=amount)

    def ashr(self, *, amount: int) -> Wire:
        return self.q.ashr(amount=amount)

    def __eq__(self, other: object) -> Wire:  # type: ignore[override]
        return self.q == other

    def __ne__(self, other: object) -> Wire:  # type: ignore[override]
        return self.q != other

    def eq(self, other: Union[Wire, "Reg", Signal, int]) -> Wire:
        return self == other

    def ne(self, other: Union[Wire, "Reg", Signal, int]) -> Wire:
        return self.q.ne(other)

    def __lt__(self, other: Union[Wire, Signal, int]) -> Wire:
        return self.q < other

    def __gt__(self, other: Union[Wire, Signal, int]) -> Wire:
        return self.q > other

    def __le__(self, other: Union[Wire, Signal, int]) -> Wire:
        return self.q <= other

    def __ge__(self, other: Union[Wire, Signal, int]) -> Wire:
        return self.q >= other

    def ult(self, other: Union[Wire, Signal, int]) -> Wire:
        return self.q.ult(other)

    def ugt(self, other: Union[Wire, Signal, int]) -> Wire:
        return self.q.ugt(other)

    def ule(self, other: Union[Wire, Signal, int]) -> Wire:
        return self.q.ule(other)

    def uge(self, other: Union[Wire, Signal, int]) -> Wire:
        return self.q.uge(other)

    def slice(self, *, lsb: int, width: int) -> Wire:
        return self.q.slice(lsb=lsb, width=width)

    def select(self, a: Union[Wire, "Reg", Signal, int], b: Union[Wire, "Reg", Signal, int]) -> Wire:
        return self.q.select(a, b)

    def trunc(self, *, width: int) -> Wire:
        return self.q.trunc(width=width)

    def zext(self, *, width: int) -> Wire:
        return self.q.zext(width=width)

    def sext(self, *, width: int) -> Wire:
        return self.q.sext(width=width)

    def shl(self, *, amount: int) -> Wire:
        return self.q.shl(amount=amount)

    def __getitem__(self, idx: int | slice) -> Wire:
        return self.q[idx]

    def set(
        self,
        value: Union[Wire, "Reg", Signal, Connector, int, LiteralValue],
        *,
        when: Union[Wire, Signal, Connector, int, LiteralValue] = 1,
    ) -> None:
        """Drive `self.next` (backedge) for a stateful variable.

        - `r.set(v)` is equivalent to `m.assign(r.next, v)`
        - `r.set(v, when=cond)` drives `cond ? v : r` (hold otherwise)
        """
        m = self.q.m
        if not isinstance(m, Circuit):
            raise TypeError("Reg.set requires the Reg to belong to a Circuit")

        def as_wire(v: Union[Wire, Reg, Signal, Connector, int, LiteralValue], *, width: int) -> Wire:
            if isinstance(v, Connector):
                v = v.read()
            if isinstance(v, Reg):
                return v.q
            if isinstance(v, Wire):
                if v.m is not m:
                    raise ValueError("cannot combine wires from different modules")
                return v
            if isinstance(v, Signal):
                return Wire(m, v)
            if isinstance(v, LiteralValue):
                lit_w, lit_signed = _coerce_literal_width(v, ctx_width=width, ctx_signed=v.signed)
                return Wire(m, Module.const(m, int(v.value), width=lit_w), signed=lit_signed)
            if isinstance(v, int):
                return m.const(int(v), width=width)
            raise TypeError(f"unsupported value type: {type(v).__name__}")

        next_w = as_wire(value, width=self.width)

        if isinstance(when, int) and int(when) == 1:
            m.assign(self.next, next_w)
            return

        cond = as_wire(when, width=1)
        if cond.ty != "i1":
            raise TypeError("when must be i1")
        m.assign(self.next, cond._select_internal(next_w, self))

    def __ilshift__(self, other: Union[Wire, "Reg", Signal, int, LiteralValue]) -> "Reg":
        self.set(other)
        return self


class Circuit(Module):
    """High-level wrapper over `Module` that returns `Wire`/`Reg` objects."""

    def __init__(self, name: str, design_ctx: Any | None = None) -> None:
        super().__init__(name)
        self._scope_stack: list[str] = []
        # Optional multi-module DesignContext (used by `Circuit.instance`).
        self._design_ctx = design_ctx
        # Stable debug exports materialized as module outputs.
        self._debug_exports: dict[str, Signal] = {}

    def scoped_name(self, name: str) -> str:
        if not self._scope_stack:
            return name
        return "__".join([*self._scope_stack, name])

    @contextmanager
    def scope(self, name: str) -> Iterator[None]:
        self._scope_stack.append(str(name))
        try:
            yield
        finally:
            self._scope_stack.pop()

    def domain(self, name: str) -> ClockDomain:
        return ClockDomain(clk=self.clock(f"{name}_clk"), rst=self.reset(f"{name}_rst"))

    def input(self, name: str, *, width: int, signed: bool = False) -> Wire:  # type: ignore[override]
        """Declare a module input port and return it as a `Wire`."""
        return Wire(self, super().input(name, width=width), signed=bool(signed))

    def const(self, value: int, *, width: int) -> Wire:  # type: ignore[override]
        """Create an integer constant `Wire` (two's complement at `width`)."""
        return Wire(self, super().const(int(value), width=width), signed=(int(value) < 0))

    def output(self, name: str, value: Union[Wire, Reg, Signal, Connector, int, LiteralValue]) -> None:  # type: ignore[override]
        if isinstance(value, Connector):
            value = value.read()
        if isinstance(value, Reg):
            super().output(name, value.q.sig)
            return
        if isinstance(value, Wire):
            super().output(name, value.sig)
            return
        if isinstance(value, Signal):
            super().output(name, value)
            return
        if isinstance(value, LiteralValue):
            lit_w, _ = _coerce_literal_width(value, ctx_width=value.width, ctx_signed=value.signed)
            super().output(name, super().const(int(value.value), width=lit_w))
            return
        if isinstance(value, int):
            w = infer_literal_width(int(value), signed=(int(value) < 0))
            super().output(name, super().const(int(value), width=w))
            return
        raise TypeError(f"output() expects Wire/Reg/Signal/Connector/int/literal, got {type(value).__name__}")

    def new_wire(self, *, width: int) -> Wire:
        return Wire(self, super().new_wire(width=width), assignable=True)

    def named_wire(self, name: str, *, width: int) -> Wire:
        return Wire(self, super().new_wire(width=width, name=self.scoped_name(name)), assignable=True)

    def wire(self, sig: Signal) -> Wire:
        return Wire(self, sig)

    def named(self, v: Union[Wire, Reg, Signal], name: str) -> Wire:
        """Attach a scoped debug name via `pyc.alias` (pure)."""
        if isinstance(v, Reg):
            v = v.q
        if isinstance(v, Wire):
            return Wire(self, self.alias(v.sig, name=self.scoped_name(name)), signed=v.signed)
        return Wire(self, self.alias(v, name=self.scoped_name(name)))

    def debug(self, name: str, value: Union[Wire, Reg, Signal]) -> Wire:
        """Export a named debug probe as a stable module output.

        Probes are emitted as `dbg__*` outputs and consumed directly by generated
        C++/SV testbench flows.
        """
        raw = str(name).strip()
        if not raw:
            raise ValueError("debug name must be non-empty")
        scoped = self.scoped_name(f"dbg__{raw}")

        if isinstance(value, Reg):
            w = value.q
            sig = w.sig
        elif isinstance(value, Wire):
            w = value
            sig = value.sig
        else:
            sig = value
            w = Wire(self, sig)

        prev = self._debug_exports.get(scoped)
        if prev is not None and prev is not sig:
            raise ValueError(f"debug probe {scoped!r} already exists with a different signal")
        if prev is None:
            self.output(scoped, sig)
            self._debug_exports[scoped] = sig
        return w

    def debug_bundle(self, prefix: str, fields: Mapping[str, Union[Wire, Reg, Signal]]) -> dict[str, Wire]:
        """Export a group of debug probes using `prefix_<field>` names."""
        raw_prefix = str(prefix).strip()
        if not raw_prefix:
            raise ValueError("debug bundle prefix must be non-empty")
        out: dict[str, Wire] = {}
        for key, value in fields.items():
            raw_key = str(key).strip()
            if not raw_key:
                raise ValueError("debug bundle field name must be non-empty")
            out[raw_key] = self.debug(f"{raw_prefix}_{raw_key}", value)
        return out

    def debug_probe(
        self,
        stage: str,
        lane: int,
        fields: Mapping[str, Union[Wire, Reg, Signal]],
        *,
        family: str = "pv",
    ) -> dict[str, Wire]:
        """Emit canonical DFX probes as `dbg__<family>_<stage>_<field>_lane<k>_<stage>`."""
        raw_stage = str(stage).strip().lower()
        if not raw_stage:
            raise ValueError("debug probe stage must be non-empty")
        if lane < 0:
            raise ValueError("debug probe lane must be >= 0")
        raw_family = str(family).strip().lower()
        if not raw_family:
            raise ValueError("debug probe family must be non-empty")
        out: dict[str, Wire] = {}
        for key, value in fields.items():
            raw_key = str(key).strip()
            if not raw_key:
                raise ValueError("debug probe field name must be non-empty")
            name = f"{raw_family}_{raw_stage}_{raw_key}_lane{int(lane)}_{raw_stage}"
            out[raw_key] = self.debug(name, value)
        return out

    def debug_occ(self, stage: str, lane: int, fields: Mapping[str, Union[Wire, Reg, Signal]]) -> dict[str, Wire]:
        """Emit occupancy probes as `dbg__occ_<stage>_<field>_lane<k>_<stage>`."""
        return self.debug_probe(stage, lane, fields, family="occ")

    def assign(
        self,
        dst: Union[Wire, Reg, Signal, Connector],
        src: Union[Wire, Reg, Signal, Connector, int, LiteralValue],
    ) -> None:  # type: ignore[override]
        if isinstance(dst, Connector):
            if isinstance(dst, RegConnector):
                dst.set(src)
                return
            dst = dst.read()
        if isinstance(src, Connector):
            src = src.read()

        def as_sig(v: Union[Wire, Reg, Signal]) -> Signal:
            if isinstance(v, Reg):
                return v.q.sig
            if isinstance(v, Wire):
                return v.sig
            return v

        def is_signed_src(v: Union[Wire, Reg, Signal, int, LiteralValue]) -> bool:
            if isinstance(v, Wire):
                return bool(v.signed)
            if isinstance(v, Reg):
                return bool(v.q.signed)
            if isinstance(v, LiteralValue):
                if v.signed is not None:
                    return bool(v.signed)
                return int(v.value) < 0
            return False

        dst_sig = as_sig(dst)
        if isinstance(src, LiteralValue):
            lit_w, _ = _coerce_literal_width(src, ctx_width=_int_width(dst_sig.ty), ctx_signed=is_signed_src(src))
            src_sig = super().const(int(src.value), width=lit_w)
            super().assign(dst_sig, src_sig)
            return
        if isinstance(src, int):
            src_sig = super().const(int(src), width=_int_width(dst_sig.ty))
            super().assign(dst_sig, src_sig)
            return

        src_signed = is_signed_src(src)
        src_sig = as_sig(src)
        if dst_sig.ty == src_sig.ty:
            super().assign(dst_sig, src_sig)
            return

        # Implicit integer resizing for convenience (zext smaller, trunc larger).
        if dst_sig.ty.startswith("i") and src_sig.ty.startswith("i"):
            dst_w = _int_width(dst_sig.ty)
            src_w = _int_width(src_sig.ty)
            if src_w < dst_w:
                src_sig = super().sext(src_sig, width=dst_w) if src_signed else super().zext(src_sig, width=dst_w)
            elif src_w > dst_w:
                src_sig = super().trunc(src_sig, width=dst_w)
            super().assign(dst_sig, src_sig)
            return

        raise TypeError(f"assign requires same types, got {dst_sig.ty} and {src_sig.ty}")

    def assert_(self, cond: Union[Wire, Reg, Signal], *, msg: str | None = None) -> None:
        c = cond.q if isinstance(cond, Reg) else cond
        sig = c.sig if isinstance(c, Wire) else c
        super().assert_(sig, msg=msg)

    def out(
        self,
        name: str,
        *,
        clk: Signal | None = None,
        rst: Signal | None = None,
        domain: ClockDomain | None = None,
        width: int,
        init: Union[Wire, Reg, Signal, int, LiteralValue] = 0,
        en: Union[Wire, Signal, int, LiteralValue] = 1,
        stage: str | None = None,
        signed: bool | None = None,  # reserved for future type inference / lowering
    ) -> Reg:
        """Declare a named stateful variable (backedge register).

        This is a higher-level replacement for `backedge_reg(...)` that:
        - takes a stable logical name (for debug/name mangling),
        - optionally tags the name with a pipeline stage prefix,
        - declares a named backedge wire for `next`.
        """
        _ = signed  # unused for now (kept for API stability)

        if domain is not None:
            clk = domain.clk
            rst = domain.rst
        if clk is None or rst is None:
            raise TypeError("out() requires either domain=... or both clk=... and rst=...")

        full = str(name)
        if stage:
            full = f"{stage}__{full}"
        full = self.scoped_name(full)

        next_w = Wire(self, super().new_wire(width=width, name=f"{full}__next"), assignable=True)
        if isinstance(en, LiteralValue):
            lit_w, lit_signed = _coerce_literal_width(en, ctx_width=1, ctx_signed=False)
            en_w = Wire(self, super().const(int(en.value), width=lit_w), signed=lit_signed)
        elif isinstance(en, int):
            en_w: Union[Wire, Signal] = self.const(int(en), width=1)
        else:
            en_w = en

        if isinstance(init, LiteralValue):
            lit_w, lit_signed = _coerce_literal_width(init, ctx_width=width, ctx_signed=init.signed)
            init_w = Wire(self, super().const(int(init.value), width=lit_w), signed=lit_signed)
        elif isinstance(init, int):
            init_w: Union[Wire, Signal] = self.const(int(init), width=width)
        else:
            init_w = init

        r = self.reg_wire(clk, rst, en_w, next_w, init_w)
        # Name the observable value of the state variable.
        q_named = Wire(self, self.alias(r.q.sig, name=full), signed=r.q.signed)
        return Reg(q=q_named, clk=r.clk, rst=r.rst, en=r.en, next=r.next, init=r.init)

    def reg_wire(
        self,
        clk: Signal,
        rst: Signal,
        en: Union[Wire, Signal],
        next_: Union[Wire, Signal],
        init: Union[Wire, Signal, int, LiteralValue],
    ) -> Reg:
        en_w = en if isinstance(en, Wire) else Wire(self, en)
        next_w = next_ if isinstance(next_, Wire) else Wire(self, next_)
        if isinstance(init, LiteralValue):
            lit_w, lit_signed = _coerce_literal_width(init, ctx_width=next_w.width, ctx_signed=next_w.signed)
            init_w = Wire(self, super().const(int(init.value), width=lit_w), signed=lit_signed)
        elif isinstance(init, int):
            init_w = self.const(init, width=next_w.width)
        else:
            init_w = init if isinstance(init, Wire) else Wire(self, init)

        q_sig = self.reg(clk, rst, en_w.sig, next_w.sig, init_w.sig)
        q_w = Wire(self, q_sig, signed=(next_w.signed or init_w.signed))
        return Reg(q=q_w, clk=clk, rst=rst, en=en_w, next=next_w, init=init_w)

    def reg_domain(
        self,
        domain: ClockDomain,
        en: Union[Wire, Signal],
        next_: Union[Wire, Signal],
        init: Union[Wire, Signal, int, LiteralValue],
    ) -> Reg:
        return self.reg_wire(domain.clk, domain.rst, en, next_, init)

    def backedge_reg(
        self,
        clk: Signal,
        rst: Signal,
        *,
        width: int,
        init: Union[Wire, Signal, int, LiteralValue],
        en: Union[Wire, Signal, int, LiteralValue] = 1,
    ) -> Reg:
        """Create a register whose `next` is a placeholder `pyc.wire` meant to be driven via `pyc.assign`.

        This pattern enables feedback loops (state machines) in a netlist-like style:

        - `r = m.backedge_reg(...)` creates `r.next` as a `pyc.wire`
        - Later: `m.assign(r.next, some_next_value)`
        """
        next_w = self.new_wire(width=width)
        if isinstance(en, LiteralValue):
            lit_w, lit_signed = _coerce_literal_width(en, ctx_width=1, ctx_signed=False)
            en_w: Union[Wire, Signal] = Wire(self, super().const(int(en.value), width=lit_w), signed=lit_signed)
        elif isinstance(en, int):
            en_w: Union[Wire, Signal] = self.const(en, width=1)
        else:
            en_w = en
        return self.reg_wire(clk, rst, en_w, next_w, init)

    def vec(self, *elems: Union["Wire", "Reg"]) -> "Vec":
        return Vec(elems)

    def cat(self, *elems: Union["Wire", "Reg", int, LiteralValue]) -> Wire:
        """Concatenate values into a packed bus (MSB-first)."""
        if not elems:
            raise ValueError("cat() requires at least one element")
        ws: list[Union[Wire, Reg]] = []
        for e in elems:
            if isinstance(e, (Wire, Reg)):
                ws.append(e)
                continue
            if isinstance(e, LiteralValue):
                lit_w, lit_signed = _coerce_literal_width(e, ctx_width=e.width, ctx_signed=e.signed)
                ws.append(Wire(self, super().const(int(e.value), width=lit_w), signed=lit_signed))
                continue
            if isinstance(e, int):
                w = infer_literal_width(int(e), signed=(int(e) < 0))
                ws.append(self.const(int(e), width=w))
                continue
            raise TypeError(f"cat() element must be Wire/Reg/int/literal, got {type(e).__name__}")
        return self.vec(*ws).pack()

    def bundle(self, **fields: Union["Wire", "Reg"]) -> "Bundle":
        return Bundle(fields)

    def as_connector(
        self,
        value: Union[Connector, Wire, Reg, Signal, LiteralValue, int],
        *,
        name: str | None = None,
    ) -> Connector:
        if isinstance(value, Connector):
            if value.owner is not self:
                raise ConnectorError("connector belongs to a different Circuit")
            return value
        if isinstance(value, Reg):
            if value.q.m is not self:
                raise ConnectorError("reg belongs to a different Circuit")
            return RegConnector(owner=self, name=str(name or value.ref), reg=value)
        if isinstance(value, Wire):
            if value.m is not self:
                raise ConnectorError("wire belongs to a different Circuit")
            return WireConnector(owner=self, name=str(name or value.ref), wire=value)
        if isinstance(value, Signal):
            return WireConnector(owner=self, name=str(name or value.ref), wire=value)
        if isinstance(value, LiteralValue):
            lit_w, lit_signed = _coerce_literal_width(value, ctx_width=value.width, ctx_signed=value.signed)
            w = Wire(self, Module.const(self, int(value.value), width=int(lit_w)), signed=lit_signed)
            return WireConnector(owner=self, name=str(name or f"lit_{int(value.value)}"), wire=w)
        if isinstance(value, int):
            ww = infer_literal_width(int(value), signed=(int(value) < 0))
            w = self.const(int(value), width=ww)
            return WireConnector(owner=self, name=str(name or f"lit_{int(value)}"), wire=w)
        raise ConnectorError(f"expected Connector/Wire/Reg/Signal/int/literal, got {type(value).__name__}")

    def input_connector(self, name: str, *, width: int, signed: bool = False) -> WireConnector:
        w = self.input(str(name), width=width, signed=signed)
        return WireConnector(owner=self, name=str(name), wire=w)

    def output_connector(
        self,
        name: str,
        value: Union[Connector, Wire, Reg, Signal, None] = None,
        *,
        width: int | None = None,
    ) -> Connector:
        if value is None:
            if width is None:
                raise TypeError("output_connector() requires `value` or `width`")
            w = self.named_wire(str(name), width=int(width))
            self.output(str(name), w)
            return WireConnector(owner=self, name=str(name), wire=w)
        c = self.as_connector(value, name=str(name))
        self.output(str(name), c)
        return c

    def reg_connector(
        self,
        name: str,
        *,
        clk: Signal | None = None,
        rst: Signal | None = None,
        domain: ClockDomain | None = None,
        width: int,
        init: Union[Wire, Reg, Signal, int, LiteralValue] = 0,
        en: Union[Wire, Signal, int, LiteralValue] = 1,
        stage: str | None = None,
    ) -> RegConnector:
        r = self.out(
            str(name),
            clk=clk,
            rst=rst,
            domain=domain,
            width=width,
            init=init,
            en=en,
            stage=stage,
        )
        return RegConnector(owner=self, name=str(name), reg=r)

    def bundle_connector(self, **fields: Union[Connector, Wire, Reg, Signal]) -> ConnectorBundle:
        out: dict[str, Connector] = {}
        for k, v in fields.items():
            out[str(k)] = self.as_connector(v, name=str(k))
        return ConnectorBundle(out)

    def connect(
        self,
        dst: Connector | ConnectorBundle,
        src: Connector | ConnectorBundle | Wire | Reg | Signal,
        *,
        when: Union[Wire, Signal, int, LiteralValue] = 1,
    ) -> None:
        if isinstance(dst, ConnectorBundle):
            if not isinstance(src, ConnectorBundle):
                raise ConnectorError("bundle connect requires ConnectorBundle source")
            dkeys = set(dst.keys())
            skeys = set(src.keys())
            if dkeys != skeys:
                missing = sorted(dkeys - skeys)
                extra = sorted(skeys - dkeys)
                parts: list[str] = []
                if missing:
                    parts.append("missing: " + ", ".join(missing))
                if extra:
                    parts.append("extra: " + ", ".join(extra))
                raise ConnectorError(f"bundle connect key mismatch ({'; '.join(parts)})")
            for k in sorted(dkeys):
                self.connect(dst[k], src[k], when=when)
            return

        d = self.as_connector(dst)
        s = self.as_connector(src) if not isinstance(src, Connector) else self.as_connector(src)

        if isinstance(d, RegConnector):
            d.set(s.read(), when=when)
            return
        if not (isinstance(when, int) and int(when) == 1):
            raise ConnectorError("conditional connect (`when=...`) is only supported for RegConnector destinations")
        self.assign(d.read(), s.read())

    def io_in(self, spec: Any, *, prefix: str | None = None) -> ConnectorBundle:
        """Declare connector-backed input ports from a meta spec."""
        from .meta.connect import declare_inputs

        return declare_inputs(self, spec, prefix=prefix)

    def io_out(
        self,
        spec: Any,
        values: ConnectorBundle | Mapping[str, Any],
        *,
        prefix: str | None = None,
    ) -> ConnectorBundle:
        """Declare connector-backed output ports from a meta spec."""
        from .meta.connect import declare_outputs

        return declare_outputs(self, spec, values, prefix=prefix)

    def state_regs(
        self,
        spec: Any,
        *,
        clk: Connector | Signal,
        rst: Connector | Signal,
        prefix: str | None = None,
        init: Mapping[str, Any] | Any = 0,
        en: Connector | Signal | int | LiteralValue = 1,
    ) -> ConnectorBundle:
        """Declare register connectors from a meta spec."""
        from .meta.connect import declare_state_regs

        return declare_state_regs(
            self,
            spec,
            clk=clk,
            rst=rst,
            prefix=prefix,
            init=init,
            en=en,
        )

    def pipe_regs(
        self,
        stage_spec: Any,
        in_bundle: ConnectorBundle | Mapping[str, Any],
        *,
        clk: Connector | Signal,
        rst: Connector | Signal,
        en: Connector | Signal | int | LiteralValue = 1,
        flush: Connector | Signal | int | LiteralValue | None = None,
        prefix: str | None = None,
        init: Mapping[str, Any] | Any = 0,
    ) -> ConnectorBundle:
        """Register a stage payload bundle and connect inputs with optional flush."""
        regs = self.state_regs(stage_spec, clk=clk, rst=rst, prefix=prefix, init=init, en=en)

        src: Mapping[str, Any]
        if isinstance(in_bundle, ConnectorBundle):
            src = {k: v for k, v in in_bundle.items()}
        else:
            src = dict(in_bundle)

        dkeys = set(regs.keys())
        skeys = set(str(k) for k in src.keys())
        missing = sorted(dkeys - skeys)
        extra = sorted(skeys - dkeys)
        if missing or extra:
            parts: list[str] = []
            if missing:
                parts.append("missing: " + ", ".join(missing))
            if extra:
                parts.append("extra: " + ", ".join(extra))
            raise ConnectorError(f"pipe_regs key mismatch ({'; '.join(parts)})")

        for key in sorted(dkeys):
            self.connect(regs[key], self.as_connector(src[key], name=key), when=en)
        if flush is not None:
            for key in sorted(dkeys):
                r = regs[key]
                if isinstance(r, RegConnector):
                    r.set(0, when=flush)
        return regs

    def instance_bind(
        self,
        fn: Any,
        *,
        name: str,
        spec_bindings: Mapping[str, Connector | ConnectorBundle | Mapping[str, Any] | Any],
        params: dict[str, Any] | None = None,
        module_name: str | None = None,
    ) -> ModuleInstanceHandle:
        """Instantiate a module from connector/spec bindings.

        `spec_bindings` values may be:
        - `Connector`: bound directly to a single callee port
        - `ConnectorBundle`/mapping: expanded as `<binding_key>_<field>`
        - `meta.bind(spec, value)` or `(spec, value)`: expanded with strict
          key/width/signed checks against the provided spec
        """
        from .meta.connect import bind_instance_ports

        ports = bind_instance_ports(self, spec_bindings)
        return self.instance_handle(
            fn,
            name=str(name),
            params=params,
            module_name=module_name,
            **ports,
        )

    def instance_auto(
        self,
        fn: Any,
        *,
        name: str,
        params: dict[str, Any] | None = None,
        module_name: str | None = None,
        **ports: Any,
    ) -> Connector | ConnectorBundle:
        """Instantiate a module while auto-wrapping port values as connectors."""
        wrapped = {str(k): self.as_connector(v, name=str(k)) for k, v in ports.items()}
        return self.instance(
            fn,
            name=str(name),
            params=params,
            module_name=module_name,
            **wrapped,
        )

    def _coerce_instance_connector(self, v: Any, *, port: str) -> Connector:
        from .design import DesignError

        if is_connector_bundle(v):
            raise DesignError(f"instance port {port!r}: ConnectorBundle is not valid for a single callee port")
        if not is_connector(v):
            raise DesignError(
                f"instance port {port!r}: expected Connector, got {type(v).__name__}; "
                "inter-module links must use input_connector/output_connector/reg_connector/bundle_connector"
            )
        c = self.as_connector(v, name=port)
        return c

    def instance_handle(
        self,
        fn: Any,
        *,
        name: str,
        params: dict[str, Any] | None = None,
        module_name: str | None = None,
        **ports: Connector,
    ) -> ModuleInstanceHandle:
        """Instantiate a specialized sub-module and return a rich instance handle."""

        if self._design_ctx is None:
            raise TypeError("Circuit.instance requires a design context (compile via pycircuit.jit.compile_design)")

        from .design import DesignContext, DesignError

        if not isinstance(self._design_ctx, DesignContext):
            raise TypeError("internal error: Circuit design context has an unexpected type")

        params_dict = dict(params or {})
        overlap = sorted(set(params_dict.keys()) & set(ports.keys()))
        if overlap:
            raise DesignError(f"instance params/ports overlap: {', '.join(overlap)}")

        normalized_ports: dict[str, Connector] = {}
        for pname, v in ports.items():
            normalized_ports[str(pname)] = self._coerce_instance_connector(v, port=str(pname))

        # Signature-bound hardware args: if a function parameter name is provided
        # as a port connection, treat it as a formal input type for specialization.
        sig_port_specs: dict[str, Any] = {}
        try:
            sig = inspect.signature(fn)
            ps = list(sig.parameters.values())
            sig_param_names = {
                p.name
                for p in ps[1:]
                if p.kind not in (inspect.Parameter.VAR_POSITIONAL, inspect.Parameter.VAR_KEYWORD)
            }
        except (TypeError, ValueError):
            sig_param_names = set()

        for pname in sorted(sig_param_names & set(normalized_ports.keys())):
            c = normalized_ports[pname]
            rv = c.read()
            if isinstance(rv, Wire):
                if rv.m is not self:
                    raise DesignError(f"instance port {pname!r}: cannot connect a wire from a different module")
                sig_port_specs[pname] = {"kind": "wire", "ty": rv.ty, "signed": bool(getattr(rv, "signed", False))}
                continue
            if isinstance(rv, Signal):
                if rv.ty == "!pyc.clock":
                    sig_port_specs[pname] = {"kind": "clock"}
                elif rv.ty == "!pyc.reset":
                    sig_port_specs[pname] = {"kind": "reset"}
                elif rv.ty.startswith("i"):
                    sig_port_specs[pname] = {"kind": "wire", "ty": rv.ty, "signed": bool(getattr(c, "signed", False))}
                else:
                    raise DesignError(f"instance port {pname!r}: unsupported signal type {rv.ty!r}")
                continue
            raise DesignError(f"instance port {pname!r}: unsupported connector payload {type(rv).__name__}")

        cm = self._design_ctx.specialize(
            fn,
            params=params_dict,
            module_name=module_name,
            port_specs=sig_port_specs,
        )

        expected = set(cm.arg_names)
        provided = set(normalized_ports.keys())
        missing = sorted(expected - provided)
        extra = sorted(provided - expected)
        if missing or extra:
            parts: list[str] = []
            if missing:
                parts.append("missing: " + ", ".join(missing))
            if extra:
                parts.append("extra: " + ", ".join(extra))
            raise DesignError(f"instance port mismatch for {cm.sym_name!r} ({'; '.join(parts)})")

        def coerce_to_sig(c: Connector, *, expected_ty: str, port: str) -> Signal:
            rv = c.read()
            if isinstance(rv, Wire):
                if rv.m is not self:
                    raise DesignError(f"instance port {port!r}: cannot connect a wire from a different module")
                sig = rv.sig
                src_signed = bool(rv.signed)
            elif isinstance(rv, Signal):
                sig = rv
                src_signed = bool(getattr(c, "signed", False))
            else:
                raise DesignError(f"instance port {port!r}: unsupported connector payload {type(rv).__name__}")

            if sig.ty == expected_ty:
                return sig

            # Convenience: allow implicit integer resizing (zext/trunc) like `Circuit.assign`.
            if sig.ty.startswith("i") and expected_ty.startswith("i"):
                got_w = _int_width(sig.ty)
                exp_w = _int_width(expected_ty)
                if got_w < exp_w:
                    return self.sext(sig, width=exp_w) if src_signed else self.zext(sig, width=exp_w)
                if got_w > exp_w:
                    return self.trunc(sig, width=exp_w)
                return sig

            raise DesignError(f"instance port {port!r}: type mismatch, got {sig.ty} expected {expected_ty}")

        # Build operands in callee signature order.
        operands: list[Signal] = []
        for pname, pty in zip(cm.arg_names, cm.arg_types):
            operands.append(coerce_to_sig(normalized_ports[pname], expected_ty=pty, port=pname))

        outs = self.instance_op(cm.sym_name, *operands, result_types=list(cm.result_types), name=str(name))
        out_fields: dict[str, Connector] = {}
        for oname, sig in zip(cm.result_names, outs):
            out_fields[oname] = WireConnector(owner=self, name=oname, wire=Wire(self, sig))
        force_bundle = False
        try:
            ann = inspect.signature(cm.fn).return_annotation
            if ann is ConnectorBundle:
                force_bundle = True
            elif isinstance(ann, str) and ann.replace(" ", "").lower() == "connectorbundle":
                force_bundle = True
        except (TypeError, ValueError):
            pass

        if len(out_fields) == 1 and not force_bundle:
            outputs: Connector | ConnectorBundle = next(iter(out_fields.values()))
        else:
            outputs = ConnectorBundle(out_fields)

        return ModuleInstanceHandle(
            name=str(name),
            symbol=str(cm.sym_name),
            inputs=dict(normalized_ports),
            outputs=outputs,
        )

    def instance(
        self,
        fn: Any,
        *,
        name: str,
        params: dict[str, Any] | None = None,
        module_name: str | None = None,
        **ports: Connector,
    ) -> Connector | ConnectorBundle:
        """Instantiate a specialized sub-module.

        Hard-break v3 contract:
        - inter-module port bindings must be `Connector`s
        - single output: return `Connector`
        - multiple outputs: return `ConnectorBundle`
        """

        return self.instance_handle(
            fn,
            name=name,
            params=params,
            module_name=module_name,
            **ports,
        ).outputs

    def byte_mem(
        self,
        clk: Signal,
        rst: Signal,
        *,
        raddr: Union[Wire, Reg, Signal],
        wvalid: Union[Wire, Reg, Signal],
        waddr: Union[Wire, Reg, Signal],
        wdata: Union[Wire, Reg, Signal],
        wstrb: Union[Wire, Reg, Signal],
        depth: int,
        name: str | None = None,
    ) -> Wire:
        def as_sig(v: Union[Wire, Reg, Signal]) -> Signal:
            if isinstance(v, Reg):
                return v.q.sig
            if isinstance(v, Wire):
                return v.sig
            return v

        rdata = super().byte_mem(
            clk,
            rst,
            as_sig(raddr),
            as_sig(wvalid),
            as_sig(waddr),
            as_sig(wdata),
            as_sig(wstrb),
            depth=depth,
            name=name,
        )
        return Wire(self, rdata)

    def sync_mem(
        self,
        clk: Signal,
        rst: Signal,
        *,
        ren: Union[Wire, Reg, Signal],
        raddr: Union[Wire, Reg, Signal],
        wvalid: Union[Wire, Reg, Signal],
        waddr: Union[Wire, Reg, Signal],
        wdata: Union[Wire, Reg, Signal],
        wstrb: Union[Wire, Reg, Signal],
        depth: int,
        name: str | None = None,
    ) -> Wire:
        def as_sig(v: Union[Wire, Reg, Signal]) -> Signal:
            if isinstance(v, Reg):
                return v.q.sig
            if isinstance(v, Wire):
                return v.sig
            return v

        rdata = super().sync_mem(
            clk,
            rst,
            as_sig(ren),
            as_sig(raddr),
            as_sig(wvalid),
            as_sig(waddr),
            as_sig(wdata),
            as_sig(wstrb),
            depth=depth,
            name=name,
        )
        return Wire(self, rdata)

    def sync_mem_dp(
        self,
        clk: Signal,
        rst: Signal,
        *,
        ren0: Union[Wire, Reg, Signal],
        raddr0: Union[Wire, Reg, Signal],
        ren1: Union[Wire, Reg, Signal],
        raddr1: Union[Wire, Reg, Signal],
        wvalid: Union[Wire, Reg, Signal],
        waddr: Union[Wire, Reg, Signal],
        wdata: Union[Wire, Reg, Signal],
        wstrb: Union[Wire, Reg, Signal],
        depth: int,
        name: str | None = None,
    ) -> tuple[Wire, Wire]:
        def as_sig(v: Union[Wire, Reg, Signal]) -> Signal:
            if isinstance(v, Reg):
                return v.q.sig
            if isinstance(v, Wire):
                return v.sig
            return v

        rdata0, rdata1 = super().sync_mem_dp(
            clk,
            rst,
            as_sig(ren0),
            as_sig(raddr0),
            as_sig(ren1),
            as_sig(raddr1),
            as_sig(wvalid),
            as_sig(waddr),
            as_sig(wdata),
            as_sig(wstrb),
            depth=depth,
            name=name,
        )
        return Wire(self, rdata0), Wire(self, rdata1)

    def async_fifo(
        self,
        in_clk: Signal,
        in_rst: Signal,
        out_clk: Signal,
        out_rst: Signal,
        *,
        in_valid: Union[Wire, Reg, Signal],
        in_data: Union[Wire, Reg, Signal],
        out_ready: Union[Wire, Reg, Signal],
        depth: int,
    ) -> tuple[Wire, Wire, Wire]:
        def as_sig(v: Union[Wire, Reg, Signal]) -> Signal:
            if isinstance(v, Reg):
                return v.q.sig
            if isinstance(v, Wire):
                return v.sig
            return v

        in_ready, out_valid, out_data = super().async_fifo(
            in_clk,
            in_rst,
            out_clk,
            out_rst,
            as_sig(in_valid),
            as_sig(in_data),
            as_sig(out_ready),
            depth=depth,
        )
        return Wire(self, in_ready), Wire(self, out_valid), Wire(self, out_data)

    def cdc_sync(self, clk: Signal, rst: Signal, a: Union[Wire, Reg, Signal], *, stages: int | None = None) -> Wire:
        sig = a.q.sig if isinstance(a, Reg) else (a.sig if isinstance(a, Wire) else a)
        out = super().cdc_sync(clk, rst, sig, stages=stages)
        return Wire(self, out)

    def fifo(
        self,
        clk: Signal,
        rst: Signal,
        *,
        in_valid: Union[Wire, Reg, Signal],
        in_data: Union[Wire, Reg, Signal],
        out_ready: Union[Wire, Reg, Signal],
        depth: int,
    ) -> tuple[Wire, Wire, Wire]:
        """Strict ready/valid FIFO (single-clock, prototype)."""

        def as_sig(v: Union[Wire, Reg, Signal]) -> Signal:
            if isinstance(v, Reg):
                return v.q.sig
            if isinstance(v, Wire):
                return v.sig
            return v

        in_ready, out_valid, out_data = super().fifo(
            clk,
            rst,
            as_sig(in_valid),
            as_sig(in_data),
            as_sig(out_ready),
            depth=depth,
        )
        return Wire(self, in_ready), Wire(self, out_valid), Wire(self, out_data)

    def fifo_domain(
        self,
        domain: ClockDomain,
        *,
        in_valid: Union[Wire, Reg, Signal],
        in_data: Union[Wire, Reg, Signal],
        out_ready: Union[Wire, Reg, Signal],
        depth: int,
    ) -> tuple[Wire, Wire, Wire]:
        return self.fifo(domain.clk, domain.rst, in_valid=in_valid, in_data=in_data, out_ready=out_ready, depth=depth)

    def queue(
        self,
        name: str,
        *,
        clk: Signal | None = None,
        rst: Signal | None = None,
        domain: ClockDomain | None = None,
        width: int,
        depth: int,
    ) -> "Queue":
        if domain is not None:
            clk = domain.clk
            rst = domain.rst
        if clk is None or rst is None:
            raise TypeError("queue() requires either domain=... or both clk=... and rst=...")
        return Queue(self, name, clk=clk, rst=rst, width=width, depth=depth)


@dataclass(frozen=True)
class Vec:
    """A small fixed-length container of wires/regs for building pipelines."""

    elems: tuple[Union[Wire, Reg], ...]

    def __post_init__(self) -> None:
        if not self.elems:
            raise ValueError("Vec cannot be empty")

        m0 = self._module_of(self.elems[0])
        for e in self.elems[1:]:
            if self._module_of(e) is not m0:
                raise ValueError("Vec elements must belong to the same Circuit/Module")

    @staticmethod
    def _module_of(e: Union[Wire, Reg]) -> Module:
        if isinstance(e, Wire):
            return e.m
        return e.q.m

    @property
    def m(self) -> Module:
        return self._module_of(self.elems[0])

    def __len__(self) -> int:
        return len(self.elems)

    def __iter__(self) -> Iterator[Union[Wire, Reg]]:
        return iter(self.elems)

    @overload
    def __getitem__(self, idx: int) -> Union[Wire, Reg]: ...

    @overload
    def __getitem__(self, idx: slice) -> "Vec": ...

    def __getitem__(self, idx: int | slice) -> Union[Wire, Reg, "Vec"]:
        if isinstance(idx, slice):
            return Vec(self.elems[idx])
        return self.elems[int(idx)]

    def wires(self) -> tuple[Wire, ...]:
        out: list[Wire] = []
        for e in self.elems:
            out.append(e if isinstance(e, Wire) else e.q)
        return tuple(out)

    @property
    def total_width(self) -> int:
        return sum(w.width for w in self.wires())

    def pack(self) -> Wire:
        """Concatenate elements into a single bus wire (MSB-first).

        `Vec([a, b, c]).pack()` yields `{a, b, c}` in Verilog terms.
        """
        ws = self.wires()
        out_w = self.total_width
        if out_w <= 0:
            raise ValueError("cannot pack a zero-width Vec")

        m = ws[0].m
        concat = getattr(m, "concat", None)
        if callable(concat):
            return Wire(m, concat(*(w.sig for w in ws)))

        # Fallback: build packing from basic shifts + ors for minimal backends.
        if not isinstance(m, Circuit):
            raise TypeError("Vec.pack requires a Circuit/Module with a concat() builder")
        acc = m.const(0, width=out_w)
        lsb = 0
        for w in reversed(ws):
            part = w._zext(width=out_w)
            if lsb:
                part = part.shl(amount=lsb)
            acc = acc | part
            lsb += w.width
        return acc

    def unpack(self, packed: Wire) -> "Vec":
        """Extract elements from a packed bus (inverse of pack())."""
        ws = self.wires()
        if packed.width != self.total_width:
            raise ValueError(f"unpack width mismatch: got i{packed.width}, expected i{self.total_width}")

        parts_rev: list[Wire] = []
        lsb = 0
        for w in reversed(ws):
            parts_rev.append(packed.slice(lsb=lsb, width=w.width))
            lsb += w.width
        return Vec(tuple(reversed(parts_rev)))

    def regs_domain(
        self,
        domain: ClockDomain,
        en: Union[Wire, Signal, int],
        init: Union[Wire, Signal, int, LiteralValue] = 0,
    ) -> "Vec":
        """Create a register per element and return a Vec of Regs."""
        ws = self.wires()
        m = ws[0].m
        if not isinstance(m, Circuit):
            raise TypeError("regs_domain requires elements to belong to a Circuit")
        regs: list[Reg] = []
        for w in ws:
            regs.append(m.reg_domain(domain, en, w, init))
        return Vec(tuple(regs))


@dataclass(frozen=True)
class Bundle:
    """A small named container (like a Verilog struct/bundle).

    Intended syntax:
      b = m.bundle(a=a, b=b)
      x = b["a"]
      packed = b.pack()
    """

    fields: dict[str, Union[Wire, Reg]]

    def __post_init__(self) -> None:
        if not self.fields:
            return
        # Ensure all elements come from the same Module.
        vals = list(self.fields.values())
        m0 = Vec._module_of(vals[0])
        for v in vals[1:]:
            mv = Vec._module_of(v)
            if mv is not m0:
                raise ValueError("Bundle fields must belong to the same Circuit/Module")

    def __getitem__(self, key: str) -> Union[Wire, Reg]:
        return self.fields[str(key)]

    def items(self) -> Iterable[tuple[str, Union[Wire, Reg]]]:
        return self.fields.items()

    def pack(self) -> Wire:
        if not self.fields:
            raise ValueError("cannot pack an empty Bundle")
        elems = tuple(self.fields.values())
        return Vec(elems).pack()

    def unpack(self, packed: Wire) -> "Bundle":
        """Extract fields from a packed bus (inverse of pack())."""
        if not self.fields:
            raise ValueError("cannot unpack into an empty Bundle")
        elems = tuple(self.fields.values())
        vec = Vec(elems)
        parts = vec.unpack(packed)
        out: dict[str, Union[Wire, Reg]] = {}
        for (k, _), v in zip(self.fields.items(), parts.elems):
            out[k] = v
        return Bundle(out)


@dataclass(frozen=True)
class Pop:
    valid: Wire
    data: Wire
    fire: Wire


class Queue:
    """Queue-like wrapper over `pyc.fifo` (single-clock, strict ready/valid).

    Intended usage (event-ish):
      q = m.queue("q", domain=dom, width=8, depth=2)
      accepted = q.push(x, when=in_valid)
      p = q.pop(when=out_ready)
      # p.valid / p.data / p.fire
    """

    def __init__(self, m: Circuit, name: str, *, clk: Signal, rst: Signal, width: int, depth: int) -> None:
        self.m = m
        self.name = str(name)
        self.width = int(width)
        self.depth = int(depth)

        if self.width <= 0:
            raise ValueError("Queue width must be > 0")
        if self.depth <= 0:
            raise ValueError("Queue depth must be > 0")

        # Input placeholders driven by the high-level API (finalized before emit_mlir()).
        self._in_valid = m.named_wire(f"{self.name}__in_valid", width=1)
        self._in_data = m.named_wire(f"{self.name}__in_data", width=self.width)
        self._out_ready = m.named_wire(f"{self.name}__out_ready", width=1)

        # Underlying FIFO instance.
        in_ready, out_valid, out_data = m.fifo(clk, rst, in_valid=self._in_valid, in_data=self._in_data, out_ready=self._out_ready, depth=self.depth)
        self.in_ready = in_ready
        self.out_valid = out_valid
        self.out_data = out_data

        self._push_bound = False
        self._pop_bound = False
        self._push_valid_expr: Union[Wire, Reg, Signal, int, LiteralValue] = 0
        self._push_data_expr: Union[Wire, Reg, Signal, int, LiteralValue] = 0
        self._pop_ready_expr: Union[Wire, Reg, Signal, int, LiteralValue] = 0

        # Defer assigns so we can keep single-driver semantics while supporting a push/pop API.
        m.add_finalizer(self._finalize)

    def push(self, data: Union[Wire, Reg, Signal, int, LiteralValue], *, when: Union[Wire, Signal, int, LiteralValue] = 1) -> Wire:
        if self._push_bound:
            raise ValueError("Queue.push() may only be called once per Queue instance (prototype limitation)")
        self._push_bound = True
        self._push_valid_expr = when
        self._push_data_expr = data
        # Fire when valid && ready.
        w_when = self._coerce_i1(when, ctx="queue push when")
        return w_when & self.in_ready

    def pop(self, *, when: Union[Wire, Signal, int, LiteralValue] = 1) -> Pop:
        if self._pop_bound:
            raise ValueError("Queue.pop() may only be called once per Queue instance (prototype limitation)")
        self._pop_bound = True
        self._pop_ready_expr = when
        w_when = self._coerce_i1(when, ctx="queue pop when")
        fire = self.out_valid & w_when
        return Pop(valid=self.out_valid, data=self.out_data, fire=fire)

    def _finalize(self) -> None:
        # Defaults: drive inactive.
        m = self.m
        m.assign(self._in_valid, self._push_valid_expr)
        m.assign(self._in_data, self._push_data_expr)
        m.assign(self._out_ready, self._pop_ready_expr)

    def _coerce_i1(self, v: Union[Wire, Signal, int, LiteralValue], *, ctx: str) -> Wire:
        if isinstance(v, Wire):
            if v.m is not self.m:
                raise ValueError("cannot combine wires from different modules")
            if v.ty != "i1":
                raise TypeError(f"{ctx}: expected i1, got {v.ty}")
            return v
        if isinstance(v, Signal):
            if v.ty != "i1":
                raise TypeError(f"{ctx}: expected i1, got {v.ty}")
            return Wire(self.m, v)
        if isinstance(v, int):
            return self.m.const(int(v), width=1)
        if isinstance(v, LiteralValue):
            lit_w, lit_signed = _coerce_literal_width(v, ctx_width=1, ctx_signed=False)
            w = Wire(self.m, Module.const(self.m, int(v.value), width=lit_w), signed=lit_signed)
            if w.ty != "i1":
                raise TypeError(f"{ctx}: expected i1 literal, got {w.ty}")
            return w
        raise TypeError(f"{ctx}: expected Wire/Signal/int, got {type(v).__name__}")


def cat(*elems: Union[Wire, Reg, int, LiteralValue]) -> Wire:
    """Concatenate wires/regs into a packed bus (MSB-first).

    Convenience wrapper so you can write:
      `bus = cat(a, b, c)`

    Equivalent to:
      `bus = m.cat(a, b, c)` (when all values belong to the same Circuit).
    """
    if not elems:
        raise ValueError("cat() requires at least one element")

    owner: Module | None = None
    for e in elems:
        if isinstance(e, Wire):
            owner = e.m
            break
        if isinstance(e, Reg):
            owner = e.q.m
            break
    if owner is None:
        raise TypeError("cat() requires at least one Wire/Reg element to establish module ownership")

    ws: list[Union[Wire, Reg]] = []
    for e in elems:
        if isinstance(e, (Wire, Reg)):
            ws.append(e)
            continue
        if isinstance(e, LiteralValue):
            lit_w, lit_signed = _coerce_literal_width(e, ctx_width=e.width, ctx_signed=e.signed)
            ws.append(Wire(owner, Module.const(owner, int(e.value), width=lit_w), signed=lit_signed))
            continue
        if isinstance(e, int):
            w = infer_literal_width(int(e), signed=(int(e) < 0))
            if isinstance(owner, Circuit):
                ws.append(owner.const(int(e), width=w))
            else:
                ws.append(Wire(owner, Module.const(owner, int(e), width=w), signed=(int(e) < 0)))
            continue
        raise TypeError(f"cat() element must be Wire/Reg/int/literal, got {type(e).__name__}")
    return Vec(tuple(ws)).pack()




def mux(*_args: Any, **_kwargs: Any) -> Wire:
    raise TypeError("mux() was removed from pyCircuit; use `true_v if cond else false_v` in JIT-compiled design code")


@overload
def unsigned(v: Wire) -> Wire:
    ...


@overload
def unsigned(v: Reg) -> Wire:
    ...


def unsigned(v: Wire | Reg) -> Wire:
    """Return the unsigned view of a hardware value."""
    if isinstance(v, Reg):
        return v.q.as_unsigned()
    if isinstance(v, Wire):
        return v.as_unsigned()
    raise TypeError(f"unsigned() expects Wire/Reg, got {type(v).__name__}")


@overload
def signed(v: Wire) -> Wire:
    ...


@overload
def signed(v: Reg) -> Wire:
    ...


def signed(v: Wire | Reg) -> Wire:
    """Return the signed view of a hardware value."""
    if isinstance(v, Reg):
        return v.q.as_signed()
    if isinstance(v, Wire):
        return v.as_signed()
    raise TypeError(f"signed() expects Wire/Reg, got {type(v).__name__}")
