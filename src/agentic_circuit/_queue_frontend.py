"""v0.2 serial-Python to Queue/Var ACIR construction."""

from __future__ import annotations

import ast
import copy
from dataclasses import dataclass


class QueueFrontendError(ValueError):
    """A stable rejection from the v0.2 queue frontend."""


@dataclass(frozen=True, slots=True)
class Payload:
    name: str
    fields: tuple[tuple[str, str], ...]

    @property
    def acir_type(self) -> str:
        return f"!ac.struct<@types::@{self.name}>"


@dataclass(frozen=True, slots=True)
class QueueBinding:
    name: str
    payload: str
    depth: int
    latency: int
    input_name: str | None
    argument: str | None = None
    expression: ast.expr | None = None
    scope: tuple[str, ...] = ()
    order: int = 0
    route_output: bool = False
    feedback_output: bool = False
    merge_output: bool = False
    atomic_group: int | None = None


@dataclass(frozen=True, slots=True)
class ScopeBinding:
    name: str
    path: tuple[str, ...]
    order: int


@dataclass(frozen=True, slots=True)
class SinkBinding:
    queue: str
    scope: tuple[str, ...]
    order: int


@dataclass(frozen=True, slots=True)
class ObservationBinding:
    queue: str
    name: str
    scope: tuple[str, ...]
    order: int


@dataclass(frozen=True, slots=True)
class RouteBinding:
    input_name: str
    outputs: tuple[str, ...]
    argument: str
    selector: ast.expr
    depth: int
    latency: int
    scope: tuple[str, ...]
    order: int


@dataclass(frozen=True, slots=True)
class ForkBinding:
    input_name: str
    outputs: tuple[str, ...]
    depth: int
    latency: int
    scope: tuple[str, ...]
    order: int


@dataclass(frozen=True, slots=True)
class FeedbackBinding:
    input_name: str
    output_name: str
    argument: str
    condition: ast.expr
    update: ast.expr
    depth: int
    latency: int
    max_iterations: int
    scope: tuple[str, ...]
    order: int


@dataclass(frozen=True, slots=True)
class MergeBinding:
    inputs: tuple[str, ...]
    output: str
    policy: str
    depth: int
    latency: int
    scope: tuple[str, ...]
    order: int


@dataclass(frozen=True, slots=True)
class AtomicBinding:
    queues: tuple[str, ...]
    scope: tuple[str, ...]
    order: int


@dataclass(frozen=True, slots=True)
class StaticQueueCollection:
    kind: str
    members: tuple[
        tuple[str | int | bool, str | StaticQueueCollection], ...
    ]


@dataclass(frozen=True, slots=True)
class CollectionBinding:
    name: str
    value: StaticQueueCollection
    scope: tuple[str, ...]
    order: int


@dataclass(frozen=True, slots=True)
class QueueProgram:
    system: str
    payloads: tuple[Payload, ...]
    queues: tuple[QueueBinding, ...]
    scopes: tuple[ScopeBinding, ...]
    routes: tuple[RouteBinding, ...]
    forks: tuple[ForkBinding, ...]
    feedbacks: tuple[FeedbackBinding, ...]
    merges: tuple[MergeBinding, ...]
    atomics: tuple[AtomicBinding, ...]
    collections: tuple[CollectionBinding, ...]
    observations: tuple[ObservationBinding, ...]
    sinks: tuple[SinkBinding, ...]


def _decorator_name(node: ast.expr) -> str:
    if isinstance(node, ast.Call):
        return _decorator_name(node.func)
    if isinstance(node, ast.Name):
        return node.id
    if isinstance(node, ast.Attribute):
        prefix = _decorator_name(node.value)
        return f"{prefix}.{node.attr}" if prefix else node.attr
    return ""


def _scalar_type(node: ast.expr) -> str:
    name = _decorator_name(node).rsplit(".", 1)[-1]
    if name == "int":
        return "i64"
    if name == "bool":
        return "i1"
    widths = {
        "u1": 1,
        "u2": 2,
        "u4": 4,
        "u8": 8,
        "u16": 16,
        "u32": 32,
        "u64": 64,
        "s8": 8,
        "s16": 16,
        "s32": 32,
        "s64": 64,
    }
    if name in widths:
        return f"i{widths[name]}"
    raise QueueFrontendError("ACPY-QUEUE-002: unsupported field type")


def _payloads(tree: ast.Module) -> tuple[Payload, ...]:
    result: list[Payload] = []
    for node in tree.body:
        if not isinstance(node, ast.ClassDef) or not any(
            _decorator_name(item).rsplit(".", 1)[-1] == "struct"
            for item in node.decorator_list
        ):
            continue
        fields: list[tuple[str, str]] = []
        for statement in node.body:
            if not isinstance(statement, ast.AnnAssign) or not isinstance(
                statement.target, ast.Name
            ):
                raise QueueFrontendError(
                    "ACPY-QUEUE-002: struct body requires annotated fields"
                )
            fields.append((statement.target.id, _scalar_type(statement.annotation)))
        if not fields or len({name for name, _ in fields}) != len(fields):
            raise QueueFrontendError(
                "ACPY-QUEUE-002: struct requires unique compile-time fields"
            )
        result.append(Payload(node.name, tuple(fields)))
    return tuple(result)


def _static_int(node: ast.expr, values: dict[str, int]) -> int | None:
    if isinstance(node, ast.Constant) and type(node.value) is int:
        return node.value
    if isinstance(node, ast.Name):
        return values.get(node.id)
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub)):
        operand = _static_int(node.operand, values)
        if operand is None:
            return None
        return operand if isinstance(node.op, ast.UAdd) else -operand
    if isinstance(node, ast.BinOp) and isinstance(
        node.op, (ast.Add, ast.Sub, ast.Mult)
    ):
        left = _static_int(node.left, values)
        right = _static_int(node.right, values)
        if left is None or right is None:
            return None
        if isinstance(node.op, ast.Add):
            return left + right
        if isinstance(node.op, ast.Sub):
            return left - right
        return left * right
    return None


def _positive_int(
    call: ast.Call,
    name: str,
    default: int,
    static_values: dict[str, int] | None = None,
) -> int:
    matches = [keyword for keyword in call.keywords if keyword.arg == name]
    if len(matches) > 1:
        raise QueueFrontendError(f"ACPY-QUEUE-001: repeated {name!r}")
    if not matches:
        return default
    value = _static_int(matches[0].value, static_values or {})
    if value is None:
        raise QueueFrontendError(f"ACPY-QUEUE-001: {name} must be a compile-time integer")
    if value <= 0:
        raise QueueFrontendError(f"ACPY-QUEUE-001: {name} must be positive")
    return value


