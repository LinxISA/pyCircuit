# Agentic Python and CLI v0.1 Specification

| Field | Value |
| --- | --- |
| Specification | Agent-facing Python frontend and command-line interface |
| Version | 0.1 |
| Status | Draft for review |
| Primary command | `agentic-circuit` |
| Optional short alias | `acirc` |
| Global contract epoch | `0.1` |

## Purpose

The primary author of an Agentic Circuit model may be an automated coding
agent. The frontend and CLI therefore optimize for discovery, deterministic
automation, structured diagnostics, fast validation, repair, and reproducible
execution.

Human-readable output is useful, but machine-readable behavior is normative.

This specification consumes [ACIR Core v0.1](acir-core-v0.1.md), the
[Python-to-ACIR Lowering v0.1](python-to-acir-lowering-v0.1.md), the
[ACIR Standard Library v0.1](acir-stdlib-v0.1.md), the
[gfsim Model Library Contract v0.1](gfsim-runtime-abi-v0.1.md), and the
[PTO Trace Schema v0.1](pto-trace-schema-v0.1.md).

## Agent-first principles

The toolchain MUST provide:

- a non-interactive default mode;
- deterministic command behavior;
- stable command names and exit codes;
- JSON or JSON Lines diagnostics;
- source locations and hierarchy paths;
- machine-readable component, protocol, and packet schemas;
- validation without requiring a full C++ build;
- explicit intermediate artifacts;
- repair-oriented error messages and fix suggestions;
- incremental compilation using content fingerprints;
- one-command build-and-run operation;
- introspection of capabilities and the single current global epoch.

The toolchain MUST NOT require a terminal UI, prompt response, browser, or
ambient working-directory state for normal automation.

## Python frontend

### Role

Python is a static elaboration and specialization language. The emitted ACIR is
the canonical compiler boundary.

The global `0.1` epoch fixes the exact public Python decorators, types, function
signatures, keyword names, and CLI spellings. Implementations MUST NOT accept
alternate public spellings as equivalent portable source. Every participating
frontend, schema, ACIR, ACSim, generated-code source contract, trace reader, and
CLI artifact declares exactly this current epoch; v0.1 does not negotiate
version ranges.

Python control flow that changes topology runs during elaboration. Runtime
control is represented by `ac.process` and standard MLIR control flow.

Architecture dataflow is authored using assignment and ordinary function
calls. Function parameters, returns, callable schemas, and strong lexical
scopes infer module interfaces and direct connections through the
Python-to-ACIR lowering contract.

### Required authoring concepts

The Python API MUST expose these exact public names:

- `@system`;
- `@module`;
- `@extern_module`;
- `@generated_module`;
- `@struct`;
- `@packet`;
- `@transaction`;
- `@protocol`;
- `@interface`;
- `@process`;
- `scope` as a semantic nested-module boundary;
- schema-generated component and module callables;
- `array`;
- `instances`;
- `view`;
- `queue`;
- `ResourceRef`;
- `address_space`;
- `address_map`.

### Explicitness

The frontend SHOULD prefer explicit handles and named arguments over ambient
builder stacks and reflection-based magic. Explicit `ins()`, `outs()`, and
`connect()` calls are not part of the normal v0.1 authoring surface. The
frontend MUST reject:

- use of a handle outside its owning elaboration context;
- topology-dependent Python values that remain unresolved;
- implicit protocol or address conversion;
- duplicate instance names;
- accidental Python truth testing of symbolic ACIR values;
- mutation of frozen objects.

### Static typing

The Python package SHOULD publish complete type hints and `.pyi` stubs.
Component registry metadata SHOULD be able to generate typed wrappers for
external components.

Frontend checks remain mandatory because Python type checking is not a sound
enforcement boundary. Every `@system` parameter and every component or model
parameter is a static metaprogramming specialization input. Trace selection and
simulator-harness controls are run inputs; the frontend has no runtime model
configuration mechanism.

### Deterministic elaboration

Elaboration MUST NOT depend on:

- object memory addresses;
- hash randomization;
- unordered set or dictionary traversal;
- filesystem enumeration order;
- wall-clock time;
- network access;
- undeclared environment variables.

Any permitted random generation requires an explicit seed recorded in the
`build-manifest.json` as a static specialization input.

## Workspace contract

A project SHOULD contain:

```text
agentic-circuit.toml
architecture.py
components/
protocols/
traces/
build/
```

`agentic-circuit.toml` declares:

- project name and version;
- Python entry file and system symbol;
- global epoch;
- standard-library providers at that exact epoch;
- static build profile and C++ toolchain selection;
- default trace and simulator-harness run inputs;
- artifact directory;
- diagnostic defaults and static build instrumentation layers.

