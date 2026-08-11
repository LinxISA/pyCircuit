"""Immutable native build orchestration."""

from __future__ import annotations

import json
import platform
import shutil
import sys
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Literal, NoReturn

from .._diagnostics import Diagnostic
from .._native_api import NativeRequest, run_native_compiler
from .._output import OutputSink
from .._workspace import UserInputError, WorkspaceConfig
from .check import _has_errors, capture


Profile = Literal["fast", "validated", "custom"]


@dataclass(frozen=True, slots=True)
class BuildOptions:
    profile: Profile
    pass_pipeline: str | None
    verify_after_each: bool
    instrumentation_layers: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class BuildPublication:
    directory: Path
    executable: Path
    manifest: Path
    fingerprint: str
    cache_hit: bool


@dataclass(frozen=True, slots=True)
class BuildAttempt:
    publication: BuildPublication | None
    diagnostics: tuple[Diagnostic, ...]
    exit_code: int
    profile: Profile


def _fail(code: str, message: str) -> NoReturn:
    raise UserInputError(
        Diagnostic(stage="build", code=code, severity="error", message=message)
    )


def build_options(arguments: object, workspace: WorkspaceConfig) -> BuildOptions:
    profile = getattr(arguments, "profile", None) or workspace.build_profile
    pipeline = getattr(arguments, "pass_pipeline", None)
    if profile == "custom" and not pipeline:
        _fail("ACPY-CLI-PIPELINE", "custom requires --pass-pipeline")
    if profile != "custom" and pipeline is not None:
        _fail("ACPY-CLI-PIPELINE", "--pass-pipeline requires profile custom")
    return BuildOptions(
        profile=profile,
        pass_pipeline=pipeline,
        verify_after_each=(
            profile == "validated"
            or bool(getattr(arguments, "verify_after_each", False))
        ),
        instrumentation_layers=workspace.instrumentation_layers,
    )


def _output(arguments: object) -> Path:
    value = getattr(arguments, "output_dir", None)
    if value is None:
        _fail("ACPY-CLI-OUTPUT", "build requires -o/--output-dir")
    return Path(value).resolve()


def _source_files(acpy: bytes) -> list[dict[str, str]]:
    try:
        document = json.loads(acpy)
        values = document["sources"]
        if type(values) is not list:
            raise TypeError("sources is not a list")
        result: list[dict[str, str]] = []
        for value in values:
            if type(value) is not dict or set(value) != {"path", "sha256"}:
                raise TypeError("source identity is not closed")
            path = value["path"]
            sha256 = value["sha256"]
            if type(path) is not str or type(sha256) is not str:
                raise TypeError("source identity fields are not strings")
            result.append({"path": path, "sha256": sha256})
        return result
    except (UnicodeError, json.JSONDecodeError, KeyError, TypeError) as error:
        _fail("ACPY-VERIFY-001", f"ACPy source provenance is invalid: {error}")


def _logical_diagnostics(
    diagnostics: tuple[Diagnostic, ...],
) -> tuple[Diagnostic, ...]:
    stages = {
        "acir-parse": "acir-elaboration",
        "acir-verify": "acir-core",
        "acir-normalize": "collection-canonicalization",
        "acir-freeze": "topology-freeze",
        "acsim-lower": "process-state-lowering",
        "acsim-verify": "acsim",
        "cxx-emit": "cxx",
        "cxx-contract": "cxx",
        "compile": "cxx",
        "link": "cxx",
        "publish": "cxx",
    }
    return tuple(
        replace(item, stage=stages.get(item.stage, item.stage))
        for item in diagnostics
    )


def _failure_exit(diagnostics: tuple[Diagnostic, ...]) -> int:
    if any(
        item.stage == "cxx"
        or item.code.startswith(("ACBUILD-", "ACLOWER-CXX", "ACLOWER-COMPILE"))
        for item in diagnostics
    ):
        return 4
    if all(
        item.code.startswith(("ACPY-", "ACELAB-", "ACIR-", "ACLOWER-"))
        for item in diagnostics
    ):
        return 2
    return 3


