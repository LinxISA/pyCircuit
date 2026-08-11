# Phase 4 ACPy, Python Frontend, and Agent-First CLI Design

**Status:** Approved for implementation

**Date:** 2026-08-11

**Contract epoch:** `0.1`

## 1. Purpose

Phase 4 makes the contracts in
[`python-to-acir-lowering-v0.1.md`](../../specs/python-to-acir-lowering-v0.1.md)
and
[`agentic-python-cli-v0.1.md`](../../specs/agentic-python-cli-v0.1.md)
executable. It adds the public Python construction surface, deterministic ACPy
frontend, ACPy-to-ACIR lowering, and the public `agentic-circuit` command while
composing the ACIR, ACSim, binding, code-generation, and gfsim work completed in
Phases 1 through 3.

The Phase 4 exit gate remains the roadmap gate: every specified public Python
API and CLI command has positive, negative, determinism, and machine-readable
output coverage, and Python execution ends before simulator runtime begins.

This design divides delivery into two sequential tracks without narrowing that
gate:

- **Phase 4A — frontend and lowering:** Python APIs, source capture, supported
  Python validation, ACPy, semantic analysis, process construction, and
  deterministic ACIR emission;
- **Phase 4B — CLI and orchestration:** the command surface, native compiler
  composition, capabilities, diagnostics, immutable artifacts, build/run
  orchestration, and runtime result publication.

Phase 4A must pass its own review and integration gate before Phase 4B builds on
it. Phase 4 is complete only when both tracks and the combined exit audit pass.

## 2. Normative authority and invariants

The normative specifications and existing schemas remain authoritative. This
design chooses implementation boundaries; it does not rename public APIs,
weaken verifiers, add aliases, or define a second architecture interchange
format.

The implementation preserves these invariants:

- Python syntax is captured from the AST. Operator overloading or executed
  proxy behavior alone is not an accepted source representation.
- Static values and symbolic architecture values remain disjoint. Symbolic
  values cannot participate in Python truthiness, hashing, integer conversion,
  iteration, or other prohibited coercions.
- ACPy is the closed source-oriented inspection IR defined by
  `schemas/acpy.schema.json`; it is not a replacement for portable ACIR.
- Frozen ACIR and verified canonical ACSim remain required compiler stages.
  Generated C++ never bypasses ACSim.
- The Phase 3 library is the supported build surface. The internal
  `acir-cxxgen` executable does not become a public CLI dependency.
- Generated simulators contain no Python dependency, Python interpreter,
  descriptor interpreter, schema walker, dynamic topology service, or runtime
  plugin lookup.
- Runtime options do not specialize or rebuild the model. Static build profile
  and instrumentation choices remain build inputs.
- User Python execution is trusted project execution, not a sandbox. The CLI
  states that boundary accurately.
- All subprocesses use argument vectors. No command constructs a shell string
  from project data.
- Artifact publication is staged, verified, immutable where required, and
  atomic at the selected-output pointer.

## 3. Current baseline and required closure

The repository already provides:

- verified ACIR and ACSim dialects and pass pipelines;
- process-state planning and deterministic ACIR-to-ACSim lowering;
- gfsim scheduling, trace streaming, validation, and termination accounting;
- schema-driven component binding and the baseline standard library;
- deterministic typed C++ generation, same-toolchain builds, build manifests,
  cache validation, and immutable build publication;
- exact public schemas for ACPy, diagnostics, capabilities, build manifests,
  run manifests, and run results.

Phase 4 must close four missing composition surfaces:

1. `pyproject.toml` declares project metadata but there is no installable
   `agentic_circuit` Python package or public command entry point.
2. There is no deterministic Python AST capture, ACPy builder, or ACPy-to-ACIR
   frontend.
3. The C++ compiler libraries have no single high-level façade suitable for a
   thin Python-native binding.
4. Generated model executables currently support ordinary execution and
   `--build-fingerprint`, but not committed run-manifest input and complete
   schema-valid run-result output.

