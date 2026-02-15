#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def _detect_pyc_compile(root: Path) -> Path:
    env = os.environ.get("PYC_COMPILE")
    if env:
        p = Path(env)
        if p.is_file() and os.access(p, os.X_OK):
            return p
        raise SystemExit(f"PYC_COMPILE is set but not executable: {p}")

    candidates = [
        root / "build" / "bin" / "pyc-compile",
        root / "compiler" / "mlir" / "build" / "bin" / "pyc-compile",
        root / "build-top" / "bin" / "pyc-compile",
    ]
    best: Path | None = None
    best_mtime = -1.0
    for c in candidates:
        if c.is_file() and os.access(c, os.X_OK):
            mtime = c.stat().st_mtime
            if mtime > best_mtime:
                best = c
                best_mtime = mtime
    if best is not None:
        return best

    found = shutil.which("pyc-compile")
    if found:
        return Path(found)
    raise SystemExit("missing pyc-compile (set PYC_COMPILE=... or build it first)")


def _pythonpath(root: Path) -> str:
    parts = [str(root / "compiler" / "frontend"), str(root / "designs")]
    old = os.environ.get("PYTHONPATH")
    if old:
        parts.append(old)
    return ":".join(parts)


def _run(
    cmd: list[str],
    *,
    cwd: Path,
    env: dict[str, str],
    capture_stdout: bool = False,
) -> tuple[float, str]:
    start = time.perf_counter()
    proc = subprocess.run(
        cmd,
        cwd=str(cwd),
        env=env,
        text=True,
        capture_output=capture_stdout,
        check=True,
    )
    elapsed = time.perf_counter() - start
    out = proc.stdout if capture_stdout else ""
    return elapsed, out


def _count_lines(path: Path) -> int:
    with path.open("r", encoding="utf-8", errors="ignore") as f:
        return sum(1 for _ in f)


def _parse_cycles(output: str) -> int:
    m = re.search(r"cycles=(\d+)", output)
    if not m:
        return 0
    return int(m.group(1))


