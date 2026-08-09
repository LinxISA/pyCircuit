# Phase 2 implementation plan — C++20 gfsim runtime and standard library (DRAFT)

Status: **draft, blocked on Phase 1 merge** (see
[phase-1-audit.md](../../implementation/phase-1-audit.md); the roadmap permits
Phase 2 only after the Phase 1 branch merges).

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
- `unittests/gfsim/core_test.cpp` — 57 unit tests.

## Gap analysis vs roadmap scope

| Roadmap item | State | Remaining work |
| --- | --- | --- |
| Exact global time | Done (`Epoch`, `kMaxDeltasPerTick`) | Invariant tests for delta overflow |
| Event scheduler | Partial (work set + event queue in `SimSystem`) | Deterministic commit order, static dispatch tables |
| Static dispatch tables | Missing | Table layout + codegen contract with `lib/CodeGen` |
| Snapshot/proposal/Xfer barrier | Partial (`Proposal`, resource proposals) | Full barrier semantics across all object kinds |
| Activation adjacency | Missing | Adjacency tracking + suppression |
| Queues / event queues | Done (`SimQueue`, `EventQueue`) | Invariant tests |
| Resources / arbitration | Partial (`Resource` proposals) | Arbitration owner resolution, contention tests |
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

1. **T1 — Scheduler core**: deterministic commit order, static dispatch table
   layout, delta-cycle overflow diagnostics. Concept tests + randomized
   work-order determinism tests.
2. **T2 — Barrier semantics**: snapshot/proposal/Xfer barrier across modules,
   queues, resources; activation adjacency and inactive-module suppression
   tests.
3. **T3 — Resources and arbitration**: owner resolution, contention and
   arbitration invariant tests.
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

## Open design questions (need owner input before T1)

1. Does the runtime dispatch-table layout get frozen as a schema (like the
   process-state plan schema), or is it an internal contract between codegen
   and runtime only?
2. Should Phase 2 land on `main` incrementally after the Phase 1 merge, or as
   another long-running phase branch with its own completion audit?
3. Are the existing `components.h` templates the final baseline set, or do the
   catalog schemas (T9) drive renames/reshaping first?
4. PTO trace format: is there an authoritative sample corpus for the streaming
   tests (T7), or do we generate fixtures from the ACIR trace ops?
