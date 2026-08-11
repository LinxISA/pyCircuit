# Phase 4A Python Frontend Audit

## Decision

Phase 4A passes its planned local exit matrix for the implemented v0.1 closed
subset. The exact Python surface is importable, frontend failures are atomic,
canonical ACPy and ACIR are root- and hash-seed-independent, native ACIR accepts
the emitted goldens, and canonical ACSim multi-block processes survive model-plan
extraction through compiled generated C++.

This decision does not widen the supported subset. The residual boundaries in
the final section remain deterministic errors and are inputs to Phase 4B rather
than implicit promises of support.

## Audited environment

| Item | Observed value |
| --- | --- |
| Host | Darwin 25.5.0, arm64 |
| Python | CPython 3.12.13 |
| CMake | 4.2.1 |
| Ninja | 1.13.0 |
| LLVM/MLIR | Homebrew LLVM 22.1.8 |
| Development preset | `build/dev-llvm22` |
| Release preset | `build/release-llvm22` |
| Contract epoch | `0.1` |

The release preset was configured with the repository virtual environment's
`lit` executable because it is not installed on the ambient shell `PATH`.

## Commit evidence

The audited branch range is `c6f27a5..HEAD`. The implementation checkpoints
are:

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

## Exact verification counts

| Gate | Development | Release |
| --- | ---: | ---: |
| Python frontend unit tests | 56 passed | CI matrix covers 3.11, 3.12, and 3.13 |
| Repository contract unit tests | 20 passed | same source contract |
| Public schemas / stdlib components | 10 / 36 accepted | same source contract |
| CTest suites | 10 of 10 passed | 10 of 10 passed |
| LLVM lit tests | 82 of 82 passed | 82 of 82 passed |

The determinism test was also run in separate interpreter processes with
`PYTHONHASHSEED=1` and `PYTHONHASHSEED=99`. CI contains independent Python 3.11,
3.12, and 3.13 matrix legs and repeats both seed runs on every leg. Existing
native, release, sanitizer, install-consumer, formatting, and clang-tidy jobs
remain present.

## Public and semantic coverage

The exact public inventory contains 21 names. The checked ledger in
`tests/python_frontend/test_determinism.py` requires every name to have at least
one resolvable positive and negative test reference. Coverage includes:

- all ten definition decorators and immutable definition metadata;
- `Static`, `Flow`, `Endpoint`, and `ResourceRef` symbolic non-coercion;
- schema-generated callable signatures, defaults, and unavailable components;
- strong-scope capture/escape ordering and owned-resource rejection;
- rectangular homogeneous arrays, heterogeneous instances, and ragged rejection;
- queue depth, protocol role, address-space, and address-map validation;
- SSA naming, result mapping, ambiguity diagnostics, and atomic failure;
- bounded/static control, forbidden Python constructs, and process effects;
- closed process blocks, branch termination, suspension, liveness, and linear use.

The ACPy schema continues to expose exactly 16 entity kinds. The repository IR
coverage checker reports complete positive and negative coverage for the ACIR
and ACSim ODS inventories, and the generated ledger is current.

## Canonical artifacts and determinism

Two corpus entries (`hierarchy` and `process`) are copied under unrelated
temporary roots and elaborated in separate interpreter processes. The complete
canonical ACPy and ACIR byte maps are equal across both roots and both hash
seeds. The canonical-content SHA-256 values are:

| Artifact | SHA-256 |
| --- | --- |
| `hierarchy.acpy.json` | `dc8c58c9052b550d34b13d324680a8c1a7e0928443c1657bc34cd6e9bc742139` |
| `process.acpy.json` | `1d85ed9f0d4e8d9d6ab3ec0fe076b2e1c88369161ca10b635160b76e35322de5` |
| `hierarchy.ac.mlir` | `82f78d2012a49aa95ac48a24a7dc9f3a3c0690e48b27576c64609801f8f4754b` |
| `process.ac.mlir` | `54af761ce99ac1b3335fb1546cf1810d1c3240ab9c056bfb4aa1bbc1d35d895f` |

The corpus additionally rejects publication when lowering fails and verifies
that serialized output contains neither temporary root.

## Native process closure

`test/CodeGen/process-control-flow.mlir` contains a canonical ACSim process with
a conditional branch, typed successor operands, arithmetic in both successor
blocks, and distinct suspensions. Model-plan tests require dense block identity
and exact branch preservation. Generator tests require typed local dispatch and
reject cross-block dominance violations and successor type mismatches.

`GeneratedModelCompileTest` generates, compiles, links, and runs a branched
bundle with typed successor arguments. Generated process tests reject
`std::function` frames and coroutine tokens; the generated path has no Python,
descriptor interpreter, or dynamic discovery dependency.

## Residual boundaries

- Python-to-ACIR component emission currently accepts one result per instance.
  Multiple-result schemas fail atomically with `ACPY-VERIFY-001`.
- Python-to-ACIR process emission is currently closed to capture-free,
  single-block `yield_sim` processes. The Python process planner accepts richer
  CFGs, and the native canonical ACSim/code-generation path now preserves rich
  CFGs, but the Python-to-ACIR bridge between those two surfaces is not widened
  in Phase 4A.
- `scope`, `array`, `instances`, and `view` are AST construction markers rather
  than an executable Python graph-builder DSL. Scope and collection planning are
  covered internally; `view` projection lowering is not part of the emitted
  corpus.
- Python 3.11 and 3.13, ASan, UBSan, clang-tidy, and the installed-tree consumer
  are CI gates but were not reproduced on this macOS host during this audit.

These boundaries must remain explicit in Phase 4B capabilities and diagnostics.
They are not authorization to silently accept or partially publish unsupported
models.
