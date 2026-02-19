from __future__ import annotations

import hashlib
import inspect
import json
from dataclasses import dataclass
from typing import Any, Callable, Iterable, Mapping, TYPE_CHECKING

from .api_contract import FRONTEND_API_VERSION
from .dsl import Module

if TYPE_CHECKING:
    from .hw import Circuit


class DesignError(RuntimeError):
    pass


def module(_fn: Any | None = None, *, name: str | None = None, structural: bool = False) -> Callable[[Any], Any] | Any:
    """Mark a function as a hierarchy-preserving module boundary.

    Module callsites are materialized as `pyc.instance` and are not inlined by
    the frontend. `structural=True` tags the symbol for structural emission.
    """

    def deco(fn: Any) -> Any:
        module_name = str(name).strip() if isinstance(name, str) and name.strip() else getattr(fn, "__name__", "Module")
        setattr(fn, "__pycircuit_module_name__", str(module_name))
        setattr(fn, "__pycircuit_kind__", "module")
        setattr(fn, "__pycircuit_inline__", False)
        setattr(fn, "__pycircuit_emit_structural__", bool(structural))
        return fn

    if _fn is None:
        return deco
    return deco(_fn)


def function(_fn: Any | None = None, *, name: str | None = None) -> Callable[[Any], Any] | Any:
    """Mark a function as an inline hardware helper.

    Function callsites are lowered inline into the caller.
    """

    def deco(fn: Any) -> Any:
        if isinstance(name, str) and name.strip():
            setattr(fn, "__pycircuit_module_name__", str(name).strip())
        setattr(fn, "__pycircuit_kind__", "function")
        setattr(fn, "__pycircuit_inline__", True)
        setattr(fn, "__pycircuit_emit_structural__", False)
        return fn

    if _fn is None:
        return deco
    return deco(_fn)


def const(_fn: Any | None = None, *, name: str | None = None) -> Callable[[Any], Any] | Any:
    """Mark a function as compile-time metaprogramming logic.

    `@const` calls execute in Python during JIT and must be pure: they may not
    emit IR or mutate module interfaces.
    """

    def deco(fn: Any) -> Any:
        if isinstance(name, str) and name.strip():
            setattr(fn, "__pycircuit_module_name__", str(name).strip())
        setattr(fn, "__pycircuit_kind__", "const")
        setattr(fn, "__pycircuit_inline__", True)
        setattr(fn, "__pycircuit_emit_structural__", False)
        return fn

    if _fn is None:
        return deco
    return deco(_fn)


def _canon_param(v: Any) -> Any:
    # Deterministic, JSON-compatible subset.
    if v is None:
        return None
    if isinstance(v, (bool, int, str)):
        return v
    if isinstance(v, (tuple, list)):
        return [_canon_param(x) for x in v]
    if isinstance(v, dict):
        out: dict[str, Any] = {}
        for k in sorted(v.keys(), key=lambda x: str(x)):
            if not isinstance(k, str):
                raise DesignError(f"param dict keys must be str, got {type(k).__name__}")
            out[k] = _canon_param(v[k])
        return out
    raise DesignError(
        "unsupported param type for specialization/caching: "
        f"{type(v).__name__} (allowed: bool/int/str, list/tuple, dict[str,...])"
    )


