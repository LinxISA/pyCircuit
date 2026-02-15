from __future__ import annotations

import ast
import inspect
import textwrap
import weakref
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class FunctionMeta:
    fn: Any
    signature: inspect.Signature
    source: str
    start_line: int
    tree: ast.AST
    fdef: ast.FunctionDef
    source_file: str | None
    source_stem: str | None


_META_CACHE: weakref.WeakKeyDictionary[Any, FunctionMeta] = weakref.WeakKeyDictionary()
_SIG_CACHE: weakref.WeakKeyDictionary[Any, inspect.Signature] = weakref.WeakKeyDictionary()
_ASSIGNED_NAMES_CACHE: dict[tuple[int, ...], frozenset[str]] = {}


def _find_function_def(tree: ast.AST, name: str) -> ast.FunctionDef:
    for node in ast.walk(tree):
        if isinstance(node, ast.FunctionDef) and node.name == name:
            return node
    raise RuntimeError(f"failed to find function definition for {name!r}")


def get_signature(fn: Any) -> inspect.Signature:
    cached = _SIG_CACHE.get(fn)
    if cached is not None:
        return cached
    sig = inspect.signature(fn)
    _SIG_CACHE[fn] = sig
    return sig


def get_function_meta(fn: Any, *, fn_name: str | None = None) -> FunctionMeta:
    cached = _META_CACHE.get(fn)
    if cached is not None and (fn_name is None or cached.fdef.name == fn_name):
        return cached

    lines, start_line = inspect.getsourcelines(fn)
    source = textwrap.dedent("".join(lines))
    tree = ast.parse(source)
    name = fn_name if fn_name is not None else getattr(fn, "__name__", None)
    if not isinstance(name, str) or not name:
        raise RuntimeError(f"failed to infer function name for {fn!r}")
    fdef = _find_function_def(tree, name)

    source_file = inspect.getsourcefile(fn) or inspect.getfile(fn)
    source_stem = None
    try:
        if source_file:
            source_stem = Path(source_file).stem
    except Exception:
        source_stem = None

    meta = FunctionMeta(
        fn=fn,
        signature=get_signature(fn),
        source=source,
        start_line=int(start_line),
        tree=tree,
        fdef=fdef,
        source_file=source_file,
        source_stem=source_stem,
    )
    _META_CACHE[fn] = meta
    return meta


def assigned_names_for(stmts: list[ast.stmt]) -> frozenset[str]:
    key = tuple(id(s) for s in stmts)
    cached = _ASSIGNED_NAMES_CACHE.get(key)
    if cached is not None:
        return cached

    out: set[str] = set()

    class _Visitor(ast.NodeVisitor):
        def visit_Assign(self, node: ast.Assign) -> None:  # noqa: N802
            for t in node.targets:
                if isinstance(t, ast.Name):
                    out.add(t.id)
            self.generic_visit(node.value)

        def visit_AnnAssign(self, node: ast.AnnAssign) -> None:  # noqa: N802
            if isinstance(node.target, ast.Name):
                out.add(node.target.id)
            if node.value is not None:
                self.generic_visit(node.value)

        def visit_AugAssign(self, node: ast.AugAssign) -> None:  # noqa: N802
            if isinstance(node.target, ast.Name):
                out.add(node.target.id)
            self.generic_visit(node.value)

    _Visitor().visit(ast.Module(body=stmts, type_ignores=[]))
    frozen = frozenset(out)
    _ASSIGNED_NAMES_CACHE[key] = frozen
    return frozen


def clear_metadata_caches() -> None:
    _META_CACHE.clear()
    _SIG_CACHE.clear()
    _ASSIGNED_NAMES_CACHE.clear()