def _payload(node: ast.expr, payloads: dict[str, Payload]) -> str:
    try:
        return _scalar_type(node)
    except QueueFrontendError:
        pass
    if isinstance(node, ast.Name) and node.id in payloads:
        return payloads[node.id].acir_type
    raise QueueFrontendError(
        "ACPY-QUEUE-002: source payload must be a compile-time supported type"
    )


def _lambda(node: ast.expr) -> tuple[str, ast.expr]:
    if not isinstance(node, ast.Lambda) or len(node.args.args) != 1:
        raise QueueFrontendError("ACPY-QUEUE-003: apply requires a one-argument lambda")
    return node.args.args[0].arg, node.body


def parse_queue_program(text: str, system: str) -> QueueProgram:
    tree = ast.parse(text, filename="<queue-model>", type_comments=True)
    for node in tree.body:
        decorators = getattr(node, "decorator_list", ())
        if any(
            _decorator_name(decorator).rsplit(".", 1)[-1]
            in {"opcode", "provider", "backend"}
            for decorator in decorators
        ):
            raise QueueFrontendError(
                "ACPY-QUEUE-010: user opcode or backend providers are forbidden"
            )
    payloads = _payloads(tree)
    payload_map = {item.name: item for item in payloads}
    candidates = [
        node
        for node in tree.body
        if isinstance(node, ast.FunctionDef)
        and node.name == system
        and any(_decorator_name(d).rsplit(".", 1)[-1] == "system" for d in node.decorator_list)
    ]
    if len(candidates) != 1:
        raise QueueFrontendError(f"ACPY-QUEUE-001: system {system!r} is missing or ambiguous")
    function = candidates[0]
    if function.args.args or function.args.posonlyargs or function.args.kwonlyargs:
        raise QueueFrontendError("ACPY-QUEUE-001: a queue system infers boundaries and takes no parameters")
    queues: list[QueueBinding] = []
    scopes: list[ScopeBinding] = []
    routes: list[RouteBinding] = []
    forks: list[ForkBinding] = []
    feedbacks: list[FeedbackBinding] = []
    merges: list[MergeBinding] = []
    atomics: list[AtomicBinding] = []
    sinks: list[SinkBinding] = []
    observations: list[ObservationBinding] = []
    by_name: dict[str, QueueBinding] = {}
    collections: dict[str, StaticQueueCollection] = {}
    collection_bindings: list[CollectionBinding] = []
    order = 0

    def call_name(call: ast.Call) -> str:
        return _decorator_name(call.func).rsplit(".", 1)[-1]

    def static_reference(
        node: ast.expr,
        aliases: dict[str, str | StaticQueueCollection],
    ) -> str | StaticQueueCollection:
        if isinstance(node, ast.Name):
            if node.id in aliases:
                return aliases[node.id]
            if node.id in by_name:
                return by_name[node.id].name
            if node.id in collections:
                return collections[node.id]
        if (
            isinstance(node, ast.Subscript)
            and isinstance(node.slice, ast.Constant)
            and type(node.slice.value) in {str, int, bool}
        ):
            collection = static_reference(node.value, aliases)
            if not isinstance(collection, StaticQueueCollection):
                raise QueueFrontendError(
                    "ACPY-QUEUE-005: static indexing requires a collection"
                )
            for key, value in collection.members:
                if type(key) is type(node.slice.value) and key == node.slice.value:
                    return value
            raise QueueFrontendError(
                f"ACPY-QUEUE-005: collection has no key {node.slice.value!r}"
            )
        raise QueueFrontendError(
            "ACPY-QUEUE-005: collection reference must be statically resolvable"
        )

    def queue_reference(
        node: ast.expr,
        aliases: dict[str, str | StaticQueueCollection],
    ) -> str:
        value = static_reference(node, aliases)
        if isinstance(value, str):
            return value
        raise QueueFrontendError(
            "ACPY-QUEUE-005: a collection cannot be used as one Queue"
        )

    def collection_signature(
        value: str | StaticQueueCollection,
    ) -> tuple[object, ...]:
        if isinstance(value, str):
            return ("queue", by_name[value].payload)
        keys = tuple(key for key, _ in value.members)
        members = tuple(collection_signature(member) for _, member in value.members)
        return (value.kind, keys, members)

    def stable_collection_identity(value: str | StaticQueueCollection) -> str:
        if isinstance(value, str):
            return value
        return value.kind + "(" + ",".join(
            f"{key}:{stable_collection_identity(member)}"
            for key, member in value.members
        ) + ")"

    def source_binding(
        name: str,
        call: ast.Call,
        scope_path: tuple[str, ...],
        current_order: int,
        static_values: dict[str, int] | None = None,
    ) -> QueueBinding:
        if call_name(call) != "source" or len(call.args) != 1:
            raise QueueFrontendError(
                "ACPY-QUEUE-005: collection elements must be Queue sources"
            )
        return QueueBinding(
            name,
            _payload(call.args[0], payload_map),
            _positive_int(call, "depth", 1, static_values),
            _positive_int(call, "latency", 1, static_values),
            None,
            scope=scope_path,
            order=current_order,
        )

    def collection_binding(
        name: str,
        call: ast.Call,
        scope_path: tuple[str, ...],
        current_order: int,
        aliases: dict[str, str | StaticQueueCollection],
        static_values: dict[str, int] | None = None,
    ) -> StaticQueueCollection | None:
        static_values = {} if static_values is None else static_values
        kind = call_name(call)
        if kind == "array":
            extent = (
                _static_int(call.args[0], static_values)
                if len(call.args) == 2
                else None
            )
            if (
                len(call.args) != 2
                or extent is None
                or extent <= 0
            ):
                raise QueueFrontendError(
                    "ACPY-QUEUE-005: array requires a positive compile-time extent"
                )
            argument, body = _lambda(call.args[1])
            members: list[
                tuple[str | int, str | StaticQueueCollection]
            ] = []
            for index in range(extent):
                if not isinstance(body, ast.Call):
                    raise QueueFrontendError(
                        "ACPY-QUEUE-005: array generator must produce a Queue"
                    )
                leaf = f"{name}__{index}"
                values = {**static_values, argument: index}
                if call_name(body) == "source":
                    binding = source_binding(
                        leaf, body, scope_path, current_order, values
                    )
                    queues.append(binding)
                    by_name[leaf] = binding
                    member: str | StaticQueueCollection = leaf
                else:
                    nested = collection_binding(
                        leaf,
                        body,
                        scope_path,
                        current_order,
                        aliases,
                        values,
                    )
                    if nested is None:
                        raise QueueFrontendError(
                            "ACPY-QUEUE-005: array generator must produce a Queue "
                            "or static collection"
                        )
                    member = nested
                members.append((index, member))
            signatures = {collection_signature(member) for _, member in members}
            if len(signatures) != 1:
                raise QueueFrontendError(
                    "ACPY-QUEUE-005: array elements must have one static shape"
                )
            return StaticQueueCollection("array", tuple(members))
        if kind == "map":
            if len(call.args) != 1 or not isinstance(call.args[0], ast.Dict):
                raise QueueFrontendError(
                    "ACPY-QUEUE-005: map requires one compile-time dictionary"
                )
            entries: list[
                tuple[str | int | bool, str | StaticQueueCollection]
            ] = []
            for key, value in zip(call.args[0].keys, call.args[0].values, strict=True):
                if (
                    not isinstance(key, ast.Constant)
                    or type(key.value) not in {str, int, bool}
                    or (type(key.value) is str and not key.value)
                ):
                    raise QueueFrontendError(
                        "ACPY-QUEUE-005: map keys must be compile-time bool/int/str"
                    )
                entries.append((key.value, static_reference(value, aliases)))
            rank = {bool: 0, int: 1, str: 2}
            entries.sort(key=lambda item: (rank[type(item[0])], item[0]))
            if not entries or len({(type(key), key) for key, _ in entries}) != len(entries):
                raise QueueFrontendError(
                    "ACPY-QUEUE-005: map keys must be unique and non-empty"
                )
            if len({collection_signature(value) for _, value in entries}) != 1:
                raise QueueFrontendError(
                    "ACPY-QUEUE-005: map values must have one static shape"
                )
            return StaticQueueCollection("map", tuple(entries))
        if kind == "set":
            if len(call.args) != 1 or not isinstance(
                call.args[0], (ast.Set, ast.List, ast.Tuple)
            ):
                raise QueueFrontendError(
                    "ACPY-QUEUE-005: set requires one compile-time collection"
                )
            members = [static_reference(item, aliases) for item in call.args[0].elts]
            identities = [stable_collection_identity(member) for member in members]
            if not members or len(set(identities)) != len(members):
                raise QueueFrontendError(
                    "ACPY-QUEUE-005: set members must be unique and non-empty"
                )
            members.sort(key=stable_collection_identity)
            if len({collection_signature(member) for member in members}) != 1:
                raise QueueFrontendError(
                    "ACPY-QUEUE-005: set members must have one static shape"
                )
            return StaticQueueCollection(
                "set", tuple((index, member) for index, member in enumerate(members))
            )
        return None

    def visit(
        statements: list[ast.stmt],
        scope_path: tuple[str, ...],
        aliases: dict[str, str | StaticQueueCollection] | None = None,
        atomic_group: int | None = None,
    ) -> None:
        nonlocal order
        aliases = {} if aliases is None else aliases
        for statement in statements:
            current_order = order
            order += 1
            if isinstance(statement, ast.If):
                if not (
                    isinstance(statement.test, ast.Constant)
                    and type(statement.test.value) is bool
                ):
                    raise QueueFrontendError(
                        "ACPY-QUEUE-011: runtime Queue condition must use route"
                    )
                selected = statement.body if statement.test.value else statement.orelse
                visit(selected, scope_path, aliases, atomic_group)
                continue
            if isinstance(statement, ast.With) and len(statement.items) == 1:
                item = statement.items[0]
                call = item.context_expr
                if (
                    item.optional_vars is None
                    and isinstance(call, ast.Call)
                    and call_name(call) == "scope"
                    and len(call.args) == 1
                    and isinstance(call.args[0], ast.Constant)
                    and type(call.args[0].value) is str
                    and call.args[0].value
                ):
                    path = (*scope_path, call.args[0].value)
                    if any(existing.path == path for existing in scopes):
                        raise QueueFrontendError("ACPY-QUEUE-004: duplicate scope path")
                    scopes.append(ScopeBinding(call.args[0].value, path, current_order))
                    visit(statement.body, path, aliases, atomic_group)
                    continue
            if (
                isinstance(statement, ast.With)
                and len(statement.items) == 1
                and atomic_group is None
            ):
                item = statement.items[0]
                call = item.context_expr
                if (
                    item.optional_vars is None
                    and isinstance(call, ast.Call)
                    and call_name(call) == "atomic"
                    and not call.args
                    and not call.keywords
                ):
                    group = len(atomics)
                    queue_start = len(queues)
                    scope_start = len(scopes)
                    route_start = len(routes)
                    fork_start = len(forks)
                    merge_start = len(merges)
                    feedback_start = len(feedbacks)
                    sink_start = len(sinks)
                    observation_start = len(observations)
                    visit(statement.body, scope_path, aliases, group)
                    created = queues[queue_start:]
                    if (
                        len(created) < 2
                        or any(queue.atomic_group != group for queue in created)
                        or len(scopes) != scope_start
                        or len(routes) != route_start
                        or len(forks) != fork_start
                        or len(merges) != merge_start
                        or len(feedbacks) != feedback_start
                        or len(sinks) != sink_start
                        or len(observations) != observation_start
                    ):
                        raise QueueFrontendError(
                            "ACPY-QUEUE-009: atomic requires at least two direct "
                            "Queue apply statements"
                        )
                    inputs = [queue.input_name for queue in created]
                    if None in inputs or len(set(inputs)) != len(inputs):
                        raise QueueFrontendError(
                            "ACPY-QUEUE-009: atomic inputs must be unique Queues"
                        )
                    atomics.append(
                        AtomicBinding(
                            tuple(queue.name for queue in created),
                            scope_path,
                            current_order,
                        )
                    )
                    continue
            if (
                isinstance(statement, ast.Assign)
                and len(statement.targets) == 1
                and isinstance(statement.targets[0], ast.Name)
                and isinstance(statement.value, ast.Call)
                and call_name(statement.value) in {"array", "map", "set"}
            ):
                name = statement.targets[0].id
                if name in by_name or name in collections:
                    raise QueueFrontendError(
                        "ACPY-QUEUE-005: collection assignment requires one fresh name"
                    )
                collection = collection_binding(
                    name,
                    statement.value,
                    scope_path,
                    current_order,
                    aliases,
                )
                assert collection is not None
                collections[name] = collection
                collection_bindings.append(
                    CollectionBinding(name, collection, scope_path, current_order)
                )
                continue
            if (
                isinstance(statement, ast.For)
                and isinstance(statement.target, ast.Name)
                and isinstance(statement.iter, ast.Call)
                and call_name(statement.iter) == "range"
                and len(statement.iter.args) == 1
                and not statement.iter.keywords
                and not statement.orelse
            ):
                extent = _static_int(statement.iter.args[0], {})
                if extent is None or extent < 0:
                    raise QueueFrontendError(
                        "ACPY-QUEUE-005: range extent must be a non-negative "
                        "compile-time integer"
                    )

                class StaticIndex(ast.NodeTransformer):
                    def visit_Name(self, node: ast.Name) -> ast.expr:
                        if node.id == statement.target.id:
                            return ast.copy_location(ast.Constant(index), node)
                        return node

                for index in range(extent):
                    expanded = [
                        StaticIndex().visit(copy.deepcopy(body))
                        for body in statement.body
                    ]
                    visit(expanded, scope_path, aliases, atomic_group)
                continue
            if (
                isinstance(statement, ast.For)
                and isinstance(statement.target, ast.Name)
                and not statement.orelse
            ):
                collection = static_reference(statement.iter, aliases)
                if not isinstance(collection, StaticQueueCollection):
                    raise QueueFrontendError(
                        "ACPY-QUEUE-005: compile-time for requires a static collection"
                    )
                for _, member in collection.members:
                    visit(
                        statement.body,
                        scope_path,
                        {**aliases, statement.target.id: member},
                        atomic_group,
                    )
                continue
            if isinstance(statement, ast.While) and not statement.orelse:
                if (
                    len(statement.body) != 1
                    or not isinstance(statement.body[0], ast.Assign)
                    or len(statement.body[0].targets) != 1
                    or not isinstance(statement.body[0].targets[0], ast.Name)
                    or not isinstance(statement.body[0].value, ast.Call)
                ):
                    raise QueueFrontendError(
                        "ACPY-QUEUE-007: runtime while requires one Queue update"
                    )
                update_statement = statement.body[0]
                variable = update_statement.targets[0].id
                call = update_statement.value
                incoming = by_name.get(variable)
                if (
                    incoming is None
                    or not isinstance(call.func, ast.Attribute)
                    or call.func.attr != "apply"
                    or not isinstance(call.func.value, ast.Name)
                    or call.func.value.id != variable
                    or len(call.args) != 1
                ):
                    raise QueueFrontendError(
                        "ACPY-QUEUE-007: runtime while must rebind one Queue through apply"
                    )
                argument, update = _lambda(call.args[0])

                class QueueCondition(ast.NodeTransformer):
                    def visit_Name(self, node: ast.Name) -> ast.expr:
                        if node.id == variable:
                            return ast.copy_location(ast.Name(id=argument), node)
                        return node

                condition = QueueCondition().visit(copy.deepcopy(statement.test))
                assert isinstance(condition, ast.expr)
                output_name = f"{variable}__feedback{len(feedbacks)}"
                depth = _positive_int(call, "depth", 1)
                latency = _positive_int(call, "latency", 1)
                output = QueueBinding(
                    output_name,
                    incoming.payload,
                    depth,
                    latency,
                    None,
                    scope=scope_path,
                    order=current_order,
                    feedback_output=True,
                )
                queues.append(output)
                by_name[variable] = output
                feedbacks.append(
                    FeedbackBinding(
                        incoming.name,
                        output_name,
                        argument,
                        condition,
                        update,
                        depth,
                        latency,
                        1024,
                        scope_path,
                        current_order,
                    )
                )
                continue
            if (
                isinstance(statement, ast.Assign)
                and len(statement.targets) == 1
                and isinstance(statement.targets[0], ast.Name)
                and isinstance(statement.value, ast.Call)
                and isinstance(statement.value.func, ast.Attribute)
                and statement.value.func.attr == "merge"
                and isinstance(statement.value.func.value, ast.Name)
            ):
                name = statement.targets[0].id
                if name in by_name or name in collections:
                    raise QueueFrontendError(
                        "ACPY-QUEUE-008: merge output requires one fresh name"
                    )
                call = statement.value
                operands = [call.func.value, *call.args]
                inputs = tuple(queue_reference(operand, aliases) for operand in operands)
                if len(inputs) < 2:
                    raise QueueFrontendError(
                        "ACPY-QUEUE-008: merge requires at least two Queues"
                    )
                payload = by_name[inputs[0]].payload
                if any(by_name[input_name].payload != payload for input_name in inputs):
                    raise QueueFrontendError(
                        "ACPY-QUEUE-008: merge Queue payloads must match"
                    )
                policies = [
                    keyword.value
                    for keyword in call.keywords
                    if keyword.arg == "policy"
                ]
                if len(policies) > 1 or (
                    policies
                    and (
                        not isinstance(policies[0], ast.Constant)
                        or policies[0].value not in {"round_robin", "priority"}
                    )
                ):
                    raise QueueFrontendError(
                        "ACPY-QUEUE-008: merge policy must be round_robin or priority"
                    )
                policy = policies[0].value if policies else "round_robin"
                depth = _positive_int(call, "depth", 1)
                latency = _positive_int(call, "latency", 1)
                output = QueueBinding(
                    name,
                    payload,
                    depth,
                    latency,
                    None,
                    scope=scope_path,
                    order=current_order,
                    merge_output=True,
                )
                queues.append(output)
                by_name[name] = output
                merges.append(
                    MergeBinding(
                        inputs,
                        name,
                        policy,
                        depth,
                        latency,
                        scope_path,
                        current_order,
                    )
                )
                continue
            if isinstance(statement, ast.Assign) and len(statement.targets) == 1 and isinstance(statement.targets[0], ast.Name) and isinstance(statement.value, ast.Call):
                name, call = statement.targets[0].id, statement.value
                if name in by_name or name in collections:
                    raise QueueFrontendError("ACPY-QUEUE-001: queue assignment requires one fresh name")
                if call_name(call) == "source" and len(call.args) == 1:
                    binding = source_binding(name, call, scope_path, current_order)
                elif isinstance(call.func, ast.Attribute) and call.func.attr == "apply" and isinstance(call.func.value, ast.Name) and len(call.args) == 1:
                    input_name = call.func.value.id
                    incoming = by_name.get(input_name)
                    if incoming is None:
                        raise QueueFrontendError(f"ACPY-QUEUE-001: input queue {input_name!r} is unbound")
                    argument, expression = _lambda(call.args[0])
                    binding = QueueBinding(
                        name,
                        incoming.payload,
                        _positive_int(call, "depth", 1),
                        _positive_int(call, "latency", 1),
                        incoming.name,
                        argument,
                        expression,
                        scope_path,
                        current_order,
                        atomic_group=atomic_group,
                    )
                else:
                    raise QueueFrontendError("ACPY-QUEUE-001: unsupported queue-producing call")
                queues.append(binding)
                by_name[name] = binding
                continue
            if (
                isinstance(statement, ast.Assign)
                and len(statement.targets) == 1
                and isinstance(statement.targets[0], (ast.Tuple, ast.List))
                and all(isinstance(item, ast.Name) for item in statement.targets[0].elts)
                and isinstance(statement.value, ast.Call)
                and isinstance(statement.value.func, ast.Attribute)
                and statement.value.func.attr == "route"
                and isinstance(statement.value.func.value, ast.Name)
            ):
                call = statement.value
                input_name = call.func.value.id
                incoming = by_name.get(input_name)
                if incoming is None:
                    raise QueueFrontendError(f"ACPY-QUEUE-001: input queue {input_name!r} is unbound")
                output_count = _positive_int(call, "outputs", 0)
                names = tuple(item.id for item in statement.targets[0].elts)
                if output_count != len(names) or len(set(names)) != len(names):
                    raise QueueFrontendError("ACPY-QUEUE-006: route outputs must match fresh tuple names")
                key = [keyword.value for keyword in call.keywords if keyword.arg == "key"]
                if len(key) != 1:
                    raise QueueFrontendError("ACPY-QUEUE-006: route requires one key lambda")
                argument, selector = _lambda(key[0])
                depth = _positive_int(call, "depth", 1)
                latency = _positive_int(call, "latency", 1)
                for name in names:
                    if name in by_name:
                        raise QueueFrontendError("ACPY-QUEUE-006: route output name is already bound")
                    output = QueueBinding(name, incoming.payload, depth, latency, None, scope=scope_path, order=current_order, route_output=True)
                    queues.append(output)
                    by_name[name] = output
                routes.append(RouteBinding(incoming.name, names, argument, selector, depth, latency, scope_path, current_order))
                continue
            if (
                isinstance(statement, ast.Assign)
                and len(statement.targets) == 1
                and isinstance(statement.targets[0], (ast.Tuple, ast.List))
                and all(isinstance(item, ast.Name) for item in statement.targets[0].elts)
                and isinstance(statement.value, ast.Call)
                and isinstance(statement.value.func, ast.Attribute)
                and statement.value.func.attr == "fork"
                and isinstance(statement.value.func.value, ast.Name)
                and not statement.value.args
            ):
                call = statement.value
                incoming = by_name.get(call.func.value.id)
                if incoming is None:
                    raise QueueFrontendError("ACPY-QUEUE-012: fork input is unbound")
                output_count = _positive_int(call, "outputs", 0)
                names = tuple(item.id for item in statement.targets[0].elts)
                if output_count != len(names) or len(names) < 2:
                    raise QueueFrontendError(
                        "ACPY-QUEUE-012: fork outputs must match tuple arity"
                    )
                depth = _positive_int(call, "depth", 1)
                latency = _positive_int(call, "latency", 1)
                for name in names:
                    if name in by_name:
                        raise QueueFrontendError(
                            "ACPY-QUEUE-012: fork output name is already bound"
                        )
                    output = QueueBinding(
                        name,
                        incoming.payload,
                        depth,
                        latency,
                        None,
                        scope=scope_path,
                        order=current_order,
                        route_output=True,
                    )
                    queues.append(output)
                    by_name[name] = output
                forks.append(
                    ForkBinding(
                        incoming.name,
                        names,
                        depth,
                        latency,
                        scope_path,
                        current_order,
                    )
                )
                continue
            if (
                isinstance(statement, ast.Expr)
                and isinstance(statement.value, ast.Call)
                and call_name(statement.value) == "observe"
                and len(statement.value.args) == 1
            ):
                name = queue_reference(statement.value.args[0], aliases)
                observations.append(
                    ObservationBinding(
                        name, f"observe_{current_order}", scope_path, current_order
                    )
                )
                continue
            if (
                isinstance(statement, ast.Expr)
                and isinstance(statement.value, ast.Call)
                and call_name(statement.value) == "sink"
                and len(statement.value.args) == 1
            ):
                name = queue_reference(statement.value.args[0], aliases)
                sinks.append(SinkBinding(name, scope_path, current_order))
                continue
            if isinstance(statement, ast.Return) and statement.value is None:
                continue
            raise QueueFrontendError(f"ACPY-QUEUE-001: unsupported statement {type(statement).__name__}")

    visit(function.body, ())
    if not queues or not sinks:
        raise QueueFrontendError("ACPY-QUEUE-001: a queue system requires source and sink boundaries")
    return QueueProgram(
        system,
        payloads,
        tuple(queues),
        tuple(scopes),
        tuple(routes),
        tuple(forks),
        tuple(feedbacks),
        tuple(merges),
        tuple(atomics),
        tuple(collection_bindings),
        tuple(observations),
        tuple(sinks),
    )


