"""Agentic Circuit's portable Python construction surface."""

from __future__ import annotations

from pkgutil import extend_path
from typing import Never


__path__ = extend_path(__path__, __name__)

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
from ._jit import config, jit
from ._resources import address_map, address_space, queue
from ._types import (
    Endpoint,
    Flow,
    ResourceRef,
    Static,
    const,
    s8,
    s16,
    s32,
    s64,
    u1,
    u2,
    u4,
    u8,
    u16,
    u32,
    u64,
)


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
    "map",
    "set",
    "instances",
    "view",
    "queue",
    "ResourceRef",
    "address_space",
    "address_map",
    "Static",
    "Flow",
    "Endpoint",
    "source",
    "sink",
    "observe",
    "expect",
    "atomic",
    "compute",
    "config",
    "const",
    "jit",
    "route",
    "merge",
    "schedule",
    "engine",
    "reorder",
    "round_robin",
    "priority",
    "fork",
    "barrier",
    "table",
    "u1",
    "u2",
    "u4",
    "u8",
    "u16",
    "u32",
    "u64",
    "s8",
    "s16",
    "s32",
    "s64",
)


def _not_implemented(primitive: str) -> Never:
    raise NotImplementedError(
        f"{primitive} is part of the v0.2 public surface but is not implemented yet"
    )


def scope(name: str) -> Never:
    return _not_implemented("scope")


def array(*values: object) -> Never:
    return _not_implemented("array")


def map(*values: object) -> Never:
    return _not_implemented("map")


def set(*values: object) -> Never:
    return _not_implemented("set")


def instances(*values: object) -> Never:
    return _not_implemented("instances")


def view(value: object, *selectors: object) -> Never:
    return _not_implemented("view")


def source(payload: object, *, depth: int = 1, latency: int = 1) -> Never:
    return _not_implemented("source")


def sink(value: object) -> Never:
    return _not_implemented("sink")


def observe(value: object) -> Never:
    return _not_implemented("observe")


def expect(value: object, *, predicate: object, message: str) -> Never:
    return _not_implemented("expect")


def atomic() -> Never:
    return _not_implemented("atomic")


def compute(
    value: object,
    function: object,
    *,
    depth: int = 1,
    latency: int = 1,
) -> Never:
    return _not_implemented("compute")


round_robin = "round_robin"
priority = "priority"


def route(
    value: object,
    *,
    by: object,
    outputs: int,
    depth: int = 1,
    latency: int = 1,
) -> Never:
    return _not_implemented("route")


def merge(
    *values: object,
    policy: object = priority,
    depth: int = 1,
    latency: int = 1,
) -> Never:
    return _not_implemented("merge")


def schedule(
    value: object,
    *,
    by: object,
    waits_for: object,
    resource: object,
    cost: object,
    entries: int = 16,
    resources: int = 1,
    no_dependency: int = 0,
    depth: int = 1,
    latency: int = 1,
) -> Never:
    return _not_implemented("schedule")


def engine(
    value: object,
    *,
    cost: object,
    lanes: int = 1,
    depth: int = 1,
    latency: int = 1,
) -> Never:
    return _not_implemented("engine")


def reorder(
    value: object,
    *,
    by: object,
    entries: int = 16,
    start: int = 0,
    depth: int = 1,
    latency: int = 1,
) -> Never:
    return _not_implemented("reorder")


def fork(
    value: object,
    *,
    outputs: int,
    depth: int = 1,
    latency: int = 1,
) -> Never:
    return _not_implemented("fork")


def barrier(
    *values: object,
    depth: int = 1,
    latency: int = 1,
) -> Never:
    return _not_implemented("barrier")


def table(
    value: object,
    *,
    address: object,
    write: object,
    data: object,
    result: object,
    entries: int = 16,
    init: int = 0,
    depth: int = 1,
    latency: int = 1,
) -> Never:
    return _not_implemented("table")
