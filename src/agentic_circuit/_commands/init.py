"""Specification-only workspace initialization."""

from __future__ import annotations

from pathlib import Path, PurePosixPath

from .._diagnostics import Diagnostic
from .._output import OutputSink
from .._staging import ArtifactStage
from .._workspace import UserInputError


_MANIFEST = """contract_epoch = "0.1"

[project]
name = "example"
version = "0.1.0"
architecture = "architecture.py"
system = "main"

[providers]
standard_library = ["ac.std"]

[build]
profile = "fast"
compiler = "c++"
standard_library = "libc++"
component_roots = ["components"]
protocol_roots = ["protocols"]
build_root = "build"
instrumentation_layers = []

[run]
trace_roots = ["traces"]
inputs = {}

[diagnostics]
format = "text"
"""

_ARCHITECTURE = '''"""Agentic Circuit architecture entry point."""

from agentic_circuit import module, system


@module
def Top() -> None:
    pass


@system
def main() -> None:
    Top()
'''

_FILES = {
    "agentic-circuit.toml": _MANIFEST,
    "architecture.py": _ARCHITECTURE,
}


def _error(code: str, message: str) -> UserInputError:
    return UserInputError(
        Diagnostic(stage="init", code=code, severity="error", message=message)
    )


def run(arguments: object, sink: OutputSink) -> int:
    destination = Path(getattr(arguments, "directory", ".")).resolve()
    force_values = tuple(getattr(arguments, "force", ()) or ())
    force = {PurePosixPath(item).as_posix() for item in force_values}
    unknown_force = sorted(force - set(_FILES))
    if unknown_force:
        raise _error("ACPY-INIT-002", f"unknown init target: {unknown_force[0]}")
    conflicts = sorted(
        name
        for name in _FILES
        if destination.joinpath(name).exists() and name not in force
    )
    if conflicts:
        raise _error(
            "ACPY-INIT-001",
            f"init target already exists and is not forced: {conflicts[0]}",
        )

    result = {
        "schema": "agentic-circuit-init-result",
        "version": "0.1",
        "contract_epoch": "0.1",
        "directory": destination.as_posix(),
        "files": sorted(_FILES),
        "dry_run": bool(getattr(arguments, "dry_run", False)),
    }
    if result["dry_run"]:
        sink.result(result, human=f"Would initialize {destination}")
        return 0

    with ArtifactStage(destination, expected=_FILES) as stage:
        for name, contents in sorted(_FILES.items()):
            stage.write_text(name, contents)
        stage.commit(allow_replace=force)
    sink.result(result, human=f"Initialized {destination}")
    return 0
