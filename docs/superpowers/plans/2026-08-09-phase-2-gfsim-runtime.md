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

## Current state (as of `42b155d` on `feature/acir-acsim`)

The branch already carries a runtime foundation (commits `125a72a`, `d44406d`,
`8ce3f99`, `4efa785`):

- `include/gfsim/core.h` — exact `Epoch` (tick + causal delta), object
  identity, `ObjectKind`, `TerminationResult`, `Proposal`, `Event`,
  `StatSnapshot`.
- `include/gfsim/object.h` — `SimObject` ownership tree with `walk`
  (uses `dynamic_cast`; requires RTTI — the build keeps RTTI on for gfsim).
- `include/gfsim/queue.h` — `SimQueue<T>`, `EventQueue`.
- `include/gfsim/resource.h` — `Resource` with reservation/release proposals.
- `include/gfsim/components.h` — `TraceSource<T>`, `Compute`, `Sink`, `Link`,
  `Memory`, `ReadyValid<T>`, `RequestResponse<Req,Resp>` templates;
  `ProtocolState`; `NoProgressReport`.
- `lib/gfsim/system.cpp` — `SimSystem`: object registry, work set, global
  event queue, termination classification.
- `unittests/gfsim/core_test.cpp` — 78 unit tests after T3.

## Gap analysis vs roadmap scope

| Roadmap item | State | Remaining work |
| --- | --- | --- |
| Exact global time | Done (`Epoch`, `kMaxDeltasPerTick`) | T1 added exact delta-overflow boundary coverage |
| Event scheduler | T1 complete | T2 activation adjacency and full barrier integration |
| Static dispatch tables | T1 complete | Generated dense row factory and runtime validation are frozen |
| Snapshot/proposal/Xfer barrier | T2 complete | Immutable Work snapshot and typed pending-commit reporting |
| Activation adjacency | T2 complete | Canonical compressed arrays, next-tick wakes, inactive suppression |
| Queues / event queues | Done (`SimQueue`, `EventQueue`) | Invariant tests |
| Resources / arbitration | T3 complete | Stable-key contention, identity, owner scope, cancellation, conservation |
| Protocol state | Partial (`ProtocolState`) | Phase-transition invariants |
| Packets | Partial (`PacketTraits`) | Serialize/deserialize round trips per ACIR packet ops |
| Processes | Missing (`ObjectKind::Process` only) | Process objects, wake/suspend, fairness caps |
| Diagnostics / statistics | Partial (`StatSnapshot`) | Full statistics surface + diagnostics taxonomy |
| Trace cursor | Missing (`TraceSource` emits; no cursor) | Cursor API + PTO trace streaming |
| No-progress handling | Partial (`NoProgressReport`) | Detection loop + reporting tests |
| Termination results | Done (`TerminationResult` + classification) | End-to-end termination tests |
| `Queue`, `Scheduler` components | Missing (primitives exist) | Component templates wrapping `SimQueue`/scheduler |
| Frozen catalog schemas | Missing | JSON schemas per catalog entry, wired into the schema gate |
| Sanitizer builds in CI | Presets exist locally (`asan-llvm22`, `ubsan-llvm22`) | CI job legs |

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
4. **T4 — Processes**: process objects with wake/suspend, fairness caps,
   continuation identity (mirrors the compiler-side process-state plan).
5. **T5 — Components `Queue`/`Scheduler`**: complete the seven baseline
   component templates; per-component concept tests.
6. **T6 — Protocol and packet runtime**: phase invariants, packet round trips.
7. **T7 — Trace cursor**: cursor API, PTO trace streaming tests.
8. **T8 — Statistics and diagnostics**: full `StatSnapshot` surface,
   no-progress detection loop, termination end-to-end tests.
9. **T9 — Frozen catalog schemas**: one JSON schema per catalog entry,
   unavailable entries explicit; wired into `scripts/check-contracts.py`.
10. **T10 — Sanitizer CI legs**: ASAN and UBSAN job legs reusing the existing
    presets.

## Resolved design decisions

1. The dispatch row and activation arrays are a frozen C++20 codegen/runtime
   contract, not a separately versioned public JSON schema.
2. Phase 2 develops on `codex/phase2-gfsim-runtime` and receives its own
   completion audit before merging.
3. The existing baseline component names remain canonical; catalog schemas
   conform to them rather than renaming the C++ surface first.
4. The repository contains no external PTO sample corpus. T7 fixtures will be
   generated from the normative trace schema and ACIR trace operations.
