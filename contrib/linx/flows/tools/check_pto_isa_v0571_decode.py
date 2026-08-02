#!/usr/bin/env python3
"""Check Linx pyCircuit example decode tables against PTO ISA 0.57.1 headers."""

from __future__ import annotations

import ast
import logging
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
DECODE_FILES = (
    ROOT / "contrib/linx/designs/examples/linx_cpu_pyc/decode.py",
    ROOT / "contrib/linx/designs/examples/linxcore_inorder/decode.py",
)
ISA_FILES = (
    ROOT / "contrib/linx/designs/examples/linx_cpu_pyc/isa.py",
    ROOT / "contrib/linx/designs/examples/linxcore_inorder/isa.py",
)

EXPECTED_TEPL_SELECTORS = (
    0,
    1,
    2,
    3,
    4,
    6,
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    15,
    16,
    17,
    18,
    19,
    20,
    21,
    22,
    23,
    26,
    27,
    32,
    33,
    34,
    35,
    36,
    38,
    39,
    40,
    41,
    42,
    43,
    44,
    45,
    47,
    58,
    59,
    64,
    65,
    66,
    67,
    68,
    69,
    70,
    71,
    72,
    73,
    74,
    75,
    76,
    77,
    80,
    81,
    82,
    83,
    84,
    85,
    86,
    87,
    88,
    89,
    90,
    91,
    92,
    93,
    96,
    97,
    98,
    99,
    100,
    101,
    102,
    103,
    104,
    106,
    107,
    108,
    109,
    110,
    111,
    112,
    113,
    114,
    115,
    116,
    117,
    118,
    119,
    120,
    121,
    122,
    123,
    124,
    125,
)

REQUIRED_PATTERNS = (
    ("BSTART.TEPL", "mask=0x000FFFFF, match=0x00019181"),
    ("BSTART.TLOAD", "mask=0x07FFFFFF, match=0x00011181"),
    ("BSTART.TMATMUL", "mask=0x07FFFFFF, match=0x00031181"),
)

FORBIDDEN_PATTERNS = (
    "mask=0x060FFFFF, match=0x00011181",
    "mask=0x060FFFFF, match=0x00031181",
)

FORBIDDEN_REGEXES = (
    re.compile(r"\bBSTART\.TMA\b"),
    re.compile(r"\bBSTART\.CUBE\b"),
)

DELETED_TILE_OPS = (
    "TFMA",
    "TFMOD",
    "TFMODS",
    "TADDC",
    "TSUBC",
    "TADDSC",
    "TSUBSC",
    "TLRELU",
    "TRANDOM",
)


def main() -> int:
    errors: list[str] = []

    for decode_file in DECODE_FILES:
        text = decode_file.read_text(encoding="utf-8")
        for label, pattern in REQUIRED_PATTERNS:
            if pattern not in text:
                errors.append(
                    f"{decode_file}: missing {label} exact decode pattern {pattern}"
                )
        for pattern in FORBIDDEN_PATTERNS:
            if pattern in text:
                errors.append(
                    f"{decode_file}: stale 0.56-era generic decode marker remains: {pattern}"
                )
        for regex in FORBIDDEN_REGEXES:
            if regex.search(text):
                errors.append(
                    f"{decode_file}: stale 0.56-era generic decode mnemonic remains: {regex.pattern}"
                )
        if "tepl_selector_valid" not in text or "PTO_TEPL_SELECTORS_V0571" not in text:
            errors.append(
                f"{decode_file}: TEPL family decode is not gated by the exact accepted selector set"
            )

    for isa_file in ISA_FILES:
        tree = ast.parse(isa_file.read_text(encoding="utf-8"), filename=str(isa_file))
        actual = None
        for node in tree.body:
            if (
                isinstance(node, ast.Assign)
                and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name)
                and node.targets[0].id == "PTO_TEPL_SELECTORS_V0571"
            ):
                actual = tuple(ast.literal_eval(node.value))
                break
        if actual != EXPECTED_TEPL_SELECTORS:
            errors.append(
                f"{isa_file}: accepted TEPL selectors differ from the exact 98-op PTO map"
            )

    search_roots = (
        ROOT / "contrib/linx/designs/examples/linx_cpu_pyc",
        ROOT / "contrib/linx/designs/examples/linxcore_inorder",
    )
    for search_root in search_roots:
        for path in search_root.rglob("*.py"):
            text = path.read_text(encoding="utf-8")
            for opname in DELETED_TILE_OPS:
                if opname in text:
                    errors.append(
                        f"{path}: deleted PTO ISA 0.57.1 tile op remains: {opname}"
                    )

    if errors:
        for error in errors:
            logging.error(error)
        return 1

    logging.info("pyCircuit PTO ISA 0.57.1 decode guard: ok")
    return 0


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    raise SystemExit(main())
