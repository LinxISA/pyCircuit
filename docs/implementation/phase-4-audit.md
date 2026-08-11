# Phase 4 Python Frontend, CLI, and Runtime Audit

## Decision

Phase 4 passes its combined local exit gate for contract epoch `0.1`. The
repository now provides the closed Python construction surface, deterministic
ACPy-to-ACIR frontend, reusable native compiler facade, exact ten-command CLI,
immutable build and run manifests, Python-free generated runtime, replay, and
installed-prefix operation.

Every public command has resolvable success, error, determinism, and
machine-readable-output evidence. Every public exit code has an exercised path.
Development and release tests pass, sanitizer builds pass, generated-code
dependency scans pass, and equivalent projects under unrelated roots produce
byte-identical compile and run artifacts.

The audit corrects an earlier prose-only inventory error: ACPy has 16 entity
kinds, as declared by `schemas/acpy.schema.json`, not 17. The schema,
implementation, and tests already used the correct closed set.

## Audited environment

| Item | Observed value |
| --- | --- |
| Host | Darwin 25.5.0, arm64 |
| Python unit-test runner | CPython 3.12.13 |
| CMake | 4.2.1 |
| Ninja | 1.13.0.git.kitware.jobserver-pipe-1 |
| LLVM/MLIR | Homebrew LLVM 22.1.8 |
| lit | 18.1.8 |
| Development preset | `build/dev-llvm22` |
| Release preset | `build/release-llvm22` |
| Sanitizer presets | `build/asan-llvm22`, `build/ubsan-llvm22` |
| Contract epoch | `0.1` |

The local ASan run used
`ASAN_OPTIONS=allow_user_poisoning=0:detect_container_overflow=0`. Homebrew's
unsanitized LLVM libraries use bump-allocator poison annotations that conflict
with the sanitized executable on this host; disabling those two runtime checks
allows the project sanitizer instrumentation to run. UBSan required no local
compatibility setting. CI builds its locked LLVM dependency separately and
retains independent sanitizer gates.

## Reviewed commit evidence

The Phase 4 design checkpoint begins at `40da285`. The implementation and audit
checkpoints through the pre-audit head are:

| Area | Commit |
| --- | --- |
| Public package and symbolic types | `61fd46f` |
| Source capture and diagnostics | `7b25a3d` |
| Supported-Python validation and static evaluation | `42100af` |
| Closed ACPy model and canonical JSON | `851f69e` |
| Definition and schema-call capture | `b4980e5` |
| SSA normalization and call resolution | `a6f840c` |
| Scope and collection planning | `a0424c8` |
| Protocol, queue, resource, and address semantics | `b63b690` |
| Process CFG construction | `6cb38a9` |
| Verified ACPy-to-ACIR lowering | `7784864` |
| Lossless canonical ACSim process CFG code generation | `f22d30e` |
| Phase 4A audit | `d28e84a` |
| Reusable native compiler facade | `4f0015c` |
| Private Python/native compiler bridge | `10b0aa0` |
| Workspace parser and output policy | `c1d77d8` |
| Schemas, capabilities, explain, and doctor | `5696a92` |
| Isolated check and elaborate commands | `55d2461` |
| Deterministic compile stages | `07f9ac3` |
| Build and frontend provenance | `91b18e9` |
| Immutable runtime-manifest execution | `2b5e30e` |
| Run and replay commands | `a790b70` |
| Deterministic architecture inspection | `36d9940` |
| Command, install, and determinism gates | `c923d4c` |

## Verification matrix

| Gate | Result |
| --- | ---: |
| Repository contract tests | 21 passed |
| Read-only contract and IR coverage checker | passed |
| Python frontend tests | 56 passed |
| CLI, native bridge, installation, and determinism tests | 51 passed |
| Development CTest | 11 of 11 passed |
| Development LLVM lit | 84 of 84 passed |
| Release CTest | 11 of 11 passed |
| Release LLVM lit | 84 of 84 passed |
| ASan CTest | 11 of 11 passed |
| UBSan CTest | 11 of 11 passed |
| Focused generated-code dependency tests | 9 of 9 passed |
| Public schemas / standard-library schemas | 10 / 36 accepted |
| Installed-prefix external consumer | passed |