def _stats(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def _build_flags(profile: str) -> list[str]:
    if profile == "dev":
        return ["-std=c++17", "-O1"]
    if profile == "release":
        return ["-std=c++17", "-O2", "-DNDEBUG"]
    raise SystemExit(f"unsupported --profile={profile!r} (expected dev|release)")


def _run_linx_case(
    root: Path,
    pyc_compile: Path,
    profile: str,
    logic_depth: int,
    sim_mode: str,
    perf_repeats: int,
    perf_max_cycles: int,
) -> dict[str, Any]:
    out_dir = root / ".pycircuit_out" / "perf" / "linx_cpu"
    out_dir.mkdir(parents=True, exist_ok=True)
    pyc_path = out_dir / "linx_cpu_pyc.pyc"
    hdr_path = out_dir / "linx_cpu_pyc_gen.hpp"
    tb_path = out_dir / f"tb_linx_cpu_pyc_cpp_{profile}"
    stats_path = Path(str(hdr_path) + ".stats.json")

    env_emit = os.environ.copy()
    env_emit["PYTHONDONTWRITEBYTECODE"] = "1"
    env_emit["PYTHONPATH"] = _pythonpath(root)

    emit_s, _ = _run(
        [
            sys.executable,
            "-m",
            "pycircuit.cli",
            "emit",
            "designs/examples/linx_cpu_pyc/linx_cpu_pyc.py",
            "--param",
            "mem_bytes=1048576",
            "-o",
            str(pyc_path),
        ],
        cwd=root,
        env=env_emit,
    )

    compile_s, _ = _run(
        [
            str(pyc_compile),
            str(pyc_path),
            "--emit=cpp",
            f"--sim-mode={sim_mode}",
            f"--logic-depth={logic_depth}",
            "-o",
            str(hdr_path),
        ],
        cwd=root,
        env=os.environ.copy(),
    )

    cxx = os.environ.get("CXX", "clang++")
    tb_build_s, _ = _run(
        [
            cxx,
            *_build_flags(profile),
            "-I",
            str(root / "runtime"),
            "-I",
            str(out_dir),
            "-o",
            str(tb_path),
            str(root / "designs" / "examples" / "linx_cpu_pyc" / "tb_linx_cpu_pyc.cpp"),
        ],
        cwd=root,
        env=os.environ.copy(),
    )

    env_run = os.environ.copy()
    env_run.setdefault("PYC_KONATA", "0")
    perf_memh = str(root / "designs" / "examples" / "linx_cpu" / "programs" / "test_csel_fixed.memh")
    sim_s, sim_out = _run(
        [
            str(tb_path),
            "--perf",
            "--perf-repeat",
            str(int(perf_repeats)),
            "--perf-max-cycles",
            str(int(perf_max_cycles)),
            "--perf-memh",
            perf_memh,
        ],
        cwd=root,
        env=env_run,
        capture_stdout=True,
    )
    cycles = _parse_cycles(sim_out)
    end_to_end_s = emit_s + compile_s + tb_build_s + sim_s
    cps = (cycles / sim_s) if sim_s > 0 else 0.0

    return {
        "emit_s": emit_s,
        "compile_s": compile_s,
        "tb_build_s": tb_build_s,
        "sim_s": sim_s,
        "end_to_end_s": end_to_end_s,
        "cycles": cycles,
        "cycles_per_sec": cps,
        "perf_repeats": int(perf_repeats),
        "perf_max_cycles": int(perf_max_cycles),
        "header_loc": _count_lines(hdr_path),
        "compile_stats": _stats(stats_path),
    }


def _run_linxcore_case(
    root: Path,
    pyc_compile: Path,
    profile: str,
    logic_depth: int,
    sim_mode: str,
    perf_repeats: int,
    perf_max_cycles: int,
) -> dict[str, Any]:
    out_dir = root / ".pycircuit_out" / "perf" / "linxcore"
    out_dir.mkdir(parents=True, exist_ok=True)

    env_run = os.environ.copy()
    env_run["PYC_COMPILE"] = str(pyc_compile)
    env_run["PYC_LOGIC_DEPTH"] = str(int(logic_depth))
    env_run.setdefault("PYC_KONATA", "0")
    env_run["PYC_MAX_CYCLES"] = str(int(perf_max_cycles))
    env_run["CORE_ITERATIONS"] = str(int(perf_repeats))
    env_run["DHRY_RUNS"] = str(int(perf_repeats) * 100)

    emit_s, bench_build_out = _run(
        ["bash", str(root / "designs" / "linxcore" / "tools" / "image" / "build_linxisa_benchmarks_memh_compat.sh")],
        cwd=root,
        env=env_run,
        capture_stdout=True,
    )
    memh_lines = [ln.strip() for ln in bench_build_out.splitlines() if ln.strip()]
    if len(memh_lines) < 2:
        raise RuntimeError("failed to build linxcore benchmark memh")
    perf_memh = memh_lines[1]

    compile_s, _ = _run(
        ["bash", str(root / "designs" / "linxcore" / "tools" / "generate" / "update_generated_linxcore.sh")],
        cwd=root,
        env=env_run,
    )

    tb_build_s = 0.0
    sim_s, sim_out = _run(
        ["bash", str(root / "designs" / "linxcore" / "tools" / "generate" / "run_linxcore_top_cpp.sh"), perf_memh],
        cwd=root,
        env=env_run,
        capture_stdout=True,
    )
    cycles = _parse_cycles(sim_out)
    end_to_end_s = emit_s + compile_s + tb_build_s + sim_s
    cps = (cycles / sim_s) if sim_s > 0 else 0.0

    return {
        "emit_s": emit_s,
        "compile_s": compile_s,
        "tb_build_s": tb_build_s,
        "sim_s": sim_s,
        "end_to_end_s": end_to_end_s,
        "cycles": cycles,
        "cycles_per_sec": cps,
        "perf_repeats": int(perf_repeats),
        "perf_max_cycles": int(perf_max_cycles),
        "header_loc": 0,
        "compile_stats": {},
    }


def main() -> int:
    root = _repo_root()
    ap = argparse.ArgumentParser(description="Run pyCircuit Linx+LinxCore perf smoke and emit JSON metrics.")
    ap.add_argument(
        "--output",
        default=str(root / ".pycircuit_out" / "perf" / "perf_smoke.json"),
        help="Output JSON path",
    )
    ap.add_argument("--profile", choices=["dev", "release"], default=os.environ.get("PYC_BUILD_PROFILE", "release"))
    ap.add_argument("--logic-depth", type=int, default=32)
    ap.add_argument("--sim-mode", choices=["default", "cpp-only"], default="cpp-only")
    ap.add_argument("--perf-repeats-linx", type=int, default=16)
    ap.add_argument("--perf-repeats-linxcore", type=int, default=16)
    ap.add_argument("--perf-max-cycles", type=int, default=4096)
    args = ap.parse_args()

    pyc_compile = _detect_pyc_compile(root)
    result = {
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "profile": str(args.profile),
        "sim_mode": str(args.sim_mode),
        "logic_depth": int(args.logic_depth),
        "pyc_compile": str(pyc_compile),
        "cases": {},
    }

    result["cases"]["linx_cpu"] = _run_linx_case(
        root,
        pyc_compile,
        profile=str(args.profile),
        logic_depth=int(args.logic_depth),
        sim_mode=str(args.sim_mode),
        perf_repeats=int(args.perf_repeats_linx),
        perf_max_cycles=int(args.perf_max_cycles),
    )
    result["cases"]["linxcore"] = _run_linxcore_case(
        root,
        pyc_compile,
        profile=str(args.profile),
        logic_depth=int(args.logic_depth),
        sim_mode=str(args.sim_mode),
        perf_repeats=int(args.perf_repeats_linxcore),
        perf_max_cycles=int(args.perf_max_cycles),
    )

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
