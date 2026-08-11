# Phase 4B Agent-First CLI and Runtime Orchestration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the exact public `agentic-circuit` command surface over the Phase 4A frontend and existing native compiler/runtime, with stable diagnostics, capabilities, immutable artifacts, deterministic builds, and manifest-driven runs/replay.

**Architecture:** The public Python CLI owns workspace behavior, trusted capture, command parsing, presentation, and artifact staging. A thin private CPython extension calls one reusable C++ compiler façade that composes ACIR verification, lowering, binding, ACSim, and Phase 3 code generation directly; generated executables use a cold-path gfsim harness to validate run manifests, configure typed limits, execute without Python, and atomically publish run results.

**Tech Stack:** Python 3.11+ standard library, `argparse`, `tomllib`, CPython limited API, CMake 3.25+, C++20, LLVM/MLIR 22.1.8, ACIR/ACSim, gfsim, RFC 8785/I-JSON, GoogleTest, LLVM lit/FileCheck, Python `unittest`, CTest.

## Global Constraints

- Global contract epoch is exactly `0.1`; public CLI identity is exactly `agentic-circuit-cli@0.1`.
- The exact commands are `init`, `schema`, `check`, `elaborate`, `compile`, `build`, `run`, `inspect`, `explain`, and `doctor`; no alias is accepted.
- Exact public exit codes are `0`, `2`, `3`, `4`, `5`, `6`, `7`, and `130` with the meanings fixed by `agentic-python-cli-v0.1.md`.
- Relevant commands implement exact common options `--json`, `--diagnostic-format text|json|jsonl`, `--no-color`, `--quiet`, `--output-dir`, `--project`, `--system`, `--jobs`, and `--seed`; warnings change status only under `--warnings-as-errors`.
- `--json` writes exactly one JSON value to stdout; JSONL modes write one JSON object per line; prose and project output never contaminate structured stdout.
- The Phase 3 library is called through the compiler façade. `acir-opt` and `acir-cxxgen` remain internal developer/test drivers.
- The private native extension exposes no public MLIR/C++ ownership, pass-manager, or object-handle API.
- Python architecture execution is trusted project execution and is never described as sandboxed.
- Python semantic execution ends before the generated simulator begins; the simulator links no Python library.
- `fast`, `validated`, and `custom` are the only build profiles. Every profile retains mandatory representation/runtime validation.
- Frozen ACIR and canonical ACSim are mandatory; C++ generation never bypasses verified ACSim.
- `build-manifest.json`, `run-manifest.json`, and `run-result.json` match their exact closed schemas and are immutable after publication.
- PTO trace bytes and run controls never enter the build fingerprint; every static topology/specialization value does.
- Replay accepts only a verified immutable `run-manifest.json` and applies no ambient override.
- Compiler/simulator subprocesses use argument vectors, bounded captures, and no shell command strings.
- Failed staging never modifies the prior published build, run, result, or current pointer.
- Each task follows red-green-refactor, records the observed failure, runs focused and broader affected suites, and ends in one reviewable commit.
- Preserve the unrelated untracked `phase-1-pr-description.md`; never stage or modify it.

---

## File and Responsibility Map

| File | Responsibility |
| --- | --- |
| `include/acir/Compiler/Driver.h` | Public typed high-level compiler request/result and stage API. |
| `lib/Compiler/Driver.cpp` | MLIR setup, parsing, stage sequencing, diagnostics, and artifact collection. |
| `lib/Compiler/Pipeline.cpp` | Exact standard/custom pass pipelines and stage controls. |
| `python/native/NativeModule.cpp` | Private CPython limited-API transport to the compiler façade. |
| `src/agentic_circuit/_native_api.py` | Immutable Python wrapper around `_native`. |
| `src/agentic_circuit/_cli.py` | Exact parser, common options, exit mapping, and entry point. |
| `src/agentic_circuit/_workspace.py` | Upward workspace discovery and closed TOML configuration. |
| `src/agentic_circuit/_output.py` | Human/JSON/JSONL output discipline and diagnostic rendering. |
| `src/agentic_circuit/_staging.py` | Contained private stages and atomic Python artifact publication. |
| `src/agentic_circuit/_capture_worker.py` | Fresh trusted import, frontend execution, and bounded project output. |
| `src/agentic_circuit/_capabilities.py` | Exact schema identities and catalog availability document. |
| `src/agentic_circuit/_commands/` | One focused module per public command family. |
| `include/gfsim/harness.h` | Typed run manifest, limits, result, and generated-main template API. |
| `lib/gfsim/harness.cpp` | Run preflight, trace/build verification, output hashing, and publication. |
| `include/gfsim/object.h` and `lib/gfsim/system.cpp` | Deadlock and per-domain limit accounting. |
| `lib/CodeGen/Generator.cpp` | Generated model configuration and manifest-aware `main`. |
| `tests/cli/` | Parser, command, output, failure, determinism, and install tests. |
| `unittests/Compiler/` | Native façade and stage tests. |
| `unittests/gfsim/` and `unittests/CodeGen/` | Harness, limit, generated-main, and no-Python tests. |
| `docs/implementation/phase-4-audit.md` | Combined Phase 4 exit evidence. |

---

### Task 1: Reusable native compiler façade

**Files:**
- Create: `include/acir/Compiler/Driver.h`
- Create: `lib/Compiler/CMakeLists.txt`
- Create: `lib/Compiler/Driver.cpp`
- Create: `lib/Compiler/Pipeline.cpp`
- Modify: `lib/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Create: `unittests/Compiler/CMakeLists.txt`
- Create: `unittests/Compiler/DriverTest.cpp`
- Modify: `unittests/CMakeLists.txt`

**Interfaces:**
- Consumes: ACIR/ACSim dialect registration, transforms, binding resolver, ACIR-to-ACSim, and `acir::codegen::buildGeneratedModel`.
- Produces: `CompilerStage`, `CompilerProfile`, `CompilerRequest`, `CompilerArtifact`, `CompilerDiagnostic`, `CompilerResult`, and `runCompiler`.

- [ ] **Step 1: Write the failing stage and diagnostic tests**

```cpp
TEST(CompilerDriverTest, StandardPipelineProducesVerifiedStageArtifacts) {
  CompilerRequest request = validRequest();
  request.stopAfter = CompilerStage::ACSimVerify;
  request.emits = {ArtifactKind::FrozenAcir, ArtifactKind::Acsim};
  auto result = runCompiler(request);
  ASSERT_TRUE(static_cast<bool>(result));
  EXPECT_EQ(paths(result->artifacts),
            ElementsAre("frozen.ac.mlir", "model.acsim.mlir"));
  EXPECT_TRUE(result->diagnostics.empty());
}

TEST(CompilerDriverTest, ParseFailureReturnsStableStructuredDiagnostic) {
  CompilerRequest request = validRequest();
  request.acirBytes = "not mlir";
  auto result = runCompiler(request);
  ASSERT_FALSE(static_cast<bool>(result));
  EXPECT_EQ(firstDiagnosticCode(result.takeError()), "ACIR-PARSE-001");
}
```

- [ ] **Step 2: Run the focused test and confirm the RED state**

Run: `cmake --build --preset dev-llvm22 --target CompilerTests`

Expected: configure/build fails because the compiler façade does not exist.

- [ ] **Step 3: Define the closed façade types**

```cpp
enum class CompilerStage {
  AcirParse, AcirVerify, AcirNormalize, AcirFreeze,
  AcsimLower, AcsimVerify, CxxEmit, CxxContract,
  Compile, Link, Publish,
};
enum class CompilerProfile { Fast, Validated, Custom };

