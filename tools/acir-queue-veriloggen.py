#!/usr/bin/env python3
"""Generate self-contained Verilog from a frozen Queue ACIR module.

This is the first in-tree PYC compatibility backend.  It deliberately consumes
the canonical textual PYC emitted by ``acir-queue-pycgen`` instead of adding a
second ACIR lowering.  The small emitter covers the PYC operations used by the
golden primitive slice (FIFO, register, combinational wiring, popcount, and
round-robin arbitration) and embeds the migrated PYC runtime modules in the
resulting Verilog file.
"""

from __future__ import annotations

import argparse
import ast
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


class PYCVerilogError(ValueError):
    pass


@dataclass(frozen=True)
class Value:
    name: str
    type: str

    @property
    def net(self) -> str:
        return self.name.lstrip("%").replace(".", "_")

    @property
    def width(self) -> int:
        match = re.fullmatch(r"i(\d+)", self.type)
        if not match:
            raise PYCVerilogError(f"expected integer PYC type, got {self.type}")
        return int(match.group(1))


@dataclass
class Module:
    name: str
    args: list[Value]
    results: list[Value]
    body: list[str]


def _split_csv(text: str) -> list[str]:
    """Split a comma list while ignoring commas inside brackets/quotes."""
    result: list[str] = []
    start = 0
    depth = 0
    quote = False
    escaped = False
    for index, char in enumerate(text):
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quote = False
            continue
        if char == '"':
            quote = True
        elif char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
        elif char == "," and depth == 0:
            result.append(text[start:index].strip())
            start = index + 1
    tail = text[start:].strip()
    if tail:
        result.append(tail)
    return result


def _parse_types(text: str) -> list[str]:
    return [part.strip() for part in _split_csv(text) if part.strip()]


def _parse_names_attr(header: str, key: str) -> list[str]:
    match = re.search(rf"{re.escape(key)}\s*=\s*(\[[^]]*\])", header)
    if not match:
        raise PYCVerilogError(f"function header is missing {key}")
    value = ast.literal_eval(match.group(1))
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise PYCVerilogError(f"{key} must be a string list")
    return value


def parse_pyc_module(text: str) -> Module:
    func_match = re.search(
        r"^\s*func\.func\s+@(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\((?P<args>.*?)\)\s*"
        r"->\s*\((?P<results>.*?)\)\s*attributes\s*(?P<attrs>\{.*\})\s*\{\s*$",
        text,
        re.MULTILINE | re.DOTALL,
    )
    if not func_match:
        raise PYCVerilogError("canonical PYC module does not contain a supported func.func")

    arg_parts = _split_csv(func_match.group("args"))
    args: list[Value] = []
    for part in arg_parts:
        match = re.fullmatch(r"(%[A-Za-z_][A-Za-z0-9_]*)\s*:\s*(.+)", part.strip())
        if not match:
            raise PYCVerilogError(f"cannot parse function argument: {part}")
        args.append(Value(match.group(1), match.group(2).strip()))

    result_types = _parse_types(func_match.group("results"))
    result_names = _parse_names_attr(func_match.group("attrs"), "result_names")
    if len(result_types) != len(result_names):
        raise PYCVerilogError("result_names and result types have different lengths")
    results = [Value(name, type_) for name, type_ in zip(result_names, result_types)]

    lines = text.splitlines()
    func_line = next(
        index for index, line in enumerate(lines) if line.strip().startswith("func.func @")
    )
    body: list[str] = []
    for line in lines[func_line + 1 :]:
        if line.strip() == "}":
            break
        if line.startswith("    "):
            body.append(line.strip())
    if not body:
        raise PYCVerilogError("PYC function body is empty")
    return Module(func_match.group("name"), args, results, body)


def _ssa_names(text: str) -> list[str]:
    return [part.strip() for part in _split_csv(text) if part.strip()]


def _value(values: dict[str, Value], name: str) -> Value:
    try:
        return values[name]
    except KeyError as exc:
        raise PYCVerilogError(f"unknown PYC SSA value {name}") from exc


