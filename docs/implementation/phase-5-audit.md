# Phase 5 End-to-End Models, PTO Trace, and NPU Audit

## Decision

Phase 5 passes its combined local exit gate for contract epoch `0.1`. The
repository now imports the pinned DavinciOO instruction-trace shape into the
single public PTO trace format, publishes committed statistics and event
observations, runs six small architectures through the complete public
pipeline, and executes the hierarchical superscalar NPU showcase with
dependency-aware out-of-order issue and in-order retirement.

The canonical trace, architectural result, statistics, event JSONL, and
Perfetto document are byte-deterministic across repeated runs, replay, hash
seeds, legal Work orders, and unrelated checkout roots. Development, Release,
targeted sanitizer, installation, contract, IR coverage, dependency, and
end-to-end gates pass. No required test is skipped and the public contract
epoch, ten-command CLI, schema inventory, and runtime input remain unchanged.

## Audited environment

| Item | Observed value |
| --- | --- |
| Host | Darwin 25.5.0, arm64 |
| Python | CPython 3.14.6 |
| CMake | 4.2.1 |
| Ninja | 1.13.0.git.kitware.jobserver-pipe-1 |
| LLVM/MLIR | Homebrew LLVM 22.1.8 |
| Development preset | `build/dev-llvm22` |
| Release preset | `build/release-llvm22` |
| Sanitizer presets | `build/asan-llvm22`, `build/ubsan-llvm22` |
| Contract epoch | `0.1` |

The CMake-selected Python interpreter on this host is 3.14, while the existing
development virtual environment contains lit for Python 3.12. The development
`check-acir` command therefore supplied that environment's site-packages on
`PYTHONPATH`; all 84 tests passed. Release `check-acir` required no adjustment.
This is a local interpreter/environment mismatch, not a source or generated
artifact dependency.

Apple's ASan runtime rejects `detect_leaks=1` before test execution with
`AddressSanitizer: detect_leaks is not supported on this platform.` The audit
ran all targeted tests with address checking, strict string checking,
user-poisoning compatibility, and halt-on-error enabled. Linux CI retains its
leak-detection gate. UBSan ran with stack traces and halt-on-error.

## Reviewed commit evidence

The Phase 5 design checkpoint begins at `70236fd`. The implementation and
pre-audit checkpoints are:

| Area | Commit |
| --- | --- |
| Design and fixed external-format boundary | `70236fd` |
| Test-first implementation plan | `6697cc8` |
| Strict DavinciOO adapter | `b5f21f9` |
| Explicit atomic import command | `927a23e` |
| Committed runtime observation model | `1ec354d` |
| Statistics and event publication | `1629a1a` |
| Typed trace injection | `daf036d` |
| Deterministic Perfetto packer | `4cbfa79` |
| Baseline component instrumentation | `ef1ffe0` |
| Six small model workspaces | `88d1b96` |
| Six complete pipeline goldens | `cfe423f` |
| Typed NPU instruction decode | `0923fcc` |
| Dependency-aware oldest-ready scheduling | `23e0906` |
| Execution, completion, memory, and retirement | `c0763c4` |
| Hierarchical NPU showcase | `2f412b0` |
| Replay, determinism, install, and negative closure | `4d79202` |

## Verification matrix

| Gate | Result |
| --- | ---: |
| Repository contract tests | 21 passed |
| Repository contract checker | passed; 10 public schemas, 36 components |
| IR coverage tests | 11 passed |
| Read-only IR coverage ledger | passed |
| Python frontend tests | 56 passed |
| CLI tests | 52 passed |
| Adapter and Perfetto tool tests | 26 passed |
| Phase 5 end-to-end tests | 6 passed |
| Development CTest | 12 of 12 passed |
| Development LLVM lit | 84 of 84 passed |
| Release CTest | 12 of 12 passed |
| Release LLVM lit | 84 of 84 passed |
| ASan focused runtime/code-generation tests | 49 + 25 passed |
| UBSan focused runtime/code-generation tests | 49 + 25 passed |
| Release installation and external consumer | passed |
| Three-run deterministic replay audit | passed |
| Generated dependency scan | passed |

Principal commands were:

```sh
PYTHONPATH=<test-dependencies>:src:build/dev-llvm22/python \
  python3 -m unittest tests.contracts.test_contracts -v
PYTHONPATH=<test-dependencies>:src:build/dev-llvm22/python \
  python3 scripts/check-contracts.py
PYTHONPATH=<test-dependencies>:src:build/dev-llvm22/python \
  python3 -m unittest tests.contracts.test_ir_coverage -v
PYTHONPATH=<test-dependencies>:src:build/dev-llvm22/python \
  python3 scripts/check-ir-coverage.py
PYTHONPATH=<test-dependencies>:src:build/dev-llvm22/python \
  python3 -m unittest discover -s tests/python_frontend -v
PYTHONPATH=<test-dependencies>:src:build/dev-llvm22/python \
  python3 -m unittest discover -s tests/cli -v
PYTHONPATH=<test-dependencies>:src:build/dev-llvm22/python \
  python3 -m unittest discover -s tests/tools -v
PYTHONPATH=<test-dependencies>:src:build/dev-llvm22/python \
  python3 -m unittest discover -s tests/e2e -v
cmake --build --preset dev-llvm22
ctest --test-dir build/dev-llvm22 --output-on-failure
cmake --build --preset dev-llvm22 --target check-acir
cmake --build --preset release-llvm22
ctest --test-dir build/release-llvm22 --output-on-failure
cmake --build --preset release-llvm22 --target check-acir
scripts/audit-phase5-determinism.sh 3
```

The installed-prefix audit installed the Release tree to a private temporary
prefix, asserted that neither repository-local trace tool was installed,
configured `tests/install-consumer` against only the installed CMake package,
built it, and ran `process-state-plan-consumer` successfully.

