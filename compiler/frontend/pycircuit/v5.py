"""PyCircuit V5 cycle-aware frontend (tutorial + Cycle-Aware API).

Maps documented grammar onto the existing Circuit/Wire MLIR builder. Library and
top-level designs should use CycleAwareCircuit / CycleAwareDomain and
compile_cycle_aware() instead of @module + compile().
"""

from __future__ import annotations

import ast
from collections.abc import Callable, Iterable, Iterator, Mapping
from contextlib import contextmanager
from dataclasses import dataclass, field
import inspect
import textwrap
import threading
from typing import Any, TypeVar, Union

from .dsl import Signal
from .hw import Circuit, ClockDomain, Reg, Wire
from .literals import LiteralValue, infer_literal_width
from .tb import Tb as _Tb

F = TypeVar("F", bound=Callable[..., Any])

_tls = threading.local()


def _current_domain() -> "CycleAwareDomain | None":
    return getattr(_tls, "domain", None)


def _set_current_domain(d: "CycleAwareDomain | None") -> None:
    _tls.domain = d


@dataclass
class _ModuleCtx:
    owner: "pyc_CircuitModule"
    inputs: list[Any]
    description: str
    outputs: list[Any] = field(default_factory=list)


class CycleAwareCircuit(Circuit):
    """V5 top-level builder; extends Circuit so m.out / m.cat / emit_mlir work unchanged."""

    def create_domain(
        self, name: str, *, frequency_desc: str = "", reset_active_high: bool = False
    ) -> "CycleAwareDomain":
        _ = (frequency_desc, reset_active_high)
        return CycleAwareDomain(self, str(name))

    def const_signal(self, value: int, width: int, domain: "CycleAwareDomain") -> Wire:
        return domain.create_const(int(value), width=int(width))

    def input_signal(self, name: str, width: int, domain: "CycleAwareDomain") -> Wire:
        return domain.create_signal(str(name), width=int(width))


class CycleAwareDomain:
    """Clock domain with logical occurrence index (tutorial: next/prev/push/pop/cycle)."""

    def __init__(self, circuit: Circuit, domain_name: str) -> None:
        self._m = circuit
        self._name = str(domain_name)
        self._cd = _clock_domain_ports(circuit, self._name)
        self._occurrence = 0
        self._stack: list[int] = []
        self._delay_serial = 0
        self._reg_serial = 0

    @property
    def clock_domain(self) -> ClockDomain:
        """Underlying clk/rst pair for m.out(..., domain=...)."""
        return self._cd

    @property
    def circuit(self) -> Circuit:
        return self._m

    def create_reset(self) -> Wire:
        return Wire(self._m, self._cd.rst)

    def create_signal(self, port_name: str, *, width: int) -> Wire:
        return self._m.input(str(port_name), width=int(width))

    def create_const(self, value: int, *, width: int, name: str = "") -> Wire:
        _ = name
        return self._m.const(int(value), width=int(width))

    def next(self) -> None:
        self._occurrence += 1

    def prev(self) -> None:
        self._occurrence -= 1

    def push(self) -> None:
        self._stack.append(self._occurrence)

    def pop(self) -> None:
        if not self._stack:
            raise RuntimeError("clock_domain.pop() without matching push()")
        self._occurrence = self._stack.pop()

    @property
    def cycle_index(self) -> int:
        return self._occurrence

    def cycle(
        self,
        sig: Union[Wire, Reg, "CycleAwareSignal"],
        reset_value: int | None = None,
        name: str = "",
    ) -> Wire:
        """Single-stage register (DFF); output is one logical cycle after the input value."""
        w = _as_wire(self._m, sig)
        width = w.width
        init = 0 if reset_value is None else int(reset_value)
        reg_name = str(name).strip() or f"_v5_reg_{self._reg_serial}"
        self._reg_serial += 1
        full = self._m.scoped_name(reg_name)
        r = self._m.out(full, domain=self._cd, width=width, init=init)
        r.set(w)
        return r.q

    def state(
        self,
        *,
        width: int,
        reset_value: int = 0,
        name: str = "",
    ) -> "StateSignal":
        """Declare a feedback state variable (register whose D depends on Q).

        Returns a :class:`StateSignal` that behaves like a ``CycleAwareSignal``
        (read its current value, use in expressions) and also supports
        ``.set(next_val)`` to close the feedback loop.

        Typical pattern::

            # Cycle 0: declare state and read current value
            counter = domain.state(width=8, reset_value=0, name="cnt")

            domain.next()  # → Cycle 1

            # Cycle 1: conditionally update
            counter.set(mux(enable, counter + 1, counter))
        """
        reg_name = str(name).strip() or f"_v5_reg_{self._reg_serial}"
        self._reg_serial += 1
        full = self._m.scoped_name(reg_name)
        reg = self._m.out(
            full, domain=self._cd, width=int(width), init=int(reset_value)
        )
        return StateSignal(self, reg, self._occurrence)

    def delay_to(self, w: Wire, *, from_cycle: int, to_cycle: int, width: int) -> Wire:
        """Insert (to_cycle - from_cycle) register stages for automatic cycle balancing."""
        if to_cycle <= from_cycle:
            return w
        d = to_cycle - from_cycle
        cur: Wire = w
        for _ in range(d):
            self._delay_serial += 1
            nm = f"_v5_bal_{self._delay_serial}"
            r = self._m.out(
                self._m.scoped_name(nm), domain=self._cd, width=width, init=0
            )
            r.set(cur)
            cur = r.q
        return cur


