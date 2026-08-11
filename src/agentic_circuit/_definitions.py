"""Immutable metadata for Python architecture definitions."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable, Literal, TypeAlias, TypeVar, overload


DefinitionKind: TypeAlias = Literal[
    "system",
    "module",
    "extern_module",
    "generated_module",
    "struct",
    "packet",
    "transaction",
    "protocol",
    "interface",
    "process",
]
F = TypeVar("F", bound=Callable[..., object])


@dataclass(frozen=True, slots=True)
class Definition:
    """A captured definition registered without executing its body."""

    kind: DefinitionKind
    function: Callable[..., object]
    qualified_name: str
    explicit_options: tuple[tuple[str, object], ...]


@overload
def _decorate(kind: DefinitionKind, function: F) -> Definition: ...


@overload
def _decorate(
    kind: DefinitionKind, function: None = None, **options: object
) -> Callable[[F], Definition]: ...


def _decorate(
    kind: DefinitionKind, function: F | None = None, **options: object
) -> Definition | Callable[[F], Definition]:
    def apply(target: F) -> Definition:
        return Definition(
            kind=kind,
            function=target,
            qualified_name=target.__qualname__,
            explicit_options=tuple(sorted(options.items())),
        )

    return apply(function) if function is not None else apply


def system(function: F | None = None, **options: object):
    return _decorate("system", function, **options)


def module(function: F | None = None, **options: object):
    return _decorate("module", function, **options)


def extern_module(function: F | None = None, **options: object):
    return _decorate("extern_module", function, **options)


def generated_module(function: F | None = None, **options: object):
    return _decorate("generated_module", function, **options)


def struct(function: F | None = None, **options: object):
    return _decorate("struct", function, **options)


def packet(function: F | None = None, **options: object):
    return _decorate("packet", function, **options)


def transaction(function: F | None = None, **options: object):
    return _decorate("transaction", function, **options)


def protocol(function: F | None = None, **options: object):
    return _decorate("protocol", function, **options)


def interface(function: F | None = None, **options: object):
    return _decorate("interface", function, **options)


def process(function: F | None = None, **options: object):
    return _decorate("process", function, **options)