struct CompilerRequest {
  std::string acirBytes;
  std::string bindingLockBytes;
  CompilerProfile profile = CompilerProfile::Fast;
  std::optional<CompilerStage> stopAfter;
  std::vector<ArtifactKind> emits;
  std::vector<std::string> dumpBefore;
  std::vector<std::string> dumpAfter;
  bool dumpAfterEach = false;
  bool verifyAfterEach = false;
  std::optional<std::string> customPipeline;
  codegen::BuildRequest build;
};
struct CompilerArtifact {
  std::string logicalPath;
  ArtifactKind kind;
  std::string bytes;
  codegen::Fingerprint sha256;
};
struct SourceLocation {
  std::string file;
  uint64_t line;
  uint64_t column;
};
struct CompilerRelated {
  std::string message;
  std::optional<SourceLocation> source;
  std::optional<std::string> objectPath;
};
struct CompilerFixIt { std::string message; };
struct CompilerDiagnostic {
  std::string stage;
  std::string code;
  std::string severity;
  std::string message;
  std::optional<SourceLocation> source;
  std::optional<std::string> objectPath;
  llvm::json::Value expected;
  llvm::json::Value actual;
  std::vector<CompilerRelated> related;
  std::vector<CompilerFixIt> fixits;
};
struct CompilerResult {
  std::vector<CompilerArtifact> artifacts;
  std::vector<CompilerDiagnostic> diagnostics;
  std::optional<codegen::BuildResult> build;
};
llvm::Expected<CompilerResult> runCompiler(const CompilerRequest &request);
```

- [ ] **Step 4: Implement one explicit stage machine**

```cpp
for (CompilerStage stage : selectedPipeline(request)) {
  if (failed(runStage(stage, state, diagnostics)))
    return compilerError(std::move(diagnostics));
  if (request.verifyAfterEach && failed(verifyState(state, diagnostics)))
    return compilerError(std::move(diagnostics));
  collectRequestedDumps(stage, state, request, result.artifacts);
  if (request.stopAfter == stage)
    break;
}
```

Create/register one MLIR context, parse bytes in memory, capture diagnostics
with normalized locations, validate legal stage/emission combinations, require
an explicit pipeline for `custom`, and call the Phase 3 library directly for
build stages.

- [ ] **Step 5: Run façade, conversion, and code-generation tests**

Run: `cmake --build --preset dev-llvm22 --target CompilerTests CodeGenTests && ctest --test-dir build/dev-llvm22 -R '^(CompilerTests|ConversionTests|CodeGenTests)$' --output-on-failure`

Expected: PASS.

- [ ] **Step 6: Commit the façade**

```bash
git add CMakeLists.txt include/acir/Compiler lib/Compiler unittests/Compiler unittests/CMakeLists.txt lib/CMakeLists.txt
git commit -m "feat(compiler): add high-level native driver facade"
```

---

### Task 2: Private CPython extension and install layout

**Files:**
- Create: `python/native/CMakeLists.txt`
- Create: `python/native/NativeModule.cpp`
- Create: `python/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Modify: `pyproject.toml`
- Create: `src/agentic_circuit/_native_api.py`
- Create: `tests/cli/test_native_api.py`

**Interfaces:**
- Consumes: `runCompiler` and compiler façade records from Task 1.
- Produces: private `_native.run_compiler`, `_native.capabilities`, Python `NativeRequest`, `NativeResult`, and `run_native_compiler`.

- [ ] **Step 1: Write the failing import and round-trip tests**

```python
class NativeApiTest(unittest.TestCase):
    def test_private_extension_compiles_verified_acir(self) -> None:
        result = run_native_compiler(NativeRequest(
            acir=fixture_bytes("minimal.ac.mlir"),
            stop_after="acsim-verify",
            emits=("frozen-acir", "acsim"),
        ))
        self.assertEqual((), result.diagnostics)
        self.assertEqual(("frozen.ac.mlir", "model.acsim.mlir"),
                         tuple(item.path for item in result.artifacts))

    def test_native_extension_is_not_exported_publicly(self) -> None:
        self.assertNotIn("_native", agentic_circuit.__all__)
```

- [ ] **Step 2: Run the focused test and confirm the RED state**

Run: `cmake --build --preset dev-llvm22 --target agentic_circuit_native && PYTHONPATH=src:build/dev-llvm22/python .venv/bin/python -m unittest tests.cli.test_native_api -v`

Expected: build/configuration fails because the extension is absent.

- [ ] **Step 3: Implement limited-API request/result conversion**

```cpp
#define Py_LIMITED_API 0x030B0000
#include <Python.h>

static PyObject *runCompiler(PyObject *, PyObject *arguments, PyObject *keywords) {
  auto request = parseClosedCompilerRequest(arguments, keywords);
  if (!request)
    return returnDiagnostics(request.takeError());
  auto result = acir::compiler::runCompiler(*request);
  if (!result)
    return returnDiagnostics(result.takeError());
  return buildClosedResult(*result);
}
```

Accept bytes, strings, booleans, tuples, and closed dictionaries only. Copy
buffers into owned C++ strings before releasing Python references. Convert all
errors to result dictionaries; reserve Python exceptions for extension misuse
or allocation failure.

- [ ] **Step 4: Add immutable Python wrapper records**

```python
@dataclass(frozen=True, slots=True)
class NativeRequest:
    acir: bytes
    stop_after: str | None
    emits: tuple[str, ...]
    options: tuple[tuple[str, JsonValue], ...] = ()

@dataclass(frozen=True, slots=True)
class NativeArtifact:
    path: str
    kind: str
    data: bytes
    sha256: str

@dataclass(frozen=True, slots=True)
class NativeResult:
    artifacts: tuple[NativeArtifact, ...]
    diagnostics: tuple[Diagnostic, ...]
    build_directory: str | None
    executable: str | None
    build_fingerprint: str | None

@dataclass(frozen=True, slots=True)
class NativeCapabilities:
    compiler_build_id: str
    runtime_build_id: str
    items: tuple[Mapping[str, JsonValue], ...]
```

Validate the extension response before exposing it internally. Sort/normalize
diagnostics through the shared Phase 4A diagnostic model.

- [ ] **Step 5: Configure build-tree and CMake install imports**

Use `find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter Development.Module)`, compile with `Py_LIMITED_API=0x030B0000`, place the module under the build-tree `agentic_circuit` package, install Python sources/resources and the extension to one package directory, and declare `[project.scripts] agentic-circuit = "agentic_circuit._cli:main"` without adding runtime dependencies.

Run: `cmake --build --preset dev-llvm22 --target agentic_circuit_native && PYTHONPATH=src:build/dev-llvm22/python .venv/bin/python -m unittest tests.cli.test_native_api -v`

Expected: PASS.

- [ ] **Step 6: Commit the native bridge**

```bash
git add CMakeLists.txt pyproject.toml python src/agentic_circuit/_native_api.py tests/cli
git commit -m "feat(python): bridge frontend to native compiler"
```

---

### Task 3: Workspace, staging, output policy, and exact CLI parser