def _port_decl(direction: str, value: Value) -> str:
    if value.type in ("!pyc.clock", "!pyc.reset"):
        return f"  {direction} wire {value.net}"
    width = value.width
    suffix = "" if width == 1 else f" [{width - 1}:0]"
    return f"  {direction} wire{suffix} {value.net}"


def _literal(value: str, width: int) -> str:
    try:
        number = int(value, 0)
    except ValueError as exc:
        raise PYCVerilogError(f"unsupported PYC constant {value}") from exc
    return f"{width}'d{number}"


def _parse_attr_int(attrs: str, key: str) -> int:
    match = re.search(rf"\b{re.escape(key)}\s*=\s*(-?\d+)", attrs)
    if not match:
        raise PYCVerilogError(f"PYC op is missing integer attribute {key}")
    return int(match.group(1))


def _runtime_sources(runtime_dir: Path) -> Iterable[str]:
    for name in ("pyc_reg.v", "pyc_fifo.v", "pyc_popcount.v", "pyc_rr_arbiter.v"):
        path = runtime_dir / name
        if not path.is_file():
            raise PYCVerilogError(f"missing in-tree PYC runtime module: {path}")
        yield f"// --- PYC runtime: {path.name}\n{path.read_text(encoding='utf-8')}"


def emit_verilog(module: Module, runtime_dir: Path) -> str:
    values: dict[str, Value] = {value.name: value for value in module.args}
    declarations: list[str] = []
    assigns: list[str] = []
    instances: list[str] = []
    returns: list[str] | None = None
    seen_outputs: set[str] = set()

    def add_value(name: str, type_: str) -> Value:
        value = Value(name, type_)
        if name in values and values[name].type != type_:
            raise PYCVerilogError(f"SSA value {name} changes type")
        values[name] = value
        if name not in {arg.name for arg in module.args}:
            if type_ not in ("!pyc.clock", "!pyc.reset"):
                width = value.width
                suffix = "" if width == 1 else f" [{width - 1}:0]"
                declarations.append(f"  wire{suffix} {value.net};")
        return value

    for line in module.body:
        if line.startswith("func.return "):
            match = re.fullmatch(r"func\.return\s+(.*?)\s*:\s*(.*)", line)
            if not match:
                raise PYCVerilogError(f"cannot parse return: {line}")
            returns = _ssa_names(match.group(1))
            continue
        if line.startswith("pyc.assert "):
            assigns.append(f"  // {line} (verification-only assertion omitted in RTL)")
            continue

        lhs: list[str] = []
        rhs = line
        if " = " in line:
            lhs_text, rhs = line.split(" = ", 1)
            lhs = _ssa_names(lhs_text)
        rhs = rhs.strip()

        if rhs.startswith("pyc.wire : "):
            if len(lhs) != 1:
                raise PYCVerilogError(f"wire expects one result: {line}")
            add_value(lhs[0], rhs[len("pyc.wire : ") :].strip())
            continue

        if rhs.startswith("pyc.constant "):
            match = re.fullmatch(r"pyc\.constant\s+(\S+)\s*:\s*(\S+)", rhs)
            if not match or len(lhs) != 1:
                raise PYCVerilogError(f"cannot parse constant: {line}")
            value = add_value(lhs[0], match.group(2))
            assigns.append(f"  assign {value.net} = {_literal(match.group(1), value.width)};")
            continue

        if rhs.startswith("pyc.fifo "):
            match = re.fullmatch(
                r"pyc\.fifo\s+(.*?)\s*\{(.*?)\}\s*:\s*(\S+)", rhs
            )
            if not match or len(lhs) != 3:
                raise PYCVerilogError(f"cannot parse FIFO: {line}")
            inputs = _ssa_names(match.group(1))
            if len(inputs) != 5:
                raise PYCVerilogError(f"FIFO expects five operands: {line}")
            depth = _parse_attr_int(match.group(2), "depth")
            payload = match.group(3)
            out_values = [add_value(lhs[0], "i1"), add_value(lhs[1], "i1"), add_value(lhs[2], payload)]
            in_valid, in_data, out_ready = (_value(values, inputs[2]), _value(values, inputs[3]), _value(values, inputs[4]))
            clk, rst = _value(values, inputs[0]), _value(values, inputs[1])
            instances.append(
                f"  pyc_fifo #(.WIDTH({out_values[2].width}), .DEPTH({depth})) fifo_{out_values[0].net} (\n"
                f"    .clk({clk.net}), .rst({rst.net}),\n"
                f"    .in_valid({in_valid.net}), .in_ready({out_values[0].net}), .in_data({in_data.net}),\n"
                f"    .out_valid({out_values[1].net}), .out_ready({out_ready.net}), .out_data({out_values[2].net})\n"
                f"  );"
            )
            continue

        if rhs.startswith("pyc.reg "):
            match = re.fullmatch(r"pyc\.reg\s+(.*?)\s*:\s*(\S+)", rhs)
            if not match or len(lhs) != 1:
                raise PYCVerilogError(f"cannot parse register: {line}")
            inputs = _ssa_names(match.group(1))
            if len(inputs) != 5:
                raise PYCVerilogError(f"register expects five operands: {line}")
            q = add_value(lhs[0], match.group(2))
            clk, rst, en, data, init = (_value(values, item) for item in inputs)
            instances.append(
                f"  pyc_reg #(.WIDTH({q.width})) reg_{q.net} (\n"
                f"    .clk({clk.net}), .rst({rst.net}), .en({en.net}),\n"
                f"    .d({data.net}), .init({init.net}), .q({q.net})\n"
                f"  );"
            )
            continue

        if rhs.startswith("pyc.rr_arbiter "):
            match = re.fullmatch(r"pyc\.rr_arbiter\s+(.*?)\s*\{(.*?)\}\s*:\s*(.*?)\s*->\s*(\S+)", rhs)
            if not match or len(lhs) != 1:
                raise PYCVerilogError(f"cannot parse round-robin arbiter: {line}")
            inputs = _ssa_names(match.group(1))
            if len(inputs) != 2:
                raise PYCVerilogError(f"round-robin arbiter expects request and cursor: {line}")
            req, cursor = (_value(values, item) for item in inputs)
            out = add_value(lhs[0], match.group(4))
            num_inputs = _parse_attr_int(match.group(2), "num_inputs")
            if num_inputs != req.width or num_inputs != out.width:
                raise PYCVerilogError("rr_arbiter num_inputs must match request and grant widths")
            instances.append(
                f"  pyc_rr_arbiter #(.NUM_INPUTS({num_inputs}), .POINTER_WIDTH({cursor.width})) rr_arbiter_{out.net} (\n"
                f"    .req({req.net}), .cursor({cursor.net}), .grant({out.net})\n"
                f"  );"
            )
            continue

        if rhs.startswith("pyc.popcount "):
            match = re.fullmatch(r"pyc\.popcount\s+(.*?)\s*\{.*?\}\s*:\s*(\S+)\s*->\s*(\S+)", rhs)
            if not match or len(lhs) != 1:
                raise PYCVerilogError(f"cannot parse popcount: {line}")
            inputs = _ssa_names(match.group(1))
            if len(inputs) != 1:
                raise PYCVerilogError(f"popcount expects one operand: {line}")
            src = _value(values, inputs[0])
            out = add_value(lhs[0], match.group(3))
            instances.append(
                f"  pyc_popcount #(.IN_WIDTH({src.width}), .OUT_WIDTH({out.width})) popcount_{out.net} (\n"
                f"    .in({src.net}), .out({out.net})\n"
                f"  );"
            )
            continue

        if rhs.startswith("pyc.concat("):
            match = re.fullmatch(r"pyc\.concat\((.*?)\)\s*:\s*(.*?)\s*->\s*(\S+)", rhs)
            if not match or len(lhs) != 1:
                raise PYCVerilogError(f"cannot parse concat: {line}")
            inputs = _ssa_names(match.group(1))
            out = add_value(lhs[0], match.group(3))
            for item in inputs:
                _value(values, item)
            assigns.append(f"  assign {out.net} = {{{', '.join(_value(values, item).net for item in inputs)}}};")
            continue

        if rhs.startswith("pyc.extract "):
            match = re.fullmatch(r"pyc\.extract\s+(.*?)\s*\{(.*?)\}\s*:\s*(\S+)\s*->\s*(\S+)", rhs)
            if not match or len(lhs) != 1:
                raise PYCVerilogError(f"cannot parse extract: {line}")
            inputs = _ssa_names(match.group(1))
            if len(inputs) != 1:
                raise PYCVerilogError(f"extract expects one operand: {line}")
            src = _value(values, inputs[0])
            out = add_value(lhs[0], match.group(4))
            lsb = _parse_attr_int(match.group(2), "lsb")
            if lsb < 0 or lsb + out.width > src.width:
                raise PYCVerilogError("extract slice is outside source width")
            if out.width == 1:
                assigns.append(f"  assign {out.net} = {src.net}[{lsb}];")
            else:
                assigns.append(
                    f"  assign {out.net} = {src.net}[{lsb + out.width - 1}:{lsb}];"
                )
            continue

        if rhs.startswith("pyc.mux "):
            match = re.fullmatch(r"pyc\.mux\s+(.*?)\s*:\s*(\S+)", rhs)
            if not match or len(lhs) != 1:
                raise PYCVerilogError(f"cannot parse mux: {line}")
            inputs = _ssa_names(match.group(1))
            if len(inputs) != 3:
                raise PYCVerilogError(f"mux expects select, true, false: {line}")
            select, true_value, false_value = (_value(values, item) for item in inputs)
            out = add_value(lhs[0], match.group(2))
            assigns.append(f"  assign {out.net} = {select.net} ? {true_value.net} : {false_value.net};")
            continue

        if rhs.startswith("pyc.assign "):
            match = re.fullmatch(r"pyc\.assign\s+(.*?),\s*(%[A-Za-z_][A-Za-z0-9_]*)\s*:\s*(\S+)", rhs)
            if not match:
                raise PYCVerilogError(f"cannot parse assign: {line}")
            target = _value(values, match.group(1).strip())
            source = _value(values, match.group(2))
            assigns.append(f"  assign {target.net} = {source.net};")
            continue

        binary = re.fullmatch(r"pyc\.(and|or|xor|add|sub|mul|eq|ne|ult|ule|ugt|uge)\s+(.*?)\s*:\s*(\S+)", rhs)
        if binary and len(lhs) == 1:
            inputs = _ssa_names(binary.group(2))
            if len(inputs) != 2:
                raise PYCVerilogError(f"binary PYC op expects two operands: {line}")
            left, right = (_value(values, item) for item in inputs)
            out = add_value(lhs[0], binary.group(3))
            operator = {"and": "&", "or": "|", "xor": "^", "add": "+", "sub": "-", "mul": "*", "eq": "==", "ne": "!=", "ult": "<", "ule": "<=", "ugt": ">", "uge": ">="}[binary.group(1)]
            assigns.append(f"  assign {out.net} = {left.net} {operator} {right.net};")
            continue

        unary = re.fullmatch(r"pyc\.not\s+(.*?)\s*:\s*(\S+)", rhs)
        if unary and len(lhs) == 1:
            inputs = _ssa_names(unary.group(1))
            if len(inputs) != 1:
                raise PYCVerilogError(f"not expects one operand: {line}")
            src = _value(values, inputs[0])
            out = add_value(lhs[0], unary.group(2))
            assigns.append(f"  assign {out.net} = ~{src.net};")
            continue

        raise PYCVerilogError(f"unsupported canonical PYC operation: {line}")

    if returns is None:
        raise PYCVerilogError("PYC function has no return")
    if len(returns) != len(module.results):
        raise PYCVerilogError("PYC return count does not match function result_names")
    for result, returned in zip(module.results, returns):
        source = _value(values, returned)
        if result.type not in ("!pyc.clock", "!pyc.reset"):
            seen_outputs.add(result.net)
            assigns.append(f"  assign {result.net} = {source.net};")

    ports = [_port_decl("input", value) for value in module.args]
    ports.extend(_port_decl("output", value) for value in module.results)
    # The output intentionally embeds several primitive modules after the
    # top-level module.  Tell Verilator that the file name only identifies the
    # requested top module; secondary runtime modules are expected here.
    text = [
        "// Generated by agentic-circuit PYC compatibility backend",
        f"// Source function: {module.name}",
        "/* verilator lint_off DECLFILENAME */",
        "",
        f"module {module.name} (",
        ",\n".join(ports),
        ");",
    ]
    text.extend(declarations)
    text.extend(assigns)
    text.append("")
    text.extend(instances)
    text.extend(["", "endmodule", ""])
    text.extend(_runtime_sources(runtime_dir))
    text.extend(["", "/* verilator lint_on DECLFILENAME */", ""])
    return "\n".join(text)


