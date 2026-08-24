"""Deterministic v0.2 QueueProgram to typed gfsim C++ lowering."""

from __future__ import annotations

import ast
from dataclasses import dataclass

from ._queue_frontend import (
    CollectionBinding,
    Payload,
    QueueBinding,
    QueueFrontendError,
    QueueProgram,
    StaticQueueCollection,
    parse_queue_program,
)


def _cpp_type(acir_type: str) -> str:
    if acir_type == "i64":
        return "std::int64_t"
    if acir_type == "i1":
        return "bool"
    prefix = "!ac.struct<@types::@"
    if acir_type.startswith(prefix) and acir_type.endswith(">"):
        return acir_type[len(prefix) : -1]
    raise QueueFrontendError(f"ACLOWER-TYPE-MISMATCH: no C++ type for {acir_type}")


class _CppExpression:
    def __init__(self, argument: str) -> None:
        self.argument = argument

    def emit(self, node: ast.expr) -> str:
        if isinstance(node, ast.Name) and node.id == self.argument:
            return "item"
        if isinstance(node, ast.Constant) and type(node.value) in {int, bool}:
            if node.value is True:
                return "true"
            if node.value is False:
                return "false"
            return str(node.value)
        if isinstance(node, ast.Attribute):
            return f"{self.emit(node.value)}.{node.attr}"
        if isinstance(node, ast.BinOp) and isinstance(
            node.op, (ast.Add, ast.Sub, ast.Mult)
        ):
            operator = {ast.Add: "+", ast.Sub: "-", ast.Mult: "*"}[
                type(node.op)
            ]
            return f"({self.emit(node.left)} {operator} {self.emit(node.right)})"
        if isinstance(node, ast.Compare) and len(node.ops) == len(node.comparators) == 1:
            operators = {
                ast.Eq: "==",
                ast.NotEq: "!=",
                ast.Lt: "<",
                ast.LtE: "<=",
                ast.Gt: ">",
                ast.GtE: ">=",
            }
            operator = operators.get(type(node.ops[0]))
            if operator is not None:
                return (
                    f"({self.emit(node.left)} {operator} "
                    f"{self.emit(node.comparators[0])})"
                )
        raise QueueFrontendError(
            "ACLOWER-UNSUPPORTED-CONSTRUCT: lambda is outside C++ expression subset"
        )


def _policy_body(queue: QueueBinding) -> list[str]:
    assert queue.argument is not None and queue.expression is not None
    return _expression_policy_body(queue.argument, queue.expression)


def _expression_policy_body(argument: str, node: ast.expr) -> list[str]:
    expression = _CppExpression(argument)
    if (
        isinstance(node, ast.Call)
        and isinstance(node.func, ast.Attribute)
        and node.func.attr == "with_fields"
        and not node.args
    ):
        lines = ["    auto result = item;"]
        for keyword in node.keywords:
            if keyword.arg is None:
                raise QueueFrontendError(
                    "ACLOWER-UNSUPPORTED-CONSTRUCT: field unpacking is forbidden"
                )
            lines.append(
                f"    result.{keyword.arg} = {expression.emit(keyword.value)};"
            )
        lines.extend(("    return result;",))
        return lines
    return [f"    return {expression.emit(node)};"]


@dataclass(frozen=True, slots=True)
class _ObjectIds:
    queues: dict[str, int]
    fanout_queues: dict[str, int]
    feedback_states: tuple[int, ...]
    broadcasts: tuple[int, ...]
    transforms: dict[str, int]
    routes: tuple[int, ...]
    merges: tuple[int, ...]
    feedbacks: tuple[int, ...]
    sinks: tuple[int, ...]


@dataclass(frozen=True, slots=True)
class _Fanout:
    source: str
    outputs: tuple[str, ...]
    consumers: tuple[str, ...]
    payload: str


def _fanouts(program: QueueProgram) -> tuple[_Fanout, ...]:
    consumers: dict[str, list[QueueBinding]] = {}
    for queue in program.queues:
        if queue.input_name is not None:
            consumers.setdefault(queue.input_name, []).append(queue)
    existing = {queue.name for queue in program.queues}
    fanouts: list[_Fanout] = []
    for source, group in consumers.items():
        if len(group) < 2:
            continue
        outputs = tuple(f"{source}__fanout{index}" for index in range(len(group)))
        if existing.intersection(outputs):
            raise QueueFrontendError(
                "ACLOWER-OWNERSHIP: inferred broadcast Queue name collides with source"
            )
        payload = next(queue.payload for queue in program.queues if queue.name == source)
        fanouts.append(
            _Fanout(source, outputs, tuple(queue.name for queue in group), payload)
        )
    return tuple(fanouts)


