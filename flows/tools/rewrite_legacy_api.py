#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ast
import json
from pathlib import Path
from typing import Iterable


class LegacyTransformer(ast.NodeTransformer):
    def __init__(self) -> None:
        self.replacements = 0
        self.ambiguous: list[dict[str, object]] = []

    def _mark(self) -> None:
        self.replacements += 1

    def _width_expr(self, node: ast.Call) -> ast.expr | None:
        for kw in node.keywords:
            if kw.arg == "width":
                if isinstance(kw.value, ast.expr):
                    return kw.value
                return None
        if len(node.args) == 1 and isinstance(node.args[0], ast.expr):
            return node.args[0]
        return None

    def visit_Call(self, node: ast.Call) -> ast.AST:  # noqa: C901
        node = self.generic_visit(node)

        f = node.func
        if isinstance(f, ast.Attribute):
            attr = f.attr
            recv = f.value

            if attr == "eq" and len(node.args) == 1 and not node.keywords:
                self._mark()
                return ast.copy_location(
                    ast.Compare(left=recv, ops=[ast.Eq()], comparators=[node.args[0]]),
                    node,
                )

            if attr == "as_unsigned" and not node.args and not node.keywords:
                self._mark()
                return ast.copy_location(
                    ast.Call(func=ast.Name(id="unsigned", ctx=ast.Load()), args=[recv], keywords=[]),
                    node,
                )

            if attr == "lt" and len(node.args) == 1 and not node.keywords:
                self._mark()
                return ast.copy_location(
                    ast.Compare(left=recv, ops=[ast.Lt()], comparators=[node.args[0]]),
                    node,
                )

            if attr == "select" and len(node.args) == 2 and not node.keywords:
                self._mark()
                return ast.copy_location(
                    ast.IfExp(test=recv, body=node.args[0], orelse=node.args[1]),
                    node,
                )

            if attr == "trunc":
                w = self._width_expr(node)
                if w is None:
                    self.ambiguous.append(
                        {
                            "kind": "trunc",
                            "lineno": getattr(node, "lineno", None),
                            "expr": ast.unparse(node),
                            "reason": "missing width",
                        }
                    )
                    return node
                self._mark()
                return ast.copy_location(
                    ast.Subscript(
                        value=recv,
                        slice=ast.Slice(lower=ast.Constant(value=0), upper=w, step=None),
                        ctx=ast.Load(),
                    ),
                    node,
                )

            if attr == "zext" and not node.args and (not node.keywords or all(k.arg == "width" for k in node.keywords)):
                self._mark()
                return ast.copy_location(
                    ast.Call(func=ast.Attribute(value=recv, attr="as_unsigned", ctx=ast.Load()), args=[], keywords=[]),
                    node,
                )

            if attr == "sext" and not node.args and (not node.keywords or all(k.arg == "width" for k in node.keywords)):
                self._mark()
                return ast.copy_location(
                    ast.Call(func=ast.Attribute(value=recv, attr="as_signed", ctx=ast.Load()), args=[], keywords=[]),
                    node,
                )

            if attr == "mux" and len(node.args) == 3 and not node.keywords:
                self._mark()
                return ast.copy_location(
                    ast.IfExp(test=node.args[0], body=node.args[1], orelse=node.args[2]),
                    node,
                )

            if attr == "const" and len(node.args) >= 1:
                width_expr: ast.expr | None = None
                for kw in node.keywords:
                    if kw.arg == "width":
                        width_expr = kw.value
                        break
                if width_expr is None and len(node.args) >= 2:
                    width_expr = node.args[1]
                if width_expr is None:
                    self.ambiguous.append(
                        {
                            "kind": "const",
                            "lineno": getattr(node, "lineno", None),
                            "expr": ast.unparse(node),
                            "reason": "missing width",
                        }
                    )
                    return node
                val = node.args[0]
                neg = False
                if isinstance(val, ast.Constant) and isinstance(val.value, int):
                    neg = int(val.value) < 0
                elif (
                    isinstance(val, ast.UnaryOp)
                    and isinstance(val.op, ast.USub)
                    and isinstance(val.operand, ast.Constant)
                    and isinstance(val.operand.value, int)
                ):
                    neg = True
                fn = "s" if neg else "u"
                self._mark()
                return ast.copy_location(
                    ast.Call(func=ast.Name(id=fn, ctx=ast.Load()), args=[width_expr, val], keywords=[]),
                    node,
                )

        if isinstance(f, ast.Name) and f.id == "mux" and len(node.args) == 3 and not node.keywords:
            self._mark()
            return ast.copy_location(
                ast.IfExp(test=node.args[0], body=node.args[1], orelse=node.args[2]),
                node,
            )

        return node


def rewrite_file(path: Path) -> dict[str, object]:
    src = path.read_text(encoding="utf-8")
    tree = ast.parse(src)
    tx = LegacyTransformer()
    new_tree = tx.visit(tree)
    ast.fix_missing_locations(new_tree)

    if tx.replacements == 0:
        return {"file": str(path), "changed": False, "replacements": 0, "ambiguous": tx.ambiguous}

    new_src = ast.unparse(new_tree) + "\n"
    ast.parse(new_src)
    path.write_text(new_src, encoding="utf-8")
    return {
        "file": str(path),
        "changed": True,
        "replacements": tx.replacements,
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
    ap = argparse.ArgumentParser(description="Rewrite pyCircuit legacy frontend APIs in Python design files.")
    ap.add_argument("paths", nargs="*", help="Roots/files to rewrite")
    ap.add_argument("--report", default=".pycircuit_out/migration_baseline/rewrite_legacy_api_report.json")
    ns = ap.parse_args()

    roots = [Path(p) for p in ns.paths] if ns.paths else [
        Path("/Users/zhoubot/pyCircuit/designs/examples"),
        Path("/Users/zhoubot/pyCircuit/designs/janus"),
    ]

    files = iter_py_files(roots)
    results: list[dict[str, object]] = []
    changed = 0
    replaced = 0
    ambiguous_total = 0

    for f in files:
        r = rewrite_file(f)
        results.append(r)
        if bool(r["changed"]):
            changed += 1
            replaced += int(r["replacements"])
        ambiguous_total += len(r["ambiguous"])

    report = {
        "files_total": len(files),
        "files_changed": changed,
        "replacements": replaced,
        "ambiguous": ambiguous_total,
        "results": results,
    }

    rp = Path(ns.report)
    rp.parent.mkdir(parents=True, exist_ok=True)
    rp.write_text(json.dumps(report, indent=2), encoding="utf-8")

    print(f"rewritten_files={changed}")
    print(f"replacements={replaced}")
    print(f"ambiguous={ambiguous_total}")
    print(f"report={rp}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
