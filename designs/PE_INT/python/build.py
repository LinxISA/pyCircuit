from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


_DELIVERABLE_MODULE_RENAMES = {
    "pyc_reg": "PYC_REG",
    "pyc_fifo": "PYC_FIFO",
    "pyc_byte_mem": "PYC_BYTE_MEM",
    "pyc_sync_mem": "PYC_SYNC_MEM",
    "pyc_sync_mem_dp": "PYC_SYNC_MEM_DP",
    "pyc_async_fifo": "PYC_ASYNC_FIFO",
    "pyc_cdc_sync": "PYC_CDC_SYNC",
}


def _normalize_module_identifier_case(verilog_text: str) -> str:
    """Use uppercase module identifiers in deliverable RTL while keeping net names intact."""
    for source_name, deliverable_name in _DELIVERABLE_MODULE_RENAMES.items():
        verilog_text = re.sub(rf"\b{re.escape(source_name)}\b", deliverable_name, verilog_text)
    return verilog_text


def _normalize_primitives_reset_style(primitives_text: str) -> str:
    """Make generated PE_INT primitives match the deliverable reset contract."""
    sync_reg = """// --- pyc_reg.v
// Simple synchronous reset register (prototype).
module pyc_reg #(
  parameter WIDTH = 1
) (
  input             clk,
  input             rst,
  input             en,
  input  [WIDTH-1:0] d,
  input  [WIDTH-1:0] init,
  output reg [WIDTH-1:0] q
);
  always @(posedge clk) begin
    if (rst)
      q <= init;
    else if (en)
      q <= d;
  end
endmodule
"""
    async_reg = """// --- pyc_reg.v
// Register with asynchronous reset assertion for PE_INT deliverable RTL.
module pyc_reg #(
  parameter WIDTH = 1
) (
  input             clk,
  input             rst,
  input             en,
  input  [WIDTH-1:0] d,
  input  [WIDTH-1:0] init,
  output reg [WIDTH-1:0] q
);
  always @(posedge clk or posedge rst) begin
    if (rst)
      q <= init;
    else if (en)
      q <= d;
  end
endmodule
"""
    if async_reg in primitives_text:
        return primitives_text
    if sync_reg not in primitives_text:
        raise RuntimeError("Unable to locate expected pyc_reg primitive for reset normalization.")
    return primitives_text.replace(sync_reg, async_reg, 1)


def _find_module_port_list_end(verilog_text: str, module_name: str) -> int:
    match = re.search(rf"\bmodule\s+{re.escape(module_name)}\b", verilog_text)
    if not match:
        raise RuntimeError(f"Unable to locate module declaration for {module_name}.")

    idx = match.end()
    while idx < len(verilog_text):
        if verilog_text.startswith("//", idx):
            newline = verilog_text.find("\n", idx)
            idx = len(verilog_text) if newline < 0 else newline + 1
            continue
        if verilog_text.startswith("/*", idx):
            end_comment = verilog_text.find("*/", idx + 2)
            if end_comment < 0:
                raise RuntimeError(f"Unterminated block comment in module {module_name} declaration.")
            idx = end_comment + 2
            continue
        if verilog_text[idx] == "(":
            depth = 1
            idx += 1
            while idx < len(verilog_text) and depth:
                if verilog_text.startswith("//", idx):
                    newline = verilog_text.find("\n", idx)
                    idx = len(verilog_text) if newline < 0 else newline + 1
                    continue
                if verilog_text.startswith("/*", idx):
                    end_comment = verilog_text.find("*/", idx + 2)
                    if end_comment < 0:
                        raise RuntimeError(f"Unterminated block comment in module {module_name} port list.")
                    idx = end_comment + 2
                    continue
                if verilog_text[idx] == "(":
                    depth += 1
                elif verilog_text[idx] == ")":
                    depth -= 1
                idx += 1
            if depth:
                raise RuntimeError(f"Unterminated module port list for {module_name}.")
            while idx < len(verilog_text) and verilog_text[idx].isspace():
                idx += 1
            if idx < len(verilog_text) and verilog_text[idx] == ";":
                return idx + 1
            raise RuntimeError(f"Module {module_name} port list is not followed by a semicolon.")
        idx += 1

    raise RuntimeError(f"Unable to find end of port list for module {module_name}.")


