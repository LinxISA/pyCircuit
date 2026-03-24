from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class LiteralValue:
    """Hardware literal with optional explicit width/signedness metadata."""

    value: int
    width: int | None = None
    signed: bool | None = None

    def __post_init__(self) -> None:
        v = int(self.value)
        object.__setattr__(self, "value", v)
        if self.width is not None:
            w = int(self.width)
            if w <= 0:
                raise ValueError("literal width must be > 0")
            object.__setattr__(self, "width", w)
        if self.signed is not None:
            object.__setattr__(self, "signed", bool(self.signed))

    def with_context(self, *, width: int | None, signed: bool | None) -> "LiteralValue":
        return LiteralValue(
            value=self.value,
            width=self.width if self.width is not None else width,
            signed=self.signed if self.signed is not None else signed,
        )

    def __invert__(self) -> "LiteralValue":
        w = self.width if self.width is not None else infer_literal_width(self.value, signed=bool(self.signed))
        mask = (1 << w) - 1
        return LiteralValue(value=(~self.value) & mask, width=self.width, signed=self.signed)

    def _op_rhs(self, other: Any) -> int | type(NotImplemented):
        if isinstance(other, int):
            return int(other)
        if isinstance(other, LiteralValue):
            return int(other.value)
        return NotImplemented

    def __and__(self, other: Any) -> Any:
        rhs = self._op_rhs(other)
        if rhs is NotImplemented: return NotImplemented
        return LiteralValue(value=self.value & rhs, width=self.width, signed=self.signed)

    def __rand__(self, other: Any) -> Any:
        return self.__and__(other)

    def __or__(self, other: Any) -> Any:
        rhs = self._op_rhs(other)
        if rhs is NotImplemented: return NotImplemented
        return LiteralValue(value=self.value | rhs, width=self.width, signed=self.signed)

    def __ror__(self, other: Any) -> Any:
        return self.__or__(other)

    def __xor__(self, other: Any) -> Any:
        rhs = self._op_rhs(other)
        if rhs is NotImplemented: return NotImplemented
        return LiteralValue(value=self.value ^ rhs, width=self.width, signed=self.signed)

    def __rxor__(self, other: Any) -> Any:
        return self.__xor__(other)

    def __add__(self, other: Any) -> Any:
        rhs = self._op_rhs(other)
        if rhs is NotImplemented: return NotImplemented
        return LiteralValue(value=self.value + rhs, width=self.width, signed=self.signed)

    def __radd__(self, other: Any) -> Any:
        return self.__add__(other)

    def __sub__(self, other: Any) -> Any:
        rhs = self._op_rhs(other)
        if rhs is NotImplemented: return NotImplemented
        return LiteralValue(value=self.value - rhs, width=self.width, signed=self.signed)

    def __rsub__(self, other: Any) -> Any:
        rhs = self._op_rhs(other)
        if rhs is NotImplemented: return NotImplemented
        return LiteralValue(value=rhs - self.value, width=self.width, signed=self.signed)

    def __mul__(self, other: Any) -> Any:
        rhs = self._op_rhs(other)
        if rhs is NotImplemented: return NotImplemented
        return LiteralValue(value=self.value * rhs, width=self.width, signed=self.signed)

    def __rmul__(self, other: Any) -> Any:
        return self.__mul__(other)

    def __lshift__(self, other: Any) -> Any:
        rhs = self._op_rhs(other)
        if rhs is NotImplemented: return NotImplemented
        if rhs < 0: raise ValueError("shift count must be >= 0")
        return LiteralValue(value=self.value << rhs, width=self.width, signed=self.signed)

    def __rshift__(self, other: Any) -> Any:
        rhs = self._op_rhs(other)
        if rhs is NotImplemented: return NotImplemented
        if rhs < 0: raise ValueError("shift count must be >= 0")
        return LiteralValue(value=self.value >> rhs, width=self.width, signed=self.signed)

    def __getitem__(self, idx: slice | int) -> "LiteralValue":
        if isinstance(idx, slice):
            if idx.step is not None:
                raise TypeError("literal slicing does not support step")
            lsb = 0 if idx.start is None else int(idx.start)
            w = self.width if self.width is not None else infer_literal_width(self.value, signed=bool(self.signed))
            stop = w if idx.stop is None else int(idx.stop)
            if lsb < 0 or stop < 0:
                raise ValueError("literal slice indices must be >= 0")
            if stop < lsb:
                raise ValueError("literal slice stop must be >= start")
            width = stop - lsb
            if width <= 0:
                raise ValueError("literal slice width must be > 0")
            val = (self.value >> lsb) & ((1 << width) - 1)
            return LiteralValue(value=val, width=width, signed=False)
        else:
            bit = int(idx)
            if bit < 0:
                raise ValueError("literal bit index must be >= 0")
            val = (self.value >> bit) & 1
            return LiteralValue(value=val, width=1, signed=False)

    def __eq__(self, other: Any) -> bool:
        if isinstance(other, LiteralValue):
            return self.value == other.value and self.width == other.width and self.signed == other.signed
        if isinstance(other, int):
            return self.value == other
        return NotImplemented

    def __ne__(self, other: Any) -> bool:
        eq = self.__eq__(other)
        if eq is NotImplemented:
            return NotImplemented
        return not eq


def _infer_unsigned_width(v: int) -> int:
    if v < 0:
        raise ValueError("unsigned literal cannot be negative")
    return max(1, int(v).bit_length())


def _infer_signed_width(v: int) -> int:
    # Two's complement minimum width that can represent v.
    if v >= 0:
        return max(1, int(v).bit_length() + 1)
    return max(1, int((-v - 1).bit_length() + 1))


def infer_literal_width(value: int, *, signed: bool) -> int:
    return _infer_signed_width(value) if signed else _infer_unsigned_width(value)


def U(value: int) -> LiteralValue:
    return LiteralValue(value=int(value), width=None, signed=False)


def S(value: int) -> LiteralValue:
    return LiteralValue(value=int(value), width=None, signed=True)


def u(width: int, value: int) -> LiteralValue:
    return LiteralValue(value=int(value), width=int(width), signed=False)


def s(width: int, value: int) -> LiteralValue:
    return LiteralValue(value=int(value), width=int(width), signed=True)
