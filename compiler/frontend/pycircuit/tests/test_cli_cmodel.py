from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

from pycircuit.cli import (
    _TopIface,
    _collect_testbench_payload,
    _emit_cmodel_scaffold,
    _find_testbench_fn,
)
from pycircuit.design import testbench


def test_find_testbench_fn_is_optional() -> None:
    assert _find_testbench_fn(SimpleNamespace()) is None


def test_find_testbench_fn_rejects_undecorated_tb() -> None:
    with pytest.raises(SystemExit, match="decorated with `@testbench`"):
        _find_testbench_fn(SimpleNamespace(tb=lambda t: None))


def test_collect_testbench_payload_exports_program_without_backend_text() -> None:
    iface = _TopIface(sym="DemoTop", in_raw=["clk", "a"], in_tys=["i1", "i8"], out_raw=["y"], out_tys=["i8"])

    @testbench
    def tb(t) -> None:
        t.clock("clk")
        t.timeout(4)
        t.finish(at=1)

    tb_name, payload_json, program = _collect_testbench_payload(tb, iface)

    payload = json.loads(payload_json)
    assert tb_name == "tb_DemoTop"
    assert program["tb_name"] == tb_name
    assert "cpp_text" in payload
    assert "sv_text" in payload
    assert "cpp_text" not in program
    assert "sv_text" not in program


def test_emit_cmodel_scaffold_writes_bridge_and_program(tmp_path: Path) -> None:
    iface = _TopIface(sym="DemoTop", in_raw=["clk", "a"], in_tys=["i1", "i8"], out_raw=["y"], out_tys=["i8"])
    tb_program = {"tb_name": "tb_DemoTop", "timeout_cycles": 4, "ports": {"inputs": [], "outputs": []}}

    paths = _emit_cmodel_scaffold(out_dir=tmp_path, iface=iface, tb_program=tb_program)

    bridge_text = paths["bridge_hpp"].read_text(encoding="utf-8")
    main_text = paths["entry_cpp"].read_text(encoding="utf-8")
    readme_text = paths["readme"].read_text(encoding="utf-8")
    tb_program_text = paths["tb_program"].read_text(encoding="utf-8")

    assert "pyc::gen::DemoTop" in bridge_text
    assert "Stimulus bridge: tb_program.json" in main_text
    assert "dut.a" in readme_text
    assert json.loads(tb_program_text)["tb_name"] == "tb_DemoTop"


def test_gen_cmake_from_manifest_supports_entry_cpp(tmp_path: Path) -> None:
    src = tmp_path / "dut.cpp"
    entry = tmp_path / "main.cpp"
    runtime_src = tmp_path / "pyc_runtime.cpp"
    src.write_text("int dut_helper() { return 0; }\n", encoding="utf-8")
    entry.write_text("int main() { return 0; }\n", encoding="utf-8")
    runtime_src.write_text("void pyc_runtime_stub() {}\n", encoding="utf-8")

    manifest = tmp_path / "cpp_project_manifest.json"
    manifest.write_text(
        json.dumps(
            {
                "sources": [str(src)],
                "entry_cpp": str(entry),
                "include_dirs": [str(tmp_path)],
                "runtime_sources": [str(runtime_src)],
                "cxx_standard": "c++17",
                "executable_name": "pyc_cmodel",
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )

    out_dir = tmp_path / "cmake"
    script = Path(__file__).resolve().parents[4] / "flows" / "tools" / "gen_cmake_from_manifest.py"
    subprocess.run(
        [sys.executable, str(script), "--manifest", str(manifest), "--out-dir", str(out_dir)],
        check=True,
    )

    cmake_text = (out_dir / "CMakeLists.txt").read_text(encoding="utf-8")
    assert "add_executable(pyc_cmodel ${PYC_TB_SOURCES})" in cmake_text
    assert "\"main.cpp\"" in cmake_text