def _clock_domain_ports(m: Circuit, name: str) -> ClockDomain:
    if name == "clk":
        return ClockDomain(clk=m.clock("clk"), rst=m.reset("rst"))
    return m.domain(name)


def _as_wire(m: Circuit, sig: Union[Wire, Reg, "CycleAwareSignal", Signal]) -> Wire:
    if isinstance(sig, CycleAwareSignal):
        return sig.wire
    if isinstance(sig, Reg):
        return sig.q
    if isinstance(sig, Wire):
        return sig
    if isinstance(sig, Signal):
        return Wire(m, sig)
    raise TypeError(
        f"expected Wire/Reg/CycleAwareSignal/Signal, got {type(sig).__name__}"
    )


class StateSignal:
    """Feedback register exposed as a cycle-aware value with deferred ``.set()``.

    Created by ``domain.state()``.  Read it like any ``CycleAwareSignal``;
    after ``domain.next()``, call ``.set(next_val)`` to close the feedback loop.
    """

    __slots__ = ("_domain", "_reg", "_cas")

    def __init__(self, domain: "CycleAwareDomain", reg: Reg, cycle: int) -> None:
        self._domain = domain
        self._reg = reg
        self._cas = CycleAwareSignal(domain, reg.out(), cycle)

    def _current_view(self) -> "CycleAwareSignal":
        # A state register's Q is readable at every later logical occurrence
        # without introducing a physical balance register.
        return CycleAwareSignal(self._domain, self._reg.out(), self._domain.cycle_index)

    def set(
        self,
        next_val: "Wire | Reg | CycleAwareSignal | StateSignal",
        *,
        when: "Wire | Reg | CycleAwareSignal | StateSignal | None" = None,
    ) -> None:
        """Connect the D input of the register (close the feedback loop)."""
        w = _to_wire(next_val)
        wh = _to_wire(when) if when is not None else None
        if wh is not None:
            self._reg.set(w, when=wh)
        else:
            self._reg.set(w)

    @property
    def wire(self) -> Wire:
        return self._cas.wire

    @property
    def w(self) -> Wire:
        return self._cas.wire

    @property
    def sig(self) -> Signal:
        return self._cas.sig

    @property
    def cycle(self) -> int:
        return self._cas.cycle

    @property
    def domain(self) -> "CycleAwareDomain":
        return self._domain

    def __getattr__(self, name: str) -> object:
        return getattr(self._current_view(), name)

    def __add__(self, other: object) -> "CycleAwareSignal":
        return self._current_view().__add__(other)

    def __radd__(self, other: object) -> "CycleAwareSignal":
        return self._current_view().__radd__(other)

    def __sub__(self, other: object) -> "CycleAwareSignal":
        return self._current_view().__sub__(other)

    def __rsub__(self, other: object) -> "CycleAwareSignal":
        return self._current_view().__rsub__(other)

    def __mul__(self, other: object) -> "CycleAwareSignal":
        return self._current_view().__mul__(other)

    def __and__(self, other: object) -> "CycleAwareSignal":
        return self._current_view().__and__(other)

    def __or__(self, other: object) -> "CycleAwareSignal":
        if isinstance(other, str):
            return self._current_view()
        return self._current_view().__or__(other)

    def __xor__(self, other: object) -> "CycleAwareSignal":
        return self._current_view().__xor__(other)

    def __invert__(self) -> "CycleAwareSignal":
        return self._current_view().__invert__()

    def __eq__(self, other: object) -> "CycleAwareSignal":  # type: ignore[override]
        return self._current_view().__eq__(other)

    def __ne__(self, other: object) -> "CycleAwareSignal":  # type: ignore[override]
        return self._current_view().__ne__(other)

    def __lt__(self, other: object) -> "CycleAwareSignal":
        return self._current_view().__lt__(other)

    def __gt__(self, other: object) -> "CycleAwareSignal":
        return self._current_view().__gt__(other)

    def __le__(self, other: object) -> "CycleAwareSignal":
        return self._current_view().__le__(other)

    def __ge__(self, other: object) -> "CycleAwareSignal":
        return self._current_view().__ge__(other)

    def __getitem__(self, idx: int | slice) -> "CycleAwareSignal":
        return self._current_view().__getitem__(idx)

    def __repr__(self) -> str:
        return f"StateSignal({self._cas.wire}, cycle={self._cas.cycle})"


