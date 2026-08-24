"""v0.2 serial-Python to Queue/Var ACIR construction."""

from __future__ import annotations

import ast
from dataclasses import dataclass


class QueueFrontendError(ValueError):
    """A stable rejection from the v0.2 queue frontend."""


@dataclass(frozen=True, slots=True)
class QueueBinding:
    name: str
    payload: str
    depth: int
    latency: int
    input_name: str | None


@dataclass(frozen=True, slots=True)
class QueueProgram:
    system: str
    queues: tuple[QueueBinding, ...]
    sinks: tuple[str, ...]


def _decorator_name(node: ast.expr) -> str:
    if isinstance(node, ast.Call):
        return _decorator_name(node.func)
    if isinstance(node, ast.Name):
        return node.id
    if isinstance(node, ast.Attribute):
        prefix = _decorator_name(node.value)
        return f"{prefix}.{node.attr}" if prefix else node.attr
    return ""


def _positive_int(call: ast.Call, name: str, default: int) -> int:
    matches = [keyword for keyword in call.keywords if keyword.arg == name]
    if len(matches) > 1:
        raise QueueFrontendError(f"ACPY-QUEUE-001: repeated {name!r}")
    if not matches:
        return default
    value = matches[0].value
    if not isinstance(value, ast.Constant) or type(value.value) is not int:
        raise QueueFrontendError(
            f"ACPY-QUEUE-001: {name} must be a compile-time integer"
        )
    if value.value <= 0:
        raise QueueFrontendError(f"ACPY-QUEUE-001: {name} must be positive")
    return value.value


def _payload(node: ast.expr) -> str:
    if isinstance(node, ast.Name) and node.id == "int":
        return "i64"
    if isinstance(node, ast.Name) and node.id == "bool":
        return "i1"
    raise QueueFrontendError(
        "ACPY-QUEUE-002: source payload must be a compile-time supported type"
    )


def _identity_lambda(node: ast.expr) -> None:
    if not isinstance(node, ast.Lambda) or len(node.args.args) != 1:
        raise QueueFrontendError(
            "ACPY-QUEUE-003: apply requires a one-argument lambda"
        )
    argument = node.args.args[0].arg
    if not isinstance(node.body, ast.Name) or node.body.id != argument:
        raise QueueFrontendError(
            "ACPY-QUEUE-003: this checkpoint supports an identity lambda"
        )


def parse_queue_program(text: str, system: str) -> QueueProgram:
    tree = ast.parse(text, filename="<queue-model>", type_comments=True)
    candidates = [
        node
        for node in tree.body
        if isinstance(node, ast.FunctionDef)
        and node.name == system
        and any(
            _decorator_name(decorator).rsplit(".", 1)[-1] == "system"
            for decorator in node.decorator_list
        )
    ]
    if len(candidates) != 1:
        raise QueueFrontendError(
            f"ACPY-QUEUE-001: system {system!r} is missing or ambiguous"
        )
    function = candidates[0]
    if function.args.args or function.args.posonlyargs or function.args.kwonlyargs:
        raise QueueFrontendError(
            "ACPY-QUEUE-001: a queue system infers boundaries and takes no parameters"
        )

    queues: list[QueueBinding] = []
    sinks: list[str] = []
    by_name: dict[str, QueueBinding] = {}
    for statement in function.body:
        if isinstance(statement, ast.Assign) and len(statement.targets) == 1:
            target = statement.targets[0]
            if not isinstance(target, ast.Name) or target.id in by_name:
                raise QueueFrontendError(
                    "ACPY-QUEUE-001: queue assignment requires one fresh name"
                )
            if not isinstance(statement.value, ast.Call):
                raise QueueFrontendError(
                    "ACPY-QUEUE-001: queue assignment requires a call"
                )
            call = statement.value
            if isinstance(call.func, ast.Name) and call.func.id == "source":
                if len(call.args) != 1:
                    raise QueueFrontendError(
                        "ACPY-QUEUE-002: source requires one payload type"
                    )
                binding = QueueBinding(
                    target.id,
                    _payload(call.args[0]),
                    _positive_int(call, "depth", 1),
                    _positive_int(call, "latency", 1),
                    None,
                )
            elif (
                isinstance(call.func, ast.Attribute)
                and call.func.attr == "apply"
                and isinstance(call.func.value, ast.Name)
            ):
                input_name = call.func.value.id
                input_binding = by_name.get(input_name)
                if input_binding is None:
                    raise QueueFrontendError(
                        f"ACPY-QUEUE-001: input queue {input_name!r} is unbound"
                    )
                if len(call.args) != 1:
                    raise QueueFrontendError(
                        "ACPY-QUEUE-003: apply requires one lambda"
                    )
                _identity_lambda(call.args[0])
                binding = QueueBinding(
                    target.id,
                    input_binding.payload,
                    _positive_int(call, "depth", 1),
                    _positive_int(call, "latency", 1),
                    input_name,
                )
            else:
                raise QueueFrontendError(
                    "ACPY-QUEUE-001: unsupported queue-producing call"
                )
            queues.append(binding)
            by_name[binding.name] = binding
            continue
        if isinstance(statement, ast.Expr) and isinstance(statement.value, ast.Call):
            call = statement.value
            if isinstance(call.func, ast.Name) and call.func.id == "sink":
                if len(call.args) != 1 or not isinstance(call.args[0], ast.Name):
                    raise QueueFrontendError(
                        "ACPY-QUEUE-001: sink requires one bound queue"
                    )
                name = call.args[0].id
                if name not in by_name:
                    raise QueueFrontendError(
                        f"ACPY-QUEUE-001: sink queue {name!r} is unbound"
                    )
                sinks.append(name)
                continue
        if isinstance(statement, ast.Return) and (
            statement.value is None
            or (isinstance(statement.value, ast.Constant) and statement.value.value is None)
        ):
            continue
        raise QueueFrontendError(
            f"ACPY-QUEUE-001: unsupported statement {type(statement).__name__}"
        )
    if not queues or not sinks:
        raise QueueFrontendError(
            "ACPY-QUEUE-001: a queue system requires source and sink boundaries"
        )
    return QueueProgram(system, tuple(queues), tuple(sinks))


def lower_queue_program(program: QueueProgram) -> str:
    lines = ['module attributes {ac.contract_epoch = "0.1"} {']
    for queue in program.queues:
        if queue.input_name is None:
            lines.append(
                f"  %{queue.name} = ac.source depth {queue.depth} "
                f"latency {queue.latency} : !ac.queue<{queue.payload}>"
            )
            continue
        lines.extend(
            (
                f"  %{queue.name} = ac.transform %{queue.input_name} "
                f"depths [{queue.depth}] latencies [{queue.latency}] {{",
                f"  ^body(%item: !ac.var<{queue.payload}>):",
                f"    ac.transform.yield %item : !ac.var<{queue.payload}>",
                f"  }} : (!ac.queue<{queue.payload}>) -> !ac.queue<{queue.payload}>",
            )
        )
    for name in program.sinks:
        payload = next(queue.payload for queue in program.queues if queue.name == name)
        lines.append(f"  ac.sink %{name} : !ac.queue<{payload}>")
    lines.append("}")
    return "\n".join(lines) + "\n"


def lower_queue_source(text: str, system: str) -> str:
    return lower_queue_program(parse_queue_program(text, system))