The Phase 3 audit also records a process integration risk: canonical ACSim can
contain multi-block process control flow and helper-rich process operations
that the current model-plan extractor does not reconstruct generically. Phase
4 may not expose a public `@process` surface that produces an unbuildable model.
The frontend, ACIR-to-ACSim lowering, and model-plan extraction therefore close
this boundary for every process form admitted by the Phase 4 Python contract.
Phase 5 retains large end-to-end examples and the NPU showcase; it does not
retain a known public Phase 4 process correctness gap.

## 4. Chosen architecture

The public command is implemented in Python and uses a thin private CPython
extension over a reusable C++ compiler façade:

```text
architecture.py
  -> isolated trusted capture worker
  -> verified immutable ACPy
  -> deterministic ACPy-to-ACIR lowering
  -> thin native extension
  -> C++ compiler façade
  -> frozen ACIR -> canonical ACSim -> generated C++
  -> same-toolchain build
  -> manifest-driven generated simulator
```

Python owns the concerns for which Python has the strongest semantic context:
source discovery, AST validation, source locations, public decorators and
types, static evaluation, ACPy construction, workspace configuration, command
presentation, and frontend inspection.

C++ owns the established compiler and runtime boundaries: MLIR parsing,
dialect verification, pass execution, binding resolution, ACIR-to-ACSim
lowering, source generation, native compilation, and gfsim runtime setup.

The native extension is a transport boundary, not a second compiler. It does
not reimplement passes, invent an intermediate JSON construction schema, or
expose MLIR objects to public Python callers. It transfers canonical byte
artifacts and closed typed options to the compiler façade and returns artifacts
and schema-shaped diagnostics.

### 4.1 Alternatives not selected

**Python orchestration over `acir-opt` and `acir-cxxgen` subprocesses** would be
quick to bootstrap, but would turn internal test drivers into accidental public
interfaces, duplicate option validation, and make stable diagnostic ordering
dependent on parsing tool output.

**A C++ CLI embedding Python** would call the compiler libraries directly, but
would make AST ownership, Python packaging, interpreter lifecycle, source
diagnostics, and frontend unit testing more complex. It would also couple every
schema-only and inspection command to a native executable.

**MLIR Python bindings as a required dependency** are not selected. The
frontend can emit deterministic textual ACIR from its typed semantic IR and use
the repository's native parser and verifier as the acceptance boundary. This
keeps the public package dependency-free at runtime and prevents a second MLIR
version from entering the toolchain identity.

## 5. Python package and public surface

The package uses a conventional `src/agentic_circuit` layout and a
`project.scripts` entry point for `agentic-circuit`. Public imports are exported
only from the documented package surface. Implementation modules use a leading
underscore and are excluded from compatibility promises.

The package exposes every exact public name defined by the Python/CLI spec:

- architecture decorators including `system`, `module`, `extern_module`,
  `generated_module`, `struct`, `packet`, `transaction`, `protocol`,
  `interface`, and `process`;
- strong scope construction through `scope`;
- schema-generated component callables;
- collection helpers `array`, `instances`, and `view`;
- queue and resource helpers including `queue`, `ResourceRef`,
  `address_space`, and `address_map`;
- the specified static, flow, endpoint, protocol, role, and result types.

Decorators register immutable definition metadata and return callable objects
that reject undocumented parameters. They do not lower architecture by
executing a graph-builder DSL. The AST remains the authority for statements,
assignments, scopes, calls, control flow, and source locations; executed
decorator metadata supplies resolved Python objects and approved static values.

Public objects have deterministic representations that omit memory addresses,
temporary paths, hash-randomized ordering, and interpreter-specific object
identities.

## 6. Trusted capture worker

CLI commands that accept Python source launch a fresh capture worker using the
selected Python interpreter and an argument vector. Fresh-process capture
prevents one architecture import from contaminating another through module
caches, registries, logging handlers, or process-global state.

The worker:

1. resolves the workspace and normalizes the requested entry path;
2. reads and hashes source bytes before execution;
3. parses the source with `ast.parse` and builds a qualified-definition index;
4. imports the public frontend package before exposing the workspace import
   root;