**Files:**
- Create: `src/agentic_circuit/_workspace.py`
- Create: `src/agentic_circuit/_staging.py`
- Create: `src/agentic_circuit/_output.py`
- Create: `src/agentic_circuit/_cli.py`
- Create: `src/agentic_circuit/_commands/__init__.py`
- Create: `src/agentic_circuit/_commands/init.py`
- Create: `tests/cli/test_cli_parser.py`
- Create: `tests/cli/test_workspace.py`
- Create: `tests/cli/fixtures/workspace/agentic-circuit.toml`

**Interfaces:**
- Consumes: shared diagnostics and canonical JSON from Phase 4A.
- Produces: `WorkspaceConfig`, `discover_workspace`, `ArtifactStage`, `OutputSink`, `build_parser`, `ExitCode`, and `main`.

- [ ] **Step 1: Write failing parser/config/output tests**

```python
class CliParserTest(unittest.TestCase):
    def test_exact_command_inventory(self) -> None:
        parser = build_parser()
        self.assertEqual(EXACT_COMMANDS, command_names(parser))

    def test_json_stdout_contains_one_value_and_no_prose(self) -> None:
        result = run_cli("init", "--dry-run", "--json", cwd=EMPTY)
        self.assertEqual(0, result.returncode)
        self.assertIsInstance(json.loads(result.stdout), dict)
        self.assertEqual("", result.stderr)

    def test_unknown_toml_key_is_exit_two(self) -> None:
        result = run_cli("check", "architecture.py", cwd=BAD_CONFIG)
        self.assertEqual(2, result.returncode)
        self.assertEqual("ACPY-CONFIG-002", json.loads(result.stdout)["code"])

    def test_explicit_project_bypasses_current_directory_discovery(self) -> None:
        result = run_cli("check", "architecture.py", "--project", PROJECT_FILE,
                         cwd=UNRELATED_DIRECTORY)
        self.assertEqual(0, result.returncode)
```

- [ ] **Step 2: Run the focused tests and confirm the RED state**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.cli.test_cli_parser tests.cli.test_workspace -v`

Expected: FAIL because CLI/workspace modules are absent.

- [ ] **Step 3: Implement closed workspace configuration**

```python
@dataclass(frozen=True, slots=True)
class WorkspaceConfig:
    root: Path
    project_name: str
    project_version: str
    contract_epoch: str
    architecture: Path
    default_system: str
    standard_library_providers: tuple[str, ...]
    build_profile: Literal["fast", "validated", "custom"]
    compiler: str
    standard_library: str
    component_roots: tuple[Path, ...]
    protocol_roots: tuple[Path, ...]
    trace_roots: tuple[Path, ...]
    build_root: Path
    default_trace: Path | None
    default_run_inputs: tuple[tuple[str, JsonValue], ...]
    diagnostic_format: Literal["text", "json", "jsonl"]
    instrumentation_layers: tuple[str, ...]

def discover_workspace(start: Path) -> WorkspaceConfig:
    for directory in (start.resolve(), *start.resolve().parents):
        candidate = directory / "agentic-circuit.toml"
        if candidate.is_file():
            return parse_closed_workspace(candidate)
    raise UserInputError("ACPY-CONFIG-001", "workspace configuration not found")
```

Reject unknown keys/sections, path escape, duplicate ownership, invalid names,
wrong epoch, unknown provider/profile, and type/range mismatches. When
`--project PATH` is present, load exactly that manifest and do not search the
current directory.

- [ ] **Step 4: Implement contained stages and output policy**

```python
class ArtifactStage:
    def __enter__(self) -> "ArtifactStage":
        self.path = Path(tempfile.mkdtemp(prefix=".agentic-stage-",
                                         dir=self.destination.parent))
        return self

    def commit(self) -> None:
        verify_stage_files(self.path, self.expected)
        atomic_replace_directory(self.path, self.destination)
        self.committed = True
```

`OutputSink` routes human results to stdout, diagnostics to stderr, single JSON
to stdout, and JSONL as one canonical object per line. It bounds captured text
and strips no semantic diagnostic content.

- [ ] **Step 5: Implement exact parser and `init` mutation rules**

```python
class ExitCode(IntEnum):
    SUCCESS = 0
    USER_INPUT = 2
    INTERNAL = 3
    BUILD = 4
    PREFLIGHT = 5
    SIMULATION = 6
    INCOMPLETE = 7
    INTERRUPTED = 130

def main(argv: Sequence[str] | None = None) -> int:
    arguments = build_parser().parse_args(argv)
    return dispatch(arguments, OutputSink.from_arguments(arguments))
```

`init` writes only absent specified files through a stage, refuses conflicts,
supports dry-run, and never overwrites source unless a repeatable
`--force TARGET` names every existing target that will be replaced.

- [ ] **Step 6: Run parser/workspace tests and commit**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.cli.test_cli_parser tests.cli.test_workspace -v`

Expected: PASS.

```bash
git add src/agentic_circuit tests/cli
git commit -m "feat(cli): add workspace parser and output policy"
```

---

### Task 4: Schema, capabilities, explain, and doctor commands

**Files:**
- Create: `src/agentic_circuit/_capabilities.py`
- Create: `src/agentic_circuit/_commands/schema.py`
- Create: `src/agentic_circuit/_commands/explain.py`
- Create: `src/agentic_circuit/_commands/doctor.py`
- Create: `resources/diagnostics-v0.1.json`
- Create: `tests/cli/test_discovery_commands.py`
- Modify: `tests/contracts/test_contracts.py`

**Interfaces:**
- Consumes: packaged schemas/catalog, native capabilities, output policy, and diagnostics.
- Produces: `CapabilityDocument`, `schema_command`, `explain_command`, and `doctor_command`.

- [ ] **Step 1: Write failing no-project-execution discovery tests**

```python
class DiscoveryCommandTest(unittest.TestCase):
    def test_capabilities_match_exact_schema_without_importing_project(self) -> None:
        result = run_cli("schema", "capabilities", "--json", cwd=HOSTILE_PROJECT)
        document = json.loads(result.stdout)
        validate_schema("capabilities.schema.json", document)
        self.assertFalse((HOSTILE_PROJECT / "imported.marker").exists())

    def test_explain_and_doctor_are_read_only(self) -> None:
        before = snapshot_tree(WORKSPACE)
        self.assertEqual(0, run_cli("explain", "ACIR-PROTOCOL-004", "--json").returncode)
        run_cli("doctor", "--json", cwd=WORKSPACE)
        self.assertEqual(before, snapshot_tree(WORKSPACE))
```

- [ ] **Step 2: Run the focused test and confirm the RED state**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.cli.test_discovery_commands -v`

Expected: FAIL because discovery commands are absent.

- [ ] **Step 3: Build the exact capability document**

```python
def capability_document(catalog: Catalog,
                        native: NativeCapabilities) -> Mapping[str, JsonValue]:
    return {
        "schema": "agentic-circuit-capabilities",
        "version": "0.1",
        "contract_epoch": "0.1",
        "contract_identities": EXACT_CONTRACT_IDENTITIES,
        "items": [capability_item(item, native) for item in catalog.sorted_items()],
        "compiler_build_id": native.compiler_build_id,
        "runtime_build_id": native.runtime_build_id,
    }
