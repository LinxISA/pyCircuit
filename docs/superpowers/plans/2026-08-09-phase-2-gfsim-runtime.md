# Phase 2 implementation plan — C++20 gfsim runtime and standard library

Status: **active**. Phase 1 passed its completion audit and was merged to
`main` at `65aabec` before this phase branch was created.

Roadmap scope (verbatim from
[2026-08-04-agentic-circuit-roadmap.md](2026-08-04-agentic-circuit-roadmap.md),
"Phase 2"):

> Implement the event scheduler, static dispatch tables, snapshot/proposal/Xfer
> barrier, exact global time, activation adjacency, queues, event queues,
> resources, arbitration, protocol state, packets, processes, diagnostics,
> statistics, trace cursor, no-progress handling, and termination results.
>
> Implement every initial executable baseline component as a reusable C++20
> template: `TraceSource`, `Queue`, `Scheduler`, `Compute`, `Link`, `Memory`,
> and `Sink`, plus `ready_valid` and `request_response`. Publish frozen schemas
> for all catalog entries and keep unavailable entries explicit.

Exit gate: runtime and component concept tests, sanitizer builds, randomized
work-order determinism tests, inactive-module suppression tests, protocol and
resource invariant tests, and PTO trace streaming tests all pass.

## Current state (after T10 implementation on `codex/phase2-gfsim-runtime`)

The branch already carries a runtime foundation (commits `125a72a`, `d44406d`,
`8ce3f99`, `4efa785`):

- `include/gfsim/core.h` — exact `Epoch` (tick + causal delta), object
  identity, `ObjectKind`, `TerminationResult`, `Proposal`, `Event`,
  `StatSnapshot`.
- `include/gfsim/object.h` — `SimObject` ownership tree with `walk`
  (uses `dynamic_cast`; requires RTTI — the build keeps RTTI on for gfsim).
- `include/gfsim/queue.h` — `SimQueue<T>`, public `Queue<T>`, `EventQueue`.
- `include/gfsim/packet.h` — exact fixed-width `Packet<T>` and
  `PacketTraits<T>` serialization/reflection contract.
- `include/gfsim/trace.h`, `lib/gfsim/trace.cpp` — typed PTO trace preflight,
  bounded buffered/streamed ingestion, and unique commit-driven cursor owner.
- `include/gfsim/resource.h` — `Resource` with reservation/release proposals.
- `include/gfsim/components.h` — `TraceSource<T>`, `Scheduler<T>`,
  `Compute<Input,Output,FunctionalPolicy>`, `Sink<T>`, `Link<T>`, `Memory<T>`,
  `ReadyValid<T>`, and `RequestResponse<Req,Resp>` templates;
  `ProtocolState`; `NoProgressReport`.
- `lib/gfsim/system.cpp` — `SimSystem`: object registry, work set, global
  event queue, termination classification.
- `include/gfsim/statistics.h` — proposal/Xfer counters, gauges, and
  histograms with deterministic snapshots.
- `unittests/gfsim/core_test.cpp` — 112 unit tests after T8.
- `schemas/stdlib/` — 36 fingerprinted component records plus an explicit
  availability catalog and deterministic generator/checker.
- `.github/workflows/ci.yml` — ASAN and UBSAN matrix legs reuse the locked LLVM
  cache and existing sanitizer presets for gfsim and codegen tests.

## Gap analysis vs roadmap scope

