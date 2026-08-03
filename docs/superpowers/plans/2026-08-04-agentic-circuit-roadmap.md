# Agentic Circuit v0.1 Delivery Roadmap

> Status: approved for implementation. The eight documents in `docs/specs/`
> and the eight schemas in `schemas/` are normative. This roadmap schedules
> their complete implementation without narrowing the public v0.1 contract.

## Outcome

Deliver a professional Apache-2.0 open-source repository in which an agent or a
human writes a simple, serial Python architecture description, elaborates it
into frozen hierarchical ACIR, lowers it through canonical ACSim, generates a
specialized pure-C++20 event-driven gfsim executable, feeds that executable a
PTO JSON trace, and receives deterministic results, statistics, diagnostics,
and a swimlane trace.

The release showcase is a hierarchical superscalar NPU model informed by the
existing DavinciOO gfsim design. The implementation reuses proven design
concepts, but ACIR, ACSim, the generated-code ABI, and runtime semantics remain
defined exclusively by this repository's specifications.

## Frozen project decisions

- Contract epoch: exact string `"0.1"` across every public surface.
- Compatibility: interface changes are hard breaks. Obsolete APIs, aliases,
  adapters, migration shims, dual readers, and legacy tests are deleted in the
  same change. Git history is the rollback mechanism.
- Toolchain: LLVM/MLIR 22.1.8 at upstream commit
  `ca7933e47d3a3451d81e72ac174dcb5aa28b59d1`; C++20; CMake plus Ninja;
  Python 3.11 or newer.
- License and visibility: Apache-2.0, public `PTO-ISA/agentic-circuit`.
- Architecture graph: static after elaboration and topology freeze. Python
  metaprogramming, AST analysis, ACPy, and MLIR passes perform all structural
  generation. Run-time topology or model configuration is forbidden.
- Frontend: assignment plus ordinary-looking function calls and lexical scopes;
  no explicit `ins()`/`outs()` wiring surface. Inputs, outputs, captures,
  ownership, and legal connections are inferred and then made explicit in IR.
- IR: hierarchical, typed, SSA-based dataflow with explicit protocols,
  packets, resources, effects, units, address spaces, processes, and standard
  `scf`/`arith`/`index` control and expression support where the spec permits.
- Lowering: generic binding resolution and library calls. Missing behavior is
  repaired by adding a reusable C++ component or runtime primitive, never by a
  component-name branch in the emitter.
- Generated simulator: specialized, structured C++ preserving module nesting,
  statically shaped collections, exact bindings, and minimal dynamic state.
- Simulation: deterministic event-driven snapshot/proposal/arbitration/Xfer
  semantics. Unchanged inactive inputs do not cause module evaluation.
- Validation: every public ACIR/ACSim operation and type has positive and
  negative tests; every Python public API and CLI command has tests; each layer
  can validate and explain its own artifacts.
- Delivery: test-first tasks, one reviewed commit per task, CI on every pushed
  branch, and frequent upstream pushes.

## Authoritative specification set

| Contract | Normative source | Primary implementation phase |
| --- | --- | --- |
| ACIR syntax, types, operations, verification | `docs/specs/acir-core-v0.1.md` | 1 |
| Standard schemas, protocols, components | `docs/specs/acir-stdlib-v0.1.md` | 2 |
| ACSim and structured C++ lowering | `docs/specs/acsim-gfsim-lowering-v0.1.md` | 1, 3 |
| C++ simulator model-library ABI | `docs/specs/gfsim-runtime-abi-v0.1.md` | 2 |
| Python language and CLI | `docs/specs/agentic-python-cli-v0.1.md` | 4 |
| Python AST, ACPy, and ACIR lowering | `docs/specs/python-to-acir-lowering-v0.1.md` | 4 |
| PTO trace representation and streaming | `docs/specs/pto-trace-schema-v0.1.md` | 2, 5 |
| Hard-break evolution and conformance | `docs/specs/interface-evolution-v0.1.md` | all |
| Machine-readable public artifacts | `schemas/*.schema.json` | all |

No implementation plan may silently weaken a normative requirement. A genuine
contradiction is repaired in the specification and all affected code, schemas,
tests, examples, fingerprints, and epoch evidence in one hard-break change.

## Phase sequence

### Phase 0 — Open-source and reproducible-build baseline

Create the public repository, governance and security files, deterministic
dependency locks, CMake presets, developer bootstrap commands, CI skeleton,
format/lint policies, documentation checks, and release/version metadata.

Exit gate: a clean clone can validate all current Markdown and JSON schemas;
CI uses pinned actions and identifies the exact LLVM/MLIR toolchain.

### Phase 1 — ACIR and ACSim compiler foundation

Implement the ACIR and ACSim MLIR dialects using ODS/TableGen, bytecode/textual
round trips, custom parsers/printers only where needed, verifier interfaces,
effect interfaces, symbol and hierarchy rules, topology freeze, deterministic
canonicalization, binding metadata, process-state lowering, and canonical
ACIR-to-ACSim structural lowering. Provide `acir-opt` and compiler libraries.

Detailed plan:
`docs/superpowers/plans/2026-08-04-acir-acsim-implementation.md`.

