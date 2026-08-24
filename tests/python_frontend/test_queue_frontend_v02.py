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

STRUCT_SOURCE = """
from agentic_circuit import sink, source, struct, system

@struct
class WorkItem:
    value: int
    remaining: int

@system
def pipeline() -> None:
    input_queue = source(WorkItem, depth=4, latency=1)
    output_queue = input_queue.apply(
        lambda item: item.with_fields(
            value=(item.value + 1) * 2,
            remaining=item.remaining - 1,
        ),
        depth=8,
        latency=2,
    )
    sink(output_queue)
"""

SCOPE_SOURCE = """
from agentic_circuit import scope, sink, source, system

@system
def pipeline() -> None:
    input_queue = source(int, depth=4, latency=1)
    with scope("frontend"):
        adjusted = input_queue.apply(lambda item: item + 1)
        with scope("inner"):
            completed = adjusted.apply(lambda item: item * 2)
    sink(completed)
"""

BROADCAST_SOURCE = """
from agentic_circuit import sink, source, system

@system
def pipeline() -> None:
    input_queue = source(int)
    left = input_queue.apply(lambda item: item + 1)
    right = input_queue.apply(lambda item: item * 2)
    sink(left)
    sink(right)
"""


class QueueFrontendV02Test(unittest.TestCase):
    def test_simple_serial_python_lowers_to_typed_queue_graph(self) -> None:
        from agentic_circuit._queue_frontend import lower_queue_source

        self.assertEqual(
            """module attributes {ac.contract_epoch = "0.1"} {
  %input_queue = ac.source depth 4 latency 1 : !ac.queue<i64>
  %output_queue = ac.transform %input_queue depths [8] latencies [2] {
  ^transform(%item: !ac.var<i64>):
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

    def test_struct_and_immutable_lambda_lower_to_var_operations(self) -> None:
        from agentic_circuit._queue_frontend import lower_queue_source

        lowered = lower_queue_source(STRUCT_SOURCE, "pipeline")
        self.assertIn("ac.type_scope @types", lowered)
        self.assertIn("ac.struct @WorkItem", lowered)
        self.assertIn("!ac.queue<!ac.struct<@types::@WorkItem>>", lowered)
        self.assertIn('ac.var.get %item field "value"', lowered)
        self.assertIn("ac.var.constant 1 : i64", lowered)
        self.assertIn("ac.var.add", lowered)
        self.assertIn("ac.var.mul", lowered)
        self.assertIn("ac.var.sub", lowered)
        self.assertIn('ac.var.with', lowered)
        self.assertIn('field "remaining"', lowered)

    def test_nested_scope_infers_borrowed_local_and_exported_queues(self) -> None:
        from agentic_circuit._queue_frontend import lower_queue_source

        lowered = lower_queue_source(SCOPE_SOURCE, "pipeline")
        self.assertIn("%completed = ac.scope @frontend(%input_queue)", lowered)
        self.assertIn("^body(%input_queue__in: !ac.queue<i64>):", lowered)
        self.assertIn(
            "%completed__inner = ac.scope @inner(%adjusted__local)", lowered,
        )
        self.assertIn("ac.scope.yield %completed__local", lowered)
        self.assertIn("ac.sink %completed : !ac.queue<i64>", lowered)

    def test_multiple_consumers_insert_strict_atomic_broadcast(self) -> None:
        from agentic_circuit._queue_frontend import lower_queue_source

        lowered = lower_queue_source(BROADCAST_SOURCE, "pipeline")
        self.assertIn(
            "%input_queue__fanout0, %input_queue__fanout1 = ac.broadcast "
            "%input_queue depths [1, 1] latencies [1, 1]",
            lowered,
        )
        self.assertIn("ac.transform %input_queue__fanout0", lowered)
        self.assertIn("ac.transform %input_queue__fanout1", lowered)

    def test_latency_zero_and_unsupported_lambda_are_rejected(self) -> None:
        from agentic_circuit._queue_frontend import (
            QueueFrontendError,
            lower_queue_source,
        )

        with self.assertRaisesRegex(QueueFrontendError, "latency must be positive"):
            lower_queue_source(SOURCE.replace("latency=2", "latency=0"), "pipeline")
        with self.assertRaisesRegex(QueueFrontendError, "unsupported lambda"):
            lower_queue_source(
                SOURCE.replace("lambda item: item", "lambda item: unknown(item)"),
                "pipeline",
            )


if __name__ == "__main__":
    unittest.main()
