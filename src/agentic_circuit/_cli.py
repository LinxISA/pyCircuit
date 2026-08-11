"""Exact public command parser and top-level exit policy."""

from __future__ import annotations

import argparse
from enum import IntEnum
from pathlib import Path
from typing import Sequence

from ._commands import init as init_command
from ._diagnostics import Diagnostic
from ._output import OutputSink
from ._workspace import UserInputError, discover_workspace, load_workspace


EXACT_COMMANDS = (
    "init",
    "schema",
    "check",
    "elaborate",
    "compile",
    "build",
    "run",
    "inspect",
    "explain",
    "doctor",
)


class ExitCode(IntEnum):
    SUCCESS = 0
    USER_INPUT = 2
    INTERNAL = 3
    BUILD = 4
    PREFLIGHT = 5
    SIMULATION = 6
    INCOMPLETE = 7
    INTERRUPTED = 130


class _OnceValue(argparse.Action):
    def __call__(
        self,
        parser: argparse.ArgumentParser,
        namespace: argparse.Namespace,
        values: object,
        option_string: str | None = None,
    ) -> None:
        marker = f"_agentic_seen_{self.dest}"
        if getattr(namespace, marker, False):
            parser.error(f"{option_string} may be specified only once")
        setattr(namespace, marker, True)
        setattr(namespace, self.dest, values)


class _OnceTrue(_OnceValue):
    def __init__(self, option_strings: Sequence[str], dest: str, **kwargs: object):
        super().__init__(
            option_strings, dest, nargs=0, const=True, default=False, **kwargs
        )

    def __call__(
        self,
        parser: argparse.ArgumentParser,
        namespace: argparse.Namespace,
        values: object,
        option_string: str | None = None,
    ) -> None:
        super().__call__(parser, namespace, self.const, option_string)


def _positive(value: str) -> int:
    parsed = int(value)
    if parsed < 1:
        raise argparse.ArgumentTypeError("value must be positive")
    return parsed


def _add_output_options(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--json", action=_OnceTrue)
    parser.add_argument(
        "--diagnostic-format",
        choices=("text", "json", "jsonl"),
        default=None,
        action=_OnceValue,
    )
    parser.add_argument("--no-color", action=_OnceTrue)
    parser.add_argument("--quiet", action=_OnceTrue)
    parser.add_argument("--warnings-as-errors", action=_OnceTrue)


def _add_workspace_options(
    parser: argparse.ArgumentParser,
    *,
    output: bool = False,
    jobs: bool = False,
    seed: bool = False,
) -> None:
    parser.add_argument("--project", type=Path, action=_OnceValue)
    parser.add_argument("--system", action=_OnceValue)
    if output:
        parser.add_argument("--output-dir", type=Path, action=_OnceValue)
    if jobs:
        parser.add_argument("--jobs", type=_positive, action=_OnceValue)
    if seed:
        parser.add_argument("--seed", type=int, action=_OnceValue)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="agentic-circuit", allow_abbrev=False)
    commands = parser.add_subparsers(dest="command", required=True)

    init = commands.add_parser("init", allow_abbrev=False)
    init.add_argument("directory", nargs="?", default=".")
    init.add_argument("--dry-run", action=_OnceTrue)
    init.add_argument("--force", action="append", default=[])
    _add_output_options(init)

    schema = commands.add_parser("schema", allow_abbrev=False)
    schema.add_argument(
        "kind",
        choices=(
            "component",
            "protocol",
            "interface",
            "packet",
            "diagnostic",
            "capabilities",
        ),
    )
    schema.add_argument("name", nargs="?")
    _add_output_options(schema)

    check = commands.add_parser("check", allow_abbrev=False)
    check.add_argument("architecture", nargs="?")
    _add_workspace_options(check, jobs=True)
    _add_output_options(check)

    elaborate = commands.add_parser("elaborate", allow_abbrev=False)
    elaborate.add_argument("architecture", nargs="?")
    elaborate.add_argument(
        "--emit", choices=("acpy", "acir"), default="acir", action=_OnceValue
    )
    elaborate.add_argument("-o", "--output", type=Path, action=_OnceValue)
    _add_workspace_options(elaborate, output=True, jobs=True)
    _add_output_options(elaborate)

    compile_command = commands.add_parser("compile", allow_abbrev=False)
    compile_command.add_argument("architecture", nargs="?")
    _add_workspace_options(compile_command, output=True, jobs=True)
    _add_output_options(compile_command)

    build = commands.add_parser("build", allow_abbrev=False)
    build.add_argument("architecture", nargs="?")
    _add_workspace_options(build, output=True, jobs=True)
    _add_output_options(build)

    run = commands.add_parser("run", allow_abbrev=False)
    run.add_argument("architecture", nargs="?")
    run.add_argument("--replay", type=Path, action=_OnceValue)
    _add_workspace_options(run, output=True, jobs=True, seed=True)
    _add_output_options(run)

    inspect = commands.add_parser("inspect", allow_abbrev=False)
    inspect.add_argument(
        "view",
        choices=(
            "graph",
            "hierarchy",
            "ports",
            "resources",
            "address-map",
            "protocols",
            "specialization",
            "artifacts",
        ),
    )
    inspect.add_argument("--path", action=_OnceValue)
    _add_workspace_options(inspect)
    _add_output_options(inspect)

    explain = commands.add_parser("explain", allow_abbrev=False)
    explain.add_argument("code")
    _add_output_options(explain)

    doctor = commands.add_parser("doctor", allow_abbrev=False)
    _add_output_options(doctor)
    return parser


def command_names(parser: argparse.ArgumentParser) -> tuple[str, ...]:
    for action in parser._actions:
        if isinstance(action, argparse._SubParsersAction):
            return tuple(action.choices)
    return ()


def _workspace_start(arguments: argparse.Namespace) -> Path:
    architecture = getattr(arguments, "architecture", None)
    return Path(architecture) if architecture else Path.cwd()


def _placeholder_result(
    arguments: argparse.Namespace, project: str
) -> dict[str, object]:
    return {
        "schema": "agentic-circuit-command-result",
        "version": "0.1",
        "contract_epoch": "0.1",
        "command": arguments.command,
        "project": project,
        "status": "accepted",
    }


def main(argv: Sequence[str] | None = None) -> int:
    arguments = build_parser().parse_args(argv)
    sink = OutputSink.from_arguments(arguments)
    try:
        if arguments.command == "init":
            return init_command.run(arguments, sink)
        if arguments.command in ("schema", "explain", "doctor"):
            sink.result(
                _placeholder_result(arguments, ""),
                human=f"{arguments.command} command accepted",
            )
            return ExitCode.SUCCESS
        workspace = (
            load_workspace(arguments.project)
            if arguments.project is not None
            else discover_workspace(_workspace_start(arguments))
        )
        sink = OutputSink.from_arguments(
            arguments, workspace_format=workspace.diagnostic_format
        )
        sink.result(
            _placeholder_result(arguments, workspace.project_name),
            human=f"{arguments.command} accepted for {workspace.project_name}",
        )
        return ExitCode.SUCCESS
    except UserInputError as error:
        sink.diagnostics((error.diagnostic,))
        return ExitCode.USER_INPUT
    except KeyboardInterrupt:
        return ExitCode.INTERRUPTED
    except Exception:
        sink.diagnostics(
            (
                Diagnostic(
                    stage="cli",
                    code="ACPY-INTERNAL-001",
                    severity="error",
                    message="internal command failure",
                ),
            )
        )
        return ExitCode.INTERNAL


if __name__ == "__main__":
    raise SystemExit(main())
