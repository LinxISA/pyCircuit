from __future__ import annotations

import os
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "examples" / "v02" / "davincioo_queue_model.py"
CONDITIONAL_SOURCE = ROOT / "examples" / "v02" / "pyc_conditional_pipeline.py"


class V02QueueCodegenTest(unittest.TestCase):
    def test_serial_runtime_if_generates_and_runs_common_blocks(self) -> None:
        compiler = shutil.which("c++")
        if compiler is None:
            self.skipTest("C++ compiler is unavailable")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            model = root / "conditional.cpp"
            acir = root / "conditional.ac.mlir"
            plan = root / "conditional.queue-plan.json"
            generated = subprocess.run(
                (
                    str(ROOT / "tools/ac-queue-cxxgen.py"),
                    str(CONDITIONAL_SOURCE),
                    "--system",
                    "pyc_conditional_pipeline",
                    "--acir-output",
                    str(acir),
                    "--plan-output",
                    str(plan),
                    "--acir-opt",
                    str(ROOT / "build/dev-llvm22/bin/acir-opt"),
                    "--queue-plan-tool",
                    str(ROOT / "build/dev-llvm22/bin/acir-queue-plan"),
                    "--queue-cxxgen-tool",
                    str(ROOT / "build/dev-llvm22/bin/acir-queue-cxxgen"),
                    "--output",
                    str(model),
                ),
                cwd=ROOT,
                env={**os.environ, "PYTHONPATH": str(ROOT / "src")},
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, generated.returncode, generated.stderr)
            content = model.read_text(encoding="utf-8")
            self.assertIn("gfsim::QueueRoute<Item, 2", content)
            self.assertIn("gfsim::QueueTransform<Item, Item", content)
            self.assertIn("gfsim::QueueMerge<Item, 2>", content)
            plan_document = json.loads(plan.read_text(encoding="utf-8"))
            self.assertEqual("0.2", plan_document["contract_epoch"])

            harness = root / "harness.cpp"
            executable = root / "conditional"
            harness.write_text(
                f'''#include "{model.name}"
#include <array>
#include <cstddef>

int main() {{
  ac_generated::PycConditionalPipeline model;
  if (!model.input_queue().proposePush(ac_generated::Item{{1, 0}}) ||
      !model.input_queue().proposePush(ac_generated::Item{{1, 1}}))
    return 1;
  auto rows = model.dispatch_rows();
  for (std::size_t tick = 0; tick < 16; ++tick) {{
    const gfsim::Epoch epoch{{tick, 0}};
    for (auto &row : rows)
      row.work(row.object, epoch);
    for (auto &row : rows)
      row.xfer(row.object, epoch, gfsim::XferPhase::Arbitrate);
    for (auto &row : rows)
      row.xfer(row.object, epoch, gfsim::XferPhase::Commit);
  }}
  const auto values = model.sink_0_values();
  if (values.size() != 2)
    return 2;
  return values[0].value == 11 && values[1].value == 21 ? 0 : 3;
}}
''',
                encoding="utf-8",
            )
            linked = subprocess.run(
                (
                    compiler,
                    "-std=c++20",
                    "-I",
                    str(ROOT / "include"),
                    str(harness),
                    "-o",
                    str(executable),
                ),
                cwd=root,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, linked.returncode, linked.stderr)
            executed = subprocess.run(
                (str(executable),),
                cwd=root,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, executed.returncode, executed.stderr)

    def test_native_frozen_acir_codegen_covers_broadcast_and_feedback(self) -> None:
        from tests.python_frontend.test_queue_codegen_v02 import (
            BROADCAST_SOURCE,
            FEEDBACK_SOURCE,
        )

        compiler = shutil.which("c++")
        if compiler is None:
            self.skipTest("C++ compiler is unavailable")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, source, expected in (
                ("broadcast", BROADCAST_SOURCE, "gfsim::QueueBroadcast"),
                ("feedback", FEEDBACK_SOURCE, "gfsim::QueueFeedback"),
            ):
                python = root / f"{name}.py"
                model = root / f"{name}.cpp"
                acir = root / f"{name}.ac.mlir"
                plan = root / f"{name}.queue-plan.json"
                python.write_text(source, encoding="utf-8")
                generated = subprocess.run(
                    (
                        str(ROOT / "tools" / "ac-queue-cxxgen.py"),
                        str(python),
                        "--system",
                        "pipeline",
                        "--acir-output",
                        str(acir),
                        "--plan-output",
                        str(plan),
                        "--acir-opt",
                        str(ROOT / "build/dev-llvm22/bin/acir-opt"),
                        "--queue-plan-tool",
                        str(ROOT / "build/dev-llvm22/bin/acir-queue-plan"),
                        "--queue-cxxgen-tool",
                        str(ROOT / "build/dev-llvm22/bin/acir-queue-cxxgen"),
                        "-o",
                        str(model),
                    ),
                    cwd=ROOT,
                    env={**os.environ, "PYTHONPATH": str(ROOT / "src")},
                    text=True,
                    capture_output=True,
                    check=False,
                )
                self.assertEqual(0, generated.returncode, generated.stderr)
                self.assertIn(expected, model.read_text(encoding="utf-8"))
                compiled = subprocess.run(
                    (
                        compiler,
                        "-std=c++20",
                        "-I",
                        str(ROOT / "include"),
                        "-fsyntax-only",
                        str(model),
                    ),
                    cwd=root,
                    text=True,
                    capture_output=True,
                    check=False,
                )
                self.assertEqual(0, compiled.returncode, compiled.stderr)

    def test_davincioo_like_python_generates_and_runs_typed_cpp(self) -> None:
        compiler = shutil.which("c++")
        if compiler is None:
            self.skipTest("C++ compiler is unavailable")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            model = root / "model.cpp"
            acir = root / "model.ac.mlir"
            plan = root / "model.queue-plan.json"
            harness = root / "harness.cpp"
            executable = root / "model"
            generated = subprocess.run(
                (
                    str(ROOT / "tools" / "ac-queue-cxxgen.py"),
                    str(SOURCE),
                    "--system",
                    "davincioo_queue_model",
                    "--acir-output",
                    str(acir),
                    "--plan-output",
                    str(plan),
                    "--acir-opt",
                    str(ROOT / "build" / "dev-llvm22" / "bin" / "acir-opt"),
                    "--queue-plan-tool",
                    str(
                        ROOT
                        / "build"
                        / "dev-llvm22"
                        / "bin"
                        / "acir-queue-plan"
                    ),
                    "--queue-cxxgen-tool",
                    str(
                        ROOT
                        / "build"
                        / "dev-llvm22"
                        / "bin"
                        / "acir-queue-cxxgen"
                    ),
                    "-o",
                    str(model),
                ),
                cwd=ROOT,
                env={**os.environ, "PYTHONPATH": str(ROOT / "src")},
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, generated.returncode, generated.stderr)
            content = model.read_text(encoding="utf-8")
            plan_document = json.loads(plan.read_text(encoding="utf-8"))
            self.assertEqual("davincioo_queue_model", plan_document["system"])
            self.assertEqual(7, len(plan_document["scopes"]))
            self.assertEqual(12, len(plan_document["queues"]))
            self.assertEqual(10, len(plan_document["blocks"]))
            copied_source = root / "copied_model.py"
            copied_model = root / "copied_model.cpp"
            copied_acir = root / "copied_model.ac.mlir"
            copied_plan = root / "copied_model.queue-plan.json"
            shutil.copyfile(SOURCE, copied_source)
            regenerated = subprocess.run(
                (
                    str(ROOT / "tools" / "ac-queue-cxxgen.py"),
                    str(copied_source),
                    "--system",
                    "davincioo_queue_model",
                    "--acir-output",
                    str(copied_acir),
                    "--plan-output",
                    str(copied_plan),
                    "--acir-opt",
                    str(ROOT / "build" / "dev-llvm22" / "bin" / "acir-opt"),
                    "--queue-plan-tool",
                    str(
                        ROOT
                        / "build"
                        / "dev-llvm22"
                        / "bin"
                        / "acir-queue-plan"
                    ),
                    "--queue-cxxgen-tool",
                    str(
                        ROOT
                        / "build"
                        / "dev-llvm22"
                        / "bin"
                        / "acir-queue-cxxgen"
                    ),
                    "-o",
                    str(copied_model),
                ),
                cwd=root,
                env={**os.environ, "PYTHONPATH": str(ROOT / "src")},
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, regenerated.returncode, regenerated.stderr)
            self.assertEqual(content, copied_model.read_text(encoding="utf-8"))
            for scope in (
                "frontend",
                "dispatch",
                "scalar_engine",
                "vector_engine",
                "cube_engine",
                "tma_engine",
                "retire",
            ):
                self.assertIn(f'("{scope}", gfsim::kInvalidObjectId', content)
            self.assertIn("gfsim::QueueRoute<WorkItem, 4", content)
            self.assertIn("gfsim::QueueMerge<WorkItem, 4>", content)

            harness.write_text(
                f'''#include "{model.name}"
#include <algorithm>
#include <array>
#include <cstddef>

int main() {{
  ac_generated::DavinciooQueueModel model;
  const std::array<ac_generated::WorkItem, 4> input{{
      ac_generated::WorkItem{{10, 0, 1}},
      ac_generated::WorkItem{{20, 1, 1}},
      ac_generated::WorkItem{{30, 2, 1}},
      ac_generated::WorkItem{{40, 3, 1}},
  }};
  for (const auto &item : input)
    if (!model.trace().proposePush(item))
      return 1;
  auto rows = model.dispatch_rows();
  for (std::size_t tick = 0; tick < 24; ++tick) {{
    const gfsim::Epoch epoch{{tick, 0}};
    for (auto &row : rows)
      row.work(row.object, epoch);
    for (auto &row : rows)
      row.xfer(row.object, epoch, gfsim::XferPhase::Arbitrate);
    for (auto &row : rows)
      row.xfer(row.object, epoch, gfsim::XferPhase::Commit);
  }}
  auto values = model.sink_0_values();
  if (values.size() != 4)
    return 2;
  std::array<long long, 4> results{{values[0].value, values[1].value,
                                   values[2].value, values[3].value}};
  std::sort(results.begin(), results.end());
  return results == std::array<long long, 4>{{112, 123, 134, 145}} ? 0 : 3;
}}
''',
                encoding="utf-8",
            )
            linked = subprocess.run(
                (
                    compiler,
                    "-std=c++20",
                    "-I",
                    str(ROOT / "include"),
                    str(harness),
                    "-o",
                    str(executable),
                ),
                cwd=root,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, linked.returncode, linked.stderr)
            executed = subprocess.run(
                (str(executable),),
                cwd=root,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, executed.returncode, executed.stderr)


if __name__ == "__main__":
    unittest.main()