class _ExpressionEmitter:
    def __init__(
        self,
        payloads: dict[str, Payload],
        argument: str,
        payload: str,
        *,
        root_name: str = "item",
        prefix: str = "",
    ) -> None:
        self.payloads = payloads
        self.argument = argument
        self.payload = payload
        self.root_name = root_name
        self.prefix = prefix
        self.lines: list[str] = []
        self.index = 0

    def _new(self) -> str:
        name = f"{self.prefix}v{self.index}"
        self.index += 1
        return name

    def emit(self, node: ast.expr, expected: str | None = None) -> tuple[str, str]:
        if isinstance(node, ast.Name) and node.id == self.argument:
            return self.root_name, self.payload
        if isinstance(node, ast.Constant) and type(node.value) in {int, bool}:
            typ = expected or ("i1" if type(node.value) is bool else "i64")
            name = self._new()
            value = "true" if node.value is True else "false" if node.value is False else str(node.value)
            attribute = value if type(node.value) is bool else f"{value} : {typ}"
            self.lines.append(
                f"    %{name} = ac.var.constant {attribute} as !ac.var<{typ}>"
            )
            return name, typ
        if isinstance(node, ast.Attribute):
            record, record_type = self.emit(node.value)
            payload_name = record_type.removeprefix("!ac.struct<@types::@").removesuffix(">")
            definition = self.payloads.get(payload_name)
            field_type = dict(definition.fields).get(node.attr) if definition else None
            if field_type is None:
                raise QueueFrontendError(f"ACPY-QUEUE-003: unknown field {node.attr!r}")
            name = self._new()
            self.lines.append(f'    %{name} = ac.var.get %{record} field "{node.attr}" : !ac.var<{record_type}> -> !ac.var<{field_type}>')
            return name, field_type
        if isinstance(node, ast.BinOp) and isinstance(node.op, (ast.Add, ast.Sub, ast.Mult)):
            left, left_type = self.emit(node.left)
            right, right_type = self.emit(node.right, left_type)
            if left_type != right_type:
                raise QueueFrontendError("ACPY-QUEUE-003: arithmetic operands must match")
            opcode = {ast.Add: "add", ast.Sub: "sub", ast.Mult: "mul"}[type(node.op)]
            name = self._new()
            self.lines.append(f"    %{name} = ac.var.{opcode} %{left}, %{right} : !ac.var<{left_type}>")
            return name, left_type
        if isinstance(node, ast.Compare) and len(node.ops) == len(node.comparators) == 1:
            left, left_type = self.emit(node.left)
            right, right_type = self.emit(node.comparators[0], left_type)
            if left_type != right_type:
                raise QueueFrontendError("ACPY-QUEUE-003: comparison operands must match")
            predicates = {
                ast.Eq: "eq",
                ast.NotEq: "ne",
                ast.Lt: "slt",
                ast.LtE: "sle",
                ast.Gt: "sgt",
                ast.GtE: "sge",
            }
            predicate = predicates.get(type(node.ops[0]))
            if predicate is None:
                raise QueueFrontendError("ACPY-QUEUE-003: unsupported comparison")
            name = self._new()
            self.lines.append(
                f'    %{name} = ac.var.cmp "{predicate}" %{left}, %{right} : '
                f"!ac.var<{left_type}> -> !ac.var<i1>"
            )
            return name, "i1"
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr == "with_fields" and not node.args:
            record, record_type = self.emit(node.func.value)
            current = record
            for keyword in node.keywords:
                if keyword.arg is None:
                    raise QueueFrontendError("ACPY-QUEUE-003: field unpacking is forbidden")
                payload_name = record_type.removeprefix("!ac.struct<@types::@").removesuffix(">")
                definition = self.payloads.get(payload_name)
                field_type = dict(definition.fields).get(keyword.arg) if definition else None
                if field_type is None:
                    raise QueueFrontendError(f"ACPY-QUEUE-003: unknown field {keyword.arg!r}")
                value, value_type = self.emit(keyword.value, field_type)
                if value_type != field_type:
                    raise QueueFrontendError("ACPY-QUEUE-003: field update type mismatch")
                name = self._new()
                self.lines.append(f'    %{name} = ac.var.with %{current}, %{value} field "{keyword.arg}" : !ac.var<{record_type}>, !ac.var<{field_type}> -> !ac.var<{record_type}>')
                current = name
            return current, record_type
        raise QueueFrontendError("ACPY-QUEUE-003: unsupported lambda expression")