Exit gate: every public operation and type from both inventories is registered,
round-trips, has valid and invalid tests, and is accounted for by an automated
contract-coverage audit. Frozen ACIR and canonical ACSim are deterministic.

### Phase 2 — C++20 gfsim runtime and standard library

Implement the event scheduler, static dispatch tables, snapshot/proposal/Xfer
barrier, exact global time, activation adjacency, queues, event queues,
resources, arbitration, protocol state, packets, processes, diagnostics,
statistics, trace cursor, no-progress handling, and termination results.

Implement every initial executable baseline component as a reusable C++20
template: `TraceSource`, `Queue`, `Scheduler`, `Compute`, `Link`, `Memory`, and
`Sink`, plus `ready_valid` and `request_response`. Publish frozen schemas for
all catalog entries and keep unavailable entries explicit.

Exit gate: runtime and component concept tests, sanitizer builds, randomized
Work-order determinism tests, inactive-module suppression tests, protocol and
resource invariant tests, and PTO trace streaming tests all pass.

### Phase 3 — Binding resolution and structured C++ generation

Implement deterministic provider discovery, exact schema and implementation
fingerprints, immutable binding locks, static build profiles, canonical ACSim
verification, hierarchical code generation, process state-machine generation,
concept checks, build manifests, cache fingerprints, staged output, compilation,
linking, and same-toolchain preflight.

Exit gate: equivalent frozen inputs emit byte-identical staged source and
manifests; generated simulators contain no Python dependency, schema walker,
runtime plugin lookup, dynamic topology, or component-name emitter branch.

### Phase 4 — ACPy, Python frontend, and agent-first CLI

Implement deterministic source capture, supported-Python validation, static
evaluation, ACPy semantic IR, assignment/SSA normalization, free-variable and
escape analysis, scope outlining, stable naming, component-call resolution,
port/result inference, collection expansion, process construction, and
ACPy-to-ACIR lowering.

Implement every specified Python public API and CLI command: `init`, `schema`,
`check`, `elaborate`, `compile`, `build`, `run`, `inspect`, `explain`, and
`doctor`, including diagnostics, exit codes, immutable manifests, cache
fingerprints, capability discovery, and security boundaries.

Exit gate: every public API and command has success, error, determinism, and
machine-readable-output tests. Python execution ends before simulator runtime.

### Phase 5 — End-to-end models, PTO trace, and swimlane output

Add small golden examples first: producer/queue/consumer, backpressured
pipeline, request/response memory path, nested arrays, multi-time-domain bridge,
and a suspended process. Each example runs Python to ACIR to ACSim to C++ build
to PTO trace simulation.

Then implement the hierarchical superscalar NPU showcase with trace source,
decode/dispatch, dependency tracking, issue queues, scalar/vector/cube units,
load/store and memory hierarchy, completion, and retirement. Generate
deterministic JSON results and Perfetto-compatible swimlane events.

Exit gate: the showcase consumes validated PTO JSON, demonstrates concurrent
modules and deterministic delta ordering under legal Work permutations, emits
an inspectable hierarchy and swimlane trace, and passes architectural golden
checks.

### Phase 6 — Release audit and v0.1 publication

Run the complete spec-coverage audit, public-interface lockstep checks, clean
clone builds, Debug assertions, Release, sanitizers, static analysis, Python
version matrix, reproducibility checks, deterministic replay, performance
benchmarks, documentation examples, security review, and independent code
review. Remove all placeholders and stale APIs.

Exit gate: every normative paragraph has implementation/test evidence or an
explicit machine-checked declaration of unavailability allowed by the spec;
all CI gates pass on the release commit; v0.1 artifacts and checksums are
published from the public repository.

## Cross-phase quality gates

Every task follows red-green-refactor and records the observed failing test.
Every change preserves a compact diff and receives a requirements review and a
code-quality review before integration. Generated files are reproducible and
are never hand-edited. Tests use public contracts unless specifically testing
an internal helper.

CI grows monotonically with implemented behavior. A gate may start as a
failing test in the task branch, but no commit merged to `main` may carry a
known failure or a skipped required contract. `xfail`, broad warning
suppression, and placeholder assertions require a written normative reason and
are otherwise forbidden.

## Spec coverage ledger

The repository will maintain a generated ledger mapping each of the following
to implementation symbols, positive tests, negative tests, documentation, and
its first implementing commit:

- ACIR and ACSim operation inventories;
- ACIR and ACSim type inventories;
- dialect, pass, interface, attribute, and file-epoch contracts;
- Python decorators, types, functions, diagnostics, and CLI commands;
- JSON schema properties and validation caps;
- C++ runtime concepts, component schemas, protocols, and policies;
- build/run artifacts, exit codes, statistics, and observation formats.

The coverage generator fails on missing rows, orphan implementation surfaces,
duplicate identities, legacy aliases, or epoch mismatch. The ledger is an
audit artifact, not a substitute for behavioral tests.

## Stop condition

Work stops only when Phase 6 passes and the public upstream contains the
verified release state. Partial phase completion is reported as progress, not
as completion of the user's goal.