def _to_wire(v: "Wire | Reg | CycleAwareSignal | StateSignal") -> Wire:
    if isinstance(v, StateSignal):
        return v.wire
    if isinstance(v, CycleAwareSignal):
        return v.wire
    if isinstance(v, Reg):
        return v.q
    if isinstance(v, Wire):
        return v
    raise TypeError(
        f"expected Wire/Reg/CycleAwareSignal/StateSignal, got {type(v).__name__}"
    )


class CycleAwareSignal:
    """Value with logical cycle tag; operators align by delaying earlier operands."""

    __slots__ = ("_domain", "_w", "_cycle")

    def __init__(self, domain: CycleAwareDomain, wire: Wire, cycle: int) -> None:
        if wire.m is not domain._m:
            raise ValueError("Wire must belong to the same circuit as the domain")
        self._domain = domain
        self._w = wire
        self._cycle = int(cycle)

    @property
    def wire(self) -> Wire:
        return self._w

    @property
    def w(self) -> Wire:
        return self._w

    @property
    def cycle(self) -> int:
        return self._cycle

    @property
    def domain(self) -> CycleAwareDomain:
        return self._domain

    @property
    def sig(self) -> Signal:
        return self._w.sig

    @property
    def name(self) -> str:
        return str(self._w)

    @property
    def signed(self) -> bool:
        return bool(self._w.signed)

    def named(self, name: str) -> "CycleAwareSignal":
        nw = self._domain._m.named(self._w, str(name))
        return CycleAwareSignal(self._domain, nw, self._cycle)

    def _align(
        self, other: "CycleAwareSignal | Wire | Reg | int | LiteralValue"
    ) -> tuple[Wire, Wire, int]:
        if isinstance(other, CycleAwareSignal):
            if other._domain is not self._domain:
                raise ValueError("CycleAwareSignal operands must share the same domain")
            oc = other._cycle
            ow = other._w
        elif isinstance(other, (Wire, Reg)):
            ow = other.q if isinstance(other, Reg) else other
            oc = self._domain.cycle_index
        elif isinstance(other, int):
            ow = self._domain._m.const(
                other, width=max(1, infer_literal_width(other, signed=other < 0))
            )
            oc = self._domain.cycle_index
        elif isinstance(other, LiteralValue):
            lit_w = (
                other.width
                if other.width is not None
                else infer_literal_width(int(other.value), signed=bool(other.signed))
            )
            ow = self._domain._m.const(int(other.value), width=int(lit_w))
            oc = self._domain.cycle_index
        else:
            raise TypeError(f"unsupported operand: {type(other).__name__}")
        mx = max(self._cycle, oc)
        aw = self._domain.delay_to(
            self._w, from_cycle=self._cycle, to_cycle=mx, width=self._w.width
        )
        bw = self._domain.delay_to(ow, from_cycle=oc, to_cycle=mx, width=ow.width)
        a2, b2 = _promote_pair(self._domain._m, aw, bw)
        return a2, b2, mx

    def __add__(self, other: object) -> "CycleAwareSignal":
        a, b, c = self._align(other)  # type: ignore[arg-type]
        return CycleAwareSignal(self._domain, a + b, c)

    def __radd__(self, other: object) -> "CycleAwareSignal":
        return self.__add__(other)

    def __sub__(self, other: object) -> "CycleAwareSignal":
        a, b, c = self._align(other)  # type: ignore[arg-type]
        return CycleAwareSignal(self._domain, a - b, c)

    def __rsub__(self, other: object) -> "CycleAwareSignal":
        a, b, c = self._align(other)  # type: ignore[arg-type]
        return CycleAwareSignal(self._domain, b - a, c)

    def __mul__(self, other: object) -> "CycleAwareSignal":
        a, b, c = self._align(other)  # type: ignore[arg-type]
        return CycleAwareSignal(self._domain, a * b, c)

    def __and__(self, other: object) -> "CycleAwareSignal":
        a, b, c = self._align(other)  # type: ignore[arg-type]
        return CycleAwareSignal(self._domain, a & b, c)

    def __or__(self, other: object) -> "CycleAwareSignal":  # type: ignore[override]
        if isinstance(other, str):
            _ = other
            return self
        a, b, c = self._align(other)  # type: ignore[arg-type]
        return CycleAwareSignal(self._domain, a | b, c)

    def __xor__(self, other: object) -> "CycleAwareSignal":
        a, b, c = self._align(other)  # type: ignore[arg-type]
        return CycleAwareSignal(self._domain, a ^ b, c)

    def __invert__(self) -> "CycleAwareSignal":
        return CycleAwareSignal(self._domain, ~self._w, self._cycle)

    def __eq__(self, other: object) -> "CycleAwareSignal":  # type: ignore[override]
        a, b, c = self._align(other)  # type: ignore[arg-type]
        return CycleAwareSignal(self._domain, a == b, c)

    def __ne__(self, other: object) -> "CycleAwareSignal":  # type: ignore[override]
        a, b, c = self._align(other)  # type: ignore[arg-type]
        return CycleAwareSignal(self._domain, a != b, c)

    def __lt__(self, other: object) -> "CycleAwareSignal":
        a, b, c = self._align(other)  # type: ignore[arg-type]
        return CycleAwareSignal(self._domain, a < b, c)

    def __gt__(self, other: object) -> "CycleAwareSignal":
        a, b, c = self._align(other)  # type: ignore[arg-type]
        return CycleAwareSignal(self._domain, a > b, c)

    def __le__(self, other: object) -> "CycleAwareSignal":
        a, b, c = self._align(other)  # type: ignore[arg-type]
        return CycleAwareSignal(self._domain, a <= b, c)

    def __ge__(self, other: object) -> "CycleAwareSignal":
        a, b, c = self._align(other)  # type: ignore[arg-type]
        return CycleAwareSignal(self._domain, a >= b, c)

    def eq(self, other: object) -> "CycleAwareSignal":
        return self.__eq__(other)

    def lt(self, other: object) -> "CycleAwareSignal":
        return self.__lt__(other)

    def gt(self, other: object) -> "CycleAwareSignal":
        return self.__gt__(other)

    def le(self, other: object) -> "CycleAwareSignal":
        return self.__le__(other)

    def ge(self, other: object) -> "CycleAwareSignal":
        return self.__ge__(other)

    def trunc(self, width: int) -> "CycleAwareSignal":
        return CycleAwareSignal(
            self._domain, self._w.trunc(width=int(width)), self._cycle
        )

    def zext(self, width: int) -> "CycleAwareSignal":
        return CycleAwareSignal(
            self._domain, self._w.zext(width=int(width)), self._cycle
        )

    def sext(self, width: int) -> "CycleAwareSignal":
        return CycleAwareSignal(
            self._domain, self._w.sext(width=int(width)), self._cycle
        )

    def slice(self, high: int, low: int) -> "CycleAwareSignal":
        lo = int(low)
        hi = int(high)
        return CycleAwareSignal(self._domain, self._w[lo : hi + 1], self._cycle)

    def select(self, true_val: object, false_val: object) -> "CycleAwareSignal":
        return mux(self, true_val, false_val)

    def as_signed(self) -> "CycleAwareSignal":
        return CycleAwareSignal(
            self._domain, Wire(self._domain._m, self._w.sig, signed=True), self._cycle
        )

    def as_unsigned(self) -> "CycleAwareSignal":
        return CycleAwareSignal(
            self._domain, Wire(self._domain._m, self._w.sig, signed=False), self._cycle
        )

    def __getitem__(self, idx: int | slice) -> "CycleAwareSignal":
        return CycleAwareSignal(self._domain, self._w[idx], self._cycle)