def _params_json(params: Mapping[str, Any]) -> str:
    canon = _canon_param(dict(params))
    return json.dumps(canon, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def _port_specs_json(port_specs: Mapping[str, Any] | None) -> str:
    if not port_specs:
        return "{}"
    canon = _canon_param(dict(port_specs))
    return json.dumps(canon, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def _params_hash8(params_json: str) -> str:
    h = hashlib.sha256(params_json.encode("utf-8")).hexdigest()
    return h[:8]


def _base_name(fn: Any) -> str:
    override = getattr(fn, "__pycircuit_module_name__", None)
    if isinstance(override, str) and override.strip():
        return override.strip()
    return getattr(fn, "__name__", "Module")


def _kind_of(fn: Any) -> str:
    k = getattr(fn, "__pycircuit_kind__", None)
    if isinstance(k, str):
        kk = k.strip().lower()
        if kk in {"module", "function", "const"}:
            if kk == "const":
                return "template"
            return kk
    return "module"


def _inline_of(fn: Any) -> bool:
    if _kind_of(fn) == "function":
        return True
    return bool(getattr(fn, "__pycircuit_inline__", False))


def _emit_structural_of(fn: Any) -> bool:
    return bool(getattr(fn, "__pycircuit_emit_structural__", False))


@dataclass(frozen=True)
class CompiledModule:
    fn: Any
    params_json: str
    sym_name: str
    mod: Module
    arg_names: tuple[str, ...]
    arg_types: tuple[str, ...]
    result_names: tuple[str, ...]
    result_types: tuple[str, ...]


class Design:
    """A multi-module compilation unit (MLIR `module`) produced by the Python frontend."""

    def __init__(self, *, top: str) -> None:
        self.top = str(top)
        self._mods: dict[str, CompiledModule] = {}

    def add(self, cm: CompiledModule) -> None:
        if cm.sym_name in self._mods:
            raise DesignError(f"duplicate module symbol: {cm.sym_name!r}")
        self._mods[cm.sym_name] = cm

    def modules(self) -> Iterable[CompiledModule]:
        return self._mods.values()

    def lookup(self, sym_name: str) -> CompiledModule | None:
        return self._mods.get(str(sym_name))

    def emit_mlir(self) -> str:
        # Emit a single MLIR `module` containing all compiled `func.func`s.
        #
        # `pyc.top` is a FlatSymbolRefAttr for tools to find the top module.
        parts: list[str] = []
        parts.append(f'module attributes {{pyc.top = @{self.top}, pyc.frontend.api = "{FRONTEND_API_VERSION}"}} {{\n')
        for cm in self._mods.values():
            parts.append(cm.mod.emit_func_mlir())
            parts.append("\n")
        parts.append("}\n")
        return "".join(parts)


class DesignContext:
    """Specialization cache + registry for a Design's compiled modules."""

    def __init__(self, design: Design) -> None:
        self.design = design
        self._cache: dict[tuple[int, str, str, str | None], CompiledModule] = {}
        self._used_sym_names: set[str] = set()

    def _unique_sym(self, base: str, *, cache_sig_json: str, module_name: str | None) -> str:
        if module_name is not None:
            sym = str(module_name)
        else:
            sym = f"{base}__p{_params_hash8(cache_sig_json)}"
        if sym in self._used_sym_names:
            # Same fn+params should map to the same symbol; collisions here mean
            # a user-provided module_name conflict.
            in_design = sym in self.design._mods
            raise DesignError(f"duplicate specialized module name: {sym!r} (already_in_design={in_design})")
        self._used_sym_names.add(sym)
        return sym

    def _bind_params(self, fn: Any, params: Mapping[str, Any], *, port_names: set[str] | None = None) -> dict[str, Any]:
        sig = inspect.signature(fn)
        ps = list(sig.parameters.values())
        if not ps:
            raise DesignError("module function must accept at least one argument (Circuit builder)")
        ports = set(port_names or ())
        # The first argument is the builder; bind remaining by name.
        bound: dict[str, Any] = {}
        for p in ps[1:]:
            if p.kind in (inspect.Parameter.VAR_POSITIONAL, inspect.Parameter.VAR_KEYWORD):
                raise DesignError("varargs are not supported for module specialization")
            if p.name in ports:
                continue
            if p.name not in params:
                if p.default is inspect._empty:
                    raise DesignError(f"missing module param {p.name!r} for {getattr(fn, '__name__', fn)!r}")
                bound[p.name] = p.default
            else:
                bound[p.name] = params[p.name]
        # Reject unknown keys early to avoid silent mismatches.
        extra = set(params.keys()) - {p.name for p in ps[1:] if p.name not in ports}
        if extra:
            raise DesignError(f"unknown module param(s) for {getattr(fn, '__name__', fn)!r}: {', '.join(sorted(extra))}")
        return bound

    def register_top(self, fn: Any, *, sym_name: str, params: Mapping[str, Any], mod: Module) -> CompiledModule:
        params_json = _params_json(params)
        base = _base_name(fn)
        # Top symbol is explicit (no hash suffix); still mark as used.
        if sym_name in self._used_sym_names:
            raise DesignError(f"duplicate top module symbol: {sym_name!r}")
        self._used_sym_names.add(sym_name)

        cm = self._finalize_compiled(fn, sym_name=sym_name, params_json=params_json, base=base, mod=mod)
        self.design.add(cm)
        return cm

    def specialize(
        self,
        fn: Any,
        *,
        params: Mapping[str, Any],
        module_name: str | None = None,
        port_specs: Mapping[str, Any] | None = None,
    ) -> CompiledModule:
        port_specs_dict = dict(port_specs or {})
        params_bound = self._bind_params(fn, params, port_names=set(port_specs_dict.keys()))
        params_json = _params_json(params_bound)
        port_specs_json = _port_specs_json(port_specs_dict)
        key = (id(fn), params_json, port_specs_json, module_name)
        if key in self._cache:
            return self._cache[key]

        base = _base_name(fn)
        cache_sig_json = json.dumps(
            {"params": json.loads(params_json), "ports": json.loads(port_specs_json)},
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
        )
        sym_guess = str(module_name) if module_name is not None else f"{base}__p{_params_hash8(cache_sig_json)}"
        if sym_guess in self._used_sym_names:
            existing = self.design.lookup(sym_guess)
            if existing is not None:
                self._cache[key] = existing
                return existing
        sym_name = self._unique_sym(base, cache_sig_json=cache_sig_json, module_name=module_name)

        mod = self._compile_module(fn, sym_name=sym_name, params=params_bound, port_specs=port_specs_dict)
        cm = self._finalize_compiled(fn, sym_name=sym_name, params_json=params_json, base=base, mod=mod)
        self.design.add(cm)
        self._cache[key] = cm
        return cm

    def _compile_module(
        self,
        fn: Any,
        *,
        sym_name: str,
        params: Mapping[str, Any],
        port_specs: Mapping[str, Any] | None = None,
    ) -> Module:
        from .jit import compile_module as jit_compile

        return jit_compile(fn, module_name=sym_name, design_ctx=self, port_specs=port_specs, **params)

    def _finalize_compiled(self, fn: Any, *, sym_name: str, params_json: str, base: str, mod: Module) -> CompiledModule:
        # Attach debug attributes (emitted in func.func header).
        try:
            mod.set_func_attr("pyc.base", base)
            mod.set_func_attr("pyc.params", params_json)
            mod.set_func_attr("pyc.kind", _kind_of(fn))
            mod.set_func_attr("pyc.inline", "true" if _inline_of(fn) else "false")
            mod.set_func_attr("pyc.frontend.api", FRONTEND_API_VERSION)
            if _emit_structural_of(fn):
                mod.set_func_attr("pyc.emit.structural", "true")
        except Exception as e:
            raise DesignError(f"failed to set module attrs for {sym_name!r}: {e}") from e

        arg_names = tuple(n for n, _ in getattr(mod, "_args", []))  # noqa: SLF001
        arg_types = tuple(sig.ty for _, sig in getattr(mod, "_args", []))  # noqa: SLF001
        res_names = tuple(n for n, _ in getattr(mod, "_results", []))  # noqa: SLF001
        res_types = tuple(sig.ty for _, sig in getattr(mod, "_results", []))  # noqa: SLF001

        return CompiledModule(
            fn=fn,
            params_json=params_json,
            sym_name=str(sym_name),
            mod=mod,
            arg_names=arg_names,
            arg_types=arg_types,
            result_names=res_names,
            result_types=res_types,
        )