def _collection_leaves(
    value: StaticQueueCollection,
    path: tuple[int, ...] = (),
) -> tuple[tuple[str, tuple[int, ...]], ...]:
    leaves: list[tuple[str, tuple[int, ...]]] = []
    for index, (_, member) in enumerate(value.members):
        member_path = (*path, index)
        if isinstance(member, str):
            leaves.append((member, member_path))
        else:
            leaves.extend(_collection_leaves(member, member_path))
    return tuple(leaves)


def _owning_arrays(program: QueueProgram) -> tuple[CollectionBinding, ...]:
    queue_names = {queue.name for queue in program.queues}
    result: list[CollectionBinding] = []
    claimed: set[str] = set()
    for collection in program.collections:
        if collection.value.kind != "array":
            continue
        leaves = {name for name, _ in _collection_leaves(collection.value)}
        if not leaves or not leaves.issubset(queue_names) or claimed.intersection(leaves):
            continue
        result.append(collection)
        claimed.update(leaves)
    return tuple(result)


def _array_cpp_type(
    value: StaticQueueCollection, queues: dict[str, QueueBinding]
) -> str:
    first = value.members[0][1]
    element = (
        f"gfsim::SimQueue<{_cpp_type(queues[first].payload)}>"
        if isinstance(first, str)
        else _array_cpp_type(first, queues)
    )
    return f"std::array<{element}, {len(value.members)}>"


def _object_ids(program: QueueProgram, fanouts: tuple[_Fanout, ...]) -> _ObjectIds:
    next_id = 0
    queues: dict[str, int] = {}
    for queue in program.queues:
        queues[queue.name] = next_id
        next_id += 1
    fanout_queues: dict[str, int] = {}
    for fanout in fanouts:
        for output in fanout.outputs:
            fanout_queues[output] = next_id
            next_id += 1
    feedback_states = tuple(range(next_id, next_id + len(program.feedbacks)))
    next_id += len(feedback_states)
    broadcasts = tuple(range(next_id, next_id + len(fanouts)))
    next_id += len(broadcasts)
    transforms: dict[str, int] = {}
    for queue in program.queues:
        if queue.input_name is not None:
            transforms[queue.name] = next_id
            next_id += 1
    routes = tuple(range(next_id, next_id + len(program.routes)))
    next_id += len(routes)
    merges = tuple(range(next_id, next_id + len(program.merges)))
    next_id += len(merges)
    feedbacks = tuple(range(next_id, next_id + len(program.feedbacks)))
    next_id += len(feedbacks)
    sinks = tuple(range(next_id, next_id + len(program.sinks)))
    return _ObjectIds(
        queues,
        fanout_queues,
        feedback_states,
        broadcasts,
        transforms,
        routes,
        merges,
        feedbacks,
        sinks,
    )


