from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from agentic_circuit._queue_frontend import lower_queue_source


ROOT = Path(__file__).resolve().parents[2]
EXAMPLE = ROOT / "examples" / "v02" / "pyc_queue_pipeline.py"
DEFAULT_TOOLCHAIN = Path(
    "/Users/zhoubot/Documents/SummerSchool/vendor/pyCircuit/"
    ".pycircuit_out/toolchain/install"
)


class V02PycBackendTest(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
