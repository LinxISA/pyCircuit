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

CROSS_SCOPE_BROADCAST_SOURCE = """
from agentic_circuit import scope, sink, source, system

@system
def pipeline() -> None:
    input_queue = source(int)
    with scope("left"):
        left = input_queue.apply(lambda item: item + 1)
    with scope("right"):
        right = input_queue.apply(lambda item: item * 2)
    sink(left)
    sink(right)
"""

ROUTE_SOURCE = """
from agentic_circuit import sink, source, struct, system

@struct
class Item:
    value: int
    route: int

@system
def pipeline() -> None:
    input_queue = source(Item)
    left, right = input_queue.route(
        outputs=2,
        key=lambda item: item.route,
        depth=2,
        latency=1,
    )
    merged = left.merge(right, policy="round_robin", depth=3, latency=1)
    sink(merged)
"""

COLLECTION_SOURCE = """
import agentic_circuit as ac

@ac.system
def pipeline() -> None:
    lanes = ac.array(2, lambda lane: ac.source(int, depth=lane + 1))
    named = ac.map({"right": lanes[1], "left": lanes[0]})
    active = ac.set({named["right"], named["left"]})
    for lane in active:
        ac.sink(lane)
"""

NESTED_COLLECTION_SOURCE = """
import agentic_circuit as ac

@ac.system
def pipeline() -> None:
    grid = ac.array(
        2,
        lambda row: ac.array(
            2,
            lambda column: ac.source(int, depth=row + column + 1),
        ),
    )
    ac.sink(grid[1][0])
"""

FEEDBACK_SOURCE = """
from agentic_circuit import sink, source, struct, system

@struct
class Item:
    value: int
    remaining: int

@system
def pipeline() -> None:
    current = source(Item)
    while current.remaining > 0:
        current = current.apply(
            lambda item: item.with_fields(
                value=item.value + 1,
                remaining=item.remaining - 1,
            ),
            depth=2,
            latency=1,
        )
    sink(current)
"""


class QueueFrontendV02Test(unittest.TestCase):
    def test_simple_serial_python_lowers_to_typed_queue_graph(self) -> None:
        from agentic_circuit._queue_frontend import lower_queue_source

        self.assertEqual(
            """module attributes {ac.contract_epoch = "0.1", ac.system = "pipeline"} {
  %input_queue = ac.source depth 4 latency 1 {ac.name = "input_queue"} : !ac.queue<i64>
  %output_queue = ac.transform %input_queue depths [8] latencies [2] {
  ^transform(%item: !ac.var<i64>):
    ac.transform.yield %item : !ac.var<i64>
  } {ac.name = "output_queue"} : (!ac.queue<i64>) -> !ac.queue<i64>
  ac.sink %output_queue {ac.name = "sink_2"} : !ac.queue<i64>
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
        self.assertIn(
            'ac.sink %completed {ac.name = "sink_5"} : !ac.queue<i64>', lowered
        )

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

    def test_cross_scope_broadcast_is_placed_at_lexical_lca(self) -> None:
        from agentic_circuit._queue_frontend import lower_queue_source

        lowered = lower_queue_source(CROSS_SCOPE_BROADCAST_SOURCE, "pipeline")
        broadcast = lowered.index("ac.broadcast %input_queue")
        left_scope = lowered.index("ac.scope @left(%input_queue__fanout0)")
        right_scope = lowered.index("ac.scope @right(%input_queue__fanout1)")
        self.assertLess(broadcast, left_scope)
        self.assertLess(broadcast, right_scope)
        self.assertIn(
            "^body(%input_queue__fanout0__in: !ac.queue<i64>):", lowered
        )

    def test_tuple_route_lowers_selector_to_var_region(self) -> None:
        from agentic_circuit._queue_frontend import lower_queue_source

        lowered = lower_queue_source(ROUTE_SOURCE, "pipeline")
        self.assertIn("%left, %right = ac.route %input_queue", lowered)
        self.assertIn("depths [2, 2] latencies [1, 1]", lowered)
        self.assertIn('ac.var.get %item field "route"', lowered)
        self.assertIn("ac.route.yield", lowered)
        self.assertIn('%merged = ac.merge %left, %right policy "round_robin"', lowered)
        self.assertIn("depth 3 latency 1", lowered)
        self.assertIn("ac.sink %merged", lowered)

    def test_static_queue_collections_flatten_in_canonical_order(self) -> None:
        from agentic_circuit._queue_frontend import lower_queue_source

        lowered = lower_queue_source(COLLECTION_SOURCE, "pipeline")
        self.assertIn("%lanes__0 = ac.source depth 1", lowered)
        self.assertIn("%lanes__1 = ac.source depth 2", lowered)
        self.assertLess(
            lowered.index("ac.sink %lanes__0"),
            lowered.index("ac.sink %lanes__1"),
        )
        self.assertNotIn("dynamic", lowered)
        reordered = COLLECTION_SOURCE.replace(
            '{named["right"], named["left"]}',
            '{named["left"], named["right"]}',
        )
        self.assertEqual(lowered, lower_queue_source(reordered, "pipeline"))

    def test_dynamic_or_duplicate_collection_shape_is_rejected(self) -> None:
        from agentic_circuit._queue_frontend import (
            QueueFrontendError,
            lower_queue_source,
        )

        with self.assertRaisesRegex(QueueFrontendError, "positive compile-time extent"):
            lower_queue_source(
                COLLECTION_SOURCE.replace("ac.array(2", "ac.array(runtime"),
                "pipeline",
            )
        with self.assertRaisesRegex(QueueFrontendError, "members must be unique"):
            lower_queue_source(
                COLLECTION_SOURCE.replace(
                    '{named["right"], named["left"]}',
                    '{named["left"], named["left"]}',
                ),
                "pipeline",
            )

    def test_nested_collection_shape_is_statically_flattened(self) -> None:
        from agentic_circuit._queue_frontend import lower_queue_source

        lowered = lower_queue_source(NESTED_COLLECTION_SOURCE, "pipeline")
        self.assertIn("%grid__0__0 = ac.source depth 1", lowered)
        self.assertIn("%grid__0__1 = ac.source depth 2", lowered)
        self.assertIn("%grid__1__0 = ac.source depth 2", lowered)
        self.assertIn("%grid__1__1 = ac.source depth 3", lowered)
        self.assertIn("ac.sink %grid__1__0", lowered)

    def test_serial_while_lowers_to_bounded_feedback(self) -> None:
        from agentic_circuit._queue_frontend import lower_queue_source

        lowered = lower_queue_source(FEEDBACK_SOURCE, "pipeline")
        self.assertIn("ac.feedback %current depth 2 latency 1", lowered)
        self.assertIn("max_iterations 1024", lowered)
        self.assertIn('ac.var.cmp "sgt"', lowered)
        self.assertIn("ac.feedback.yield", lowered)
        self.assertIn("ac.sink %current__feedback0", lowered)

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