def lower_queue_program_to_cpp(program: QueueProgram) -> str:
    if program.scopes:
        raise QueueFrontendError(
            "ACLOWER-UNSUPPORTED-CONSTRUCT: initial gfsim slice supports serial "
            "root Queue graphs"
        )
    fanouts = _fanouts(program)
    arrays = _owning_arrays(program)
    ids = _object_ids(program, fanouts)
    queues_by_name = {queue.name: queue for queue in program.queues}
    array_leaf: dict[str, tuple[str, tuple[int, ...]]] = {
        leaf: (collection.name, path)
        for collection in arrays
        for leaf, path in _collection_leaves(collection.value)
    }

    def queue_ref(name: str) -> str:
        owner = array_leaf.get(name)
        if owner is None:
            return f"{name}_"
        collection, path = owner
        return f"{collection}_" + "".join(f"[{index}]" for index in path)

    def queue_initializer(name: str) -> str:
        queue = queues_by_name[name]
        return (
            f'gfsim::SimQueue<{_cpp_type(queue.payload)}>("{queue.name}", '
            f"{ids.queues[queue.name]}, this, {queue.depth}, "
            "std::numeric_limits<size_t>::max(), nullptr, "
            f"{queue.latency})"
        )

    def array_initializer(value: StaticQueueCollection) -> str:
        members = []
        for _, member in value.members:
            members.append(
                queue_initializer(member)
                if isinstance(member, str)
                else array_initializer(member)
            )
        return "{{" + ", ".join(members) + "}}"
    effective_input = {
        consumer: output
        for fanout in fanouts
        for consumer, output in zip(fanout.consumers, fanout.outputs, strict=True)
    }
    lines = [
        "// Generated by Agentic Circuit Queue/Var v0.2; do not edit.",
        '#include "gfsim/dispatch.h"',
        '#include "gfsim/object.h"',
        '#include "gfsim/queue.h"',
        '#include "gfsim/queue_blocks.h"',
        "",
        "#include <array>",
        "#include <cstdint>",
        "#include <limits>",
        "",
        "namespace ac_generated {",
        "",
    ]
    for payload in program.payloads:
        lines.extend(_emit_payload(payload))
    for queue in program.queues:
        if queue.input_name is None:
            continue
        payload = _cpp_type(queue.payload)
        lines.extend(
            (
                f"struct {queue.name}_policy {{",
                f"  {payload} operator()(const {payload} &item) const {{",
                *_policy_body(queue),
                "  }",
                "};",
                "",
            )
        )
    for index, route in enumerate(program.routes):
        payload = _cpp_type(
            next(queue.payload for queue in program.queues if queue.name == route.input_name)
        )
        expression = _CppExpression(route.argument).emit(route.selector)
        lines.extend(
            (
                f"struct route_{index}_policy {{",
                f"  size_t operator()(const {payload} &item) const {{",
                f"    return static_cast<size_t>({expression});",
                "  }",
                "};",
                "",
            )
        )
    for index, feedback in enumerate(program.feedbacks):
        payload = _cpp_type(
            next(
                queue.payload
                for queue in program.queues
                if queue.name == feedback.output_name
            )
        )
        lines.extend(
            (
                f"struct feedback_{index}_update_policy {{",
                f"  {payload} operator()(const {payload} &item) const {{",
                *_expression_policy_body(feedback.argument, feedback.update),
                "  }",
                "};",
                "",
                f"struct feedback_{index}_condition_policy {{",
                f"  bool operator()(const {payload} &item) const {{",
                f"    return {_CppExpression(feedback.argument).emit(feedback.condition)};",
                "  }",
                "};",
                "",
            )
        )

    class_name = "".join(part.capitalize() for part in program.system.split("_"))
    lines.extend((f"class {class_name} final : public gfsim::Module {{", "public:"))
    lines.append(
        f'  {class_name}() : gfsim::Module("{program.system}", '
        "gfsim::kInvalidObjectId, nullptr),"
    )
    initializers: list[str] = []
    for queue in program.queues:
        if queue.name in array_leaf:
            continue
        payload = _cpp_type(queue.payload)
        initializers.append(
            f'{queue.name}_("{queue.name}", {ids.queues[queue.name]}, this, '
            f"{queue.depth}, std::numeric_limits<size_t>::max(), nullptr, "
            f"{queue.latency})"
        )
    for collection in arrays:
        initializers.append(
            f"{collection.name}_" + array_initializer(collection.value)
        )
    for fanout in fanouts:
        for output in fanout.outputs:
            initializers.append(
                f'{output}_("{output}", {ids.fanout_queues[output]}, this, '
                "1, std::numeric_limits<size_t>::max(), nullptr, 1)"
            )
    for index, feedback in enumerate(program.feedbacks):
        payload = _cpp_type(
            next(
                queue.payload
                for queue in program.queues
                if queue.name == feedback.output_name
            )
        )
        initializers.append(
            f'feedback_{index}_state_("feedback_{index}_state", '
            f"{ids.feedback_states[index]}, this, 1, "
            "std::numeric_limits<size_t>::max(), nullptr, 1)"
        )
    for index, fanout in enumerate(fanouts):
        payload = _cpp_type(fanout.payload)
        outputs = ", ".join(f"&{queue_ref(name)}" for name in fanout.outputs)
        initializers.append(
            f'broadcast_{index}_block_("broadcast_{index}", '
            f"{ids.broadcasts[index]}, this, {queue_ref(fanout.source)}, "
            f"std::array<gfsim::SimQueue<{payload}> *, {len(fanout.outputs)}>"
            f"{{{outputs}}})"
        )
    for index, route in enumerate(program.routes):
        payload = _cpp_type(
            next(queue.payload for queue in program.queues if queue.name == route.input_name)
        )
        outputs = ", ".join(f"&{queue_ref(name)}" for name in route.outputs)
        initializers.append(
            f'route_{index}_block_("route_{index}", {ids.routes[index]}, this, '
            f"{queue_ref(route.input_name)}, std::array<gfsim::SimQueue<{payload}> *, "
            f"{len(route.outputs)}>{{{outputs}}})"
        )
    for index, merge in enumerate(program.merges):
        payload = _cpp_type(
            next(queue.payload for queue in program.queues if queue.name == merge.output)
        )
        inputs = ", ".join(f"&{queue_ref(name)}" for name in merge.inputs)
        policy = (
            "gfsim::QueueMergePolicy::RoundRobin"
            if merge.policy == "round_robin"
            else "gfsim::QueueMergePolicy::Priority"
        )
        initializers.append(
            f'merge_{index}_block_("merge_{index}", {ids.merges[index]}, this, '
            f"std::array<gfsim::SimQueue<{payload}> *, {len(merge.inputs)}>"
            f"{{{inputs}}}, {queue_ref(merge.output)}, {policy})"
        )
    for index, feedback in enumerate(program.feedbacks):
        initializers.append(
            f'feedback_{index}_block_("feedback_{index}", '
            f"{ids.feedbacks[index]}, this, {queue_ref(feedback.input_name)}, "
            f"feedback_{index}_state_, {queue_ref(feedback.output_name)}, "
            f"{feedback.max_iterations})"
        )
    for queue in program.queues:
        if queue.input_name is None:
            continue
        input_name = effective_input.get(queue.name, queue.input_name)
        initializers.append(
            f'{queue.name}_block_("{queue.name}_transform", '
            f"{ids.transforms[queue.name]}, this, {queue_ref(input_name)}, "
            f"{queue_ref(queue.name)})"
        )
    for index, sink in enumerate(program.sinks):
        initializers.append(
            f'sink_{index}_("sink_{index}", {ids.sinks[index]}, this, '
            f"{queue_ref(sink.queue)})"
        )
    for index, initializer in enumerate(initializers):
        suffix = "," if index + 1 < len(initializers) else ""
        lines.append(f"        {initializer}{suffix}")
    lines.extend(("  {", f'    setPath("/{program.system}");'))
    for queue in program.queues:
        lines.append(f"    attachChild({queue_ref(queue.name)});")
    for fanout in fanouts:
        for output in fanout.outputs:
            lines.append(f"    attachChild({output}_);")
    for index, _ in enumerate(program.feedbacks):
        lines.append(f"    attachChild(feedback_{index}_state_);")
    for index, _ in enumerate(fanouts):
        lines.append(f"    attachChild(broadcast_{index}_block_);")
    for queue in program.queues:
        if queue.input_name is not None:
            lines.append(f"    attachChild({queue.name}_block_);")
    for index, _ in enumerate(program.routes):
        lines.append(f"    attachChild(route_{index}_block_);")
    for index, _ in enumerate(program.merges):
        lines.append(f"    attachChild(merge_{index}_block_);")
    for index, _ in enumerate(program.feedbacks):
        lines.append(f"    attachChild(feedback_{index}_block_);")
    for index, _ in enumerate(program.sinks):
        lines.append(f"    attachChild(sink_{index}_);")
    lines.extend(("  }", ""))
    source_queues = [queue for queue in program.queues if queue.input_name is None]
    for queue in source_queues:
        payload = _cpp_type(queue.payload)
        lines.append(
            f"  gfsim::SimQueue<{payload}> &{queue.name}() {{ "
            f"return {queue_ref(queue.name)}; }}"
        )
    for index, sink in enumerate(program.sinks):
        payload = _cpp_type(next(q.payload for q in program.queues if q.name == sink.queue))
        lines.append(
            f"  const std::vector<{payload}> &sink_{index}_values() const {{ "
            f"return sink_{index}_.received(); }}"
        )
    object_count = (
        len(program.queues)
        + len(ids.fanout_queues)
        + len(ids.feedback_states)
        + len(ids.broadcasts)
        + len(ids.transforms)
        + len(ids.routes)
        + len(ids.merges)
        + len(ids.feedbacks)
        + len(ids.sinks)
    )
    lines.extend(
        (
            "",
            f"  std::array<gfsim::DispatchRow, {object_count}> dispatch_rows() {{",
            f"    return std::array<gfsim::DispatchRow, {object_count}>{{",
        )
    )
    rows: list[str] = []
    for queue in program.queues:
        rows.append(f"gfsim::makeDispatchRow(&{queue_ref(queue.name)})")
    for fanout in fanouts:
        for output in fanout.outputs:
            rows.append(f"gfsim::makeDispatchRow(&{output}_)")
    for index, _ in enumerate(program.feedbacks):
        rows.append(f"gfsim::makeDispatchRow(&feedback_{index}_state_)")
    for index, _ in enumerate(fanouts):
        rows.append(f"gfsim::makeDispatchRow(&broadcast_{index}_block_)")
    for queue in program.queues:
        if queue.input_name is not None:
            rows.append(f"gfsim::makeDispatchRow(&{queue.name}_block_)")
    for index, _ in enumerate(program.routes):
        rows.append(f"gfsim::makeDispatchRow(&route_{index}_block_)")
    for index, _ in enumerate(program.merges):
        rows.append(f"gfsim::makeDispatchRow(&merge_{index}_block_)")
    for index, _ in enumerate(program.feedbacks):
        rows.append(f"gfsim::makeDispatchRow(&feedback_{index}_block_)")
    for index, _ in enumerate(program.sinks):
        rows.append(f"gfsim::makeDispatchRow(&sink_{index}_)")
    for index, row in enumerate(rows):
        suffix = "," if index + 1 < len(rows) else ""
        lines.append(f"        {row}{suffix}")
    lines.extend(("    };", "  }", "", "private:"))
    for queue in program.queues:
        if queue.name in array_leaf:
            continue
        lines.append(f"  gfsim::SimQueue<{_cpp_type(queue.payload)}> {queue.name}_;")
    for collection in arrays:
        lines.append(
            f"  {_array_cpp_type(collection.value, queues_by_name)} "
            f"{collection.name}_;"
        )
    for fanout in fanouts:
        for output in fanout.outputs:
            lines.append(
                f"  gfsim::SimQueue<{_cpp_type(fanout.payload)}> {output}_;"
            )
    for index, feedback in enumerate(program.feedbacks):
        payload = _cpp_type(
            next(
                queue.payload
                for queue in program.queues
                if queue.name == feedback.output_name
            )
        )
        lines.append(
            f"  gfsim::SimQueue<gfsim::FeedbackToken<{payload}>> "
            f"feedback_{index}_state_;"
        )
    for index, fanout in enumerate(fanouts):
        payload = _cpp_type(fanout.payload)
        lines.append(
            f"  gfsim::QueueBroadcast<{payload}, {len(fanout.outputs)}> "
            f"broadcast_{index}_block_;"
        )
    for queue in program.queues:
        if queue.input_name is None:
            continue
        payload = _cpp_type(queue.payload)
        lines.append(
            f"  gfsim::QueueTransform<{payload}, {payload}, {queue.name}_policy> "
            f"{queue.name}_block_;"
        )
    for index, route in enumerate(program.routes):
        payload = _cpp_type(
            next(queue.payload for queue in program.queues if queue.name == route.input_name)
        )
        lines.append(
            f"  gfsim::QueueRoute<{payload}, {len(route.outputs)}, "
            f"route_{index}_policy> route_{index}_block_;"
        )
    for index, merge in enumerate(program.merges):
        payload = _cpp_type(
            next(queue.payload for queue in program.queues if queue.name == merge.output)
        )
        lines.append(
            f"  gfsim::QueueMerge<{payload}, {len(merge.inputs)}> "
            f"merge_{index}_block_;"
        )
    for index, feedback in enumerate(program.feedbacks):
        payload = _cpp_type(
            next(
                queue.payload
                for queue in program.queues
                if queue.name == feedback.output_name
            )
        )
        lines.append(
            f"  gfsim::QueueFeedback<{payload}, feedback_{index}_update_policy, "
            f"feedback_{index}_condition_policy> feedback_{index}_block_;"
        )
    for index, sink in enumerate(program.sinks):
        payload = _cpp_type(next(q.payload for q in program.queues if q.name == sink.queue))
        lines.append(f"  gfsim::QueueSink<{payload}> sink_{index}_;")
    lines.extend(("};", "", "} // namespace ac_generated", ""))
    return "\n".join(lines)


def _emit_payload(payload: Payload) -> list[str]:
    lines = [f"struct {payload.name} {{"]
    for name, typ in payload.fields:
        lines.append(f"  {_cpp_type(typ)} {name}{{}};")
    lines.extend(("};", ""))
    return lines


def lower_queue_source_to_cpp(text: str, system: str) -> str:
    return lower_queue_program_to_cpp(parse_queue_program(text, system))