5. executes the explicitly requested trusted project entry;
6. associates registered definitions with indexed AST nodes and source spans;
7. performs frontend analysis and writes canonical ACPy into a private stage;
8. writes bounded captured project stdout/stderr to a report rather than
   allowing it to corrupt structured CLI stdout;
9. returns only verified ACPy and ordered diagnostics to the parent command.

Interpreter isolation constrains accidental ambient inputs but is not described
as a security sandbox. The worker deliberately permits trusted project imports,
filesystem access, environment access, and network access unless a future
contract epoch defines a sandbox mode.

Source identities use normalized workspace-relative POSIX paths and exact
SHA-256 hashes. Symlink and path-escape checks occur before execution. The
frontend never embeds an absolute workspace path in canonical ACPy, ACIR, a
fingerprint preimage, or a machine-readable diagnostic source path.

## 7. Supported-Python validation and static evaluation

Validation is syntax-directed and runs before semantic lowering. Each AST node
is classified as supported, statically evaluable, symbolic, or forbidden in
its current context. Unsupported syntax produces an `ACPY-*` diagnostic at the
smallest complete source span; it is never silently executed and ignored.

Static evaluation uses a closed AST interpreter. It does not call `eval`,
`exec`, arbitrary descriptors, implicit conversions, or user-defined numeric
operators. The evaluator admits only the literals, containers, names,
attributes, arithmetic, comparisons, approved helper calls, and bounded static
control flow named by the normative subset. Results are recursively converted
to the closed I-JSON/static value domain before entering ACPy.

Every loop bound, collection shape, specialization argument, generated name,
and static branch decision is evaluated before topology construction. A value
that depends on a `Flow`, `Endpoint`, `ResourceRef`, symbolic result, or runtime
state is rejected as non-static rather than coerced.

Diagnostics distinguish:

- unsupported syntax;
- permitted syntax with a non-static operand;
- an invalid static value type or range;
- use of a symbolic value in a prohibited Python context;
- a static expression whose approved evaluation raises an error.

## 8. ACPy semantic IR

The in-memory ACPy representation is a closed set of frozen typed records whose
serialized form matches `schemas/acpy.schema.json` exactly. Its public entity
inventory remains:

`system`, `module`, `scope`, `arg`, `call`, `result`, `get_result`, `bind`,
`static_if`, `static_for`, `collection`, `get_static`, `return`, `capture`,
`escape`, and `process`.

Construction is two-pass:

1. source and definition discovery establishes lexical scopes, annotations,
   schema identities, and stable definition keys;
2. semantic construction normalizes assignments and uses, resolves calls,
   outlines strong scopes, expands collections, and assigns dense `eN` entity
   identifiers in canonical source-and-semantic order.

Entity identifiers never depend on Python allocation order or dictionary/hash
iteration. Semantically unordered inputs are sorted by their normative stable
keys before identifier assignment. Semantically ordered statements, function
arguments, results, and collection elements preserve source order.

ACPy verification checks at least:

- exact entity inventory and dense unique identifiers;
- parent, definition, use, and entry references;
- normalized assignment and source-name versions;
- type/category consistency across static and symbolic values;
- schema identity and fingerprint resolution;
- call argument, port, result, protocol, and role compatibility;
- stable explicit and inferred instance names;
- collection shape and element consistency;
- capture/escape ordering and strong-scope signatures;
- flow linearity and explicit fan-in/fan-out requirements;
- process effects, suspension legality, and supported control flow.

Canonical serialization uses the repository's RFC 8785/I-JSON rules. A shared
cross-language corpus proves that the Python and C++ canonical JSON and
fingerprint implementations agree byte for byte.

## 9. Semantic normalization and scope outlining

The frontend normalizes ordinary assignment-and-call Python into explicit
semantic operations before ACIR emission:

- each source assignment receives a stable SSA version;
- tuple and named results become explicit `result`/`get_result` relationships;
- component calls resolve against exact schema identities and approved static
  specializations;
