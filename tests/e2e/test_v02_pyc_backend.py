from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from agentic_circuit._queue_frontend import lower_queue_source


ROOT = Path(__file__).resolve().parents[2]
EXAMPLE = ROOT / "examples" / "v02" / "pyc_queue_pipeline.py"
DAVINCIOO_EXAMPLE = ROOT / "examples" / "v02" / "davincioo_queue_model.py"
STRUCT_EXAMPLE = ROOT / "examples" / "v02" / "pyc_struct_pipeline.py"
ROUTE_EXAMPLE = ROOT / "examples" / "v02" / "pyc_route_merge_pipeline.py"
ATOMIC_EXAMPLE = ROOT / "examples" / "v02" / "pyc_atomic_pipeline.py"
FORK_EXAMPLE = ROOT / "examples" / "v02" / "pyc_fork_pipeline.py"
CONDITIONAL_EXAMPLE = ROOT / "examples" / "v02" / "pyc_conditional_pipeline.py"
FEEDBACK_EXAMPLE = ROOT / "examples" / "v02" / "pyc_feedback_pipeline.py"
DEFAULT_TOOLCHAIN = Path(
    "/Users/zhoubot/Documents/SummerSchool/vendor/pyCircuit/"
    ".pycircuit_out/toolchain/install"
)


