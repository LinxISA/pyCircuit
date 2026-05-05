from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path
from shutil import copy2


def _load_current_design_name(repo_root: Path) -> str:
    py_dir = repo_root / "python"
    if str(py_dir) not in sys.path:
        sys.path.insert(0, str(py_dir))
    try:
        import pe_int_pycircuit  # type: ignore
    except Exception as exc:  # pragma: no cover - environment-dependent import path.
        raise RuntimeError(f"Unable to import pe_int_pycircuit to resolve design name: {exc}") from exc
    design_name = getattr(pe_int_pycircuit.build, "__pycircuit_name__", "")
    if not design_name:
        raise RuntimeError("Missing build.__pycircuit_name__ in pe_int_pycircuit.")
    return str(design_name)


def _sync_generated_rtl(repo_root: Path, out_dir: Path, design_name: str) -> None:
    verilog_root = out_dir / "device" / "verilog"
    design_dir = verilog_root / design_name
    design_verilog = design_dir / f"{design_name}.v"
    primitives_verilog = design_dir / "pyc_primitives.v"
    if not design_verilog.exists():
        raise RuntimeError(f"Generated design verilog not found: {design_verilog}")
    if not primitives_verilog.exists():
        raise RuntimeError(f"Generated primitives verilog not found: {primitives_verilog}")

    rtl_dir = repo_root / "rtl" / "build"
    rtl_dir.mkdir(parents=True, exist_ok=True)
    dst_top = rtl_dir / "pe_int_l3.v"
    dst_primitives = rtl_dir / "pyc_primitives.v"

    # Keep RTL TB interface stable on pe_int_l3, while always sourcing from fresh pycc output.
    top_text = design_verilog.read_text(encoding="utf-8")
    if design_name != "pe_int_l3":
        top_text = re.sub(rf"\bmodule\s+{re.escape(design_name)}\b", "module pe_int_l3", top_text, count=1)
        top_text = re.sub(rf"\bendmodule\s*//\s*{re.escape(design_name)}\b", "endmodule // pe_int_l3", top_text)

    # Normalize external reset interface to spec: keep only rst_n as top-level input.
    # pycircuit v5 default CycleAwareDomain emits a top-level active-high `rst` port.
    # We keep internal reset net as `rst` but derive it from `rst_n` for deliverable RTL.
    if re.search(r"^\s*input\s+rst,\s*$", top_text, flags=re.MULTILINE):
        top_text = re.sub(r"^\s*input\s+rst,\s*$\n?", "", top_text, count=1, flags=re.MULTILINE)
        if "assign rst = ~rst_n;" not in top_text:
            top_text = re.sub(r"\);\n", ");\n\nwire rst;\nassign rst = ~rst_n;\n\n", top_text, count=1)

    dst_top.write_text(top_text, encoding="utf-8")
    copy2(primitives_verilog, dst_primitives)

    filelist = repo_root / "filelist" / "pe_int.f"
    filelist.parent.mkdir(parents=True, exist_ok=True)
    filelist.write_text("$PE_INT/rtl/build/pyc_primitives.v\n$PE_INT/rtl/build/pe_int_l3.v\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Build PE_INT via pyCircuit/pycc.")
    parser.add_argument("--out-dir", default="build/pe_int", help="pycircuit build output directory")
    parser.add_argument("--target", default="both", choices=("rtl", "sim", "both"), help="build target")
    parser.add_argument("--jobs", default="8", help="parallel jobs for pycircuit build")
    parser.add_argument(
        "--pyc-tb-vectors",
        default=os.environ.get("PE_INT_PYC_TB_VEC_PER_CASE", "8"),
        help="number of vectors per PyCircuit-level testcase (default: smoke-sized 8)",
    )
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
    env["PE_INT_PYC_TB_VEC_PER_CASE"] = str(args.pyc_tb_vectors)

    pycircuit_target = {
        # pycircuit.cli no longer accepts a pure `rtl` target; `verilator` emits
        # the Verilog device artifacts that build.py syncs into rtl/build.
        "rtl": "verilator",
        "sim": "verilator",
        "both": "both",
    }[args.target]

    cmd = [
        sys.executable,
        "-m",
        "pycircuit.cli",
        "build",
        str(tb_file),
        "--out-dir",
        str(out_dir),
        "--target",
        pycircuit_target,
        "--jobs",
        str(args.jobs),
    ]
    print("Running:", " ".join(cmd))
    rc = subprocess.call(cmd, cwd=str(repo_root), env=env)
    if rc != 0:
        return rc

    design_name = _load_current_design_name(repo_root)
    _sync_generated_rtl(repo_root, out_dir, design_name)
    print(f"Synced generated RTL into {repo_root / 'rtl' / 'build'} and refreshed filelist/pe_int.f")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
