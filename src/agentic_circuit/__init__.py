"""Agentic Circuit's portable Python construction surface."""

from __future__ import annotations

from typing import Never

from ._definitions import (
    extern_module,
    generated_module,
    interface,
    module,
    packet,
    process,
    protocol,
    struct,
    system,
    transaction,
)
from ._resources import address_map, address_space, queue
from ._types import Endpoint, Flow, ResourceRef, Static


__all__ = (
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
    "scope",
    "array",
    "instances",
    "view",
    "queue",
    "ResourceRef",
    "address_space",
    "address_map",
    "Static",
    "Flow",
    "Endpoint",
)


def _not_implemented(primitive: str) -> Never:
    raise NotImplementedError(
        f"{primitive} is part of the v0.1 public surface but is not implemented yet"
    )


def scope(name: str) -> Never:
    return _not_implemented("scope")


def array(*values: object) -> Never:
    return _not_implemented("array")


def instances(*values: object) -> Never:
    return _not_implemented("instances")


def view(value: object, *selectors: object) -> Never:
    return _not_implemented("view")