def _default_pycgen() -> str:
    script = Path(__file__).resolve()
    candidates = (
        script.parents[1] / "build" / "dev-llvm22" / "bin" / "acir-queue-pycgen",
        script.parent / "acir-queue-pycgen",
    )
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    return shutil.which("acir-queue-pycgen") or "acir-queue-pycgen"


def _default_runtime_dir() -> Path:
    script = Path(__file__).resolve()
    candidates = (
        # Source-tree invocation.
        script.parents[1] / "resources" / "pyc_runtime" / "verilog",
        # Installed invocation: <prefix>/bin/script and
        # <prefix>/share/AgenticCircuit/pyc_runtime/verilog.
        script.parents[1] / "share" / "AgenticCircuit" / "pyc_runtime" / "verilog",
    )
    for candidate in candidates:
        if candidate.is_dir():
            return candidate
    # Keep a useful diagnostic from emit_verilog if packaging is incomplete.
    return candidates[0]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="frozen Queue ACIR MLIR, or canonical PYC with --pyc-input")
    parser.add_argument("-o", "--output", type=Path, default=Path("-"), help="output Verilog path (default: stdout)")
    parser.add_argument("--pycgen", default=_default_pycgen(), help="acir-queue-pycgen executable")
    parser.add_argument(
        "--timeout",
        type=float,
        default=120.0,
        help="maximum seconds allowed for ACIR-to-PYC lowering (default: 120)",
    )
    parser.add_argument("--pyc-input", action="store_true", help="treat input as canonical PYC instead of ACIR")
    parser.add_argument("--emit-pyc", type=Path, help="also save the canonical PYC artifact")
    parser.add_argument("--runtime-dir", type=Path, default=_default_runtime_dir())
    args = parser.parse_args(argv)

    if args.pyc_input:
        pyc_text = args.input.read_text(encoding="utf-8")
    else:
        try:
            completed = subprocess.run(
                [args.pycgen, str(args.input)],
                check=True,
                capture_output=True,
                text=True,
                timeout=args.timeout,
            )
        except FileNotFoundError:
            parser.error(f"cannot find ACIR-to-PYC generator: {args.pycgen}")
        except subprocess.TimeoutExpired:
            parser.error(
                f"ACIR-to-PYC generator exceeded --timeout={args.timeout:g}s; "
                "reduce the input or run the single queue target with -j1"
            )
        except subprocess.CalledProcessError as exc:
            sys.stderr.write(exc.stderr or "")
            return exc.returncode or 1
        pyc_text = completed.stdout
    if args.emit_pyc:
        args.emit_pyc.parent.mkdir(parents=True, exist_ok=True)
        args.emit_pyc.write_text(pyc_text, encoding="utf-8")

    try:
        output = emit_verilog(parse_pyc_module(pyc_text), args.runtime_dir)
    except PYCVerilogError as exc:
        parser.error(str(exc))
    if str(args.output) == "-":
        sys.stdout.write(output)
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