def _promote_pair(m: Circuit, a: Wire, b: Wire) -> tuple[Wire, Wire]:
    if a.width == b.width:
        return a, b
    out_w = max(a.width, b.width)
    if a.width < out_w:
        a = a._sext(width=out_w) if a.signed else a._zext(width=out_w)
    if b.width < out_w:
        b = b._sext(width=out_w) if b.signed else b._zext(width=out_w)
    return a, b


def _is_cas(v: object) -> bool:
    return isinstance(v, (CycleAwareSignal, StateSignal))


def mux(
    cond: Union[Wire, Reg, CycleAwareSignal, StateSignal],
    a: Union[Wire, Reg, CycleAwareSignal, StateSignal, int, LiteralValue],
    b: Union[Wire, Reg, CycleAwareSignal, StateSignal, int, LiteralValue],
) -> Union[Wire, CycleAwareSignal]:
    if _is_cas(cond) or _is_cas(a) or _is_cas(b):
        c2 = cond._cas if isinstance(cond, StateSignal) else cond
        a2 = a._cas if isinstance(a, StateSignal) else a
        b2 = b._cas if isinstance(b, StateSignal) else b
        return _mux_cycle_aware(c2, a2, b2)
    return _mux_wire(cond, a, b)


def _mux_wire(
    cond: Union[Wire, Reg],
    a: Union[Wire, Reg, int, LiteralValue],
    b: Union[Wire, Reg, int, LiteralValue],
) -> Wire:
    c = cond.q if isinstance(cond, Reg) else cond
    m = c.m
    if not isinstance(m, Circuit):
        raise TypeError("mux(cond, ...) requires wires from a Circuit")

    def as_wire(v: Union[Wire, Reg, int, LiteralValue], *, ctx_w: int | None) -> Wire:
        if isinstance(v, Reg):
            return v.q
        if isinstance(v, Wire):
            return v
        if isinstance(v, LiteralValue):
            if v.width is not None:
                lit_w = int(v.width)
            else:
                lit_w = infer_literal_width(
                    int(v.value),
                    signed=(
                        bool(v.signed) if v.signed is not None else int(v.value) < 0
                    ),
                )
            return m.const(int(v.value), width=int(lit_w))
        if isinstance(v, int):
            w = (
                ctx_w
                if ctx_w is not None
                else max(1, infer_literal_width(int(v), signed=(int(v) < 0)))
            )
            return m.const(int(v), width=int(w))
        raise TypeError(f"mux: unsupported branch type {type(v).__name__}")

    aw = as_wire(a, ctx_w=c.width)
    bw = as_wire(b, ctx_w=c.width)
    aw, bw = _promote_pair(m, aw, bw)
    if c.ty != "i1":
        raise TypeError("mux condition must be i1")
    return c._select_internal(aw, bw)