```

Represent known missing implementations as `declared_unavailable`; include no
version range; validate every result against `capabilities.schema.json`.

- [ ] **Step 4: Implement packaged schema/explanation and read-only doctor checks**

`schema` resolves only known packaged versioned resources. `explain` validates
the code and returns rule, causes, examples, and repairs from a closed catalog.
`doctor` checks Python, native extension, LLVM/MLIR identity, compiler,
standard-library providers, gfsim source contract, JSON, and exact epoch without
creating files or importing architecture modules.

The exact schema queries are `component [NAME]`, `protocol [NAME]`,
`interface [NAME]`, `packet [NAME]`, `diagnostic [CODE]`, and `capabilities`.
Unknown names/codes return a schema-valid `ACPY-SCHEMA-*` diagnostic and exit
`2`; list forms are sorted by exact identity.

```python
@dataclass(frozen=True, slots=True)
class DoctorCheck:
    name: str
    status: Literal["passed", "failed"]
    observed: str
    required: str
    diagnostic_code: str | None
```

- [ ] **Step 5: Run discovery and repository contract tests**

Run: `PYTHONPATH=src:build/dev-llvm22/python .venv/bin/python -m unittest tests.cli.test_discovery_commands -v && .venv/bin/python -m unittest tests.contracts.test_contracts -v`

Expected: PASS.

- [ ] **Step 6: Commit discovery commands**

```bash
git add src/agentic_circuit resources tests/cli tests/contracts
git commit -m "feat(cli): add schema capability and doctor discovery"
```

---

### Task 5: Trusted capture worker, `check`, and `elaborate`

**Files:**
- Create: `src/agentic_circuit/_capture_worker.py`
- Create: `src/agentic_circuit/_commands/check.py`
- Create: `src/agentic_circuit/_commands/elaborate.py`
- Create: `tests/cli/test_frontend_commands.py`
- Create: `tests/cli/fixtures/frontend/architecture.py`
- Create: `tests/cli/fixtures/frontend/noisy.py`

**Interfaces:**
- Consumes: Phase 4A `CaptureRequest`/`elaborate_frontend`, workspace, staging, output policy, and native ACIR verification.
- Produces: `CaptureWorkerRequest`, `CaptureWorkerResult`, `run_capture_worker`, `check_command`, and `elaborate_command`.

- [ ] **Step 1: Write failing isolation and artifact tests**

```python
class FrontendCommandTest(unittest.TestCase):
    def test_check_is_fast_machine_readable_and_writes_no_build(self) -> None:
        result = run_cli("check", "architecture.py", "--system", "main", "--json",
                         cwd=FRONTEND)
        self.assertEqual(0, result.returncode)
        self.assertEqual("passed", json.loads(result.stdout)["status"])
        self.assertFalse((FRONTEND / "build").exists())

    def test_elaborate_is_deterministic_and_noisy_project_output_is_captured(self) -> None:
        first = run_cli("elaborate", "noisy.py", "--emit=acpy", "-o", "a.json")
        second = run_cli("elaborate", "noisy.py", "--emit=acpy", "-o", "b.json")
        self.assertEqual(read_bytes("a.json"), read_bytes("b.json"))
        self.assertNotIn("project noise", first.stdout)
```

- [ ] **Step 2: Run the focused test and confirm the RED state**

Run: `PYTHONPATH=src:build/dev-llvm22/python .venv/bin/python -m unittest tests.cli.test_frontend_commands -v`

Expected: FAIL because capture commands are absent.

- [ ] **Step 3: Implement fresh trusted-worker execution**

```python
@dataclass(frozen=True, slots=True)
class CaptureWorkerRequest:
    python: str
    workspace: Path
    entry: Path
    system: str
    static_arguments: tuple[tuple[str, JsonValue], ...]
    private_output: Path
    timeout: float

@dataclass(frozen=True, slots=True)
class CaptureWorkerResult:
    acpy: bytes | None
    acir: bytes | None
    diagnostics: tuple[Diagnostic, ...]
    project_report: bytes | None

def run_capture_worker(request: CaptureWorkerRequest) -> CaptureWorkerResult:
    with ArtifactStage(request.private_output) as stage:
        completed = subprocess.run(
            [request.python, "-I", "-m", "agentic_circuit._capture_worker",
             "--request", os.fspath(request_file(stage.path))],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, timeout=request.timeout, check=False,
        )
        return read_verified_worker_result(stage.path, completed)
```

The worker imports the package first, then adds the explicit workspace root,
executes only the requested trusted entry, redirects project stdout/stderr to a
bounded report, and writes verified ACPy/ACIR plus diagnostics into its stage.

- [ ] **Step 4: Implement command stop/output behavior**

`check` runs through ACIR construction and native parse/verification without
lowering or C++ compilation. Support `--stop-after=acpy-verify`. `elaborate`
accepts only `acpy|acir`, defaults to `acir`, and atomically publishes one
canonical output.

```python
def elaborate_command(args: Namespace, sink: OutputSink) -> ExitCode:
    result = run_capture_worker(worker_request(args))
    if has_errors(result.diagnostics):
        return sink.fail(result.diagnostics, ExitCode.USER_INPUT)
    data = result.acpy if args.emit == "acpy" else result.acir
    publish_single_artifact(Path(args.output), data)
    return sink.success({"path": args.output, "sha256": sha256_bytes(data)})
```

- [ ] **Step 5: Run frontend command and determinism tests**

Run: `PYTHONPATH=src:build/dev-llvm22/python .venv/bin/python -m unittest tests.cli.test_frontend_commands tests.python_frontend.test_determinism -v`

Expected: PASS.

- [ ] **Step 6: Commit capture/check/elaborate**

```bash
git add src/agentic_circuit tests/cli
git commit -m "feat(cli): add isolated check and elaborate commands"
```

---

### Task 6: `compile` stages, emits, dumps, and failure mapping

**Files:**
- Create: `src/agentic_circuit/_commands/compile.py`
- Create: `tests/cli/test_compile_command.py`
- Create: `tests/cli/fixtures/compile/architecture.py`
- Create: `test/Python/cli-compile.mlir`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: capture worker, native façade, staging, workspace, and output policy.
- Produces: `CompileOptions`, `compile_command`, canonical emitted artifact set, and pass dump reports.

- [ ] **Step 1: Write failing stage/emission tests**

```python
class CompileCommandTest(unittest.TestCase):
    def test_all_exact_emits_are_published_in_fixed_order(self) -> None:
        result = run_cli("compile", "architecture.py",
                         "--emit=acpy,acir,frozen-acir,acsim,cpp",
                         "--output-dir", "build/main", cwd=COMPILE)
        self.assertEqual(0, result.returncode)
        self.assertEqual(EXPECTED_COMPILE_TREE, relative_tree(COMPILE / "build/main"))

    def test_custom_pipeline_and_bad_stage_map_to_exact_errors(self) -> None:
        missing = run_cli("compile", "architecture.py", "--profile=custom")
        self.assertEqual(2, missing.returncode)
        self.assertIn("ACPY-CLI-PIPELINE", missing.stderr)
```

- [ ] **Step 2: Run the focused test and confirm the RED state**

Run: `PYTHONPATH=src:build/dev-llvm22/python .venv/bin/python -m unittest tests.cli.test_compile_command -v`

Expected: FAIL because `compile` is absent.

- [ ] **Step 3: Implement exact compile option normalization**

```python
@dataclass(frozen=True, slots=True)
class CompileOptions:
    emits: tuple[Literal["acpy", "acir", "frozen-acir", "acsim", "cpp"], ...]
    stop_after: str | None
    dump_before: tuple[str, ...]
    dump_after: tuple[str, ...]
    dump_after_each: bool
    verify_after_each: bool
    pass_pipeline: str | None
