# Phase 2 completion audit

Audit of `codex/phase2-gfsim-runtime` against Phase 2 in the
[Agentic Circuit roadmap](../superpowers/plans/2026-08-04-agentic-circuit-roadmap.md)
and the detailed
[Phase 2 implementation plan](../superpowers/plans/2026-08-09-phase-2-gfsim-runtime.md).

Reviewed commit range:
`65aabec40ba5d88f7a4aa3f6061ddb4bd02fdc77..29064b7373cf0112e5c41a83ae3aaf56fc1111d4`
(11 commits).

## 1. Scheduler, dispatch, and barrier semantics — PASS

The runtime uses exact `(tick, causal_delta)` epochs, dense static dispatch
rows, canonical activation adjacency, immutable Work snapshots, global
arbitration-before-Xfer ordering, and stable object-ID iteration. Randomized
insertion-order tests produce the same committed result and execution order.

The audit added preflight identity checks across the registry, hierarchy, and
dispatch table; recursive canonical-path refresh after reparenting; read-only
Xfer probes; and explicit rejection of a second stateful object or event-queue
commit at one integer tick.

## 2. Queues, resources, protocols, packets, and processes — PASS

Finite queues and schedulers enforce capacity and stable ordering. Resource
arbitration uses stable keys, preserves conservation, scopes releases and
cancellation to owners, and keeps accepted and rejected results private until
Xfer. Ready-valid and request-response state enforce exactly-once transfer,
bounded correlations, and phase invariants. Packet traits validate layout and
round-trip exact serialized bytes. CRTP processes preserve continuation,
wake, reset, and fairness-cap state.

## 3. Trace, diagnostics, statistics, and termination — PASS

Typed PTO parsing and bounded streaming share one preflight path. The unique
trace cursor advances only on committed acceptance, preserves dependencies and
issue times, and reports exact position and EOF state. No-progress diagnostics
aggregate blocked object state deterministically. Statistics use stable paths,
names, kinds, and epochs that agree with the frozen standard-library schemas.

Termination distinguishes completed, incomplete, and failed outcomes. Time,
event, delta, and trace caps stop at their exact boundaries. In particular, a
future event beyond `maxTicks` remains pending and the final epoch is the cap,
not the event time.

## 4. Baseline components and frozen catalog — PASS

`TraceSource`, `Queue`, `Scheduler`, `Compute`, `Link`, `Memory`, `Sink`,
`ready_valid`, and `request_response` are reusable C++20 templates with frozen
component identities and concept coverage. The catalog contains 36 generated,
fingerprinted schemas: nine available entries and 27 explicit
`declared_unavailable` entries. Regeneration is byte-stable and checked by the
repository contract gate. `TraceSource::Decoder` is constrained by the runtime
`TraceDecoder` concept rather than the packet concept.

## 5. Clean build and gate matrix — PASS

The audited tree passed:

- Debug: complete build and 10/10 CTest suites;
- Release: complete build and 10/10 CTest suites;
- gfsim: 120/120 unit tests;
- lit: 72/72 tests;
- contract and IR coverage tests: 29/29;
- public operation coverage: all 53 operations have positive and negative tests;
- ASAN and UBSAN: `GfsimTests` and `CodeGenTests` pass in both presets;
- five-run determinism: stable text, bytecode, and topology hashes for all 11 inputs;
- repository-wide C++ format dry run and changed-file Python Ruff checks;
- changed-translation-unit clang-tidy, with only two existing `NOLINT` suppressions;
- generated catalog, repository contracts, IR inventory, and `git diff --check`;
- Release install followed by an external exact-version package consumer build
  and successful `process-state-plan-consumer` execution.

An exploratory repository-wide Ruff invocation reports existing findings in
`scripts/audit-op-coverage.py` and lit configuration globals. Those files are
outside the Phase 2 Python diff and Ruff is not a configured repository gate;
all changed Python files pass Ruff.

## 6. Requirements and code-quality review — PASS

The completion review found and repaired the following release blockers:

- hierarchy paths could remain stale after nested reparenting;
- registry and dispatch objects could shadow one stable object ID;
- resource rejection results became observable before Xfer;
- stateful objects and the global event queue could commit twice in one tick;
- the maximum-tick cap could jump to a later event epoch;
- reset could omit registered runtime objects outside the root hierarchy;
- runtime statistic names and kinds could drift from frozen catalog observations;
- the TraceSource decoder parameter carried the wrong schema constraint.

The final review found no remaining Phase 2 correctness blocker.

## Verdict

The Phase 2 roadmap scope and exit gate pass. The branch is ready to merge to
`main`; Phase 3 may begin from the merged runtime and standard-library baseline.