def _mux_cycle_aware(
    cond: Union[Wire, Reg, CycleAwareSignal],
    a: Union[Wire, Reg, CycleAwareSignal, int, LiteralValue],
    b: Union[Wire, Reg, CycleAwareSignal, int, LiteralValue],
) -> CycleAwareSignal:
    def pick_dom() -> CycleAwareDomain:
        for x in (cond, a, b):
            if isinstance(x, CycleAwareSignal):
                return x._domain
        raise RuntimeError("internal: mux cycle-aware without CycleAwareSignal")

    dom = pick_dom()
    m = dom._m

    def to_cas(
        x: Union[Wire, Reg, CycleAwareSignal, int, LiteralValue]
    ) -> CycleAwareSignal:
        if isinstance(x, CycleAwareSignal):
            return x
        if isinstance(x, Reg):
            return CycleAwareSignal(dom, x.q, dom.cycle_index)
        if isinstance(x, Wire):
            return CycleAwareSignal(dom, x, dom.cycle_index)
        if isinstance(x, int):
            w = m.const(x, width=max(1, infer_literal_width(x, signed=x < 0)))
            return CycleAwareSignal(dom, w, dom.cycle_index)
        if isinstance(x, LiteralValue):
            lw = (
                x.width
                if x.width is not None
                else infer_literal_width(int(x.value), signed=bool(x.signed))
            )
            w = m.const(int(x.value), width=int(lw))
            return CycleAwareSignal(dom, w, dom.cycle_index)
        raise TypeError(f"mux: unsupported value {type(x).__name__}")

    c_cas = to_cas(cond) if not isinstance(cond, CycleAwareSignal) else cond
    ca = to_cas(a)
    cb = to_cas(b)
    cc = c_cas._cycle
    cw = c_cas._w
    mx = max(cc, ca._cycle, cb._cycle)
    cw2 = dom.delay_to(cw, from_cycle=cc, to_cycle=mx, width=cw.width)
    aw = dom.delay_to(ca.wire, from_cycle=ca._cycle, to_cycle=mx, width=ca.wire.width)
    bw = dom.delay_to(cb.wire, from_cycle=cb._cycle, to_cycle=mx, width=cb.wire.width)
    aw, bw = _promote_pair(m, aw, bw)
    if cw2.ty != "i1":
        raise TypeError("mux condition must be i1")
    out_w = cw2._select_internal(aw, bw)
    return CycleAwareSignal(dom, out_w, mx)


