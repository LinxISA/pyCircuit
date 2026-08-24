from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]

SOURCE = """
from agentic_circuit import sink, source, struct, system

@struct
class Item:
    value: int
    remaining: int

@system
def pipeline() -> None:
    input_queue = source(Item, depth=4, latency=1)
    updated = input_queue.apply(
        lambda item: item.with_fields(
            value=item.value + 1,
            remaining=item.remaining - 1,
        ),
        depth=8,
        latency=2,
    )
    sink(updated)
"""


class QueueCodegenV02Test(unittest.TestCase):
    def test_serial_python_generates_typed_queue_wired_cpp(self) -> None:
        from agentic_circuit._queue_codegen import lower_queue_source_to_cpp

        generated = lower_queue_source_to_cpp(SOURCE, "pipeline")
        self.assertIn("struct Item", generated)
        self.assertIn("gfsim::SimQueue<Item> input_queue_;", generated)
        self.assertIn("gfsim::SimQueue<Item> updated_;", generated)
        self.assertIn("gfsim::QueueTransform<Item, Item, updated_policy>", generated)
        self.assertIn("result.value = (item.value + 1);", generated)
        self.assertIn("gfsim::QueueSink<Item> sink_0_;", generated)
        self.assertEqual(generated, lower_queue_source_to_cpp(SOURCE, "pipeline"))

    def test_explicit_tool_writes_cpp_accepted_by_cxx20_compiler(self) -> None:
        compiler = shutil.which("c++")
        if compiler is None:
            self.skipTest("C++ compiler is unavailable")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "pipeline.py"
            output = root / "pipeline.cpp"
            source.write_text(SOURCE, encoding="utf-8")
            environment = os.environ.copy()
            environment["PYTHONPATH"] = str(ROOT / "src")
            generated = subprocess.run(
                (
                    str(ROOT / "tools" / "ac-queue-cxxgen.py"),
                    str(source),
                    "--system",
                    "pipeline",
                    "-o",
                    str(output),
                ),
                cwd=ROOT,
                env=environment,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, generated.returncode, generated.stderr)
            compiled = subprocess.run(
                (
                    compiler,
                    "-std=c++20",
                    "-I",
                    str(ROOT / "include"),
                    "-fsyntax-only",
                    str(output),
                ),
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(0, compiled.returncode, compiled.stderr)

            harness = root / "harness.cpp"
            executable = root / "pipeline"
            harness.write_text(
                f'''#include "{output.name}"
#include <cstddef>

int main() {{
  ac_generated::Pipeline model;
  if (!model.input_queue().proposePush(ac_generated::Item{{41, 3}}))
    return 1;
  auto rows = model.dispatch_rows();
  for (std::size_t tick = 0; tick < 5; ++tick) {{
    const gfsim::Epoch epoch{{tick, 0}};
    for (auto &row : rows)
      row.work(row.object, epoch);
    for (auto &row : rows)
      row.xfer(row.object, epoch, gfsim::XferPhase::Arbitrate);
    for (auto &row : rows)
      row.xfer(row.object, epoch, gfsim::XferPhase::Commit);
  }}
  const auto &values = model.sink_0_values();
  return values.size() == 1 && values[0].value == 42 &&
                 values[0].remaining == 2
             ? 0
             : 2;
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