- argument-to-port and result-to-port mappings are recorded explicitly;
- list comprehensions and approved static loops become canonical collections;
- homogeneous rectangular collections select `ac.array`; heterogeneous or
  schema-distinct collections select `ac.instances`;
- direct symbolic producer/consumer relationships become explicit `bind`
  entities without a user-visible connection builder.

A `with scope(name):` region is a strong scope. Capture and escape analysis
computes the minimal ordered signature from free symbolic variables and values
used outside the scope. The outlined scope becomes a real nested module and
instance with canonical ownership. Static values needed for specialization are
recorded as specialization inputs rather than runtime ports.

Naming precedence is exact and deterministic:

1. an explicit valid `name=` argument;
2. a stable assignment target when the call has one result binding;
3. a schema-derived base name plus a source-ordered collision suffix.

Names derived from dynamic string formatting, unordered containers, object
representations, or non-static expressions are rejected.

## 10. ACPy-to-ACIR lowering

Lowering consumes only verified ACPy. It is a deterministic emitter over typed
semantic records, not a second Python analysis pass.

The emitter creates:

- exact ACIR file and epoch markers;
- system, module, type, protocol, interface, resource, address, workload,
  instrumentation, and result-schema declarations;
- Graph regions with binding SSA;
- nested module definitions and instances for strong scopes;
- exact component schema references and specialization attributes;
- `ac.array` or `ac.instances` according to the canonical collection decision;
- process regions in the supported ACIR structured-control subset;
- normalized source locations and hierarchy identities.

The frontend never emits `ac.connect`; ordinary Python value flow lowers to
Graph-region SSA and explicit ACIR bindings. It also never fabricates a port or
result when schema-driven inference is ambiguous.

Textual ACIR is formatted canonically by the frontend and then parsed and
verified by the native compiler façade. Successful parsing is not sufficient:
the same ACIR verification and normalization gates used for hand-authored ACIR
must pass before the artifact is returned or fingerprinted.

`check` stops after all frontend and ACIR validations that do not require
simulator lowering or C++ compilation. `elaborate` may publish verified ACPy or
canonical ACIR. Neither command requires a C++ compiler.

## 11. Process construction closure

The Python process subset lowers to the existing effect-aware ACIR process
model and closed enum-PC planning contract. Suspension points, live slots,
wakes, cursor state, resource effects, and fairness costs remain explicit
compiler facts; Python generators, coroutines, polling loops, and dynamic
frames are not runtime mechanisms.

Phase 4 adds a lossless compiler path for every process operation and control
shape admitted by the Python frontend:

1. the frontend emits verified structured ACIR;
2. process-state planning constructs the closed state machine;
3. ACIR-to-ACSim lowering preserves every required typed operation, helper,
   edge, live slot, wake, and PC transition in canonical ACSim;
4. the Phase 3 model-plan extractor reconstructs the same closed plan;
5. generated C++ remains a typed enum-PC class with no descriptor interpreter.

This closure is implemented in the dialect and compiler contracts, not as an
opaque JSON attribute or a side file consumed by code generation. If the
lossless representation requires an ACSim operation or attribute not already
specified, the same change updates the normative ACSim spec, TableGen dialect,
verifier, round-trip tests, coverage manifest, lowering, extraction, and
generated-code tests. Unknown or incomplete process representation fails at
compile time with `ACLOWER-*`; it never degrades to a runtime helper.

## 12. Native compiler façade and Python extension

A reusable installed C++ façade composes the existing libraries behind closed
typed requests. It owns one MLIR context setup, dialect/pass registration,
diagnostic capture, and the legal stage transitions from textual ACIR through
build publication.

The façade supports these logical operations:

- parse and verify canonical ACIR;
- normalize and freeze ACIR;
- lower and verify canonical ACSim;
- inspect registered passes and compiler/runtime capabilities;
- emit selected compiler artifacts and stage dumps;
- resolve bindings and invoke `buildGeneratedModel`;
- return ordered structured diagnostics and artifact identities.

