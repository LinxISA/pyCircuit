#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

FORBIDDEN_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    (".eq(", re.compile(r"\.eq\(")),
    (".lt(", re.compile(r"\.lt\(")),
    (".select(", re.compile(r"\.select\(")),
    ("import mux", re.compile(r"from\s+pycircuit\s+import[^\n]*\bmux\b")),
    ("mux(", re.compile(r"\bmux\(")),
    ("cond(", re.compile(r"\bcond\(")),
    (".trunc(", re.compile(r"\.trunc\(")),
    (".zext(", re.compile(r"\.zext\(")),
    (".sext(", re.compile(r"\.sext\(")),
    ("m.const(", re.compile(r"\bm\.const\(")),
    ("CycleAware", re.compile(r"\bCycleAware[A-Za-z_]*\b")),
    ("compile_cycle_aware", re.compile(r"\bcompile_cycle_aware\b")),
)

EXAMPLES_ONLY_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    (".as_unsigned(", re.compile(r"\.as_unsigned\(")),
)

DEFAULT_TARGETS: tuple[str, ...] = (
    "designs/examples",
    "designs/linxcore",
    "docs",
    "README.md",
)

TEXT_SUFFIXES = {
    ".md",
    ".py",
    ".rst",
    ".txt",
    ".toml",
    ".yaml",
    ".yml",
    ".json",
    ".sh",
}

SKIP_DIRS = {".git", ".pycircuit_out", "__pycache__", "build"}


def iter_target_files(path: Path) -> list[Path]:
    if not path.exists():
        return []
    if path.is_file():
        return [path]
    files: list[Path] = []
    for f in path.rglob("*"):
        if not f.is_file():
            continue
        if any(part in SKIP_DIRS for part in f.parts):
            continue
        if f.suffix and f.suffix.lower() not in TEXT_SUFFIXES:
            continue
        files.append(f)
    return files


def scan_file(path: Path, *, extra_patterns: tuple[tuple[str, re.Pattern[str]], ...] = ()) -> list[tuple[int, int, str]]:
    hits: list[tuple[int, int, str]] = []
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return hits
    pats = (*FORBIDDEN_PATTERNS, *extra_patterns)
    for line_no, line in enumerate(text.splitlines(), start=1):
        for label, pattern in pats:
            for m in pattern.finditer(line):
                hits.append((line_no, m.start() + 1, label))
    return hits


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Fail if legacy pyCircuit frontend APIs appear in design/docs trees."
    )
    ap.add_argument(
        "targets",
        nargs="*",
        default=list(DEFAULT_TARGETS),
        help="Files/directories to scan (default: designs/examples designs/linxcore docs README.md)",
    )
    args = ap.parse_args()

    root = Path(__file__).resolve().parents[2]
    violations = 0

    for target in args.targets:
        target_path = (root / target).resolve() if not Path(target).is_absolute() else Path(target)
        for f in iter_target_files(target_path):
            rel = f.relative_to(root) if f.is_relative_to(root) else f
            rel_posix = rel.as_posix() if isinstance(rel, Path) else str(rel)
            extra = EXAMPLES_ONLY_PATTERNS if rel_posix.startswith("designs/examples/") else ()
            for line_no, col, label in scan_file(f, extra_patterns=extra):
                print(f"{rel}:{line_no}:{col}: forbidden legacy API pattern `{label}`")
                violations += 1

    if violations:
        print(f"error: found {violations} forbidden legacy API pattern(s)")
        return 1
    print("ok: no forbidden legacy API patterns found")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
