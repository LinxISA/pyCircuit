from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Build PE_INT via pyCircuit/pycc.")
    parser.add_argument("--out-dir", default="build/pe_int", help="pycircuit build output directory")
    parser.add_argument("--target", default="both", choices=("rtl", "sim", "both"), help="build target")
    parser.add_argument("--jobs", default="8", help="parallel jobs for pycircuit build")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    tb_file = repo_root / "tb" / "tb_pe_int_pycircuit.py"
    out_dir = repo_root / args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    py_path = env.get("PYTHONPATH", "")
    if str(repo_root / "python") not in py_path:
        env["PYTHONPATH"] = str(repo_root / "python") + (os.pathsep + py_path if py_path else "")

    # Default toolchain resolution:
    # PE_INT is expected under <pycircuit_root>/designs/PE_INT,
    # so pycircuit root can be derived by walking up two levels.
    pycircuit_root = repo_root.parents[1]
    default_toolchain_root = pycircuit_root / ".pycircuit_out" / "toolchain" / "install"
    default_pycc = default_toolchain_root / "bin" / "pycc"
    if "PYC_TOOLCHAIN_ROOT" not in env and default_toolchain_root.exists():
        env["PYC_TOOLCHAIN_ROOT"] = str(default_toolchain_root)
    if "PYCC" not in env and default_pycc.exists():
        env["PYCC"] = str(default_pycc)

    cmd = [
        sys.executable,
        "-m",
        "pycircuit.cli",
        "build",
        str(tb_file),
        "--out-dir",
        str(out_dir),
        "--target",
        args.target,
        "--jobs",
        str(args.jobs),
    ]
    print("Running:", " ".join(cmd))
    return subprocess.call(cmd, cwd=str(repo_root), env=env)


if __name__ == "__main__":
    raise SystemExit(main())