def cas(
    domain: CycleAwareDomain, w: Wire, *, cycle: int | None = None
) -> CycleAwareSignal:
    c = domain.cycle_index if cycle is None else int(cycle)
    return CycleAwareSignal(domain, w, c)


def _strip_domain_for_jit(
    fn: Callable[..., Any], *, domain_name: str
) -> Callable[..., Any]:
    """Drop the ``domain`` parameter for JIT and prepend ``domain = m.create_domain(...)``."""
    try:
        source = textwrap.dedent(inspect.getsource(fn))
    except OSError as e:
        raise TypeError(
            "compile_cycle_aware(fn): need inspectable source for JIT; use eager=True or define fn in a .py file"
        ) from e
    tree = ast.parse(source)
    name = getattr(fn, "__name__", None)
    if not isinstance(name, str) or not name:
        raise TypeError("compile_cycle_aware(fn): function must have a __name__")
    fdef: ast.FunctionDef | None = None
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name == name:
            fdef = node
            break
    if fdef is None:
        raise TypeError(
            f"compile_cycle_aware: could not find def {name!r} in source of {fn!r}"
        )
    pos = fdef.args.args
    if len(pos) < 2:
        raise TypeError(
            "compile_cycle_aware(fn): source must declare at least (m, domain, ...)"
        )
    m_arg = pos[0].arg
    if pos[1].arg != "domain":
        raise TypeError(
            "compile_cycle_aware(fn): second parameter must be named 'domain' for JIT (or use eager=True)"
        )
    fdef.args.args.pop(1)
    prelude = ast.Assign(
        targets=[ast.Name(id="domain", ctx=ast.Store())],
        value=ast.Call(
            func=ast.Attribute(
                value=ast.Name(id=m_arg, ctx=ast.Load()),
                attr="create_domain",
                ctx=ast.Load(),
            ),
            args=[ast.Constant(value=str(domain_name))],
            keywords=[],
        ),
    )
    fdef.body.insert(0, prelude)
    ast.fix_missing_locations(fdef)
    new_src = ast.unparse(fdef) + "\n"
    globs = dict(fn.__globals__)
    exec(compile(ast.parse(new_src), "<pycircuit_v5_strip_domain>", "exec"), globs)
    out: Callable[..., Any] = globs[name]
    out.__pycircuit_jit_source__ = new_src
    out.__pycircuit_jit_start_line__ = 1
    out.__pycircuit_jit_source_file__ = "<pycircuit_v5_strip_domain>"
    setattr(out, "__pycircuit_kind__", "module")
    setattr(out, "__pycircuit_inline__", False)
    for attr in ("__pycircuit_name__", "__pycircuit_module_name__"):
        if hasattr(fn, attr):
            setattr(out, attr, getattr(fn, attr))
    return out


def compile_cycle_aware(
    fn: F,
    *,
    name: str | None = None,
    domain_name: str = "clk",
    eager: bool = False,
    structural: bool | None = None,
    value_params: Mapping[str, str] | dict[str, str] | None = None,
    **jit_params: Any,
) -> Any:
    """Compile or execute ``fn(m, domain, **kwargs)``.

    By default this lowers through :func:`pycircuit.jit.compile`: a tiny ``@module``-style
    wrapper instantiates :class:`CycleAwareDomain` from ``domain_name`` and calls ``fn``.
    Pass ``eager=True`` to run ``fn`` directly in Python and get a
    :class:`CycleAwareCircuit` (no JIT; no ``if Wire`` / JIT control flow).
    """
    if eager:
        circuit_name = (
            name
            if isinstance(name, str) and name.strip()
            else getattr(fn, "__name__", "design") or "design"
        )
        m = CycleAwareCircuit(str(circuit_name))
        dom = m.create_domain(str(domain_name))
        out = fn(m, dom, **jit_params)
        if out is not None:
            _register_implicit_outputs(m, out)
        return m

    from .jit import compile as jit_compile

    if name is None or not str(name).strip():
        override = getattr(fn, "__pycircuit_name__", None)
        if isinstance(override, str) and override.strip():
            sym = override.strip()
        else:
            sym = getattr(fn, "__name__", "Top")
    else:
        sym = str(name).strip()

    struc = (
        bool(getattr(fn, "__pycircuit_emit_structural__", False))
        if structural is None
        else bool(structural)
    )

    if value_params is None:
        vp_raw = getattr(fn, "__pycircuit_value_params__", None)
        vp: dict[str, str] = dict(vp_raw) if isinstance(vp_raw, dict) else {}
    else:
        vp = dict(value_params)

    domain_n = str(domain_name)

    _jit_fn = _strip_domain_for_jit(fn, domain_name=domain_n)
    setattr(_jit_fn, "__pycircuit_module_name__", sym)
    setattr(_jit_fn, "__pycircuit_kind__", "module")
    setattr(_jit_fn, "__pycircuit_inline__", False)
    setattr(_jit_fn, "__pycircuit_emit_structural__", struc)
    setattr(_jit_fn, "__pycircuit_value_params__", vp)
    pn = getattr(fn, "__pycircuit_name__", None)
    if isinstance(pn, str) and pn.strip():
        setattr(_jit_fn, "__pycircuit_name__", pn.strip())
    else:
        setattr(_jit_fn, "__pycircuit_name__", sym)

    return jit_compile(_jit_fn, name=name, **jit_params)