The CLI MUST support an explicit `--project` path and MUST NOT require discovery
through the current directory when that option is present.

## Public command surface

### `agentic-circuit init`

Creates a specification-only project skeleton with manifest, architecture entry,
and example references. It MUST NOT overwrite existing files unless
`--force` identifies each target.

### `agentic-circuit schema`

Queries available machine-readable definitions:

```text
agentic-circuit schema component [NAME]
agentic-circuit schema protocol [NAME]
agentic-circuit schema interface [NAME]
agentic-circuit schema packet [NAME]
agentic-circuit schema diagnostic [CODE]
agentic-circuit schema capabilities
```

With `--json`, output is a single JSON value and stdout contains no prose.

### `agentic-circuit check`

Runs Python import isolation, frontend validation, ACPy construction and
verification, static elaboration, ACIR construction, and all validations that
do not require lowering or C++ compile.

```text
agentic-circuit check architecture.py --system main --json
```

This command is the primary fast repair loop for an agent.

### `agentic-circuit elaborate`

Elaborates Python into an inspectable frontend artifact or canonical ACIR
without simulator lowering:

```text
agentic-circuit elaborate architecture.py \
  --emit=acir -o build/main.ac.mlir

agentic-circuit elaborate architecture.py \
  --emit=acpy -o build/main.acpy.json
```

The CLI MUST support `--emit=acpy|acir`; `acir` is the default when the option
is omitted. The output MUST be deterministic and formatted canonically. ACPy
is an inspection and compiler-debugging artifact, not the portable architecture
interchange format.

### `agentic-circuit compile`

Runs the ACIR and ACSim pipeline and emits selected artifacts:

```text
agentic-circuit compile architecture.py \
  --emit=acpy,acir,frozen-acir,acsim,cpp \
  --output-dir build/main
```

Supported controls include:

- `--stop-after STAGE`;
- `--dump-before PASS`;
- `--dump-after PASS`;
- `--dump-after-each`;
- `--verify-after-each`;
- `--pass-pipeline PIPELINE` for expert use.

ACSim is a normative, deterministic compiler artifact between frozen/lowered
ACIR and C++. It records fully specialized object types, ownership,
construction order, SSA-derived bindings, enum-PC process state, C++ bindings,
and runtime registration. `--emit=acsim` MUST produce canonical ACSim, and every
C++ build MUST consume verified ACSim rather than bypassing it.

### `agentic-circuit build`

Compiles generated C++ and links a gfsim executable. It reuses artifacts whose
fingerprints and source-contract dependencies match.

```text
agentic-circuit build architecture.py -o build/main/gfsim
```

`--profile=fast|validated|custom` selects one of three static build profiles:

- `fast` runs every representation's required verifier and uses the standard
  minimal release pipeline;
- `validated` additionally verifies after each compiler pass and emits all
  validation reports;
- `custom` requires an explicit `--pass-pipeline` and records it verbatim.

The selected profile is a specialization/build input and is recorded in the
build fingerprint. It is never a runtime validation toggle. Runtime preflight,
invariant checks, and post-run validation are mandatory for every profile.

### `agentic-circuit run`

Provides the default end-to-end path:

```text
agentic-circuit run architecture.py \
  --trace traces/model.json \
  --output-dir runs/model-001 \
  --seed 1 \
  --max-ticks 1000000 \
  --max-domain-cycles core=250000 \
  --expect-termination
```

It performs incremental check, compile, build, runtime preflight, execution, and
post-run validation.

Useful runtime controls include:

- `--deadlock-window N`;
- `--max-ticks N` for the global simulator tick bound;
- repeatable `--max-domain-cycles DOMAIN=N` for per-time-domain bounds;
- `--expect-termination` to require declared model termination before any run
  bound is reached;
- `--stats-format json`;
- `--event-log jsonl`;
- `--replay-manifest PATH`.

These are simulator-harness run inputs and do not configure or respecialize the
model. Instrumentation layers are selected only by the static build profile;
there is no runtime enable/disable mechanism.
`--replay-manifest` accepts only an immutable `run-manifest.json`; a build
manifest or run result is not replayable input.

### `agentic-circuit inspect`

Provides read-only introspection:

```text
agentic-circuit inspect graph
agentic-circuit inspect hierarchy
agentic-circuit inspect ports --path chip.cluster[0]
agentic-circuit inspect resources
agentic-circuit inspect address-map
agentic-circuit inspect protocols
agentic-circuit inspect specialization
agentic-circuit inspect artifacts
```

