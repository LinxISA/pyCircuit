#!/usr/bin/env python3
"""Reject product-version and implementation-phase tokens in tracked paths."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


FORBIDDEN = re.compile(
    r"(?:^|[-_./])(?:v0?\d+(?:[._]\d+)*|phase[-_]?\d+[a-z]?)"
    r"(?:[-_./]|$)",
    re.IGNORECASE,
)
REQUIRED_ROOTS = (
    Path("docs/spec"),
    Path("examples/pipelines"),
    Path("examples/memory"),
    Path("examples/blocks"),
    Path("examples/architecture"),
    Path("examples/workspaces"),
    Path("references"),
    Path("tests/goldens"),
)


def tracked_paths(root: Path) -> list[str]:
    completed = subprocess.run(
        ("git", "ls-files"),
        cwd=root,
        text=True,
        capture_output=True,
        check=True,
    )
    return completed.stdout.splitlines()


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    errors = [
        f"forbidden release/phase token in tracked path: {file_path}"
        for file_path in tracked_paths(root)
        if FORBIDDEN.search(file_path)
    ]
    errors.extend(
        f"required semantic root is missing: {required}"
        for required in REQUIRED_ROOTS
        if not (root / required).is_dir()
    )
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("release layout: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