def _register_implicit_outputs(m: Circuit, out: Any) -> None:
    if isinstance(out, CycleAwareSignal):
        m.output("result", out.wire)
        return
    if isinstance(out, Wire):
        m.output("result", out)
        return
    if isinstance(out, Reg):
        m.output("result", out.q)
        return
    if isinstance(out, tuple):
        for i, x in enumerate(out):
            _register_implicit_outputs_single(m, f"result{i}", x)
        return
    _register_implicit_outputs_single(m, "result", out)


def _register_implicit_outputs_single(m: Circuit, port: str, x: Any) -> None:
    if isinstance(x, CycleAwareSignal):
        m.output(port, x.wire)
    elif isinstance(x, Wire):
        m.output(port, x)
    elif isinstance(x, Reg):
        m.output(port, x.q)


class pyc_CircuitModule:
    """Tutorial-style module base (hierarchy + with self.module(...))."""

    def __init__(self, name: str, clock_domain: CycleAwareDomain) -> None:
        self.name = str(name)
        self.clock_domain = clock_domain
        self._m = clock_domain.circuit

    @property
    def circuit(self) -> CycleAwareCircuit:
        return self._m

    @contextmanager
    def module(
        self,
        *,
        inputs: list[Any] | None = None,
        description: str = "",
    ) -> Iterator[_ModuleCtx]:
        _ = description
        ctx = _ModuleCtx(self, list(inputs or []), description)
        prev = _current_domain()
        _set_current_domain(self.clock_domain)
        try:
            with self._m.scope(self.name):
                yield ctx
        finally:
            _set_current_domain(prev)
        for out in ctx.outputs:
            _ = out


# Tutorial aliases
pyc_ClockDomain = CycleAwareDomain
pyc_Signal = CycleAwareSignal


class pyc_CircuitLogger:
    """Minimal hierarchical text logger (tutorial compatibility)."""

    def __init__(self, filename: str, is_flatten: bool = False) -> None:
        self.filename = str(filename)
        self.is_flatten = bool(is_flatten)
        self._lines: list[str] = []

    def reset(self) -> None:
        self._lines.clear()

    def write_to_file(self) -> None:
        with open(self.filename, "w", encoding="utf-8") as f:
            f.write("\n".join(self._lines))


def log(value: Any) -> Any:
    return value


class _SignalSlice:
    def __init__(self, high: int, low: int) -> None:
        self.high = int(high)
        self.low = int(low)
        self.width = self.high - self.low + 1

    def __call__(self, *, value: Any = 0, name: str = "") -> CycleAwareSignal:
        dom = _current_domain()
        if dom is None:
            raise RuntimeError(
                "signal[...](...) requires an active pyc_CircuitModule.module() context"
            )
        w = _materialize_signal_value(dom, value, self.width, str(name))
        return CycleAwareSignal(dom, w, dom.cycle_index)


class _SignalMeta(type):
    def __getitem__(cls, item: Any) -> _SignalSlice:
        if isinstance(item, slice):
            if item.step not in (None, 1):
                raise ValueError("signal slice step must be 1")
            hi, lo = item.start, item.stop
            if hi is None or lo is None:
                raise ValueError("signal[h:l] requires both high and low")
            return _SignalSlice(int(hi), int(lo))
        if isinstance(item, str):
            part = item.split(":", 1)
            if len(part) != 2:
                raise ValueError('signal["h:l"] expects one ":"')
            return _SignalSlice(int(part[0].strip()), int(part[1].strip()))
        raise TypeError("signal[...] expects slice like [7:0] or string '7:0'")

    def __call__(cls, *, value: Any = 0, name: str = "") -> CycleAwareSignal:
        if cls is signal:
            return _signal_plain(value=value, name=name)
        return type.__call__(cls)