```

Reject unknown/duplicate emits, invalid stage names, contradictory stop/emits,
unknown passes, and custom mode without an explicit pipeline before native work.
Implement exact controls `--stop-after STAGE`, repeatable `--dump-before PASS`,
repeatable `--dump-after PASS`, `--dump-after-each`, `--verify-after-each`, and
`--pass-pipeline PIPELINE`.

The public logical stage spellings are `frontend-capture`,
`acpy-construction`, `acpy-verify`, `acir-elaboration`,
`process-construction`, `collection-canonicalization`, `acir-core`,
`topology-freeze`, `process-state-lowering`, `acsim`, and `cxx`. Map each to
the owning Python/native stage and retain the same spelling in diagnostics and
validation reports even when a release pipeline combines physical passes.

- [ ] **Step 4: Implement compile composition and atomic artifact publication**

```python
def compile_command(args: Namespace, sink: OutputSink) -> ExitCode:
    frontend = run_capture_worker(worker_request(args))
    if has_errors(frontend.diagnostics):
        return sink.fail(frontend.diagnostics, ExitCode.USER_INPUT)
    native = run_native_compiler(native_request(frontend, args))
    if has_errors(native.diagnostics):
        return sink.fail(native.diagnostics, classify_compile_exit(native))
    with ArtifactStage(Path(args.output_dir)) as stage:
        write_compile_artifacts(stage.path, frontend, native, args.emit)
        stage.commit()
    return sink.success(compile_summary(stage.destination, native))
```

- [ ] **Step 5: Run CLI, lit, and native compiler tests**

Run: `PYTHONPATH=src:build/dev-llvm22/python .venv/bin/python -m unittest tests.cli.test_compile_command -v && cmake --build --preset dev-llvm22 --target CompilerTests check-acir`

Expected: PASS.

- [ ] **Step 6: Commit compile orchestration**

```bash
git add src/agentic_circuit tests/cli test/Python test/CMakeLists.txt
git commit -m "feat(cli): add deterministic compile stages"
```

---

### Task 7: Build profiles, frontend provenance, and `build`

**Files:**
- Modify: `include/acir/CodeGen/Build.h`
- Modify: `lib/CodeGen/Build.cpp`
- Modify: `lib/CodeGen/Manifest.cpp`
- Create: `src/agentic_circuit/_commands/build.py`
- Create: `tests/cli/test_build_command.py`
- Modify: `unittests/CodeGen/BuildTest.cpp`
- Modify: `unittests/CodeGen/CodeGenTest.cpp`

**Interfaces:**
- Consumes: compile command inputs, native façade, Phase 3 build API, and build-manifest schema.
- Produces: `FrontendProvenance`, extended `BuildRequest::frontend`, `BuildOptions`, and `build_command`.

- [ ] **Step 1: Write failing provenance/profile/cache tests**

```python
class BuildCommandTest(unittest.TestCase):
    def test_build_manifest_records_frontend_and_exact_profile(self) -> None:
        result = run_cli("build", "architecture.py", "--profile=validated",
                         "-o", "build/model", cwd=BUILD)
        self.assertEqual(0, result.returncode)
        manifest = load_current_build_manifest(BUILD / "build/model")
        validate_schema("build-manifest.schema.json", manifest)
        self.assertEqual("validated", manifest["build_profile"])
        self.assertIn("architecture.py", paths(manifest["source_files"]))
        self.assertIn("input/model.acpy.json", paths(manifest["artifacts"]))

    def test_identical_build_is_cache_hit_and_changed_static_input_is_miss(self) -> None:
        first = build_fixture(lanes=4)
        second = build_fixture(lanes=4)
        third = build_fixture(lanes=8)
        self.assertFalse(first.cache_hit)
        self.assertTrue(second.cache_hit)
        self.assertNotEqual(first.fingerprint, third.fingerprint)
```

- [ ] **Step 2: Run focused Python/native tests and confirm the RED state**

Run: `PYTHONPATH=src:build/dev-llvm22/python .venv/bin/python -m unittest tests.cli.test_build_command -v && cmake --build --preset dev-llvm22 --target CodeGenTests`

Expected: FAIL because frontend provenance and `build` are absent.

- [ ] **Step 3: Extend the build request with exact frontend provenance**

```cpp
struct FrontendProvenance {
  std::vector<FileHash> sourceFiles;
  Artifact acpy;
  Artifact canonicalAcir;
  std::string pythonVersion;
  std::vector<NamedFingerprint> helperIdentities;
};

struct BuildRequest {
  // Existing fields remain unchanged.
  FrontendProvenance frontend;
};
```

Validate normalized unique paths, exact hashes, `ArtifactKind::Acpy`/`Acir`,
non-empty Python identity, sorted helpers, and consistency between canonical
ACIR bytes and its recorded hash. Merge frontend sources/artifacts into the
closed manifest and publication stage without changing schema fields.

- [ ] **Step 4: Implement profile normalization and build composition**

```python
@dataclass(frozen=True, slots=True)
class BuildPublication:
    directory: Path
    executable: Path
    manifest: Path
    fingerprint: str
    cache_hit: bool

@dataclass(frozen=True, slots=True)
class BuildOptions:
    profile: Literal["fast", "validated", "custom"]
    pass_pipeline: str | None
    verify_after_each: bool
    instrumentation_layers: tuple[str, ...]

def build_options(args: Namespace) -> BuildOptions:
    profile = args.profile or "fast"
    if profile == "custom" and not args.pass_pipeline:
        raise UserInputError("ACPY-CLI-PIPELINE", "custom requires --pass-pipeline")
    return BuildOptions(profile, args.pass_pipeline,
                        profile == "validated", static_layers(profile))

def build_command(args: Namespace, sink: OutputSink) -> ExitCode:
    frontend = run_capture_worker(worker_request(args))
    native = run_native_compiler(build_native_request(frontend, args))
    if has_errors(native.diagnostics):
        return sink.fail(native.diagnostics, classify_build_exit(native))
    return sink.success(build_summary(native))