## Roadmap coverage

| Phase 5 roadmap requirement | Implementation and direct evidence |
| --- | --- |
| Producer/queue/consumer example | `examples/phase5/producer_queue_consumer`; complete build/run/replay and hierarchy/statistics/event goldens in `tests/e2e/test_phase5_examples.py` |
| Backpressured pipeline | `examples/phase5/backpressured_pipeline`; retained-offer and rejected-proposal observations plus complete E2E golden |
| Request/response memory path | `examples/phase5/request_response_memory`; correlated memory activity and ordering golden |
| Nested arrays | `examples/phase5/nested_arrays`; stable expanded hierarchy and independent-lane golden |
| Multi-time-domain bridge | `examples/phase5/multi_time_domain_bridge`; exact domain advancement and bridge-ordering golden |
| Suspended process | `examples/phase5/suspended_process`; wake/resume/state/termination golden through the public process path |
| Every example traverses Python through PTO simulation | The shared E2E driver executes capture, ACIR/ACSim lowering, generated C++ build, manifest publication, canonical PTO run, replay, and schema-valid output comparison for all six examples |
| Hierarchical superscalar NPU | `examples/phase5/npu`, `include/gfsim/npu.h`, and `lib/gfsim/npu.cpp`; the hierarchy golden covers trace source, frontend, backend issue queues, four engine classes, memory, completion, and retirement |
| Validated PTO trace consumption | `tools/import-davincioo-pto-trace.py` produces the checked-in canonical fixture; harness preflight moves one typed document into the one statically generated trace owner; raw JSONL is rejected by CLI tests |
| Concurrent modules and deterministic delta ordering | NPU unit tests cover independent four-engine issue, dependent stalls, out-of-order completion, strict retirement, and equal committed results under permuted Work order |
| Deterministic JSON results | Run-result, statistics, validation, trace position, and architectural state are exact goldens and compare byte-identically on replay and under unrelated roots |
| Perfetto-compatible swimlane events | Committed Chrome Trace Event JSONL is packed without inference or reordering; the NPU Perfetto document is an exact checked-in golden with slices, counters, and dependency flows |
| Architectural golden checks | The NPU retires all six records in root order, completes independent work in order `[1, 4, 0, 2, 3, 5]`, retires `[0, 1, 2, 3, 4, 5]`, finishes in three modeled ticks, and produces digest `11635184385557860000` |

## Trace and observation closure

The simulator still accepts exactly one public trace document identity:
`pto-trace@0.1`. The repository-only adapter pins DavinciOO producer commit
`e73633301cabed0d871ea5ff66e76a91df870aeb` and PTO-ISA commit
`f6d0567c1cae2d6a7b0ebaf7ad0e3b93f8a39da3`. It validates duplicate keys,
closed record and descriptor shapes, bounded values, portable integers,
sequence ordering, canonical addresses, normalized lossless attributes,
metadata identities, and content hashes before atomic publication.

Runtime statistics and events are admitted only from committed Xfer state.
Rejected proposals consume no owner-local event index. Event ordering uses the
frozen key `(time, delta, stable object ID, local committed index)`, and stable
output excludes absolute paths, timestamps, process IDs, pointer values, and
host scheduling order.

The three-run audit produced these stable SHA-256 values:

| Artifact | SHA-256 |
| --- | --- |
| Canonical PTO trace | `976cc088712124f7e1e866b59ac01e88202b9686c935b463f90806ab9faf5fe3` |
| Run result | `d59eb7814e96cfaa4b90c09f2e36f2997bdad6c69975bd2c929362db106ad525` |
| Statistics | `6103c153779e5cccf6ad2f2ef3cf88d2c1f1d3b509e1100955afa1216c7505c6` |
| Event JSONL | `1d28b1fc6845b150d268c0f319cecdf92599ff1055302fdc527c19815516e722` |
| Perfetto JSON | `836f88c18d97bceeb9bbde80d8c783244ae84c33c3f326d73c12f27d8832dcf3` |

## Public surface and dependency audit

- The global epoch remains `0.1`, the CLI remains the exact existing ten
  commands, and no public schema property or diagnostic entry was added.
- DavinciOO import and Perfetto packing remain repository tools. They are not
  installed, exported from the Python package, or advertised as capabilities.
- Generated executables and binary dependency scans contain no Python runtime,
  MLIR/frontend library, dynamic plugin lookup, runtime schema walker, or
  dynamic model-discovery path.
- Provider implementation fingerprints cover the actual compiled NPU runtime
  implementation, and generated topology remains immutable after build.
- Changed files were scanned for unfinished-work markers, placeholder
  assertions, disabled or skipped required tests, local absolute paths,
  accidental schema changes, and checked-in generated build products. No
  actionable finding remained.

## Residual boundaries

- The adapter intentionally supports only the pinned DavinciOO JSONL shape.
  A future upstream record change requires a reviewed adapter update; it does
  not silently widen runtime input.
- The NPU is a bounded architectural proof model, not a calibrated performance
  model. Frozen engine latencies and capacities exist to prove scheduling,
  dependency, memory, and retirement contracts deterministically.
- The Perfetto document is a derived audit/example artifact. The public run
  bundle continues to publish committed `events.jsonl` as specified.
- LeakSanitizer cannot run on this macOS host. Linux CI owns that platform gate;
  supported local ASan and UBSan checks passed without suppressing a test.
- Python 3.11 through 3.13, Linux sanitizer behavior, static analysis, clean
  clone validation, benchmarks, and publication artifacts remain Phase 6
  release-audit gates. Phase 5 does not claim those future release checks.

These are explicit, deterministic boundaries. No Phase 5 exit criterion is
silently accepted, partially published, or deferred.
