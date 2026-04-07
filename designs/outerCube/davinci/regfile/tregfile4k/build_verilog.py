#!/usr/bin/env python3
"""Build Verilog RTL for the TRegFile module.

Usage:
    python build_verilog.py                 # test-size build (quick)
    python build_verilog.py --full          # full-size build (1 MB TRegFile)
    python build_verilog.py --mlir-only     # emit MLIR only, skip pycc
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

THIS_DIR = Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parents[4]  # pyCircuit repo root

sys.path.insert(0, str(THIS_DIR))
sys.path.insert(0, str(REPO_ROOT / "compiler" / "frontend"))

PYCC_SEARCH_PATHS = [
    REPO_ROOT / "build" / "bin" / "pycc",
    REPO_ROOT / "compiler" / "mlir" / "build" / "bin" / "pycc",
    REPO_ROOT / "compiler" / "mlir" / "build2" / "bin" / "pycc",
    REPO_ROOT / ".pycircuit_out" / "toolchain" / "install" / "bin" / "pycc",
    REPO_ROOT / "dist" / "pycircuit" / "bin" / "pycc",
]


def find_pycc() -> Path | None:
    env = os.environ.get("PYCC")
    if env:
        p = Path(env)
        if p.is_file() and os.access(p, os.X_OK):
            return p
    for p in PYCC_SEARCH_PATHS:
        if p.is_file() and os.access(p, os.X_OK):
            return p
    return None


def _stamp_pycc_metadata(m: Any, name: str, params_json: str = "{}") -> None:
    m.set_func_attr("pyc.kind", "module")
    m.set_func_attr("pyc.inline", "false")
    m.set_func_attr("pyc.params", params_json)
    m.set_func_attr("pyc.base", name)
    metrics = json.dumps(
        {
            "ast_node_count": 0,
            "collection_count": 0,
            "collection_instance_count": 0,
            "estimated_inline_cost": 0,
            "hardware_call_count": 0,
            "instance_count": 0,
            "loop_count": 0,
            "module_call_count": 0,
            "module_family_collection_count": 0,
            "repeat_pressure": 0,
            "repeated_body_clusters": [],
            "source_loc": 0,
            "state_alloc_count": 0,
            "state_call_count": 0,
        },
        sort_keys=True,
        separators=(",", ":"),
    )
    m.set_func_attr("pyc.struct.metrics", metrics)
    m.set_func_attr("pyc.struct.collections", "[]")
    m.set_func_attr_json("pyc.value_params", [])
    m.set_func_attr_json("pyc.value_param_types", [])


def _wrap_module_attrs(mlir: str, top_name: str) -> str:
    return mlir.replace(
        "module {\n",
        f'module attributes {{pyc.top = @{top_name}, pyc.frontend.contract = "pycircuit"}} {{\n',
        1,
    )


def run_pycc(pycc: Path, pyc_path: Path, verilog_dir: Path, *, name: str) -> Path:
    verilog_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(pycc),
        str(pyc_path),
        "--emit=verilog",
        f"--out-dir={verilog_dir}",
        "--hierarchy-policy=strict",
        "--inline-policy=off",
        "--logic-depth=128",
    ]
    subprocess.check_call(cmd)
    return verilog_dir / f"{name}.v"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build TRegFile-4K Verilog (8R8W, 64 banks)"
    )
    parser.add_argument("--full", action="store_true", help="Full-size build (1 MB)")
    parser.add_argument("--mlir-only", action="store_true", help="Emit MLIR only")
    args = parser.parse_args()

    from parameters import (
        FULL_BANK_DEPTH,
        FULL_BANK_WIDTH,
        TEST_BANK_DEPTH,
        TEST_BANK_WIDTH,
    )
    from pycircuit import compile_cycle_aware
    from tregfile import tregfile

    name = "tregfile"
    if args.full:
        kwargs = {"bank_depth": FULL_BANK_DEPTH, "bank_width": FULL_BANK_WIDTH}
        out_dir = THIS_DIR / "build_out_full"
    else:
        kwargs = {"bank_depth": TEST_BANK_DEPTH, "bank_width": TEST_BANK_WIDTH}
        out_dir = THIS_DIR / "build_out"

    t0 = time.time()

    params_json = json.dumps(kwargs, sort_keys=True, separators=(",", ":"))
    circuit = compile_cycle_aware(tregfile, name=name, eager=True, **kwargs)

    try:
        _stamp_pycc_metadata(circuit, name, params_json)
    except AttributeError:
        pass

    mlir_text = circuit.emit_mlir()
    try:
        mlir_text = _wrap_module_attrs(mlir_text, name)
    except Exception:
        pass

    mlir_dir = out_dir / "mlir"
    mlir_dir.mkdir(parents=True, exist_ok=True)
    pyc_path = mlir_dir / f"{name}.pyc"
    pyc_path.write_text(mlir_text, encoding="utf-8")
    time.time() - t0

    if args.mlir_only:
        return 0

    pycc = find_pycc()
    if pycc is None:
        return 0

    verilog_dir = out_dir / "verilog" / name
    t1 = time.time()
    run_pycc(pycc, pyc_path, verilog_dir, name=name)
    time.time() - t1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
