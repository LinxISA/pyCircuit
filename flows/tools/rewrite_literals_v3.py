#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ast
import json
from pathlib import Path
from typing import Iterable


class LiteralV3Transformer(ast.NodeTransformer):
    def __init__(self) -> None:
        self.changed = False
        self.need_u = False
        self.need_s = False
        self.ambiguous: list[dict[str, object]] = []

    @staticmethod
    def _is_const_call(node: ast.Call) -> bool:
        f = node.func
        return isinstance(f, ast.Attribute) and f.attr == "const"

    @staticmethod
    def _extract_width(node: ast.Call) -> ast.expr | None:
        for kw in node.keywords:
            if kw.arg == "width":
                return kw.value
        if len(node.args) >= 2:
            return node.args[1]
        return None

    @staticmethod
    def _is_negative_literal(expr: ast.expr) -> bool | None:
        if isinstance(expr, ast.Constant) and isinstance(expr.value, int):
            return int(expr.value) < 0
        if isinstance(expr, ast.UnaryOp) and isinstance(expr.op, ast.USub):
            if isinstance(expr.operand, ast.Constant) and isinstance(expr.operand.value, int):
                return True
        if isinstance(expr, ast.UnaryOp) and isinstance(expr.op, ast.UAdd):
            if isinstance(expr.operand, ast.Constant) and isinstance(expr.operand.value, int):
                return int(expr.operand.value) < 0
        return None

    def visit_Call(self, node: ast.Call) -> ast.AST:  # noqa: C901
        node = self.generic_visit(node)

        if not self._is_const_call(node):
            return node

        if not node.args:
            self.ambiguous.append(
                {
                    "kind": "const-rewrite",
                    "lineno": getattr(node, "lineno", None),
                    "expr": ast.unparse(node),
                    "reason": "missing value argument",
                }
            )
            return node

        value = node.args[0]
        width = self._extract_width(node)
        if width is None:
            self.ambiguous.append(
                {
                    "kind": "const-rewrite",
                    "lineno": getattr(node, "lineno", None),
                    "expr": ast.unparse(node),
                    "reason": "missing width argument",
                }
            )
            return node

        neg = self._is_negative_literal(value)
        if neg is None:
            # Conservative default for non-obvious sign; width stays explicit.
            neg = False
            self.ambiguous.append(
                {
                    "kind": "const-rewrite",
                    "lineno": getattr(node, "lineno", None),
                    "expr": ast.unparse(node),
                    "reason": "non-literal sign inferred as unsigned by default",
                }
            )

        fn = "s" if neg else "u"
        if fn == "s":
            self.need_s = True
        else:
            self.need_u = True
        self.changed = True
        return ast.copy_location(
            ast.Call(func=ast.Name(id=fn, ctx=ast.Load()), args=[width, value], keywords=[]),
            node,
        )


def ensure_imports(tree: ast.Module, *, need_u: bool, need_s: bool) -> None:
    wanted = {n for n, needed in (("u", need_u), ("s", need_s)) if needed}
    if not wanted:
        return

    for stmt in tree.body:
        if isinstance(stmt, ast.ImportFrom) and stmt.module == "pycircuit" and stmt.level == 0:
            present = {a.name for a in stmt.names}
            for name in sorted(wanted - present):
                stmt.names.append(ast.alias(name=name, asname=None))
            return

    insert_at = 0
    if tree.body and isinstance(tree.body[0], ast.Expr) and isinstance(tree.body[0].value, ast.Constant) and isinstance(tree.body[0].value.value, str):
        insert_at = 1
    while insert_at < len(tree.body):
        s = tree.body[insert_at]
        if isinstance(s, ast.ImportFrom) and s.module == "__future__":
            insert_at += 1
            continue
        break

    imp = ast.ImportFrom(module="pycircuit", names=[ast.alias(name=n, asname=None) for n in sorted(wanted)], level=0)
    tree.body.insert(insert_at, imp)


def rewrite_file(path: Path) -> dict[str, object]:
    src = path.read_text(encoding="utf-8")
    tree = ast.parse(src)

    tx = LiteralV3Transformer()
    new_tree = tx.visit(tree)
    assert isinstance(new_tree, ast.Module)
    ensure_imports(new_tree, need_u=tx.need_u, need_s=tx.need_s)
    ast.fix_missing_locations(new_tree)

    if not tx.changed:
        return {
            "file": str(path),
            "changed": False,
            "ambiguous": tx.ambiguous,
        }

    new_src = ast.unparse(new_tree) + "\n"
    ast.parse(new_src)
    path.write_text(new_src, encoding="utf-8")
    return {
        "file": str(path),
        "changed": True,
        "ambiguous": tx.ambiguous,
    }


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


def main() -> int:
    ap = argparse.ArgumentParser(description="Rewrite m.const(...) callsites to u()/s() literals for pyCircuit V3")
    ap.add_argument("paths", nargs="*", help="Roots/files to rewrite")
    ap.add_argument("--report", default=".pycircuit_out/migration_baseline/rewrite_literals_v3_report.json")
    ns = ap.parse_args()

    roots = [Path(p) for p in ns.paths] if ns.paths else [
        Path("/Users/zhoubot/pyCircuit/designs/examples"),
        Path("/Users/zhoubot/pyCircuit/designs/linxcore"),
    ]

    files = iter_py_files(roots)
    changed = 0
    ambiguous = 0
    results: list[dict[str, object]] = []

    for f in files:
        r = rewrite_file(f)
        results.append(r)
        if bool(r["changed"]):
            changed += 1
        ambiguous += len(r["ambiguous"])

    report = {
        "files_total": len(files),
        "files_changed": changed,
        "ambiguous": ambiguous,
        "results": results,
    }
    rp = Path(ns.report)
    rp.parent.mkdir(parents=True, exist_ok=True)
    rp.write_text(json.dumps(report, indent=2), encoding="utf-8")

    print(f"rewritten_files={changed}")
    print(f"ambiguous={ambiguous}")
    print(f"report={rp}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