```

Map source/static errors to `2`, internal compiler failures to `3`, and
generation/compile/link failures to `4`. Do not publish partial CLI artifacts
outside Phase 3's immutable build stage.

- [ ] **Step 5: Run build, manifest, and failure-atomicity suites**

Run: `PYTHONPATH=src:build/dev-llvm22/python .venv/bin/python -m unittest tests.cli.test_build_command -v && cmake --build --preset dev-llvm22 --target CodeGenTests && build/dev-llvm22/bin/CodeGenTests --gtest_filter='CodeGenManifestTest.*:BuildTest.*'`

Expected: PASS.

- [ ] **Step 6: Commit build orchestration**

```bash
git add include/acir/CodeGen lib/CodeGen unittests/CodeGen src/agentic_circuit tests/cli
git commit -m "feat(cli): build with exact frontend provenance"
```

---

### Task 8: Manifest-driven gfsim harness and runtime limits

**Files:**
- Create: `include/gfsim/harness.h`
- Create: `lib/gfsim/harness.cpp`
- Modify: `lib/gfsim/CMakeLists.txt`
- Modify: `include/gfsim/object.h`
- Modify: `lib/gfsim/system.cpp`
- Modify: `include/acir/CodeGen/ModelPlan.h`
- Modify: `lib/CodeGen/ModelPlan.cpp`
- Modify: `lib/CodeGen/Generator.cpp`
- Modify: `lib/Conversion/ACIRToACSim/ACIRToACSim.cpp`
- Modify: `docs/specs/acsim-gfsim-lowering-v0.1.md`
- Modify: `contracts/acsim-v0.1.yaml`
- Modify: `unittests/gfsim/core_test.cpp`
- Modify: `unittests/CodeGen/GeneratorTest.cpp`
- Modify: `unittests/CodeGen/GeneratedModelRuntimeTest.cpp`

**Interfaces:**
- Consumes: exact run/build manifest schemas, ACIR time-domain attributes, gfsim termination/statistics, and generated `Model`.
- Produces: `RunManifest`, `RuntimeLimits`, `TimeDomainRuntime`, `RunResultDocument`, `loadRunManifest`, `runGeneratedModel`, and manifest-aware generated `main`.

- [ ] **Step 1: Write failing manifest, domain-cap, and result tests**

```cpp
TEST(RuntimeHarnessTest, ExactManifestConfiguresLimitsAndProducesClosedResult) {
  auto manifest = loadRunManifest(validRunManifestBytes(), fixtureRoot());
  ASSERT_TRUE(static_cast<bool>(manifest));
  TestModel model;
  auto result = runGeneratedModel(model, *manifest, resultStage());
  ASSERT_TRUE(static_cast<bool>(result));
  EXPECT_EQ(result->status, RunStatus::Incomplete);
  EXPECT_EQ(result->terminationReason, "max_domain_cycles");
  EXPECT_EQ(result->domainCycles.at("core"), 25u);
}

TEST(RuntimeHarnessTest, BuildOrTraceHashMismatchFailsBeforeModelWork) {
  TestModel model;
  auto result = runGeneratedModel(model, mismatchedManifest(), resultStage());
  EXPECT_FALSE(static_cast<bool>(result));
  EXPECT_EQ(model.workCount(), 0u);
}
```

- [ ] **Step 2: Run focused tests and confirm the RED state**

Run: `cmake --build --preset dev-llvm22 --target GfsimTests CodeGenTests && build/dev-llvm22/bin/GfsimTests --gtest_filter='RuntimeHarnessTest.*'`

Expected: compilation fails because the harness does not exist.

- [ ] **Step 3: Define exact typed runtime documents**

```cpp
struct TimeDomainRuntime {
  std::string name;
  uint64_t period;
  uint64_t phase;
  uint64_t tickScale;
};
struct HarnessFileHash { std::string path; std::string sha256; };
struct TraceIdentity {
  std::string path;
  std::string schema;
  std::string version;
  std::string sha256;
};
struct TerminationExpectation {
  std::string kind;
  std::optional<std::string> reason;
};
struct RuntimeLimits {
  std::optional<uint64_t> deadlockWindow;
  std::optional<uint64_t> maxTicks;
  std::map<std::string, uint64_t> maxDomainCycles;
};
struct RunManifest {
  HarnessFileHash buildManifest;
  TraceIdentity trace;
  uint64_t seed;
  std::string outputDirectory;
  RuntimeLimits limits;
  std::string statsFormat;
  std::string eventLog;
  TerminationExpectation expectation;
};
enum class RunStatus { Completed, Incomplete, Failed };
struct TracePosition {
  uint64_t nextRecordIndex;
  std::optional<uint64_t> lastCommittedSequenceId;
};
struct ValidationResult {
  std::string status;
  std::optional<std::string> reportSha256;
};
struct RunResultDocument {
  RunStatus status;
  std::string terminationReason;
  uint64_t simulatedTicks;
  std::map<std::string, uint64_t> domainCycles;
  uint64_t eventCount;
  TracePosition tracePosition;
  std::vector<FileHash> outputs;
  ValidationResult validation;
};

template <typename Model>
llvm::Expected<RunResultDocument>
runGeneratedModel(Model &model, const RunManifest &manifest,
                  llvm::StringRef resultStage)
requires requires(Model &value, const RuntimeLimits &limits) {
  value.configure(limits);
  { value.run() } -> std::same_as<TerminationResult>;
};
```

Parse with LLVM JSON and require exact fields, constants, uint64 ranges,
normalized paths, known domain names, and schema-semantic status/reason rules.

- [ ] **Step 4: Preserve time-domain runtime metadata in canonical ACSim**

Attach exact `period`, `phase`, `tick_scale`, optional parent, and bridge
attributes to `acsim.type` when `kind = "time_domain"`; update its verifier,
normative lowering spec, coverage manifest, conversion tests, and model-plan
extraction. Generate a sorted `constexpr std::array<TimeDomainRuntime, N>`.

```cpp
struct TimeDomainPlan {
  std::string name;
  uint64_t period;
  uint64_t phase;
  uint64_t tickScale;
};
```

- [ ] **Step 5: Implement typed limits and termination classification**

Add `SimSystem::setDeadlockWindow`, `setTimeDomains`, and
`setMaxDomainCycles`. Count a domain cycle only at a committed global tick
`phase + n * period`; stop before work beyond the declared cycle cap. Reset the
deadlock counter on committed events, transfers, trace advancement, or declared
progress; classify exact cap/deadlock reasons into the run-result enums.

```cpp
void Model::configure(const gfsim::RuntimeLimits &limits) {
  system_.setRuntimeLimits(limits);
  system_.setTimeDomains(kTimeDomains);
}
gfsim::TerminationResult Model::run() { return system_.run(); }
```

- [ ] **Step 6: Implement cold-path preflight and atomic result publication**

Verify build manifest hash and embedded fingerprint, trace hash/schema/epoch,
output containment, limits, and expectation before `Model::run`. Write canonical
stats/events/validation files, hash them, validate the result document, then
atomically publish. Generated `main` accepts only no arguments,
`--build-fingerprint`, or `--run-manifest PATH --run-result-stage PATH`.

- [ ] **Step 7: Run runtime, generated-model, and forbidden dependency tests**

Run: `cmake --build --preset dev-llvm22 --target GfsimTests CodeGenTests acir-cxxgen && build/dev-llvm22/bin/GfsimTests --gtest_filter='RuntimeHarnessTest.*:SimSystem*' && build/dev-llvm22/bin/CodeGenTests --gtest_filter='GeneratorTest.*Main*:GeneratedModelRuntimeTest.*' && lit -v build/dev-llvm22/test/CodeGen`

Expected: PASS; generated executables have no Python dependency and JSON work
occurs only in harness pre/postflight.

- [ ] **Step 8: Commit runtime harness closure**

```bash
git add include/gfsim lib/gfsim include/acir/CodeGen lib/CodeGen lib/Conversion docs/specs contracts unittests
git commit -m "feat(runtime): execute immutable run manifests"
```

---

### Task 9: `run`, replay, results, and exact exit mapping

**Files:**
- Create: `src/agentic_circuit/_run.py`
- Create: `src/agentic_circuit/_commands/run.py`
- Create: `tests/cli/test_run_command.py`
- Create: `tests/cli/fixtures/run/trace.json`
- Create: `tests/cli/fixtures/run/architecture.py`
- Test fixture: `schemas/run-manifest.schema.json`
- Test fixture: `schemas/run-result.schema.json`

**Interfaces:**
- Consumes: build command, runtime harness executable, staging, run schemas, and output policy.
- Produces: `RunOptions`, `RunPublication`, `create_run_manifest`, `execute_run`, `replay_run`, and `run_command`.

- [ ] **Step 1: Write failing complete/incomplete/failure/replay tests**

```python
class RunCommandTest(unittest.TestCase):
    def test_completed_run_publishes_exact_documents(self) -> None:
        result = run_cli("run", "architecture.py", "--trace", "trace.json",
                         "--output-dir", "runs/one", "--seed", "1",
                         "--expect-termination", cwd=RUN)
        self.assertEqual(0, result.returncode)
        validate_schema("run-manifest.schema.json", load_json("runs/one/run-manifest.json"))
        validate_schema("run-result.schema.json", load_json("runs/one/run-result.json"))

    def test_tick_cap_is_incomplete_exit_seven(self) -> None:
        result = run_cli("run", "architecture.py", "--trace", "trace.json",
                         "--max-ticks", "1", "--output-dir", "runs/capped", cwd=RUN)
        self.assertEqual(7, result.returncode)
        self.assertEqual("incomplete", load_json("runs/capped/run-result.json")["status"])

    def test_replay_rejects_ambient_override(self) -> None:
        result = run_cli("run", "--replay-manifest", "runs/one/run-manifest.json",
                         "--seed", "2", cwd=RUN)
        self.assertEqual(2, result.returncode)