Requests contain explicit bytes, normalized paths, selected system, profile,
pipeline, toolchain identity, provider inputs, binding lock, requested emits,
and output root. They do not read semantic defaults from the process working
directory or environment.

The private `_native` extension is compiled against the CPython 3.11 limited
API and imports as part of the installed package. It exposes byte-buffer and
closed-option functions only to internal package modules. It converts native
results into immutable Python records and raises no user-facing ad hoc exception
strings; failures return structured diagnostics that the common CLI policy
maps to output and exit status.

The extension does not expose C++ object ownership, MLIR handles, pass-manager
objects, or Phase 3 internal drivers. Tests can call the C++ façade directly
without Python and can exercise the extension boundary without launching a
command.

## 13. Public command behavior

The public `agentic-circuit` entry point implements the exact command inventory:

- `init` creates only the specified absent workspace files and refuses
  conflicting mutation;
- `schema` reads packaged versioned schemas and capability records without
  importing project Python;
- `check` performs isolated capture, ACPy verification, static elaboration,
  ACIR construction, and non-lowering validation;
- `elaborate` publishes canonical `acpy` or `acir`, with `acir` as the default;
- `compile` exposes the specified stage, pass-dump, verification, pipeline, and
  artifact-emission controls;
- `build` runs the complete verified compiler/build path and selects one of the
  exact `fast`, `validated`, or `custom` profiles;
- `run` performs incremental check, compile, build, runtime preflight,
  execution, and post-run validation, or replays an immutable run manifest;
- `inspect` provides the specified graph, hierarchy, port, resource,
  address-map, protocol, specialization, and artifact views;
- `explain` reads a versioned diagnostic catalog and reports rule, likely
  causes, examples, and safe repair guidance without mutating source;
- `doctor` performs read-only Python, MLIR, compiler, standard-library, gfsim,
  JSON, and epoch compatibility checks.

The parser rejects aliases, undocumented options, ambiguous output
destinations, repeated singleton options, malformed key/value bounds, and
command-inapplicable options. Help text and schema output are generated from
the same closed command metadata so documentation and accepted syntax cannot
drift.

`--json` emits exactly one JSON value on stdout. JSONL modes emit one complete
JSON object per line. Human diagnostics go to stderr. Project stdout/stderr,
compiler output, and simulator output are captured into bounded reports and
never corrupt structured stdout.

## 14. Diagnostics and exit status

Python and C++ use one logical diagnostic model matching
`schemas/diagnostic.schema.json`. A diagnostic contains the exact schema and
epoch identity, stable code, logical stage, severity, message, optional source
start location, optional object path, expected and actual values, related
locations, and safe repair messages in `fixits`.

Diagnostics are sorted by normalized source path, start position, object path,
code, and message. A diagnostic generated without a source position sorts
after positioned diagnostics in the same phase. Parallel or subprocess
completion order never affects presentation.

The top-level command maps the dominant failure class to the exact public exit
codes:

- `0`: success;
- `2`: invalid source, specialization input, run input, ACIR, or schema;
- `3`: internal compiler/tool failure;
- `4`: C++ generation, compilation, or linking failure;
- `5`: runtime preflight or trace failure;
- `6`: invariant, deadlock, or declared expectation failure;
- `7`: incomplete run due to a declared cap;
- `130`: interruption.

Warnings affect exit status only under `--warnings-as-errors`. An interruption
is handled at command and simulator boundaries so an incomplete immutable run
result can be committed when the runtime has enough validated state; otherwise
the prior published result remains unchanged.

Unexpected Python exceptions and native errors are converted at the boundary
to bounded `ACPY-*`, `ACLOWER-*`, or `ACBUILD-*` diagnostics with a stable public
message. Tracebacks and host details may be retained in a private debug report
but do not enter canonical outputs or fingerprints.

## 15. Workspace, configuration, and capabilities

Workspace discovery searches upward from the explicit entry or current path
for `agentic-circuit.toml`, stops at the filesystem root, and rejects ambiguous
nested ownership. TOML parsing uses `tomllib` and a closed typed configuration
model. Unknown sections and keys are errors. Command-line options override only
the fields the spec declares overridable; all effective values are recorded in
the relevant artifact or diagnostic report.