The frontend determinism corpus also runs in separate processes with
`PYTHONHASHSEED=1` and `PYTHONHASHSEED=99`. CI retains Python 3.11, 3.12, and
3.13 matrix legs, formatting, clang-tidy, install-consumer, sanitizer, and
release gates.

## Exact public coverage

### Python API and ACPy

The public Python inventory is exactly 21 names. The checked ledger in
`tests/python_frontend/test_determinism.py` requires a resolvable positive and
negative test for every name.

| Surface | Exact members | Principal implementation and evidence |
| --- | --- | --- |
| Definitions | `system`, `module`, `extern_module`, `generated_module`, `struct`, `packet`, `transaction`, `protocol`, `interface`, `process` | `_definitions.py`; public API, definition, capture, and schema-call tests |
| Construction markers | `scope`, `array`, `instances`, `view`, `queue`, `address_space`, `address_map` | `_collections.py`, `_scopes.py`, `_resources.py`; collection, scope, and resource tests |
| Symbolic types | `ResourceRef`, `Static`, `Flow`, `Endpoint` | `_types.py`; public API and symbolic coercion tests |

The closed ACPy entity-kind set is exactly:

`system`, `module`, `scope`, `arg`, `call`, `result`, `get_result`, `bind`,
`static_if`, `static_for`, `collection`, `get_static`, `return`, `capture`,
`escape`, and `process`.

`schemas/acpy.schema.json`, `_acpy.py`, schema tests, canonical serialization
tests, and the generated IR coverage ledger enforce this set without aliases or
private extension kinds.

### CLI commands and option families

`tests/cli/test_all_commands.py` requires all four behavior classes for every
exact command:

| Commands | Required evidence |
| --- | --- |
| `init`, `schema`, `check`, `elaborate`, `compile`, `build`, `run`, `inspect`, `explain`, `doctor` | success, error, determinism, machine-readable output |

The parser rejects aliases, abbreviations, repeated singleton options, unknown
TOML keys, and invalid cross-option combinations. Coverage is grouped as:

| Option family | Exact covered surface |
| --- | --- |
| Output | `--json`, `--diagnostic-format`, `--no-color`, `--quiet`, `--warnings-as-errors` |
| Workspace | `--project`, `--system`, `--output-dir`, `--jobs`, `--seed` |
| Initialization | directory, `--dry-run`, repeated `--force` targets |
| Discovery | schema kind/name and exact capability inventory |
| Frontend and compile | `--stop-after`, `--emit`, logical dumps, `--verify-after-each`, profile, custom pass pipeline |
| Runtime | trace, replay manifest, deadlock window, tick/domain caps, termination expectation, statistics, event log |
| Inspection | exact view, canonical hierarchy path, JSON/Graphviz format |

The exact logical compile stages are `frontend-capture`, `acpy-construction`,
`acpy-verify`, `acir-elaboration`, `process-construction`,
`collection-canonicalization`, `acir-core`, `topology-freeze`,
`process-state-lowering`, `acsim`, and `cxx`. The exact profiles are `fast`,
`validated`, and `custom`; custom requires an explicit pipeline, while all
profiles retain mandatory representation and runtime validation.

### Exit codes

| Code | Meaning | Exercised path |
| ---: | --- | --- |
| 0 | success | all command-family positive tests |
| 2 | user input or semantic rejection | parser, workspace, frontend, compile-option tests |
| 3 | internal tool failure | native tooling unavailable during `doctor` |
| 4 | build failure | configured C++ compiler unavailable |
| 5 | run preflight failure | invalid trace or immutable-bundle mismatch |
| 6 | simulation failure | failed runtime and invalid result/replay paths |
| 7 | valid but incomplete run | tick-capped runtime |
| 130 | interruption | generated runtime terminated by `SIGINT` with no publication |