def lower_queue_program(program: QueueProgram) -> str:
    lines = [
        f'module attributes {{ac.contract_epoch = "0.1", '
        f'ac.system = "{program.system}"}} {{'
    ]
    payloads = {item.name: item for item in program.payloads}
    if program.payloads:
        lines.append("  ac.type_scope @types {")
        for payload in program.payloads:
            fields = ", ".join(f'{{name = "{name}", type = {typ}}}' for name, typ in payload.fields)
            lines.append(f"    ac.struct @{payload.name} fields [{fields}]")
        layouts: list[str] = []
        for payload in program.payloads:
            sizes = [
                max(1, (int(typ.removeprefix("i")) + 7) // 8)
                for _, typ in payload.fields
            ]
            alignment = max(sizes)
            offset = 0
            for size in sizes:
                offset = ((offset + size - 1) // size) * size + size
            total = ((offset + alignment - 1) // alignment) * alignment
            layouts.append(
                f"!ac.struct<@types::@{payload.name}> = "
                f'{{abi_alignment = {alignment} : i64, endianness = "little", '
                f"preferred_alignment = {alignment} : i64, size = {total} : i64}}"
            )
        lines.append("  } {dlti.dl_spec = #dlti.dl_spec<" + ", ".join(layouts) + ">}")
    by_name = {item.name: item for item in program.queues}

    def name_array(names: list[str] | tuple[str, ...]) -> str:
        return "[" + ", ".join(f'"{name}"' for name in names) + "]"
    consumers: dict[str, list[QueueBinding]] = {}
    for queue in program.queues:
        if queue.input_name is not None:
            consumers.setdefault(queue.input_name, []).append(queue)
    fanouts: dict[str, tuple[tuple[str, ...], tuple[QueueBinding, ...]]] = {}

    def common_scope(scopes: list[tuple[str, ...]]) -> tuple[str, ...]:
        common: list[str] = []
        for parts in zip(*scopes, strict=False):
            if len(set(parts)) != 1:
                break
            common.append(parts[0])
        return tuple(common)

    for source_name, group in consumers.items():
        if len(group) < 2:
            continue
        fanouts[source_name] = (
            common_scope([consumer.scope for consumer in group]),
            tuple(group),
        )
    payload_by_queue = {name: queue.payload for name, queue in by_name.items()}
    queue_scope = {name: queue.scope for name, queue in by_name.items()}
    effective_input: dict[str, str] = {}
    for source_name, (fanout_scope, group) in fanouts.items():
        for index, consumer in enumerate(group):
            synthetic = f"{source_name}__fanout{index}"
            effective_input[consumer.name] = synthetic
            payload_by_queue[synthetic] = by_name[source_name].payload
            queue_scope[synthetic] = fanout_scope

    uses: dict[str, list[tuple[str, ...]]] = {
        name: [] for name in payload_by_queue
    }
    for queue in program.queues:
        if queue.input_name is None:
            continue
        selected = effective_input.get(queue.name, queue.input_name)
        uses[selected].append(queue.scope)
    for source_name, (fanout_scope, _) in fanouts.items():
        uses[source_name].append(fanout_scope)
    for sink_binding in program.sinks:
        uses[sink_binding.queue].append(sink_binding.scope)
    for observation in program.observations:
        uses[observation.queue].append(observation.scope)
    for route in program.routes:
        uses[route.input_name].append(route.scope)
    for fork in program.forks:
        uses[fork.input_name].append(fork.scope)
    for feedback in program.feedbacks:
        uses[feedback.input_name].append(feedback.scope)
    for merge in program.merges:
        for input_name in merge.inputs:
            uses[input_name].append(merge.scope)

    def inside(container: tuple[str, ...], candidate: tuple[str, ...]) -> bool:
        return candidate[: len(container)] == container

    def scope_io(path: tuple[str, ...]) -> tuple[list[str], list[str]]:
        inputs = [
            name
            for name, producer_scope in queue_scope.items()
            if not inside(path, producer_scope)
            and any(inside(path, use) for use in uses[name])
        ]
        outputs = [
            name
            for name, producer_scope in queue_scope.items()
            if inside(path, producer_scope)
            and any(not inside(path, use) for use in uses[name])
        ]
        return inputs, outputs

    def emit_queue(
        queue: QueueBinding,
        output_ssa: str,
        mapping: dict[str, str],
        indent: str,
    ) -> None:
        if queue.input_name is None:
            lines.append(
                f"{indent}%{output_ssa} = ac.source depth {queue.depth} "
                f'latency {queue.latency} {{ac.name = "{queue.name}"}} : '
                f"!ac.queue<{queue.payload}>"
            )
            mapping[queue.name] = output_ssa
            return
        assert queue.argument is not None and queue.expression is not None
        emitter = _ExpressionEmitter(payloads, queue.argument, queue.payload)
        result, result_type = emitter.emit(queue.expression)
        if result_type != queue.payload:
            raise QueueFrontendError(
                "ACPY-QUEUE-003: lambda result must preserve Queue payload type"
            )
        input_name = effective_input.get(queue.name, queue.input_name)
        input_ssa = mapping[input_name]
        lines.append(
            f"{indent}%{output_ssa} = ac.transform %{input_ssa} "
            f"depths [{queue.depth}] latencies [{queue.latency}] {{"
        )
        lines.append(f"{indent}^transform(%item: !ac.var<{queue.payload}>):")
        lines.extend(indent + line[2:] for line in emitter.lines)
        lines.append(
            f"{indent}  ac.transform.yield %{result} : !ac.var<{queue.payload}>"
        )
        lines.append(
            f'{indent}}} {{ac.name = "{queue.name}"}} : '
            f"(!ac.queue<{queue.payload}>) -> "
            f"!ac.queue<{queue.payload}>"
        )
        mapping[queue.name] = output_ssa

    def render_items(
        path: tuple[str, ...], mapping: dict[str, str], indent: str
    ) -> None:
        def visible_order(consumer: QueueBinding) -> int:
            if consumer.scope == path:
                return consumer.order
            child_path = (*path, consumer.scope[len(path)])
            return next(scope.order for scope in program.scopes if scope.path == child_path)

        events: list[tuple[float, str, object]] = []
        events.extend(
            (queue.order, "queue", queue)
            for queue in program.queues
            if queue.scope == path
            and not queue.route_output
            and not queue.feedback_output
            and not queue.merge_output
            and queue.atomic_group is None
        )
        events.extend(
            (atomic.order, "atomic", atomic)
            for atomic in program.atomics
            if atomic.scope == path
        )
        events.extend(
            (fork.order, "fork", fork)
            for fork in program.forks
            if fork.scope == path
        )
        events.extend(
            (route.order, "route", route)
            for route in program.routes
            if route.scope == path
        )
        events.extend(
            (merge.order, "merge", merge)
            for merge in program.merges
            if merge.scope == path
        )
        events.extend(
            (feedback.order, "feedback", feedback)
            for feedback in program.feedbacks
            if feedback.scope == path
        )
        events.extend(
            (min(visible_order(consumer) for consumer in group) - 0.5, "broadcast", source)
            for source, (fanout_scope, group) in fanouts.items()
            if fanout_scope == path
        )
        events.extend(
            (scope.order, "scope", scope)
            for scope in program.scopes
            if scope.path[:-1] == path
        )
        events.extend(
            (observation.order, "observe", observation)
            for observation in program.observations
            if observation.scope == path
        )
        events.extend(
            (sink_binding.order, "sink", sink_binding)
            for sink_binding in program.sinks
            if sink_binding.scope == path
        )
        for _, kind, item in sorted(events, key=lambda event: event[0]):
            if kind == "queue":
                queue = item
                assert isinstance(queue, QueueBinding)
                output = queue.name if not path else f"{queue.name}__local"
                emit_queue(queue, output, mapping, indent)
            elif kind == "scope":
                scope = item
                assert isinstance(scope, ScopeBinding)
                render_scope(scope, mapping, indent)
            elif kind == "broadcast":
                source = item
                assert isinstance(source, str)
                _, group = fanouts[source]
                outputs = [f"{source}__fanout{index}" for index in range(len(group))]
                lhs = ", ".join(f"%{name}" for name in outputs)
                depths = ", ".join("1" for _ in outputs)
                payload = payload_by_queue[source]
                output_types = ", ".join(f"!ac.queue<{payload}>" for _ in outputs)
                lines.append(
                    f"{indent}{lhs} = ac.broadcast %{mapping[source]} depths "
                    f"[{depths}] latencies [{depths}] "
                    f"{{ac.output_names = {name_array(outputs)}}} : "
                    f"!ac.queue<{payload}> -> "
                    f"({output_types})"
                )
                for consumer, output in zip(group, outputs, strict=True):
                    mapping[effective_input[consumer.name]] = output
            elif kind == "route":
                route = item
                assert isinstance(route, RouteBinding)
                incoming = by_name[route.input_name]
                emitter = _ExpressionEmitter(payloads, route.argument, incoming.payload)
                selector, selector_type = emitter.emit(route.selector)
                output_names = [
                    name if not path else f"{name}__local" for name in route.outputs
                ]
                lhs = ", ".join(f"%{name}" for name in output_names)
                depths = ", ".join(str(route.depth) for _ in output_names)
                latencies = ", ".join(str(route.latency) for _ in output_names)
                output_types = ", ".join(
                    f"!ac.queue<{incoming.payload}>" for _ in output_names
                )
                lines.append(
                    f"{indent}{lhs} = ac.route %{mapping[route.input_name]} "
                    f"depths [{depths}] latencies [{latencies}] {{"
                )
                lines.append(
                    f"{indent}^selector(%item: !ac.var<{incoming.payload}>):"
                )
                lines.extend(indent + line[2:] for line in emitter.lines)
                lines.append(
                    f"{indent}  ac.route.yield %{selector} : !ac.var<{selector_type}>"
                )
                lines.append(
                    f"{indent}}} "
                    f"{{ac.output_names = {name_array(route.outputs)}}} : "
                    f"!ac.queue<{incoming.payload}> -> ({output_types})"
                )
                for name, output in zip(route.outputs, output_names, strict=True):
                    mapping[name] = output
            elif kind == "fork":
                fork = item
                assert isinstance(fork, ForkBinding)
                incoming = by_name[fork.input_name]
                output_names = [
                    name if not path else f"{name}__local" for name in fork.outputs
                ]
                lhs = ", ".join(f"%{name}" for name in output_names)
                depths = ", ".join(str(fork.depth) for _ in output_names)
                latencies = ", ".join(str(fork.latency) for _ in output_names)
                output_types = ", ".join(
                    f"!ac.queue<{incoming.payload}>" for _ in output_names
                )
                lines.append(
                    f"{indent}{lhs} = ac.fork %{mapping[fork.input_name]} "
                    f"depths [{depths}] latencies [{latencies}] "
                    f"{{ac.output_names = {name_array(fork.outputs)}}} : "
                    f"!ac.queue<{incoming.payload}> -> ({output_types})"
                )
                for name, output in zip(fork.outputs, output_names, strict=True):
                    mapping[name] = output
            elif kind == "feedback":
                feedback = item
                assert isinstance(feedback, FeedbackBinding)
                incoming = by_name[feedback.input_name]
                emitter = _ExpressionEmitter(
                    payloads, feedback.argument, incoming.payload
                )
                condition, condition_type = emitter.emit(feedback.condition)
                update, update_type = emitter.emit(feedback.update)
                if condition_type != "i1" or update_type != incoming.payload:
                    raise QueueFrontendError(
                        "ACPY-QUEUE-007: while condition must be bool and update "
                        "must preserve Queue payload"
                    )
                output = (
                    feedback.output_name
                    if not path
                    else f"{feedback.output_name}__local"
                )
                lines.append(
                    f"{indent}%{output} = ac.feedback %{mapping[feedback.input_name]} "
                    f"depth {feedback.depth} latency {feedback.latency} "
                    f"max_iterations {feedback.max_iterations} {{"
                )
                lines.append(
                    f"{indent}^body(%item: !ac.var<{incoming.payload}>):"
                )
                lines.extend(indent + line[2:] for line in emitter.lines)
                lines.append(
                    f"{indent}  ac.feedback.yield %{update} continue %{condition} : "
                    f"!ac.var<{incoming.payload}>, !ac.var<i1>"
                )
                lines.append(
                    f'{indent}}} {{ac.name = "{feedback.output_name}"}} : '
                    f"!ac.queue<{incoming.payload}> -> "
                    f"!ac.queue<{incoming.payload}>"
                )
                mapping[feedback.output_name] = output
            elif kind == "atomic":
                atomic = item
                assert isinstance(atomic, AtomicBinding)
                members = [by_name[name] for name in atomic.queues]
                inputs = [
                    effective_input.get(queue.name, queue.input_name)
                    for queue in members
                ]
                if any(input_name is None for input_name in inputs):
                    raise QueueFrontendError(
                        "ACPY-QUEUE-009: atomic input resolution failed"
                    )
                input_names = [str(input_name) for input_name in inputs]
                outputs = [
                    queue.name if not path else f"{queue.name}__local"
                    for queue in members
                ]
                lhs = ", ".join(f"%{name}" for name in outputs)
                operands = ", ".join(
                    f"%{mapping[input_name]}" for input_name in input_names
                )
                depths = ", ".join(str(queue.depth) for queue in members)
                latencies = ", ".join(str(queue.latency) for queue in members)
                input_types = ", ".join(
                    f"!ac.queue<{payload_by_queue[input_name]}>"
                    for input_name in input_names
                )
                output_types = ", ".join(
                    f"!ac.queue<{queue.payload}>" for queue in members
                )
                lines.append(
                    f"{indent}{lhs} = ac.transform {operands} depths [{depths}] "
                    f"latencies [{latencies}] {{"
                )
                arguments = ", ".join(
                    f"%item{index}: !ac.var<{payload_by_queue[input_name]}>"
                    for index, input_name in enumerate(input_names)
                )
                lines.append(f"{indent}^transform({arguments}):")
                yielded: list[str] = []
                for index, queue in enumerate(members):
                    assert queue.argument is not None and queue.expression is not None
                    emitter = _ExpressionEmitter(
                        payloads,
                        queue.argument,
                        payload_by_queue[input_names[index]],
                        root_name=f"item{index}",
                        prefix=f"a{index}_",
                    )
                    result, result_type = emitter.emit(queue.expression)
                    if result_type != queue.payload:
                        raise QueueFrontendError(
                            "ACPY-QUEUE-009: atomic lambda result type mismatch"
                        )
                    lines.extend(indent + line[2:] for line in emitter.lines)
                    yielded.append(f"%{result}")
                lines.append(
                    f"{indent}  ac.transform.yield {', '.join(yielded)} : "
                    + ", ".join(f"!ac.var<{queue.payload}>" for queue in members)
                )
                names_attr = name_array(tuple(queue.name for queue in members))
                lines.append(
                    f"{indent}}} {{ac.output_names = {names_attr}}} : "
                    f"({input_types}) -> ({output_types})"
                )
                for queue, output in zip(members, outputs, strict=True):
                    mapping[queue.name] = output
            elif kind == "merge":
                merge = item
                assert isinstance(merge, MergeBinding)
                output = merge.output if not path else f"{merge.output}__local"
                operands = ", ".join(f"%{mapping[name]}" for name in merge.inputs)
                input_types = ", ".join(
                    f"!ac.queue<{by_name[name].payload}>" for name in merge.inputs
                )
                payload = by_name[merge.output].payload
                lines.append(
                    f'{indent}%{output} = ac.merge {operands} policy "{merge.policy}" '
                    f"depth {merge.depth} latency {merge.latency} "
                    f'{{ac.name = "{merge.output}"}} : '
                    f"({input_types}) -> !ac.queue<{payload}>"
                )
                mapping[merge.output] = output
            elif kind == "observe":
                observation = item
                assert isinstance(observation, ObservationBinding)
                queue = by_name[observation.queue]
                lines.append(
                    f"{indent}ac.observe %{mapping[observation.queue]} name "
                    f'"{observation.name}" : !ac.queue<{queue.payload}>'
                )
            else:
                sink_binding = item
                assert isinstance(sink_binding, SinkBinding)
                queue = by_name[sink_binding.queue]
                lines.append(
                    f"{indent}ac.sink %{mapping[sink_binding.queue]} "
                    f'{{ac.name = "sink_{sink_binding.order}"}} : '
                    f"!ac.queue<{queue.payload}>"
                )

    def render_scope(
        scope: ScopeBinding, parent_mapping: dict[str, str], indent: str
    ) -> None:
        inputs, outputs = scope_io(scope.path)
        result_names = [
            name if len(scope.path) == 1 else f"{name}__inner" for name in outputs
        ]
        lhs = "" if not result_names else ", ".join(f"%{name}" for name in result_names) + " = "
        operands = ", ".join(f"%{parent_mapping[name]}" for name in inputs)
        input_types = ", ".join(f"!ac.queue<{payload_by_queue[name]}>" for name in inputs)
        output_types = ", ".join(f"!ac.queue<{payload_by_queue[name]}>" for name in outputs)
        lines.append(f"{indent}{lhs}ac.scope @{scope.name}({operands}) {{")
        local_mapping = dict(parent_mapping)
        if inputs:
            args = ", ".join(
                f"%{name}__in: !ac.queue<{payload_by_queue[name]}>" for name in inputs
            )
            lines.append(f"{indent}^body({args}):")
            for name in inputs:
                local_mapping[name] = f"{name}__in"
        else:
            lines.append(f"{indent}^body:")
        render_items(scope.path, local_mapping, indent + "  ")
        yielded = ", ".join(f"%{local_mapping[name]}" for name in outputs)
        yield_types = ", ".join(f"!ac.queue<{payload_by_queue[name]}>" for name in outputs)
        lines.append(
            f"{indent}  ac.scope.yield"
            + (f" {yielded} : {yield_types}" if outputs else "")
        )
        result_signature = output_types if len(outputs) == 1 else f"({output_types})"
        lines.append(f"{indent}}} : ({input_types}) -> {result_signature}")
        for name, result in zip(outputs, result_names, strict=True):
            parent_mapping[name] = result

    render_items((), {}, "  ")
    lines.append("}")
    return "\n".join(lines) + "\n"


def lower_queue_source(text: str, system: str) -> str:
    return lower_queue_program(parse_queue_program(text, system))