def build_publication(
    arguments: object,
    workspace: WorkspaceConfig,
    *,
    output: Path | None = None,
) -> BuildAttempt:
    options = build_options(arguments, workspace)
    destination = output.resolve() if output is not None else _output(arguments)
    frontend = capture(arguments, workspace)
    if _has_errors(frontend.diagnostics):
        return BuildAttempt(None, frontend.diagnostics, 2, options.profile)
    if frontend.acpy is None or frontend.acir is None:
        _fail("ACPY-VERIFY-001", "frontend produced incomplete build artifacts")
    compiler = shutil.which(workspace.compiler)
    if compiler is None:
        return BuildAttempt(
            None,
            (
                Diagnostic(
                    stage="cxx",
                    code="ACBUILD-COMPILER-001",
                    severity="error",
                    message=f"C++ compiler is unavailable: {workspace.compiler}",
                ),
            ),
            4,
            options.profile,
        )

    native_options: list[tuple[str, object]] = [
        ("profile", options.profile),
        ("binding_lock", b"[]"),
        ("frontend_acpy", frontend.acpy),
        ("frontend_acir", frontend.acir),
        (
            "build",
            {
                "project_name": workspace.project_name,
                "project_identity": (
                    f"project:{workspace.project_name}@{workspace.project_version}"
                ),
                "system_name": getattr(arguments, "system", None)
                or workspace.default_system,
                "system_identity": (
                    "system:"
                    + (getattr(arguments, "system", None) or workspace.default_system)
                ),
                "source_files": _source_files(frontend.acpy),
                "python_version": (
                    f"{sys.implementation.name} {platform.python_version()}"
                ),
                "helper_identities": [],
                "compiler": compiler,
                "standard_library": workspace.standard_library,
                "instrumentation_layers": list(options.instrumentation_layers),
                "output_root": destination.as_posix(),
            },
        ),
    ]
    if options.pass_pipeline is not None:
        native_options.append(("custom_pipeline", options.pass_pipeline))
    if options.verify_after_each:
        native_options.append(("verify_after_each", True))
    native = run_native_compiler(
        NativeRequest(
            acir=frontend.acir,
            stop_after="publish",
            emits=("executable",),
            options=tuple(native_options),
        )
    )
    diagnostics = _logical_diagnostics(native.diagnostics)
    if _has_errors(diagnostics):
        return BuildAttempt(
            None, diagnostics, _failure_exit(diagnostics), options.profile
        )
    if (
        native.build_directory is None
        or native.executable is None
        or native.build_fingerprint is None
        or native.cache_hit is None
    ):
        raise RuntimeError("native build returned incomplete publication identity")
    publication = BuildPublication(
        directory=Path(native.build_directory),
        executable=Path(native.executable),
        manifest=Path(native.build_directory) / "build-manifest.json",
        fingerprint=native.build_fingerprint,
        cache_hit=native.cache_hit,
    )
    return BuildAttempt(publication, diagnostics, 0, options.profile)


def run(arguments: object, workspace: WorkspaceConfig, sink: OutputSink) -> int:
    attempt = build_publication(arguments, workspace)
    if attempt.publication is None:
        sink.diagnostics(attempt.diagnostics)
        return attempt.exit_code
    publication = attempt.publication
    sink.result(
        {
            "schema": "agentic-circuit-build-result",
            "version": "0.1",
            "contract_epoch": "0.1",
            "status": "passed",
            "profile": attempt.profile,
            "directory": publication.directory.as_posix(),
            "executable": publication.executable.as_posix(),
            "manifest": publication.manifest.as_posix(),
            "build_fingerprint": publication.fingerprint,
            "cache_hit": publication.cache_hit,
        },
        human=f"built {publication.executable}",
    )
    return 0