### Diagnostic catalog

The packaged explanatory catalog has exactly seven entries:

| Code | Covered boundary |
| --- | --- |
| `ACBUILD-COMPILE-001` | native compilation failure |
| `ACIR-PROTOCOL-004` | protocol contract mismatch |
| `ACLOWER-BINDING-MISSING` | missing exact available binding |
| `ACPY-CONFIG-001` | workspace configuration failure |
| `ACPY-SCHEMA-001` | Python/schema mismatch |
| `ACRUN-DEADLOCK-001` | deadlock termination |
| `ACTRACE-SCHEMA-001` | invalid PTO trace envelope |

`schema`, `capabilities`, and `explain` tests verify the packaged catalog can be
read without importing a project. Runtime and frontend code may emit additional
stable diagnostic codes; every emitted record is still checked against the
closed diagnostic schema.

### Public schema properties

All public schemas are recursively compiled with closed-object checks. Focused
tests exercise their required fields, enums, hashes, canonical encodings, and
negative forms. Their top-level property inventories are:

| Schema | Top-level properties |
| --- | --- |
| `acir-process-state-plan` | `callees`, `contract_epoch`, `processes`, `schema`, `value_types` |
| `acpy` | `contract_epoch`, `entities`, `entry`, `schema`, `sources`, `version` |
| `acsim-binding` | `activation_sources`, `availability`, `binding`, `binding_schema`, `component_schema`, `component_schema_fingerprint`, `construction`, `contract_epoch`, `cpp`, `cpp_type`, `effect`, `fingerprint`, `implementation`, `ownership`, `parameters`, `ports`, `provider`, `provider_implementation_fingerprint`, `resources`, `results` |
| `build-manifest` | `artifacts`, `build_fingerprint`, `build_profile`, `compiler`, `component_specializations`, `contract_epoch`, `instrumentation_layers`, `normalized_acir_sha256`, `pass_pipeline`, `project`, `protocol_identities`, `providers`, `schema`, `source_files`, `specialization_inputs`, `system`, `validation_gates`, `version` |
| `capabilities` | `compiler_build_id`, `contract_epoch`, `contract_identities`, `items`, `runtime_build_id`, `schema`, `version` |
| `component` | `activation`, `address_behavior`, `bindings`, `canonical_name`, `contract_epoch`, `cpp_binding`, `effect`, `family`, `observation`, `protocol_contracts`, `provider_namespace`, `resources`, `results`, `schema_fingerprint`, `schema_kind`, `schema_version`, `stability`, `static_parameters` |
| `diagnostic` | `actual`, `code`, `contract_epoch`, `expected`, `fixits`, `message`, `object_path`, `related`, `schema`, `severity`, `source`, `stage`, `version` |
| `pto-trace` | `contract_epoch`, `metadata`, `records`, `schema`, `version` |
| `run-manifest` | `build_manifest`, `contract_epoch`, `deadlock_window`, `event_log`, `max_domain_cycles`, `max_ticks`, `output_directory`, `schema`, `seed`, `stats_format`, `termination_expectation`, `trace`, `version` |
| `run-result` | `contract_epoch`, `domain_cycles`, `event_count`, `outputs`, `run_manifest`, `schema`, `simulated_ticks`, `status`, `termination_reason`, `trace_position`, `validation`, `version` |

## Canonical artifact sample

The audit copied the same project under two unrelated temporary roots, then ran
compile, build, and capped simulation independently. Recursive comparisons of
the compile publication, build publication, run manifest, result, statistics,
trace, and validation report were byte-identical. Representative hashes are:

