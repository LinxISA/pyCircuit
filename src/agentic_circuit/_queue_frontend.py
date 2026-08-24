"""v0.2 serial-Python to Queue/Var ACIR construction."""

from __future__ import annotations

import ast
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


@dataclass(frozen=True, slots=True)
class QueueProgram:
    system: str
    payloads: tuple[Payload, ...]
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


def _scalar_type(node: ast.expr) -> str:
    if isinstance(node, ast.Name) and node.id == "int":
        return "i64"
    if isinstance(node, ast.Name) and node.id == "bool":
        return "i1"
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


def _positive_int(call: ast.Call, name: str, default: int) -> int:
    matches = [keyword for keyword in call.keywords if keyword.arg == name]
    if len(matches) > 1:
        raise QueueFrontendError(f"ACPY-QUEUE-001: repeated {name!r}")
    if not matches:
        return default
    value = matches[0].value
    if not isinstance(value, ast.Constant) or type(value.value) is not int:
        raise QueueFrontendError(f"ACPY-QUEUE-001: {name} must be a compile-time integer")
    if value.value <= 0:
        raise QueueFrontendError(f"ACPY-QUEUE-001: {name} must be positive")
    return value.value


def _payload(node: ast.expr, payloads: dict[str, Payload]) -> str:
    if isinstance(node, ast.Name) and node.id in {"int", "bool"}:
        return _scalar_type(node)
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
    sinks: list[str] = []
    by_name: dict[str, QueueBinding] = {}
    for statement in function.body:
        if isinstance(statement, ast.Assign) and len(statement.targets) == 1 and isinstance(statement.targets[0], ast.Name) and isinstance(statement.value, ast.Call):
            name, call = statement.targets[0].id, statement.value
            if name in by_name:
                raise QueueFrontendError("ACPY-QUEUE-001: queue assignment requires one fresh name")
            if isinstance(call.func, ast.Name) and call.func.id == "source" and len(call.args) == 1:
                binding = QueueBinding(name, _payload(call.args[0], payload_map), _positive_int(call, "depth", 1), _positive_int(call, "latency", 1), None)
            elif isinstance(call.func, ast.Attribute) and call.func.attr == "apply" and isinstance(call.func.value, ast.Name) and len(call.args) == 1:
                input_name = call.func.value.id
                incoming = by_name.get(input_name)
                if incoming is None:
                    raise QueueFrontendError(f"ACPY-QUEUE-001: input queue {input_name!r} is unbound")
                argument, expression = _lambda(call.args[0])
                binding = QueueBinding(name, incoming.payload, _positive_int(call, "depth", 1), _positive_int(call, "latency", 1), input_name, argument, expression)
            else:
                raise QueueFrontendError("ACPY-QUEUE-001: unsupported queue-producing call")
            queues.append(binding)
            by_name[name] = binding
            continue
        if isinstance(statement, ast.Expr) and isinstance(statement.value, ast.Call) and isinstance(statement.value.func, ast.Name) and statement.value.func.id == "sink" and len(statement.value.args) == 1 and isinstance(statement.value.args[0], ast.Name):
            name = statement.value.args[0].id
            if name not in by_name:
                raise QueueFrontendError(f"ACPY-QUEUE-001: sink queue {name!r} is unbound")
            sinks.append(name)
            continue
        if isinstance(statement, ast.Return) and statement.value is None:
            continue
        raise QueueFrontendError(f"ACPY-QUEUE-001: unsupported statement {type(statement).__name__}")
    if not queues or not sinks:
        raise QueueFrontendError("ACPY-QUEUE-001: a queue system requires source and sink boundaries")
    return QueueProgram(system, payloads, tuple(queues), tuple(sinks))


class _ExpressionEmitter:
    def __init__(self, payloads: dict[str, Payload], argument: str, payload: str) -> None:
        self.payloads = payloads
        self.argument = argument
        self.payload = payload
        self.lines: list[str] = []
        self.index = 0

    def _new(self) -> str:
        name = f"v{self.index}"
        self.index += 1
        return name

    def emit(self, node: ast.expr, expected: str | None = None) -> tuple[str, str]:
        if isinstance(node, ast.Name) and node.id == self.argument:
            return "item", self.payload
        if isinstance(node, ast.Constant) and type(node.value) in {int, bool}:
            typ = expected or ("i1" if type(node.value) is bool else "i64")
            name = self._new()
            value = "true" if node.value is True else "false" if node.value is False else str(node.value)
            self.lines.append(f"    %{name} = ac.var.constant {value} : {typ} as !ac.var<{typ}>")
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
    lines = ['module attributes {ac.contract_epoch = "0.1"} {']
    payloads = {item.name: item for item in program.payloads}
    if program.payloads:
        lines.append("  ac.type_scope @types {")
        for payload in program.payloads:
            fields = ", ".join(f'{{name = "{name}", type = {typ}}}' for name, typ in payload.fields)
            lines.append(f"    ac.struct @{payload.name} fields [{fields}]")
        layouts: list[str] = []
        for payload in program.payloads:
            sizes = [8 if typ == "i64" else 1 for _, typ in payload.fields]
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
    for queue in program.queues:
        if queue.input_name is None:
            lines.append(f"  %{queue.name} = ac.source depth {queue.depth} latency {queue.latency} : !ac.queue<{queue.payload}>")
            continue
        assert queue.argument is not None and queue.expression is not None
        emitter = _ExpressionEmitter(payloads, queue.argument, queue.payload)
        result, result_type = emitter.emit(queue.expression)
        if result_type != queue.payload:
            raise QueueFrontendError("ACPY-QUEUE-003: lambda result must preserve Queue payload type")
        lines.append(f"  %{queue.name} = ac.transform %{queue.input_name} depths [{queue.depth}] latencies [{queue.latency}] {{")
        lines.append(f"  ^body(%item: !ac.var<{queue.payload}>):")
        lines.extend(emitter.lines)
        lines.append(f"    ac.transform.yield %{result} : !ac.var<{queue.payload}>")
        lines.append(f"  }} : (!ac.queue<{queue.payload}>) -> !ac.queue<{queue.payload}>")
    by_name = {item.name: item for item in program.queues}
    for name in program.sinks:
        lines.append(f"  ac.sink %{name} : !ac.queue<{by_name[name].payload}>")
    lines.append("}")
    return "\n".join(lines) + "\n"


def lower_queue_source(text: str, system: str) -> str:
    return lower_queue_program(parse_queue_program(text, system))
