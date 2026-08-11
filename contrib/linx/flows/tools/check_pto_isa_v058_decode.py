#!/usr/bin/env python3
"""Check Linx pyCircuit example decode tables against PTO ISA 0.58 headers."""

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
    28,
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
)

EXPECTED_TLSU_HEADER_MATCHES = (
    0x00011181,
    0x00111181,
    0x00211181,
    0x00311181,
    0x00411181,
    0x00511181,
    0x00611181,
    0x00711181,
    0x00811181,
    0x00D11181,
)

EXPECTED_CUBE_HEADER_MATCHES = (
    0x00031181,
    0x00131181,
    0x00231181,
    0x00431181,
    0x00531181,
    0x00631181,
    0x01031181,
    0x01131181,
    0x01231181,
    0x01431181,
    0x01531181,
    0x01631181,
)

REQUIRED_PATTERNS = (
    ("BSTART.TEPL", "mask=0x000FFFFF, match=0x00019181"),
    ("B.IOS", "mask=0xF00871FF, match=0x00001013"),
    ("B.IOT two-source", "mask=0x0000707F, match=0x00004013"),
    ("B.IOT one-source", "mask=0xFC00707F, match=0x00005013"),
    ("B.IOT destination-only", "mask=0xFFF0707F, match=0x00006013"),
)

FORBIDDEN_PATTERNS = (
    "mask=0x060FFFFF, match=0x00011181",
    "mask=0x060FFFFF, match=0x00031181",
    "mask=0x0000607F, match=0x00004013",
    "mask=0xC03FFFFF, match=0x00006013",
    "PTO_TEPL_SELECTORS_V0571",
)

FORBIDDEN_REGEXES = (
    re.compile(r"\bBSTART\.TMA\b"),
    re.compile(r"\bBSTART\.CUBE\b"),
)

RETIRED_TILE_OPS = (
    "TPRELU",
    "TAXPY",
    "TDEINTERLEAVE",
    "TINTERLEAVE",
    "TGATHERB",
    "TRESHAPE",
    "TALLOC",
    "TFREE",
    "TPUSH",
    "TPOP",
    "TPARTARGMAX",
    "TPARTARGMIN",
    "ACCCVT",
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
        if "tepl_selector_valid" not in text or "PTO_TEPL_SELECTORS" not in text:
            errors.append(
                f"{decode_file}: TEPL family decode is not gated by the exact accepted selector set"
            )
        if (
            "PTO_TLSU_HEADER_MATCHES" not in text
            or "PTO_CUBE_HEADER_MATCHES" not in text
        ):
            errors.append(
                f"{decode_file}: TLSU/CUBE header decode is not gated by the exact named-form sets"
            )

    expected_constants = {
        "PTO_TEPL_SELECTORS": EXPECTED_TEPL_SELECTORS,
        "PTO_TLSU_HEADER_MATCHES": EXPECTED_TLSU_HEADER_MATCHES,
        "PTO_CUBE_HEADER_MATCHES": EXPECTED_CUBE_HEADER_MATCHES,
    }
    for isa_file in ISA_FILES:
        tree = ast.parse(isa_file.read_text(encoding="utf-8"), filename=str(isa_file))
        actual_constants: dict[str, tuple[int, ...]] = {}
        for node in tree.body:
            if (
                isinstance(node, ast.Assign)
                and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name)
                and node.targets[0].id in expected_constants
            ):
                actual_constants[node.targets[0].id] = tuple(
                    ast.literal_eval(node.value)
                )
        for name, expected in expected_constants.items():
            if actual_constants.get(name) != expected:
                errors.append(
                    f"{isa_file}: {name} differs from the exact PTO ISA 0.58 map"
                )

    search_roots = (
        ROOT / "contrib/linx/designs/examples/linx_cpu_pyc",
        ROOT / "contrib/linx/designs/examples/linxcore_inorder",
    )
    for search_root in search_roots:
        for path in search_root.rglob("*.py"):
            text = path.read_text(encoding="utf-8")
            for opname in RETIRED_TILE_OPS:
                if opname in text:
                    errors.append(
                        f"{path}: retired PTO ISA 0.58 tile op remains: {opname}"
                    )

    if errors:
        for error in errors:
            logging.error(error)
        return 1

    logging.info("pyCircuit PTO ISA 0.58 decode guard: ok")
    return 0


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    raise SystemExit(main())