| Artifact | SHA-256 |
| --- | --- |
| ACPy `input/model.acpy.json` | `2f7978c6efe1eb517516cf54e8e732f11746f04fb158e6c86bf36206c4773937` |
| ACIR `input/model.ac.mlir` | `54af761ce99ac1b3335fb1546cf1810d1c3240ab9c056bfb4aa1bbc1d35d895f` |
| ACSim `model.acsim.mlir` | `b7e7fef085afa6ba74d98a926888257a275f42632c618a234e4dd4d53eabdfc6` |
| `build-manifest.json` | `cd0a333e8ca604a9a515bc9a0bba8bf0e3f3c385926fd862d7484298467355b7` |
| `run-manifest.json` | `cae9aa8f7bcc224b21d779b272ac48b5d39cdabef2ae952eeb211d6f805a2d54` |
| `run-result.json` | `48253b2ead05af9fd0cd2a896a817e10c74e4b26fc874f96333bdf70b24cfbb9` |
| `stats.json` | `37517e5f3dc66819f61f5a7bb8ace1921282415f10551d2defa5c3eb0985b570` |
| `trace.json` | `24ccf421cd480b18e4236e92ac68ab51d887dc45cec3f4685e4aa6a3ae084b0e` |
| `validation-report.json` | `b215ff190a08ad5dcf98fbe6ee599c5a3a2b5b7034c6b526eeeaa887f67ae648` |

The sample intentionally terminates as incomplete at `max_ticks`, returning
exit code 7. Completed, failed, deadlocked, capped, invalid-preflight, and
interrupted outcomes are covered independently by unit and generated-model
tests.

## Build, runtime, and security closure

- Build manifests record exact frontend inputs, normalized ACIR, selected
  profile/pipeline, compiler identity, component specializations, providers,
  protocols, validation gates, instrumentation, and artifact hashes.
- Repeating an identical build validates the immutable publication and reports
  a cache hit. Changed topology or specialization inputs change the fingerprint;
  trace bytes and run controls do not.
- Compile, build, and run use private staging directories. Validation or tool
  failure leaves prior output and current pointers unchanged. Symlink and path
  containment tests prevent publication outside the selected root.
- Replay accepts only the immutable run bundle, verifies manifest/build/trace
  identities, rejects ambient overrides, and produces the same canonical result
  without importing architecture Python.
- The generated executable parses typed limits, validates the trace, runs gfsim,
  and atomically publishes schema-valid results after Python semantic execution
  has ended.
- Generated dependency scans reject Python runtime linkage, plugin loaders,
  descriptor interpreters, runtime schema walkers, coroutines, `std::function`
  process frames, and hot-path RTTI.
- Subprocess calls use argument vectors and bounded captures; no shell command
  is constructed from project data.
- The installed-prefix consumer runs from an unrelated directory without
  `PYTHONPATH`, discovers packaged schemas/resources, initializes and checks a
  project, builds through installed headers/libraries, and exercises capability
  discovery.

## Residual boundaries

- Python-to-ACIR component emission accepts one result per instance.
  Multiple-result schemas fail atomically with `ACPY-VERIFY-001`.
- Python-to-ACIR process emission remains capture-free and single-block with
  `yield_sim`. The internal Python planner and native ACSim/code-generation
  pipeline support richer CFGs, which are covered by native compile-and-run
  tests, but the public Python bridge does not yet admit them.
- `scope`, `array`, `instances`, and `view` remain AST construction markers, not
  runtime Python graph-builder calls. Scope and collection planning are covered;
  public `view` projection lowering is outside this closed subset.
- The naturally source-lowerable process corpus is recurrent, so the canonical
  real generated-model audit run demonstrates capped termination. Completed
  runtime publication is covered by a generated harness model rather than a
  naturally trace-drained Python workload.
- Python 3.11 and 3.13, Linux sanitizer configurations, clang-tidy, and the full
  CI matrix are retained as CI gates rather than reproduced on this macOS host.

These are explicit capability boundaries with deterministic rejection or
separate native evidence. No Phase 4 public contract is silently accepted or
partially published.