Schemas, the standard-library catalog, diagnostic explanations, and default
templates are installed as package resources. Commands do not depend on the
source checkout layout.

`schema capabilities --json` is assembled without executing architecture
Python. It joins:

- the exact global epoch and schema identities;
- the generated standard-library catalog and availability state;
- installed provider/component implementation fingerprints;
- supported policies, interfaces, protocols, and output formats;
- compiler and runtime build identifiers.

The output validates against `schemas/capabilities.schema.json`. Known but
unimplemented catalog entries use `declared_unavailable`; omission is not used
as an availability signal, and version ranges are not advertised.

## 16. Canonical artifacts, fingerprints, and publication

Frontend artifacts add source hashes, Python version, approved helper/provider
versions, static specialization inputs, and ACPy/ACIR hashes to the build
provenance required by the CLI spec. They do not alter the Phase 3 rule that
the final native build fingerprint is computed from frozen ACIR and the exact
compiler, pipeline, source-contract, component, provider, and static build
inputs.

PTO trace bytes, seed, tick/cycle bounds, deadlock window, event-log selection,
and termination expectation remain run inputs and never enter the build
fingerprint.

Each artifact-producing command creates a private sibling stage on the target
filesystem, validates all files and hashes, flushes required data, and commits
the complete destination through an atomic rename or immutable
fingerprint-addressed publication. It never edits a previously published
immutable manifest. Cache hits verify the manifest and every declared artifact
before reuse.

Canonical files use UTF-8, LF endings, deterministic final newlines where the
format permits them, normalized relative paths, sorted closed maps, and
RFC 8785/I-JSON serialization. Temporary nonces, timestamps, absolute paths,
process identifiers, and unordered iteration do not enter artifact bytes or
fingerprint preimages.

## 17. Runtime harness and replay

The generated executable keeps `--build-fingerprint` and adds a cold-path
runtime harness interface that consumes a verified run-manifest path and an
explicit run-result staging path. Manifest parsing and preflight occur before
the model runs. The harness validates the exact schema/epoch, build-manifest
hash, trace identity/hash, output directory, seed, limits, output formats, and
termination expectation.

Validated inputs are converted once into typed gfsim configuration. The hot
scheduler does not traverse JSON or manifests. Trace ownership, event and delta
caps, tick/domain bounds, deadlock detection, validation work, and termination
classification remain runtime-library behavior.

At termination the harness writes the required statistics, event log, and
validation artifacts into the private run stage, hashes them, constructs a
closed `run-result.json`, validates status/reason consistency, and atomically
publishes the result set. The parent Python command verifies the published
result and maps it to the public exit code; it does not reinterpret simulator
state from console text.

Replay accepts only an immutable `run-manifest.json`. It verifies the referenced
build manifest and exact executable fingerprint, ignores architecture source,
and applies no ambient runtime overrides. A replayed manifest therefore
reproduces the same committed run inputs even if the workspace configuration
has changed.

Python has exited the semantic pipeline before the generated simulator starts.
The simulator process links no Python library and imports no Python module.

## 18. Security and side-effect boundaries

The CLI separates three trust modes visibly:

- schema, capability, explanation, doctor, and ACIR-only operations do not
  execute project Python;
- Python architecture check/elaboration/build commands execute trusted project
  code and say so in help and machine-readable metadata;
- generated simulator execution consumes only verified native artifacts and
  manifest-declared inputs.

All file mutations are constrained to normalized, explicitly selected workspace
or output roots. Path traversal, symlink escape, absolute artifact paths, and
overlap between input and private staging targets are rejected before writes.
Cleanup removes only the exact private stage created by the active command.

External compilers and simulators receive argument vectors and a documented
environment. Captured output is bounded. The implementation never passes user
content through a shell, treats a build or run result as executable replay
input, or claims that trusted Python capture is safe for hostile code.

## 19. Verification strategy