class signal(metaclass=_SignalMeta):
    """Tutorial: ``signal[7:0](value=0) | \"desc\"`` and ``signal(value=...)``."""


def _signal_plain(*, value: Any = 0, name: str = "") -> CycleAwareSignal:
    dom = _current_domain()
    if dom is None:
        raise RuntimeError(
            "signal(value=...) requires an active pyc_CircuitModule.module() context"
        )
    w = _materialize_signal_value(dom, value, None, str(name))
    return CycleAwareSignal(dom, w, dom.cycle_index)


def _materialize_signal_value(
    dom: CycleAwareDomain, value: Any, width: int | None, name: str
) -> Wire:
    m = dom._m
    if isinstance(value, int):
        w = (
            infer_literal_width(int(value), signed=(int(value) < 0))
            if width is None
            else int(width)
        )
        return m.const(int(value), width=w)
    if isinstance(value, str):
        base = str(value).strip()
        if base.isidentifier():
            guess = 8 if width is None else int(width)
            return m.input(base, width=guess)
        return m.named_wire(dom._m.scoped_name(name or "sig"), width=int(width or 8))
    if isinstance(value, Wire):
        return value
    raise TypeError(f"unsupported signal value: {type(value).__name__}")


# ---------------------------------------------------------------------------
# V5 Cycle-Aware Testbench wrapper
# ---------------------------------------------------------------------------


class CycleAwareTb:
    """V5 cycle-aware testbench wrapper.

    Wraps :class:`Tb` so that ``drive`` / ``expect`` / ``finish`` calls use the
    current cycle tracked by :meth:`next` instead of an explicit ``at=``
    parameter, mirroring ``domain.next()`` in design code.

    Usage inside a ``@testbench`` function::

        @testbench
        def tb(t: Tb) -> None:
            tb = CycleAwareTb(t)
            tb.clock("clk")
            tb.reset("rst", cycles_asserted=2, cycles_deasserted=1)
            tb.timeout(64)

            # --- cycle 0 ---
            tb.drive("enable", 1)
            tb.expect("count", 1)

            tb.next()  # --- cycle 1 ---
            tb.expect("count", 2)

            tb.finish()
    """

    __slots__ = ("_t", "_cycle")

    def __init__(self, t: _Tb) -> None:
        if not isinstance(t, _Tb):
            raise TypeError(
                f"CycleAwareTb requires a Tb instance, got {type(t).__name__}"
            )
        self._t = t
        self._cycle = 0

    # -- cycle management ---------------------------------------------------

    def next(self) -> None:
        """Advance to the next clock cycle (like ``domain.next()``)."""
        self._cycle += 1

    @property
    def cycle(self) -> int:
        """Current cycle index."""
        return self._cycle

    # -- setup (cycle-independent) ------------------------------------------

    def clock(self, port: str, **kw: Any) -> None:
        self._t.clock(port, **kw)

    def reset(self, port: str, **kw: Any) -> None:
        self._t.reset(port, **kw)

    def timeout(self, cycles: int) -> None:
        self._t.timeout(cycles)

    # -- stimulus / check (cycle-relative) ----------------------------------

    def drive(self, port: str, value: int | bool) -> None:
        """Drive *port* at the current cycle."""
        self._t.drive(port, value, at=self._cycle)

    def expect(
        self,
        port: str,
        value: int | bool,
        *,
        phase: str = "post",
        msg: str | None = None,
    ) -> None:
        """Check *port* at the current cycle."""
        self._t.expect(port, value, at=self._cycle, phase=phase, msg=msg)

    def finish(self, *, at: int | None = None) -> None:
        """End the simulation at the current cycle (or at an explicit cycle)."""
        self._t.finish(at=self._cycle if at is None else int(at))

    # -- print helpers ------------------------------------------------------

    def print(self, fmt: str, *, ports: Iterable[str] = ()) -> None:
        """Print at the current cycle."""
        self._t.print(fmt, at=self._cycle, ports=ports)

    def print_every(self, fmt: str, **kw: Any) -> None:
        self._t.print_every(fmt, **kw)

    # -- pass-through -------------------------------------------------------

    def sva_assert(self, expr: Any, **kw: Any) -> None:
        self._t.sva_assert(expr, **kw)

    def random(self, port: str, **kw: Any) -> None:
        self._t.random(port, **kw)