Graph output SHOULD support JSON and Graphviz DOT. Hierarchy output MUST preserve
canonical object paths.

### `agentic-circuit explain`

Explains a stable diagnostic code with its rule, likely causes, examples, and
possible repairs:

```text
agentic-circuit explain ACIR-PROTOCOL-004 --json
```

### `agentic-circuit doctor`

Checks Python, MLIR, C++ compiler, standard-library providers, gfsim C++20
source contract,
JSON support, and exact global-epoch compatibility without mutating the project.

## Common options

All relevant commands support:

- `--json` for a single JSON result;
- `--diagnostic-format text|json|jsonl`;
- `--no-color`;
- `--quiet`;
- `--output-dir PATH`;
- `--project PATH`;
- `--system SYMBOL`;
- `--jobs N`;
- `--seed N` when randomness is possible.

When structured output is requested, logs MUST go to stderr or declared files,
and stdout MUST remain parseable.

## Validation gates

Each stage consumes a declared artifact, validates it, and emits a validation
report before the next stage begins.

| Gate | Input | Required checks | Output |
| --- | --- | --- | --- |
| Frontend capture | Python source | Syntax, imports, decorators, source availability | Captured AST |
| ACPy construction | Captured AST, schemas | Static/symbolic classification, calls, SSA, effects | Typed ACPy |
| ACPy verification | Typed ACPy | Scope capture/escape, ownership, naming, source maps | Verified ACPy |
| ACIR elaboration | Verified ACPy | Ports, results, SSA bindings, collection shape | Unfrozen ACIR |
| Process construction | Unfrozen ACIR | SSACFG, suspension points, effects | Process-verified ACIR |
| Collection canonicalization | Process-verified ACIR | `ac.array`/`ac.instances`, lexical paths | Canonical ACIR |
| ACIR Core | Canonical ACIR | Types, protocols, resources, addresses, units, effects | Verified ACIR |
| Topology freeze | Verified ACIR | Ownership, routes, cycles, stable paths | Frozen ACIR |
| Process state lowering | Frozen ACIR | Static enum-PC state and scheduled resumption | Lowered ACIR |
| ACSim | Lowered ACIR | Specialized object ownership, SSA bindings, C++ binding, construction order | Verified ACSim |
| C++ | Generated C++ | Compile, static assertions, link contract | gfsim executable |
| Runtime preflight | Binary, run inputs, trace | Exact epoch, schemas, paths, bounds | Immutable run manifest |
| Runtime | Simulation state | Protocol, capacity, lifetime, determinism guards | Results |
| Post-run | Results | Conservation, completion, declared expectations | Validation report |

`--verify-after-each` MUST be available for compiler development. Release-mode
commands MAY combine gates but MUST identify the failing logical gate.

## Diagnostic contract

Every JSON diagnostic MUST validate against
[`diagnostic.schema.json`](../../schemas/diagnostic.schema.json).

Every diagnostic has a stable code and includes:

```json
{
  "schema": "agentic-circuit-diagnostic",
  "version": "0.1",
  "contract_epoch": "0.1",
  "code": "ACIR-PROTOCOL-004",
  "stage": "ac-resolve-protocols",
  "severity": "error",
  "source": {
    "file": "architecture.py",
    "line": 88,
    "column": 12
  },
  "object_path": "chip.cluster[2].dma.request",
  "message": "Interface roles are incompatible",
  "expected": "target",
  "actual": "initiator",
  "related": [],
  "fixits": [
    {
      "message": "Bind the DMA request endpoint using the target role"
    }
  ]
}
```

Required diagnostic classes include:

- `ACPY-*`: Python/frontend;
- `ACELAB-*`: elaboration;
- `ACIR-TYPE-*`: types and layout;
- `ACIR-PROTOCOL-*`: protocol and interface;
- `ACIR-TOPOLOGY-*`: topology and ownership;
- `ACIR-RESOURCE-*`: resources and reservations;
- `ACIR-ADDRESS-*`: address spaces and maps;
- `ACLOWER-*`: process and ACSim lowering;
- `ACBUILD-*`: generated C++ and link;
- `ACTRACE-*`: trace parsing and decoding;
- `ACRUN-*`: runtime invariants and termination.

Diagnostics SHOULD include a source repair when the tool can express one safely.
The CLI MUST NOT silently apply repairs unless an explicit future command
authorizes mutation.

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | Success, including a successful validation with no errors. |
| `2` | Invalid user source, static specialization input, run input, ACIR, or schema. |
| `3` | Internal compiler or tool failure. |
| `4` | C++ generation, compile, or link failure. |
| `5` | Runtime preflight or trace failure. |
| `6` | Simulation invariant, deadlock, or declared expectation failure. |
| `7` | Run incomplete because a declared time, event, delta, trace, or validation cap was reached. |
| `130` | Interrupted execution. |

