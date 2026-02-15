#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ast
from pathlib import Path
from typing import Iterable

NEEDED_NAMES = ("u", "s", "U", "S", "unsigned", "signed")


def iter_py_files(roots: Iterable[Path]) -> list[Path]:
    out: list[Path] = []
    for root in roots:
        if root.is_file() and root.suffix == ".py":
            out.append(root)
            continue
        if not root.exists():
            continue
        out.extend(sorted(p for p in root.rglob("*.py") if p.is_file()))
    return out


def _used_names(tree: ast.AST) -> set[str]:
    out: set[str] = set()
    for n in ast.walk(tree):
        if isinstance(n, ast.Name) and isinstance(n.ctx, ast.Load):
            out.add(n.id)
    return out


def _existing_pyc_imports(tree: ast.Module) -> set[str]:
    out: set[str] = set()
    for stmt in tree.body:
        if isinstance(stmt, ast.ImportFrom) and stmt.module == "pycircuit" and stmt.level == 0:
            for alias in stmt.names:
                out.add(alias.asname or alias.name)
    return out


def _insert_after_preamble(lines: list[str]) -> int:
    i = 0
    if i < len(lines) and lines[i].startswith("#!"):
        i += 1
    if i < len(lines) and lines[i].lstrip().startswith(('"""', "'''")):
        quote = '"""' if '"""' in lines[i] else "'''"
        if lines[i].count(quote) < 2:
            i += 1
            while i < len(lines) and quote not in lines[i]:
                i += 1
            if i < len(lines):
                i += 1
        else:
            i += 1
    while i < len(lines):
        s = lines[i].strip()
        if s.startswith("from __future__ import"):
            i += 1
            continue
        if not s:
            i += 1
            continue
        break
    return i


def rewrite_file(path: Path) -> bool:
    src = path.read_text(encoding="utf-8")
    try:
        tree = ast.parse(src)
    except SyntaxError:
        return False

    used = _used_names(tree)
    needed = {n for n in NEEDED_NAMES if n in used}
    if not needed:
        return False

    imported = _existing_pyc_imports(tree)
    missing = sorted(needed - imported)
    if not missing:
        return False

    lines = src.splitlines(keepends=True)
    for i, line in enumerate(lines):
        s = line.strip()
        if not s.startswith("from pycircuit import "):
            continue
        if "(" in s or s.endswith("\\"):
            continue
        body = s[len("from pycircuit import ") :].strip()
        names = [x.strip() for x in body.split(",") if x.strip()]
        have = set(names)
        add = [n for n in missing if n not in have]
        if not add:
            return False
        names.extend(add)
        nl = "\n" if line.endswith("\n") else ""
        indent = line[: len(line) - len(line.lstrip(" "))]
        lines[i] = f"{indent}from pycircuit import {', '.join(names)}{nl}"
        path.write_text("".join(lines), encoding="utf-8")
        return True

    insert_at = _insert_after_preamble([l.rstrip("\n") for l in lines])
    imp_line = f"from pycircuit import {', '.join(missing)}\n"
    lines.insert(insert_at, imp_line)
    path.write_text("".join(lines), encoding="utf-8")
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description="Ensure pyCircuit V3 literal/helper imports are present.")
    ap.add_argument("paths", nargs="*")
    ns = ap.parse_args()

    roots = [Path(p) for p in ns.paths] if ns.paths else [
        Path("/Users/zhoubot/pyCircuit/designs/examples"),
        Path("/Users/zhoubot/pyCircuit/designs/linxcore"),
    ]
    changed = 0
    for f in iter_py_files(roots):
        if rewrite_file(f):
            changed += 1
    print(f"files_changed={changed}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
