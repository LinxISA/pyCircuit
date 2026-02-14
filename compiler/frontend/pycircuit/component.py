from __future__ import annotations

import inspect
from dataclasses import MISSING, dataclass, fields, is_dataclass
from typing import Any, Callable, TypeVar

from .design import module
from .hw import Circuit

_CLS = TypeVar("_CLS")
_COMPONENT_FN_CACHE: dict[type[Any], Callable[..., Any]] = {}
_COMPONENT_PORT_CACHE: dict[type[Any], tuple[str, ...]] = {}


def _component_name(cls: type[Any]) -> str:
    override = getattr(cls, "__pycircuit_component_name__", None)
    if isinstance(override, str) and override.strip():
        return override.strip()
    return cls.__name__


def _component_param_names(cls: type[Any]) -> tuple[str, ...]:
    if not is_dataclass(cls):
        raise TypeError(f"@component requires dataclass-like class, got {cls.__name__}")
    return tuple(f.name for f in fields(cls))


def _component_port_names(cls: type[Any]) -> tuple[str, ...]:
    cached = _COMPONENT_PORT_CACHE.get(cls)
    if cached is not None:
        return cached

    build = getattr(cls, "build", None)
    if not callable(build):
        raise TypeError(f"component {cls.__name__} must define build(self, m, ...) method")

    sig = inspect.signature(build)
    ps = list(sig.parameters.values())
    if len(ps) < 2:
        raise TypeError(f"component {cls.__name__}.build must take at least (self, m)")

    ports: list[str] = []
    for p in ps[2:]:
        if p.kind in (inspect.Parameter.VAR_POSITIONAL, inspect.Parameter.VAR_KEYWORD):
            raise TypeError(f"component {cls.__name__}.build does not support varargs")
        ports.append(p.name)

    out = tuple(ports)
    _COMPONENT_PORT_CACHE[cls] = out
    return out


def _build_component_module_fn(cls: type[Any]) -> Callable[..., Any]:
    cached = _COMPONENT_FN_CACHE.get(cls)
    if cached is not None:
        return cached

    ports = list(_component_port_names(cls))
    params = list(_component_param_names(cls))

    overlap = sorted(set(ports) & set(params))
    if overlap:
        raise TypeError(f"component {cls.__name__} has overlapping build ports/params: {', '.join(overlap)}")

    fn_name = f"__pyc_component__{cls.__name__}"
    args = ["m", *ports, *params]
    sig_text = ", ".join(args)

    comp_args = ", ".join(f"{p}={p}" for p in params)
    port_args = ", ".join(ports)
    if comp_args:
        comp_ctor = f"_cls({comp_args})"
    else:
        comp_ctor = "_cls()"

    if port_args:
        body_call = f"return _cls.build(_inst, m, {port_args})"
    else:
        body_call = "return _cls.build(_inst, m)"

    src = (
        f"def {fn_name}({sig_text}):\n"
        f"    _inst = {comp_ctor}\n"
        f"    {body_call}\n"
    )
    ns: dict[str, Any] = {"_cls": cls}
    exec(src, ns, ns)
    fn = ns[fn_name]
    fn.__name__ = fn_name
    fn.__qualname__ = fn_name
    fn.__module__ = cls.__module__
    fn = module(name=_component_name(cls))(fn)
    _COMPONENT_FN_CACHE[cls] = fn
    return fn


def _component_call(self: Any, m: Circuit, /, *args: Any, name: str | None = None, **kwargs: Any) -> Any:
    if not isinstance(m, Circuit):
        raise TypeError(f"component call expects Circuit as first argument, got {type(m).__name__}")

    cls = type(self)
    ports = list(_component_port_names(cls))
    if len(args) > len(ports):
        raise TypeError(f"component {cls.__name__} got too many positional ports: {len(args)} > {len(ports)}")

    bound_ports: dict[str, Any] = {}
    for pname, val in zip(ports, args):
        bound_ports[pname] = val

    for k, v in kwargs.items():
        if k in bound_ports:
            raise TypeError(f"component {cls.__name__} duplicated port argument: {k}")
        if k not in ports:
            raise TypeError(f"component {cls.__name__} unknown port argument: {k}")
        bound_ports[k] = v

    missing = [p for p in ports if p not in bound_ports]
    if missing:
        raise TypeError(f"component {cls.__name__} missing port argument(s): {', '.join(missing)}")

    param_names = _component_param_names(cls)
    params = {p: getattr(self, p) for p in param_names}

    inst_name = name
    if inst_name is None:
        base = _component_name(cls)
        counts = getattr(m, "_component_call_counts", None)
        if not isinstance(counts, dict):
            counts = {}
            setattr(m, "_component_call_counts", counts)
        idx = int(counts.get(base, 0)) + 1
        counts[base] = idx
        inst_name = m.scoped_name(f"{base}__N{idx}")

    fn = _build_component_module_fn(cls)
    return m.instance(fn, name=str(inst_name), params=params, **bound_ports)


def component(_cls: type[_CLS] | None = None, *, name: str | None = None) -> type[_CLS] | Callable[[type[_CLS]], type[_CLS]]:
    """Decorator for dataclass-based parametric components.

    Canonical authoring model:
      @component
      class Adder:
          WIDTH: int
          def build(self, m: Circuit, a, b):
              return (a + b)[0:self.WIDTH]

      y = Adder(WIDTH=8)(m, a=x, b=z)
    """

    def wrap(cls: type[_CLS]) -> type[_CLS]:
        out: type[Any] = cls
        if not is_dataclass(out):
            out = dataclass(frozen=True)(out)

        # Validate dataclass fields for component params.
        for f in fields(out):
            if f.default is MISSING and f.default_factory is MISSING:
                # Required params are allowed; no-op.
                pass

        if not callable(getattr(out, "build", None)):
            raise TypeError(f"component {out.__name__} must define build(self, m, ...)")

        setattr(out, "__pycircuit_component__", True)
        if isinstance(name, str) and name.strip():
            setattr(out, "__pycircuit_component_name__", name.strip())

        setattr(out, "__call__", _component_call)
        return out

    if _cls is None:
        return wrap
    return wrap(_cls)