Warnings do not change the exit code unless `--warnings-as-errors` is set.

## Immutable manifests

The CLI defines exactly three immutable manifest artifacts:

- `build-manifest.json`, committed after a successful verified build;
- `run-manifest.json`, committed by runtime preflight before execution;
- `run-result.json`, committed after execution and post-run validation.

Their canonical machine-readable forms are
[`build-manifest.schema.json`](../../schemas/build-manifest.schema.json),
[`run-manifest.schema.json`](../../schemas/run-manifest.schema.json), and
[`run-result.schema.json`](../../schemas/run-result.schema.json). The semantic
rules below are additional to JSON Schema validation.

`build-manifest.json` contains:

- schema identity and exact global epoch;
- project and system identity;
- source file hashes;
- normalized frozen ACIR hash;
- compiler build identity and pass pipeline;
- standard-library provider identities at the current epoch;
- component and protocol identities at the current epoch;
- produced artifact paths and hashes;
- validation gate results;
- static build profile and all specialization inputs.

`run-manifest.json` references one build-manifest hash and contains the trace
hash, deterministic seed, `max_ticks`, all
`max_domain_cycles` bounds, termination expectation, and other simulator-harness
run inputs. It is the only accepted replay input.

`run-result.json` references the run-manifest hash and records start/completion
status, termination reason, simulated ticks, per-domain cycles, event count,
trace position, result hashes, and validation status. It is output only and
MUST NOT be used as replay input.

Each artifact-producing command MUST begin with a clean, command-specific
staging directory under the destination filesystem. It writes and verifies the
complete artifact set in that staging directory, then atomically replaces the
destination set. A failed stage removes or quarantines its staging directory
and MUST NOT modify the most recent valid destination or any immutable
manifest.

## Incremental build fingerprint

The gfsim build fingerprint includes:

- normalized frozen ACIR;
- compiler identity;
- pass pipeline;
- generated C++ source-contract identity at the current epoch;
- component schema and implementation identities;
- static build options affecting behavior.

PTO trace contents and simulator-harness run inputs are never part of the build
fingerprint. Any value that affects topology or C++ specialization is a static
specialization input and therefore belongs in the build fingerprint instead.

## Runtime outputs

An output directory SHOULD contain:

```text
build-manifest.json
run-manifest.json
run-result.json
diagnostics.jsonl
stats.json
events.jsonl
validation-report.json
```

Reaching `max_ticks` or any `max_domain_cycles` bound produces an immutable
`run-result.json` with status `incomplete` and exit code `7`. With
`--expect-termination`, the result also identifies the unmet termination
expectation. This condition is distinct from a simulation invariant failure.

## Capability discovery

`agentic-circuit schema capabilities --json` MUST report:

- the single current global epoch;
- ACIR, ACSim, CLI schema, component schema/C++ source-contract, and trace
  schema identities at that exact epoch;
- providers, components, policies, interfaces, protocols, and output formats,
  each with availability exactly `available` or `declared_unavailable`;
- compiler and runtime build identifiers.

The result MUST validate against
[`capabilities.schema.json`](../../schemas/capabilities.schema.json).

The capability document MUST NOT advertise supported version ranges. A
`declared_unavailable` item is known to the current schema but absent from the
installed toolchain; omission MUST NOT be used to encode that state.

An agent MUST be able to decide whether a requested architecture can be
described without importing Python modules or attempting a build.

## Security and isolation

Python elaboration executes code and therefore is not inherently sandboxed.
The CLI MUST clearly distinguish trusted project execution from parsing ACIR.

A future sandbox mode may restrict imports, filesystem access, environment, and
network access. ACIR v0.1 does not claim safe execution of untrusted Python.

## Acceptance criteria

The v0.1 CLI conforms when an agent can:

- discover components and their schemas as JSON;
- generate a Python architecture without undocumented parameters;
- inspect inferred calls, ports, strong-scope signatures, and connections in
  ACPy before ACIR lowering;
- validate it without compiling C++;
- locate every error in Python source or hierarchy paths;
- inspect elaborated topology and interfaces;
- build a structured gfsim executable;
- run a PTO JSON trace with one command;
- receive machine-readable diagnostics and results;
- reproduce the run only from its immutable `run-manifest.json`;
- distinguish model errors, compiler errors, build errors, and runtime failures
  from exit status and diagnostic codes.
