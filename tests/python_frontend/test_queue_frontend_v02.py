from __future__ import annotations

import unittest


SOURCE = """
from agentic_circuit import sink, source, system

@system
def pipeline() -> None:
    input_queue = source(int, depth=4, latency=1)
    output_queue = input_queue.apply(lambda item: item, depth=8, latency=2)
    sink(output_queue)
"""


class QueueFrontendV02Test(unittest.TestCase):
    def test_simple_serial_python_lowers_to_typed_queue_graph(self) -> None:
        from agentic_circuit._queue_frontend import lower_queue_source

        self.assertEqual(
            """module attributes {ac.contract_epoch = "0.1"} {
  %input_queue = ac.source depth 4 latency 1 : !ac.queue<i64>
  %output_queue = ac.transform %input_queue depths [8] latencies [2] {
  ^body(%item: !ac.var<i64>):
    ac.transform.yield %item : !ac.var<i64>
  } : (!ac.queue<i64>) -> !ac.queue<i64>
  ac.sink %output_queue : !ac.queue<i64>
}
""",
            lower_queue_source(SOURCE, "pipeline"),
        )

    def test_repeated_lowering_is_byte_identical(self) -> None:
        from agentic_circuit._queue_frontend import lower_queue_source

        self.assertEqual(
            lower_queue_source(SOURCE, "pipeline"),
            lower_queue_source(SOURCE, "pipeline"),
        )

    def test_latency_zero_and_non_identity_lambda_are_rejected(self) -> None:
        from agentic_circuit._queue_frontend import (
            QueueFrontendError,
            lower_queue_source,
        )

        with self.assertRaisesRegex(QueueFrontendError, "latency must be positive"):
            lower_queue_source(SOURCE.replace("latency=2", "latency=0"), "pipeline")
        with self.assertRaisesRegex(QueueFrontendError, "identity lambda"):
            lower_queue_source(
                SOURCE.replace("lambda item: item", "lambda item: item + 1"),
                "pipeline",
            )


if __name__ == "__main__":
    unittest.main()