```

- [ ] **Step 2: Run the focused test and confirm the RED state**

Run: `PYTHONPATH=src:build/dev-llvm22/python .venv/bin/python -m unittest tests.cli.test_run_command -v`

Expected: FAIL because `run` is absent.

- [ ] **Step 3: Normalize exact run inputs and canonical manifest**

```python
@dataclass(frozen=True, slots=True)
class RunOptions:
    trace: Path
    output_directory: Path
    seed: int
    deadlock_window: int | None
    max_ticks: int | None
    max_domain_cycles: tuple[tuple[str, int], ...]
    stats_format: Literal["json"]
    event_log: Literal["disabled", "jsonl"]
    termination_kind: Literal["complete", "incomplete", "any"]

@dataclass(frozen=True, slots=True)
class RunPublication:
    directory: Path
    manifest: Path
    result: Path
    status: Literal["completed", "incomplete", "failed"]
    termination_reason: str
    exit_code: ExitCode

def create_run_manifest(build: BuildPublication,
                        options: RunOptions) -> bytes:
    document = exact_run_manifest_document(build, options)
    validate_run_manifest(document)
    return canonical_json_bytes(document) + b"\n"
```

Validate positive bounds, uint64 seed, unique domain entries, exact formats,
trace schema/hash, output containment, and expectation/reason consistency.
Accept exact controls `--deadlock-window N`, `--max-ticks N`, repeatable
`--max-domain-cycles DOMAIN=N`, `--expect-termination`, `--stats-format json`,
`--event-log jsonl`, and `--replay-manifest PATH`; replay rejects every runtime
override other than the output destination allowed by the spec.

- [ ] **Step 4: Implement end-to-end run and replay**

```python
def execute_run(publication: BuildPublication, options: RunOptions) -> RunPublication:
    with ArtifactStage(options.output_directory) as stage:
        manifest = create_run_manifest(publication, options)
        write_exclusive(stage.path / "run-manifest.json", manifest)
        completed = subprocess.run([
            publication.executable,
            "--run-manifest", os.fspath(stage.path / "run-manifest.json"),
            "--run-result-stage", os.fspath(stage.path),
        ], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
           stderr=subprocess.PIPE, check=False)
        result = verify_run_result(stage.path, manifest, completed.returncode)
        stage.commit()
        return result
```

Replay verifies the immutable manifest/build/executable and uses no architecture
source or ambient runtime override. Map preflight/trace to `5`, runtime failure
to `6`, caps to `7`, interruption to `130`, and completed success to `0`.

- [ ] **Step 5: Run run/replay, schema, and failure-atomicity tests**

Run: `PYTHONPATH=src:build/dev-llvm22/python .venv/bin/python -m unittest tests.cli.test_run_command -v && .venv/bin/python -m unittest tests.contracts.test_contracts -v`

Expected: PASS.

- [ ] **Step 6: Commit run orchestration**

```bash
git add src/agentic_circuit tests/cli
git commit -m "feat(cli): run and replay immutable manifests"
```

---

### Task 10: Read-only `inspect`

**Files:**
- Create: `src/agentic_circuit/_commands/inspect.py`
- Create: `src/agentic_circuit/_inspect.py`
- Create: `tests/cli/test_inspect_command.py`
- Create: `tests/cli/fixtures/inspect/architecture.py`

**Interfaces:**
- Consumes: ACPy, canonical ACIR, build artifacts/manifests, and workspace selection.
- Produces: `InspectionKind`, `InspectionRequest`, `InspectionResult`, JSON/DOT graph renderers, and `inspect_command`.

- [ ] **Step 1: Write failing inspection-kind and path tests**

```python
class InspectCommandTest(unittest.TestCase):
    def test_every_exact_view_is_machine_readable(self) -> None:
        for kind in ("graph", "hierarchy", "ports", "resources", "address-map",
                     "protocols", "specialization", "artifacts"):
            result = run_cli("inspect", kind, "--json", cwd=INSPECT)
            self.assertEqual(0, result.returncode, kind)
            self.assertEqual(kind, json.loads(result.stdout)["kind"])

    def test_hierarchy_path_is_canonical_and_unknown_path_is_diagnostic(self) -> None:
        result = run_cli("inspect", "ports", "--path", "chip.cluster[0]", "--json",
                         cwd=INSPECT)
        self.assertEqual("chip.cluster[0]", json.loads(result.stdout)["path"])
        self.assertEqual(2, run_cli("inspect", "ports", "--path", "missing").returncode)
```

- [ ] **Step 2: Run the focused test and confirm the RED state**

Run: `PYTHONPATH=src:build/dev-llvm22/python .venv/bin/python -m unittest tests.cli.test_inspect_command -v`

Expected: FAIL because inspect is absent.

- [ ] **Step 3: Implement one immutable inspection model**

```python
InspectionKind = Literal[
    "graph", "hierarchy", "ports", "resources", "address-map",
    "protocols", "specialization", "artifacts",
]

@dataclass(frozen=True, slots=True)
class InspectionResult:
    kind: InspectionKind
    system: str
    path: str | None
    records: tuple[Mapping[str, JsonValue], ...]
