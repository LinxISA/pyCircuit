#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def _load(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise SystemExit("manifest must be a JSON object")
    return data


def _rel(path: Path, base: Path) -> str:
    try:
        return str(path.resolve().relative_to(base.resolve()))
    except Exception:
        return str(path.resolve())


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate CMake project from pyCircuit cpp manifest")
    ap.add_argument("--manifest", required=True, help="Path to cpp_project_manifest.json")
    ap.add_argument("--out-dir", required=True, help="Directory to write CMakeLists.txt")
    args = ap.parse_args()

    manifest_path = Path(args.manifest).resolve()
    out_dir = Path(args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    data = _load(manifest_path)

    srcs = [Path(s).resolve() for s in data.get("sources", []) if isinstance(s, str) and s]
    tb_cpp = Path(str(data.get("tb_cpp", ""))).resolve()
    incs = [Path(s).resolve() for s in data.get("include_dirs", []) if isinstance(s, str) and s]
    std = str(data.get("cxx_standard", "c++17"))

    if not srcs:
        raise SystemExit("manifest missing `sources`")
    if not tb_cpp.is_file():
        raise SystemExit(f"missing tb cpp: {tb_cpp}")
    for s in srcs:
        if not s.is_file():
            raise SystemExit(f"missing source: {s}")

    lines: list[str] = []
    lines.append("cmake_minimum_required(VERSION 3.20)\n")
    lines.append("project(pyc_tb LANGUAGES CXX)\n")
    lines.append(f"set(CMAKE_CXX_STANDARD {std.replace('c++', '')})\n")
    lines.append("set(CMAKE_CXX_STANDARD_REQUIRED ON)\n")
    lines.append("set(CMAKE_CXX_EXTENSIONS OFF)\n\n")

    lines.append("set(PYC_TB_SOURCES\n")
    for s in srcs:
        lines.append(f"  \"{_rel(s, out_dir)}\"\n")
    lines.append(f"  \"{_rel(tb_cpp, out_dir)}\"\n")
    lines.append(")\n\n")

    lines.append("add_executable(pyc_tb ${PYC_TB_SOURCES})\n")
    if incs:
        lines.append("target_include_directories(pyc_tb PRIVATE\n")
        for i in incs:
            lines.append(f"  \"{_rel(i, out_dir)}\"\n")
        lines.append(")\n")
    lines.append("\n")

    out = out_dir / "CMakeLists.txt"
    text = "".join(lines)
    if out.is_file():
        try:
            if out.read_text(encoding="utf-8") == text:
                print(str(out))
                return 0
        except OSError:
            pass
    out.write_text(text, encoding="utf-8")
    print(str(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