def _insert_internal_reset_wire(top_text: str, module_name: str = "PE_INT") -> str:
    reset_decl = "\n\nwire rst;\nassign rst = ~rst_n;\n"
    if "assign rst = ~rst_n;" in top_text:
        return top_text
    insert_idx = _find_module_port_list_end(top_text, module_name)
    return top_text[:insert_idx] + reset_decl + top_text[insert_idx:]


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
    for stale_verilog in rtl_dir.glob("*.v"):
        stale_verilog.unlink()
    dst_top = rtl_dir / "pe_int.v"
    dst_primitives = rtl_dir / "pyc_primitives.v"
    dst_submodules: list[Path] = []

    top_text = design_verilog.read_text(encoding="utf-8")
    if design_name != "PE_INT":
        top_text = re.sub(rf"\bmodule\s+{re.escape(design_name)}\b", "module PE_INT", top_text, count=1)
        top_text = re.sub(rf"\bendmodule\s*//\s*{re.escape(design_name)}\b", "endmodule // PE_INT", top_text)

    if re.search(r"^\s*input\s+rst,\s*$", top_text, flags=re.MULTILINE):
        top_text = re.sub(r"^\s*input\s+rst,\s*$\n?", "", top_text, count=1, flags=re.MULTILINE)
        top_text = _insert_internal_reset_wire(top_text)

    dst_top.write_text(_normalize_module_identifier_case(top_text), encoding="utf-8")
    primitives_text = primitives_verilog.read_text(encoding="utf-8")
    primitives_text = _normalize_primitives_reset_style(primitives_text)
    dst_primitives.write_text(_normalize_module_identifier_case(primitives_text), encoding="utf-8")

    for module_dir in sorted(path for path in verilog_root.iterdir() if path.is_dir()):
        if module_dir.name == design_name:
            continue
        module_verilog = module_dir / f"{module_dir.name}.v"
        if not module_verilog.exists():
            continue
        dst_module = rtl_dir / f"{module_dir.name.lower()}.v"
        module_text = module_verilog.read_text(encoding="utf-8")
        dst_module.write_text(_normalize_module_identifier_case(module_text), encoding="utf-8")
        dst_submodules.append(dst_module)

    filelist = repo_root / "filelist" / "pe_int.f"
    filelist.parent.mkdir(parents=True, exist_ok=True)
    filelist_lines = ["$PE_INT/rtl/build/pyc_primitives.v"]
    filelist_lines.extend(f"$PE_INT/rtl/build/{path.name}" for path in dst_submodules)
    filelist_lines.append("$PE_INT/rtl/build/pe_int.v")
    filelist.write_text("\n".join(filelist_lines) + "\n", encoding="utf-8")


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
    shutil.rmtree(out_dir / "device", ignore_errors=True)
    shutil.rmtree(out_dir / "cpp_build", ignore_errors=True)

    env = os.environ.copy()
    py_path = env.get("PYTHONPATH", "")
    if str(repo_root / "python") not in py_path:
        env["PYTHONPATH"] = str(repo_root / "python") + (os.pathsep + py_path if py_path else "")
    env["PE_INT_PYC_TB_VEC_PER_CASE"] = str(args.pyc_tb_vectors)

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
    result = subprocess.call(cmd, cwd=str(repo_root), env=env)
    if result:
        return int(result)

    design_name = _load_current_design_name(repo_root)
    _sync_generated_rtl(repo_root, out_dir, design_name)
    print(f"Synced generated RTL into {repo_root / 'rtl' / 'build'} and refreshed filelist/pe_int.f")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