| Roadmap item | State | Remaining work |
| --- | --- | --- |
| Exact global time | Done (`Epoch`, `kMaxDeltasPerTick`) | T1 added exact delta-overflow boundary coverage |
| Event scheduler | T1 complete | T2 activation adjacency and full barrier integration |
| Static dispatch tables | T1 complete | Generated dense row factory and runtime validation are frozen |
| Snapshot/proposal/Xfer barrier | T2 complete | Immutable Work snapshot and typed pending-commit reporting |
| Activation adjacency | T2 complete | Canonical compressed arrays, next-tick wakes, inactive suppression |
| Queues / event queues | T5 complete | Finite public `Queue<T>`, commit and statistic invariants |
| Resources / arbitration | T3 complete | Stable-key contention, identity, owner scope, cancellation, conservation |
| Protocol state | T6 complete | Immutable ready-valid offers, exact correlation, phase/credit invariants |
| Packets | T6 complete | Static layout validation, reflection, exact round trips, queue byte sizing |
| Processes | T4 complete | CRTP enum-PC runtime, exact wake/continuation, fairness failure propagation |
| Diagnostics / statistics | T8 complete | Full snapshots, stable ordering, blocked-state taxonomy |
| Trace cursor | T7 complete | Exact schema preflight, streamed equivalence, dependencies, backpressure, position/EOF |
| No-progress handling | T8 complete | Stable aggregate report + detection loop |
| Termination results | T8 complete | End-to-end completion, cap, deadlock, and cursor tests |
| Baseline component templates | T5 complete | Exact compile-time identities, concepts, finite Queue/Scheduler behavior |
| Frozen catalog schemas | T9 complete | 36 component records, explicit availability, fingerprint/generation gate |
| Sanitizer builds in CI | T10 complete | ASAN/UBSAN matrix legs reuse existing presets and locked LLVM cache |

## Proposed task breakdown

1. **T1 — Scheduler core (complete)**: deterministic commit order, static
   dispatch table layout and codegen, exact event ordering and cap enforcement,
   delta-cycle overflow diagnostics. Concept tests + randomized work-order
   determinism tests.
2. **T2 — Barrier semantics (complete)**: snapshot/proposal/Xfer barrier across
   modules, queues, resources; generated activation adjacency, next-tick wake
   semantics, and inactive-module suppression tests.
3. **T3 — Resources and arbitration (complete)**: owner/root-transaction
   identity, stable-key contention, owner-scoped release/cancellation, exact
   ready epochs, and conservation invariant tests.
4. **T4 — Processes (complete)**: final CRTP process objects with committed
   wake/suspend state, exact continuation identity, compiler-supplied fairness
   caps, reset behavior, and system failure propagation.
5. **T5 — Components `Queue`/`Scheduler` (complete)**: all seven baseline
   names are reusable C++20 templates with exact compile-time catalog identity;
   finite Queue/Scheduler behavior and per-component concept tests pass.
6. **T6 — Protocol and packet runtime (complete)**: ready-valid exactly-once
   offers, bounded correlated request-response, credit/phase invariants, and
   compile-time packet layout plus exact serialization round trips.
7. **T7 — Trace cursor (complete)**: exact closed-schema preflight, typed PTO
   records, bounded streaming equivalence, dependency/issue-time readiness,
   stable diagnostics, and commit-only cursor advancement under backpressure.
8. **T8 — Statistics and diagnostics (complete)**: full `StatSnapshot`
   surface, stable component diagnostics, no-progress detection loop, trace
   cap enforcement, and termination end-to-end tests.
9. **T9 — Frozen catalog schemas (complete)**: one validated, fingerprinted
   JSON component schema per catalog entry; unavailable entries explicit;
   deterministic regeneration wired into `scripts/check-contracts.py`.
10. **T10 — Sanitizer CI legs (complete)**: ASAN and UBSAN matrix legs reuse
    the existing presets, exact LLVM cache, and focused runtime/codegen tests.

## Resolved design decisions

1. The dispatch row and activation arrays are a frozen C++20 codegen/runtime
   contract, not a separately versioned public JSON schema.
2. Phase 2 develops on `codex/phase2-gfsim-runtime` and receives its own
   completion audit before merging.
3. The existing baseline component names remain canonical; catalog schemas
   conform to them rather than renaming the C++ surface first.
4. The repository contains no external PTO sample corpus. T7 fixtures will be
   generated from the normative trace schema and ACIR trace operations.
