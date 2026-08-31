#!/usr/bin/env python3
"""Upgrade historical pyCircuit commit fixtures to the current trace shape."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def _instruction_lengths(rows: list[dict[str, object]]) -> list[int]:
    lengths = [0] * len(rows)
    begin = 0
    while begin < len(rows):
        pc = int(rows[begin].get("pc", 0))
        insn = int(rows[begin].get("insn", 0))
        end = begin + 1
        while (
            end < len(rows)
            and int(rows[end].get("pc", 0)) == pc
            and int(rows[end].get("insn", 0)) == insn
        ):
            end += 1
        candidates = {
            int(row.get("next_pc", pc)) - pc
            for row in rows[begin:end]
            if int(row.get("next_pc", pc)) > pc
        }
        candidates = {value for value in candidates if value in {2, 4, 6, 8}}
        length = min(candidates) if candidates else (2 if insn & 0b11 != 0b11 else 4)
        for index in range(begin, end):
            lengths[index] = length
        begin = end
    return lengths


def normalize(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    lengths = _instruction_lengths(rows)
    normalized: list[dict[str, object]] = []
    for index, source in enumerate(rows):
        row = dict(source)
        row.setdefault("len", lengths[index])
        row.setdefault("src0_valid", 0)
        row.setdefault("src0_reg", 0)
        row.setdefault("src0_data", 0)
        row.setdefault("src1_valid", 0)
        row.setdefault("src1_reg", 0)
        row.setdefault("src1_data", 0)
        row.setdefault("dst_valid", row.get("wb_valid", 0))
        row.setdefault("dst_reg", row.get("wb_rd", 0))
        row.setdefault("dst_data", row.get("wb_data", 0))
        row.setdefault("mem_is_store", 0)
        row.setdefault("traparg0", 0)
        normalized.append(row)
    return normalized


def main() -> int:
    parser = argparse.ArgumentParser(allow_abbrev=False)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    arguments = parser.parse_args()

    rows = [
        json.loads(line)
        for line in arguments.input.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    if not rows or not all(isinstance(row, dict) for row in rows):
        parser.error("commit trace must contain JSON objects")
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(
        "".join(json.dumps(row, sort_keys=True) + "\n" for row in normalize(rows)),
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