```

Derive frontend views from verified ACPy/normalized semantics and artifact views
from verified immutable manifests. Never infer from console output or mutate a
build/run.

- [ ] **Step 4: Implement exact renderers and Graphviz DOT**

Sort graph nodes by canonical hierarchy path and edges by source/target/port.
JSON is normative. DOT quotes identifiers and contains no host path or timestamp.
Human hierarchy preserves canonical indexed object paths.

- [ ] **Step 5: Run inspection and frontend determinism tests**

Run: `PYTHONPATH=src:build/dev-llvm22/python .venv/bin/python -m unittest tests.cli.test_inspect_command tests.python_frontend.test_determinism -v`

Expected: PASS.

- [ ] **Step 6: Commit inspection**

```bash
git add src/agentic_circuit tests/cli
git commit -m "feat(cli): add deterministic architecture inspection"
```

---

### Task 11: Black-box command, installation, determinism, and CI gates

**Files:**
- Modify: `.github/workflows/ci.yml`
- Modify: `CMakeLists.txt`
- Modify: `cmake/AgenticCircuitConfig.cmake.in`
- Create: `tests/cli/test_all_commands.py`
- Create: `tests/cli/test_installation.py`
- Create: `tests/cli/test_determinism.py`
- Modify: `tests/install-consumer/CMakeLists.txt`

**Interfaces:**
- Consumes: all Phase 4A/4B public and installation surfaces.
- Produces: installed command/package/native extension and complete success/error/determinism/machine-output gate.

- [ ] **Step 1: Add failing complete command ledger tests**

```python
class AllCommandsTest(unittest.TestCase):
    def test_every_command_has_required_behavior_classes(self) -> None:
        ledger = cli_test_ledger()
        self.assertEqual(EXACT_COMMANDS, set(ledger))
        for row in ledger.values():
            self.assertTrue(row.success)
            self.assertTrue(row.error)
            self.assertTrue(row.determinism)
            self.assertTrue(row.machine_readable)

    def test_installed_prefix_runs_without_source_checkout(self) -> None:
        prefix = install_to_temporary_prefix()
        result = run_installed(prefix, "agentic-circuit", "doctor", "--json")
        self.assertEqual(0, result.returncode)
```

- [ ] **Step 2: Run ledger/install tests and confirm the RED state**

Run: `PYTHONPATH=src:build/dev-llvm22/python .venv/bin/python -m unittest tests.cli.test_all_commands tests.cli.test_installation -v`

Expected: FAIL because the complete ledger/install path is absent.

- [ ] **Step 3: Finish CMake install and source-independent resource lookup**

Install the Python package, `_native` module, schemas, stdlib catalog,
diagnostic catalog, templates, and executable launcher under one relocatable
prefix. Resolve resources with `importlib.resources`, never repository-relative
paths. Extend the external install consumer to run `schema capabilities` and a
minimal `check`.

- [ ] **Step 4: Add complete CLI and Python-version CI matrices**

```yaml
- name: Run public CLI tests
  env:
    PYTHONPATH: src:${{ github.workspace }}/build/dev-llvm22/python
  run: |
    python -m unittest discover -s tests/cli -v
    python -m unittest discover -s tests/python_frontend -v
```

Add Python 3.11/3.12/3.13 pure frontend/CLI parser jobs and native integration
on the locked primary interpreter. Retain Debug, Release, ASan, UBSan,
clang-format, clang-tidy, contract, lit, and install-consumer gates.

- [ ] **Step 5: Run complete development, release, and install checks**

Run: `PYTHONPATH=src:build/dev-llvm22/python .venv/bin/python -m unittest discover -s tests/cli -v && PYTHONPATH=src .venv/bin/python -m unittest discover -s tests/python_frontend -v && cmake --build --preset dev-llvm22 && ctest --test-dir build/dev-llvm22 --output-on-failure && cmake --build --preset dev-llvm22 --target check-acir && cmake --build --preset release-llvm22 && ctest --test-dir build/release-llvm22 --output-on-failure && cmake --build --preset release-llvm22 --target check-acir`

Expected: PASS.

- [ ] **Step 6: Commit public installation and CI gates**

```bash
git add .github CMakeLists.txt cmake tests
git commit -m "test(cli): gate commands installation and determinism"
```

---

### Task 12: Combined Phase 4 audit and plan closure

**Files:**
- Create: `docs/implementation/phase-4-audit.md`
- Modify: `docs/superpowers/plans/2026-08-11-phase-4a-python-frontend.md`
- Modify: `docs/superpowers/plans/2026-08-11-phase-4b-agent-cli-runtime.md`
- Modify: `docs/superpowers/specs/2026-08-11-phase-4-python-cli-design.md`

**Interfaces:**
- Consumes: Phase 4A audit, all Phase 4B tests, schemas, manifests, and commits.
- Produces: complete roadmap exit evidence and reviewed plan checkboxes.

- [ ] **Step 1: Run the clean combined verification gate**

Run: `.venv/bin/python -m unittest tests.contracts.test_contracts -v && .venv/bin/python scripts/check-contracts.py && PYTHONPATH=src .venv/bin/python -m unittest discover -s tests/python_frontend -v && PYTHONPATH=src:build/dev-llvm22/python .venv/bin/python -m unittest discover -s tests/cli -v && cmake --build --preset dev-llvm22 && ctest --test-dir build/dev-llvm22 --output-on-failure && cmake --build --preset dev-llvm22 --target check-acir && cmake --build --preset release-llvm22 && ctest --test-dir build/release-llvm22 --output-on-failure && cmake --build --preset release-llvm22 --target check-acir && git diff --check`

Expected: every command passes with no skipped required contract.

- [ ] **Step 2: Run sanitizer and forbidden-dependency gates**

Run: `cmake --build --preset asan-llvm22 && ctest --test-dir build/asan-llvm22 --output-on-failure && cmake --build --preset ubsan-llvm22 && ctest --test-dir build/ubsan-llvm22 --output-on-failure && lit -v build/dev-llvm22/test/CodeGen`

Expected: PASS; generated simulator dependency scans find no Python runtime,
dynamic plugin loader, descriptor interpreter, schema walker, coroutine,
`std::function` process frame, or hot-path RTTI.

- [ ] **Step 3: Verify exact public coverage and canonical samples**

Generate the coverage table mapping every public API, ACPy entity kind,
diagnostic class, command, option family, schema property, exit code, manifest,
and security rule to implementation/test/commit evidence. Produce two identical
builds and runs from equivalent roots, recursively compare canonical artifacts,
and record representative ACPy, ACIR, ACSim, build-manifest, run-manifest, and
run-result hashes.

- [ ] **Step 4: Write the combined audit and close checkboxes**

Record environment, reviewed commit ranges, exact counts, profile/stage gates,
failure atomicity, cache behavior, process closure, install evidence,
Python-free runtime proof, replay proof, and residual risks that do not leave a
public contract broken. Change the design status from approved for implementation to
implemented only after evidence is complete.

- [ ] **Step 5: Commit the audit**

```bash
git add docs/implementation/phase-4-audit.md docs/superpowers/plans docs/superpowers/specs/2026-08-11-phase-4-python-cli-design.md
git commit -m "docs(audit): complete phase 4 python and cli verification"
```

---

## Phase 4B Final Verification

```bash
.venv/bin/python -m unittest tests.contracts.test_contracts -v
.venv/bin/python scripts/check-contracts.py
PYTHONPATH=src .venv/bin/python -m unittest discover -s tests/python_frontend -v
PYTHONPATH=src:build/dev-llvm22/python .venv/bin/python -m unittest discover -s tests/cli -v
cmake --build --preset dev-llvm22
ctest --test-dir build/dev-llvm22 --output-on-failure
cmake --build --preset dev-llvm22 --target check-acir
cmake --build --preset release-llvm22
ctest --test-dir build/release-llvm22 --output-on-failure
cmake --build --preset release-llvm22 --target check-acir
git diff --check
```

Expected: every command passes. A clean installed prefix can discover schemas,
check/elaborate an architecture, compile/build it, run/replay a PTO trace, and
inspect schema-valid artifacts without the source checkout. The generated model
starts after Python semantic execution has ended and links no Python runtime.
