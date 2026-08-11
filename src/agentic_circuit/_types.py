"""Public annotation categories and frontend-only symbolic values."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Generic, Never, TypeVar


T = TypeVar("T")
P = TypeVar("P")
I = TypeVar("I")
R = TypeVar("R")


class Static(Generic[T]):
    """Mark an elaboration-time specialization parameter."""


class Flow(Generic[T, P]):
    """Describe a typed logical dataflow edge using protocol ``P``."""


class Endpoint(Generic[I, R]):
    """Describe interface ``I`` bound in role ``R``."""


class ResourceRef(Generic[T, R]):
    """Describe a typed resource capability bound in role ``R``."""


@dataclass(frozen=True, slots=True)
class SymbolicValue:
    """Frontend identity for an architecture value without a Python value."""

    stable_name: str
    annotation: object

    def _reject(self, operation: str) -> Never:
        raise TypeError(
            f"ACPY-STATIC-002: {self.stable_name!r} cannot be used for {operation}"
        )

    def __bool__(self) -> Never:
        return self._reject("truth testing")

    def __int__(self) -> Never:
        return self._reject("integer conversion")

    def __hash__(self) -> Never:
        return self._reject("hashing")

    def __iter__(self) -> Never:
        return self._reject("iteration")


def _test_symbolic(stable_name: str, annotation: object) -> SymbolicValue:
    """Create a symbolic value for contract tests without elaboration state."""

    return SymbolicValue(stable_name=stable_name, annotation=annotation)