class V02PycBackendTest(unittest.TestCase):
    def test_bounded_feedback_is_cycle_equivalent_in_pyc_cpp_and_verilog(
        self,
    ) -> None:
        toolchain = Path(os.environ.get("PYC_V02_TOOLCHAIN_ROOT", DEFAULT_TOOLCHAIN))
        pycc = toolchain / "bin" / "pycc"
        metadata = toolchain / "share" / "pycircuit" / "toolchain-metadata.json"
        cxx = shutil.which("c++")
        verilator = shutil.which("verilator")
        if not pycc.is_file() or not metadata.is_file() or cxx is None or verilator is None:
            self.skipTest("pinned pyCircuit toolchain, C++, or Verilator is unavailable")
        source = FEEDBACK_EXAMPLE.read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw = root / "feedback.raw.ac.mlir"
            frozen = root / "feedback.frozen.ac.mlir"
            output = root / "output"
            raw.write_text(
                lower_queue_source(source, "pyc_feedback_pipeline"),
                encoding="utf-8",
            )
            optimized = subprocess.run(
                (str(ROOT / "build/dev-llvm22/bin/acir-opt"), str(raw)),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, optimized.returncode, optimized.stderr)
            frozen.write_text(optimized.stdout, encoding="utf-8")
            completed = subprocess.run(
                (
                    str(ROOT / "tools/ac-queue-pyc-build.py"),
                    str(frozen),
                    "--pycgen-tool",
                    str(ROOT / "build/dev-llvm22/bin/acir-queue-pycgen"),
                    "--pycc",
                    str(pycc),
                    "--toolchain-lock",
                    str(ROOT / "toolchains/pyc-v0.2.lock.json"),
                    "--toolchain-metadata",
                    str(metadata),
                    "--cxx",
                    cxx,
                    "--verilator",
                    verilator,
                    "--pyc-output",
                    str(output / "model.pyc"),
                    "--cpp-output-dir",
                    str(output / "cpp"),
                    "--verilog-output-dir",
                    str(output / "verilog"),
                    "--manifest",
                    str(output / "manifest.json"),
                ),
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, completed.returncode, completed.stderr)
            pyc = (output / "model.pyc").read_text(encoding="utf-8")
            self.assertEqual(3, pyc.count("pyc.reg"))
            self.assertIn("pyc.ult", pyc)
            self.assertIn("feedback_iteration_limit", pyc)

            cpp_harness = root / "cpp_harness.cpp"
            cpp_executable = root / "cpp_model"
            cpp_harness.write_text(
                '''#include "pyc_feedback_pipeline.hpp"
#include <cstdint>
#include <iostream>

int main() {
  pyc::gen::pyc_feedback_pipeline dut;
  for (std::uint64_t cycle = 0; cycle < 14; ++cycle) {
    dut.rst = pyc::cpp::Wire<1>(cycle == 0 ? 1 : 0);
    dut.in_valid = pyc::cpp::Wire<1>(cycle == 1 ? 1 : 0);
    dut.in_data = pyc::cpp::Wire<36>(cycle == 1 ? ((10ULL << 4) | 3) : 0);
    dut.out_ready = pyc::cpp::Wire<1>(1);
    dut.clk = pyc::cpp::Wire<1>(0);
    dut.step();
    dut.clk = pyc::cpp::Wire<1>(1);
    dut.step();
    std::cout << cycle << " " << dut.out_valid.value() << " "
              << dut.out_data.value() << " " << dut.in_ready.value() << "\\n";
    dut.clk = pyc::cpp::Wire<1>(0);
    dut.step();
  }
}
''',
                encoding="utf-8",
            )
            cpp_build = subprocess.run(
                (
                    cxx,
                    "-std=c++17",
                    "-I",
                    str(output / "cpp"),
                    "-I",
                    str(toolchain / "include"),
                    str(output / "cpp/pyc_feedback_pipeline.cpp"),
                    str(cpp_harness),
                    str(toolchain / "lib/libpyc4_runtime.a"),
                    "-o",
                    str(cpp_executable),
                ),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, cpp_build.returncode, cpp_build.stderr)
            cpp_run = subprocess.run(
                (str(cpp_executable),),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, cpp_run.returncode, cpp_run.stderr)

            verilator_harness = root / "verilator_harness.cpp"
            verilator_harness.write_text(
                '''#include "Vpyc_feedback_pipeline.h"
#include <cstdint>
#include <iostream>

int main() {
  Vpyc_feedback_pipeline dut;
  for (std::uint64_t cycle = 0; cycle < 14; ++cycle) {
    dut.rst = cycle == 0 ? 1 : 0;
    dut.in_valid = cycle == 1 ? 1 : 0;
    dut.in_data = cycle == 1 ? ((10ULL << 4) | 3) : 0;
    dut.out_ready = 1;
    dut.clk = 0;
    dut.eval();
    dut.clk = 1;
    dut.eval();
    std::cout << cycle << " " << unsigned(dut.out_valid) << " "
              << dut.out_data << " " << unsigned(dut.in_ready) << "\\n";
    dut.clk = 0;
    dut.eval();
  }
}
''',
                encoding="utf-8",
            )
            object_dir = root / "verilator_obj"
            verilator_build = subprocess.run(
                (
                    verilator,
                    "--cc",
                    "--exe",
                    "--build",
                    "-Wno-fatal",
                    "--top-module",
                    "pyc_feedback_pipeline",
                    "--Mdir",
                    str(object_dir),
                    str(output / "verilog/pyc_primitives.v"),
                    str(output / "verilog/pyc_feedback_pipeline.v"),
                    str(verilator_harness),
                ),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, verilator_build.returncode, verilator_build.stderr)
            verilator_run = subprocess.run(
                (str(object_dir / "Vpyc_feedback_pipeline"),),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, verilator_run.returncode, verilator_run.stderr)
            self.assertEqual(cpp_run.stdout, verilator_run.stdout)
            transactions = [
                int(fields[2])
                for line in cpp_run.stdout.splitlines()
                if len(fields := line.split()) == 4 and fields[1] == "1"
            ]
            self.assertEqual([13 << 4], transactions)

            gfsim_model = root / "gfsim_model.cpp"
            gfsim_harness = root / "gfsim_harness.cpp"
            gfsim_executable = root / "gfsim_model"
            gfsim_generated = subprocess.run(
                (str(ROOT / "build/dev-llvm22/bin/acir-queue-cxxgen"), str(frozen)),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, gfsim_generated.returncode, gfsim_generated.stderr)
            gfsim_model.write_text(gfsim_generated.stdout, encoding="utf-8")
            gfsim_harness.write_text(
                f'''#include "{gfsim_model.name}"
#include <cstddef>
#include <iostream>

int main() {{
  ac_generated::PycFeedbackPipeline model;
  if (!model.current().proposePush(ac_generated::Item{{10, 3}}))
    return 1;
  auto rows = model.dispatch_rows();
  for (std::size_t tick = 0; tick < 14; ++tick) {{
    const gfsim::Epoch epoch{{tick, 0}};
    for (auto &row : rows)
      row.work(row.object, epoch);
    for (auto &row : rows)
      row.xfer(row.object, epoch, gfsim::XferPhase::Arbitrate);
    for (auto &row : rows)
      row.xfer(row.object, epoch, gfsim::XferPhase::Commit);
  }}
  for (const auto &item : model.sink_0_values())
    std::cout << ((static_cast<unsigned long long>(item.value) << 4) |
                  static_cast<unsigned long long>(item.remaining)) << "\\n";
}}
''',
                encoding="utf-8",
            )
            gfsim_build = subprocess.run(
                (
                    cxx,
                    "-std=c++20",
                    "-I",
                    str(ROOT / "include"),
                    str(gfsim_harness),
                    "-o",
                    str(gfsim_executable),
                ),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, gfsim_build.returncode, gfsim_build.stderr)
            gfsim_run = subprocess.run(
                (str(gfsim_executable),),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, gfsim_run.returncode, gfsim_run.stderr)
            self.assertEqual(transactions, [int(value) for value in gfsim_run.stdout.split()])

    def test_serial_runtime_if_builds_pyc_cpp_and_verilog(self) -> None:
        toolchain = Path(os.environ.get("PYC_V02_TOOLCHAIN_ROOT", DEFAULT_TOOLCHAIN))
        pycc = toolchain / "bin" / "pycc"
        metadata = toolchain / "share" / "pycircuit" / "toolchain-metadata.json"
        cxx = shutil.which("c++")
        verilator = shutil.which("verilator")
        if not pycc.is_file() or not metadata.is_file() or cxx is None or verilator is None:
            self.skipTest("pinned pyCircuit toolchain, C++, or Verilator is unavailable")
        source = CONDITIONAL_EXAMPLE.read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw = root / "conditional.raw.ac.mlir"
            frozen = root / "conditional.frozen.ac.mlir"
            output = root / "output"
            raw.write_text(
                lower_queue_source(source, "pyc_conditional_pipeline"),
                encoding="utf-8",
            )
            optimized = subprocess.run(
                (str(ROOT / "build/dev-llvm22/bin/acir-opt"), str(raw)),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, optimized.returncode, optimized.stderr)
            frozen.write_text(optimized.stdout, encoding="utf-8")
            completed = subprocess.run(
                (
                    str(ROOT / "tools/ac-queue-pyc-build.py"),
                    str(frozen),
                    "--pycgen-tool",
                    str(ROOT / "build/dev-llvm22/bin/acir-queue-pycgen"),
                    "--pycc",
                    str(pycc),
                    "--toolchain-lock",
                    str(ROOT / "toolchains/pyc-v0.2.lock.json"),
                    "--toolchain-metadata",
                    str(metadata),
                    "--cxx",
                    cxx,
                    "--verilator",
                    verilator,
                    "--pyc-output",
                    str(output / "model.pyc"),
                    "--cpp-output-dir",
                    str(output / "cpp"),
                    "--verilog-output-dir",
                    str(output / "verilog"),
                    "--manifest",
                    str(output / "manifest.json"),
                ),
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, completed.returncode, completed.stderr)
            pyc = (output / "model.pyc").read_text(encoding="utf-8")
            self.assertIn("pyc.eq", pyc)
            self.assertIn("pyc.fifo", pyc)
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertEqual("0.2", manifest["contract_epoch"])
            verilog = "\n".join(
                path.read_text(encoding="utf-8")
                for path in sorted((output / "verilog").glob("*.v"))
            )
            self.assertIn("==", verilog)

    def test_decoupled_fork_builds_delivered_state_in_pyc(self) -> None:
        toolchain = Path(os.environ.get("PYC_V02_TOOLCHAIN_ROOT", DEFAULT_TOOLCHAIN))
        pycc = toolchain / "bin" / "pycc"
        metadata = toolchain / "share" / "pycircuit" / "toolchain-metadata.json"
        cxx = shutil.which("c++")
        verilator = shutil.which("verilator")
        if not pycc.is_file() or not metadata.is_file() or cxx is None or verilator is None:
            self.skipTest("pinned pyCircuit toolchain, C++, or Verilator is unavailable")
        source = FORK_EXAMPLE.read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw = root / "fork.raw.ac.mlir"
            frozen = root / "fork.frozen.ac.mlir"
            output = root / "output"
            raw.write_text(
                lower_queue_source(source, "pyc_fork_pipeline"),
                encoding="utf-8",
            )
            optimized = subprocess.run(
                (str(ROOT / "build/dev-llvm22/bin/acir-opt"), str(raw)),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, optimized.returncode, optimized.stderr)
            frozen.write_text(optimized.stdout, encoding="utf-8")
            completed = subprocess.run(
                (
                    str(ROOT / "tools/ac-queue-pyc-build.py"),
                    str(frozen),
                    "--pycgen-tool",
                    str(ROOT / "build/dev-llvm22/bin/acir-queue-pycgen"),
                    "--pycc",
                    str(pycc),
                    "--toolchain-lock",
                    str(ROOT / "toolchains/pyc-v0.2.lock.json"),
                    "--toolchain-metadata",
                    str(metadata),
                    "--cxx",
                    cxx,
                    "--verilator",
                    verilator,
                    "--pyc-output",
                    str(output / "model.pyc"),
                    "--cpp-output-dir",
                    str(output / "cpp"),
                    "--verilog-output-dir",
                    str(output / "verilog"),
                    "--manifest",
                    str(output / "manifest.json"),
                ),
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, completed.returncode, completed.stderr)
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertEqual("0.2", manifest["contract_epoch"])
            pyc = (output / "model.pyc").read_text(encoding="utf-8")
            self.assertEqual(2, pyc.count("pyc.reg"))
            self.assertIn("%out0_ready", pyc)
            self.assertIn("%out1_ready", pyc)

    def test_atomic_multi_queue_firing_builds_pyc_and_verilog(self) -> None:
        toolchain = Path(os.environ.get("PYC_V02_TOOLCHAIN_ROOT", DEFAULT_TOOLCHAIN))
        pycc = toolchain / "bin" / "pycc"
        metadata = toolchain / "share" / "pycircuit" / "toolchain-metadata.json"
        cxx = shutil.which("c++")
        verilator = shutil.which("verilator")
        if not pycc.is_file() or not metadata.is_file() or cxx is None or verilator is None:
            self.skipTest("pinned pyCircuit toolchain, C++, or Verilator is unavailable")
        source = ATOMIC_EXAMPLE.read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw = root / "atomic.raw.ac.mlir"
            frozen = root / "atomic.frozen.ac.mlir"
            output = root / "output"
            raw.write_text(
                lower_queue_source(source, "pyc_atomic_pipeline"),
                encoding="utf-8",
            )
            optimized = subprocess.run(
                (
                    str(ROOT / "build/dev-llvm22/bin/acir-opt"),
                    "--canonicalize",
                    "--cse",
                    str(raw),
                ),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, optimized.returncode, optimized.stderr)
            frozen.write_text(optimized.stdout, encoding="utf-8")
            completed = subprocess.run(
                (
                    str(ROOT / "tools/ac-queue-pyc-build.py"),
                    str(frozen),
                    "--pycgen-tool",
                    str(ROOT / "build/dev-llvm22/bin/acir-queue-pycgen"),
                    "--pycc",
                    str(pycc),
                    "--toolchain-lock",
                    str(ROOT / "toolchains/pyc-v0.2.lock.json"),
                    "--toolchain-metadata",
                    str(metadata),
                    "--cxx",
                    cxx,
                    "--verilator",
                    verilator,
                    "--pyc-output",
                    str(output / "model.pyc"),
                    "--cpp-output-dir",
                    str(output / "cpp"),
                    "--verilog-output-dir",
                    str(output / "verilog"),
                    "--manifest",
                    str(output / "manifest.json"),
                ),
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, completed.returncode, completed.stderr)
            pyc = (output / "model.pyc").read_text(encoding="utf-8")
            self.assertIn("%in0_valid", pyc)
            self.assertIn("%in1_valid", pyc)
            self.assertIn("%out0_ready", pyc)
            self.assertIn("%out1_ready", pyc)
            self.assertIn("result_names = [\"out0_valid\"", pyc)

    def test_davincioo_like_graph_builds_full_pyc_and_verilog(self) -> None:
        toolchain = Path(os.environ.get("PYC_V02_TOOLCHAIN_ROOT", DEFAULT_TOOLCHAIN))
        pycc = toolchain / "bin" / "pycc"
        metadata = toolchain / "share" / "pycircuit" / "toolchain-metadata.json"
        cxx = shutil.which("c++")
        verilator = shutil.which("verilator")
        if not pycc.is_file() or not metadata.is_file() or cxx is None or verilator is None:
            self.skipTest("pinned pyCircuit toolchain, C++, or Verilator is unavailable")
        source = DAVINCIOO_EXAMPLE.read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw = root / "davincioo.raw.ac.mlir"
            frozen = root / "davincioo.frozen.ac.mlir"
            output = root / "output"
            raw.write_text(
                lower_queue_source(source, "davincioo_queue_model"),
                encoding="utf-8",
            )
            optimized = subprocess.run(
                (str(ROOT / "build/dev-llvm22/bin/acir-opt"), str(raw)),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, optimized.returncode, optimized.stderr)
            frozen.write_text(optimized.stdout, encoding="utf-8")
            completed = subprocess.run(
                (
                    str(ROOT / "tools/ac-queue-pyc-build.py"),
                    str(frozen),
                    "--pycgen-tool",
                    str(ROOT / "build/dev-llvm22/bin/acir-queue-pycgen"),
                    "--pycc",
                    str(pycc),
                    "--toolchain-lock",
                    str(ROOT / "toolchains/pyc-v0.2.lock.json"),
                    "--toolchain-metadata",
                    str(metadata),
                    "--cxx",
                    cxx,
                    "--verilator",
                    verilator,
                    "--pyc-output",
                    str(output / "model.pyc"),
                    "--cpp-output-dir",
                    str(output / "cpp"),
                    "--verilog-output-dir",
                    str(output / "verilog"),
                    "--manifest",
                    str(output / "manifest.json"),
                ),
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, completed.returncode, completed.stderr)
            pyc = (output / "model.pyc").read_text(encoding="utf-8")
            self.assertIn("%in_data: i192", pyc)
            self.assertIn("pyc.reg", pyc)
            self.assertIn(": i2", pyc)
            self.assertGreaterEqual(pyc.count("pyc.fifo"), 12)
            self.assertTrue(
                (output / "verilog/davincioo_queue_model.v").is_file()
            )

    def test_route_and_priority_merge_lower_to_static_pyc_topology(self) -> None:
        toolchain = Path(os.environ.get("PYC_V02_TOOLCHAIN_ROOT", DEFAULT_TOOLCHAIN))
        pycc = toolchain / "bin" / "pycc"
        metadata = toolchain / "share" / "pycircuit" / "toolchain-metadata.json"
        cxx = shutil.which("c++")
        verilator = shutil.which("verilator")
        if not pycc.is_file() or not metadata.is_file() or cxx is None or verilator is None:
            self.skipTest("pinned pyCircuit toolchain, C++, or Verilator is unavailable")
        source = ROUTE_EXAMPLE.read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw = root / "route.raw.ac.mlir"
            frozen = root / "route.frozen.ac.mlir"
            output = root / "output"
            raw.write_text(
                lower_queue_source(source, "pyc_route_merge_pipeline"),
                encoding="utf-8",
            )
            optimized = subprocess.run(
                (str(ROOT / "build/dev-llvm22/bin/acir-opt"), str(raw)),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, optimized.returncode, optimized.stderr)
            frozen.write_text(optimized.stdout, encoding="utf-8")
            completed = subprocess.run(
                (
                    str(ROOT / "tools/ac-queue-pyc-build.py"),
                    str(frozen),
                    "--pycgen-tool",
                    str(ROOT / "build/dev-llvm22/bin/acir-queue-pycgen"),
                    "--pycc",
                    str(pycc),
                    "--toolchain-lock",
                    str(ROOT / "toolchains/pyc-v0.2.lock.json"),
                    "--toolchain-metadata",
                    str(metadata),
                    "--cxx",
                    cxx,
                    "--verilator",
                    verilator,
                    "--pyc-output",
                    str(output / "model.pyc"),
                    "--cpp-output-dir",
                    str(output / "cpp"),
                    "--verilog-output-dir",
                    str(output / "verilog"),
                    "--manifest",
                    str(output / "manifest.json"),
                ),
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, completed.returncode, completed.stderr)
            pyc = (output / "model.pyc").read_text(encoding="utf-8")
            self.assertIn("pyc.eq", pyc)
            self.assertIn("pyc.mux", pyc)
            self.assertIn("pyc.not", pyc)
            self.assertIn("pyc.or", pyc)
            self.assertIn("pyc.and", pyc)
            self.assertTrue(
                (output / "verilog/pyc_route_merge_pipeline.v").is_file()
            )

    def test_struct_payload_has_stable_packed_pyc_and_verilog_layout(self) -> None:
        toolchain = Path(os.environ.get("PYC_V02_TOOLCHAIN_ROOT", DEFAULT_TOOLCHAIN))
        pycc = toolchain / "bin" / "pycc"
        metadata = toolchain / "share" / "pycircuit" / "toolchain-metadata.json"
        cxx = shutil.which("c++")
        verilator = shutil.which("verilator")
        if not pycc.is_file() or not metadata.is_file() or cxx is None or verilator is None:
            self.skipTest("pinned pyCircuit toolchain, C++, or Verilator is unavailable")
        source = STRUCT_EXAMPLE.read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw = root / "struct.raw.ac.mlir"
            frozen = root / "struct.frozen.ac.mlir"
            output = root / "output"
            raw.write_text(
                lower_queue_source(source, "pyc_struct_pipeline"), encoding="utf-8"
            )
            optimized = subprocess.run(
                (str(ROOT / "build/dev-llvm22/bin/acir-opt"), str(raw)),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, optimized.returncode, optimized.stderr)
            frozen.write_text(optimized.stdout, encoding="utf-8")
            completed = subprocess.run(
                (
                    str(ROOT / "tools/ac-queue-pyc-build.py"),
                    str(frozen),
                    "--pycgen-tool",
                    str(ROOT / "build/dev-llvm22/bin/acir-queue-pycgen"),
                    "--pycc",
                    str(pycc),
                    "--toolchain-lock",
                    str(ROOT / "toolchains/pyc-v0.2.lock.json"),
                    "--toolchain-metadata",
                    str(metadata),
                    "--cxx",
                    cxx,
                    "--verilator",
                    verilator,
                    "--pyc-output",
                    str(output / "model.pyc"),
                    "--cpp-output-dir",
                    str(output / "cpp"),
                    "--verilog-output-dir",
                    str(output / "verilog"),
                    "--manifest",
                    str(output / "manifest.json"),
                ),
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, completed.returncode, completed.stderr)
            pyc = (output / "model.pyc").read_text(encoding="utf-8")
            verilog = (output / "verilog/pyc_struct_pipeline.v").read_text(
                encoding="utf-8"
            )
            self.assertIn("%in_data: i128", pyc)
            self.assertIn("pyc.extract", pyc)
            self.assertIn("pyc.concat", pyc)
            self.assertIn("input [127:0] in_data", verilog)
            self.assertIn("pyc_fifo #(.WIDTH(128), .DEPTH(2))", verilog)

    def test_frozen_acir_builds_deterministic_pyc_cpp_and_verilog(self) -> None:
        toolchain = Path(os.environ.get("PYC_V02_TOOLCHAIN_ROOT", DEFAULT_TOOLCHAIN))
        pycc = toolchain / "bin" / "pycc"
        metadata = toolchain / "share" / "pycircuit" / "toolchain-metadata.json"
        cxx = shutil.which("c++")
        verilator = shutil.which("verilator")
        if not pycc.is_file() or not metadata.is_file() or cxx is None or verilator is None:
            self.skipTest("pinned pyCircuit toolchain, C++, or Verilator is unavailable")

        source = EXAMPLE.read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw = root / "model.raw.ac.mlir"
            frozen = root / "model.frozen.ac.mlir"
            raw.write_text(lower_queue_source(source, "pyc_queue_pipeline"), encoding="utf-8")
            optimized = subprocess.run(
                (str(ROOT / "build/dev-llvm22/bin/acir-opt"), str(raw)),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, optimized.returncode, optimized.stderr)
            frozen.write_text(optimized.stdout, encoding="utf-8")

            manifests: list[bytes] = []
            pyc_files: list[bytes] = []
            artifact_sets: list[dict[str, bytes]] = []
            for index in range(2):
                output = root / f"run_{index}"
                completed = subprocess.run(
                    (
                        str(ROOT / "tools/ac-queue-pyc-build.py"),
                        str(frozen),
                        "--pycgen-tool",
                        str(ROOT / "build/dev-llvm22/bin/acir-queue-pycgen"),
                        "--pycc",
                        str(pycc),
                        "--toolchain-lock",
                        str(ROOT / "toolchains/pyc-v0.2.lock.json"),
                        "--toolchain-metadata",
                        str(metadata),
                        "--cxx",
                        cxx,
                        "--verilator",
                        verilator,
                        "--pyc-output",
                        str(output / "model.pyc"),
                        "--cpp-output-dir",
                        str(output / "cpp"),
                        "--verilog-output-dir",
                        str(output / "verilog"),
                        "--manifest",
                        str(output / "manifest.json"),
                    ),
                    cwd=ROOT,
                    text=True,
                    capture_output=True,
                    check=False,
                )
                self.assertEqual(0, completed.returncode, completed.stderr)
                manifests.append((output / "manifest.json").read_bytes())
                pyc_files.append((output / "model.pyc").read_bytes())
                artifact_sets.append(
                    {
                        path.relative_to(output).as_posix(): path.read_bytes()
                        for path in sorted(output.rglob("*"))
                        if path.is_file() and path.name != "manifest.json"
                    }
                )
            self.assertEqual(manifests[0], manifests[1])
            self.assertEqual(pyc_files[0], pyc_files[1])
            self.assertEqual(artifact_sets[0], artifact_sets[1])
            self.assertIn(b"pyc.fifo", pyc_files[0])
            self.assertIn(b"pyc.add", pyc_files[0])

            first = root / "run_0"
            cpp_harness = first / "cpp_harness.cpp"
            cpp_executable = first / "cpp_model"
            cpp_harness.write_text(
                '''#include "pyc_queue_pipeline.hpp"
#include <cstdint>
#include <iostream>

int main() {
  pyc::gen::pyc_queue_pipeline dut;
  for (std::uint64_t cycle = 0; cycle < 7; ++cycle) {
    dut.rst = pyc::cpp::Wire<1>(cycle == 0 ? 1 : 0);
    dut.in_valid = pyc::cpp::Wire<1>(cycle == 1 ? 1 : 0);
    dut.in_data = pyc::cpp::Wire<64>(cycle == 1 ? 10 : 0);
    dut.out_ready = pyc::cpp::Wire<1>(1);
    dut.clk = pyc::cpp::Wire<1>(0);
    dut.step();
    dut.clk = pyc::cpp::Wire<1>(1);
    dut.step();
    std::cout << cycle << " " << dut.out_valid.value() << " "
              << dut.out_data.value() << " " << dut.in_ready.value() << "\\n";
    dut.clk = pyc::cpp::Wire<1>(0);
    dut.step();
  }
}
''',
                encoding="utf-8",
            )
            cpp_build = subprocess.run(
                (
                    cxx,
                    "-std=c++17",
                    "-I",
                    str(first / "cpp"),
                    "-I",
                    str(toolchain / "include"),
                    str(first / "cpp/pyc_queue_pipeline.cpp"),
                    str(cpp_harness),
                    str(toolchain / "lib/libpyc4_runtime.a"),
                    "-o",
                    str(cpp_executable),
                ),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, cpp_build.returncode, cpp_build.stderr)
            cpp_run = subprocess.run(
                (str(cpp_executable),),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, cpp_run.returncode, cpp_run.stderr)

            verilator_harness = first / "verilator_harness.cpp"
            verilator_harness.write_text(
                '''#include "Vpyc_queue_pipeline.h"
#include <cstdint>
#include <iostream>

int main() {
  Vpyc_queue_pipeline dut;
  for (std::uint64_t cycle = 0; cycle < 7; ++cycle) {
    dut.rst = cycle == 0 ? 1 : 0;
    dut.in_valid = cycle == 1 ? 1 : 0;
    dut.in_data = cycle == 1 ? 10 : 0;
    dut.out_ready = 1;
    dut.clk = 0;
    dut.eval();
    dut.clk = 1;
    dut.eval();
    std::cout << cycle << " " << unsigned(dut.out_valid) << " "
              << dut.out_data << " " << unsigned(dut.in_ready) << "\\n";
    dut.clk = 0;
    dut.eval();
  }
}
''',
                encoding="utf-8",
            )
            object_dir = first / "verilator_obj"
            verilator_build = subprocess.run(
                (
                    verilator,
                    "--cc",
                    "--exe",
                    "--build",
                    "-Wno-fatal",
                    "--top-module",
                    "pyc_queue_pipeline",
                    "--Mdir",
                    str(object_dir),
                    str(first / "verilog/pyc_primitives.v"),
                    str(first / "verilog/pyc_queue_pipeline.v"),
                    str(verilator_harness),
                ),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, verilator_build.returncode, verilator_build.stderr)
            verilator_run = subprocess.run(
                (str(object_dir / "Vpyc_queue_pipeline"),),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, verilator_run.returncode, verilator_run.stderr)
            self.assertEqual(cpp_run.stdout, verilator_run.stdout)
            pyc_transactions = [
                int(fields[2])
                for line in cpp_run.stdout.splitlines()
                if len(fields := line.split()) == 4 and fields[1] == "1"
            ]
            self.assertEqual([11], pyc_transactions)

            gfsim_model = first / "gfsim_model.cpp"
            gfsim_harness = first / "gfsim_harness.cpp"
            gfsim_executable = first / "gfsim_model"
            gfsim_generated = subprocess.run(
                (str(ROOT / "build/dev-llvm22/bin/acir-queue-cxxgen"), str(frozen)),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, gfsim_generated.returncode, gfsim_generated.stderr)
            gfsim_model.write_text(gfsim_generated.stdout, encoding="utf-8")
            gfsim_harness.write_text(
                f'''#include "{gfsim_model.name}"
#include <cstddef>
#include <iostream>

int main() {{
  ac_generated::PycQueuePipeline model;
  if (!model.input_queue().proposePush(10))
    return 1;
  auto rows = model.dispatch_rows();
  for (std::size_t tick = 0; tick < 6; ++tick) {{
    const gfsim::Epoch epoch{{tick, 0}};
    for (auto &row : rows)
      row.work(row.object, epoch);
    for (auto &row : rows)
      row.xfer(row.object, epoch, gfsim::XferPhase::Arbitrate);
    for (auto &row : rows)
      row.xfer(row.object, epoch, gfsim::XferPhase::Commit);
  }}
  for (auto value : model.sink_0_values())
    std::cout << value << "\\n";
}}
''',
                encoding="utf-8",
            )
            gfsim_build = subprocess.run(
                (
                    cxx,
                    "-std=c++20",
                    "-I",
                    str(ROOT / "include"),
                    str(gfsim_harness),
                    "-o",
                    str(gfsim_executable),
                ),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, gfsim_build.returncode, gfsim_build.stderr)
            gfsim_run = subprocess.run(
                (str(gfsim_executable),),
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, gfsim_run.returncode, gfsim_run.stderr)
            gfsim_transactions = [
                int(value) for value in gfsim_run.stdout.splitlines() if value
            ]
            self.assertEqual(pyc_transactions, gfsim_transactions)


if __name__ == "__main__":
    unittest.main()