Every implementation task follows red-green-refactor and records the observed
failing test in the implementation plan. Verification is layered so failures
identify the owning boundary.

### 19.1 Pure Python tests

Python `unittest` coverage exercises every public decorator, type, helper,
static expression, supported/unsupported AST form, naming rule, call inference,
collection rule, scope capture/escape case, process form, diagnostic, config
field, and public CLI parser branch.

Tests run with different hash seeds, workspace roots, source import orders, and
equivalent formatting to prove semantic and byte determinism where required.

### 19.2 Golden frontend tests

Small source fixtures produce canonical ACPy JSON and ACIR text goldens.
Negative fixtures assert exact diagnostic code and source span. Round-trip
tests validate ACPy against its schema and parse/verify every emitted ACIR file
with the native compiler.

The corpus includes ordinary assignment-and-call modules, strong nested scopes,
captures and escapes, homogeneous and heterogeneous collections, explicit
fan-in/fan-out components, protocols and roles, resources/address maps, and
suspended processes with nested supported control flow.

### 19.3 Native and runtime tests

C++ unit tests cover the compiler façade, diagnostic translation, process
representation closure, stage controls, capability identities, runtime-manifest
preflight, result classification, and failure atomicity. Generated-model tests
prove that no Python symbol or dependency is present and that the manifest
harness does not enter the hot dispatch path.

### 19.4 Black-box CLI tests

Every public command receives at least:

- a successful human-output invocation;
- a successful machine-readable invocation validated against its schema;
- invalid source/input/options with exact diagnostic and exit code;
- repeated equivalent invocations with byte-identical outputs;
- an interruption or injected boundary failure where applicable;
- a check that failed staging preserves the prior valid publication.

The run corpus distinguishes completed, incomplete, preflight-failed, and
runtime-failed outcomes and verifies exit codes `0`, `5`, `6`, `7`, and `130`.

### 19.5 Repository gates

CI adds Python package build/install tests, supported Python-version tests,
schema-resource installation checks, the frontend/CLI suite, native bridge
tests, and Phase 4 lit/golden tests. Existing Debug, Release, sanitizer,
clang-format, clang-tidy, contract, coverage-ledger, install-consumer, and
determinism gates continue to pass. Main never contains a required skipped or
known-failing Phase 4 contract.

## 20. Delivery and review gates

Phase 4A ends when all exact public Python construction APIs are importable,
the supported subset produces verified deterministic ACPy and ACIR, every
frontend diagnostic carries the required source evidence, all frontend-emitted
processes cross the ACSim/code-generation boundary, and `check`/`elaborate`
work through their library surfaces.

Phase 4B ends when all ten commands, exact exit codes, capabilities, profiles,
stage controls, diagnostics, immutable artifacts, build caching, manifest-driven
runtime, replay, and security statements meet their public contracts.

The combined Phase 4 audit maps every Python API, ACPy entity kind, diagnostic
class, command, option group, schema property, exit code, artifact, and security
requirement to implementation symbols, positive and negative tests,
determinism evidence, and first implementing commits.

Only after that audit passes is the Phase 4 branch merged to `main`. Phase 5
then adds the larger end-to-end model corpus, PTO-driven architecture examples,
swimlane output, and the hierarchical NPU showcase on top of a closed public
frontend and CLI.

## 21. Acceptance summary

This design is satisfied when an agent can, using only documented surfaces:

1. discover exact available schemas and components without executing project
   Python;
2. author and validate the complete specified Python architecture subset;
3. inspect deterministic ACPy inference and canonical ACIR;
4. compile through verified frozen ACIR and canonical ACSim;
5. build a typed, Python-free gfsim executable through the Phase 3 library;
6. run or replay a validated PTO trace from immutable manifest inputs;
7. consume schema-valid diagnostics, manifests, results, statistics, and event
   logs;
8. distinguish source, compiler, build, preflight, runtime, incomplete, and
   interrupted outcomes from exact diagnostics and exit status;
9. repeat equivalent operations with byte-identical canonical artifacts;
10. observe no known unsupported path within the advertised Phase 4 public
    surface.
