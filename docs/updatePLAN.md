# pyc 0.35 → 0.40 Upgrade Plan (pyc4.0)

This plan is derived directly from `docs/rfcs/pyc4.0-decisions.md` and is
intended to be executed by many agents in multiple batches. The core principle:
**all implementation must be gated by MLIR-level verifiers/passes and must not
introduce backend drift**.

## 0) Ground Rules (must be enforced)

- `docs/rfcs/pyc4.0-decisions.md` is the single source of truth.
- Every PR MUST:
  - reference the decision IDs it implements (e.g. `Implements 0134/0135`)
  - include gate evidence (commands + logs)
- Prefer: “add gate first, then implement”.
- No “backend-only fixes”: semantics belong in dialect + passes.

## 1) Milestones

### M0 — Baseline freeze + upgrade scaffolding

**Goal**: Freeze 0.35 behavior and set up 0.40 pipeline + gate scaffolding.

**Deliverables**
- Baseline regression set for 0.35 (golden outputs for key examples)
- CI scaffold for 0.40 gates (can start minimal)

**Gates (must be green before M1)**
- Build `pycc` and run minimal examples end-to-end.
- LinxCore cosim sanity:
  - `rtl/LinxCore/tests/test_runner_protocol.sh`
  - `rtl/LinxCore/tests/test_cosim_smoke.sh`
- No-regression on existing pyCircuit examples (compile + run where applicable)

**Checklist**
- [ ] Introduce feature flags / staging (avoid big-bang breakage).
- [ ] Create `docs/gates/` and standardize gate output/log paths.

---

### M1 — Static-hardware IR contract (control-flow + type gates)

**Primary decisions**: 0133, 0136 (+ existing contract checks)

**Goal**: Keep Pythonic authoring, but enforce that backend IR is static hardware.

**Required pipeline gates**
- `pyc-lower-scf-static` must fully lower SCF control flow.
- `pyc-check-no-dynamic` must fail if any `scf.*` or `index` remains.
- `pyc-check-flat-types` must fail if unsupported types remain (flat emission).

**Checklist**
- [ ] Document the “compilable Python subset” (what is allowed/forbidden).
- [ ] Ensure `@module` boundaries preserve hierarchy via `pyc.instance`.

---

### M2 — CombDepGraph upgrade + comb-cycle + depth/WNS proxy across instances

**Primary decisions**: 0127, 0128, 0134, 0135, 0119, 0124, 0131

**Goal**: Fix the known hard issue: instance boundaries must not hide comb loops
or depth.

**Gates**
- Comb-cycle gate must be instance-aware:
  - cross-instance combinational loops must be detected.
  - diagnostics must include hierarchical context.
- Logic-depth gate must propagate through instances:
  - WNS/TNS-equivalent values must be meaningful for hierarchical designs.

**Mandatory regression tests**
- [ ] module-local comb loop (wire/assign)
- [ ] cross-instance comb loop (previously missed)
- [ ] cross-instance depth propagation (previously underestimated)

---

### M3 — Observation points + memory/reset semantics

**Primary decisions**: 0121, 0114, 0115, 0122

**Goal**: Make `TICK-OBS`/`XFER-OBS` real and unify state semantics.

**Gates**
- Memory litmus tests for tick-read / transfer-write and RDW(old-data) default.
- Reset/init semantics stable across C++ and Verilog/Verilator.
- Observation-point sampling tests (tick vs xfer probes).

---

### M4 — DFX productization + hardened metadata (bundle/probe/trace)

**Primary decisions**: 0138, 0140, 0125, 0132, 0143, 0145, 0147

**Goal**: Make DFX scalable for ultra-large designs.

**Deliverables**
- Interface-first authoring with Bundle/Struct.
- Deterministic flattening with `layout_id` + field maps.
- Trace configuration DSL (instance globs + tags + triggers + windows).

**Gates**
- Bundle flatten stability (`layout_id` stable; field-path mapping stable).
- Probe naming stability (non-semantic edits do not break names).
- Trace DSL can enable traces without design edits and produces reproducible output.

---

### M5 — Const/template + incremental build + cosim standardization

**Primary decisions**: 0139, 0141, 0142, 0144, 0146

**Goal**: Make “write big circuits like Python” practical with fast iteration and
first-class cosim.

**Deliverables**
- Const/template canonicalization with pinpoint diagnostics.
- Multi-layer incremental caches:
  - JIT layer (source + const params)
  - MLIR layer (IR + pipeline + options)
  - C++ layer (object cache)
- Cosim schema id/versioning + forward-compatible commit bundles + mismatch DFX dump.

**Gates**
- Const canonicalization tests (success + failure diagnostics).
- Incremental build hit-rate / rebuild-scope gates (non-semantic edits should not
  force full rebuild).
- Cosim gates:
  - schema id/versioning validation
  - unknown fields ignored
  - mismatch triggers DFX dump with instance/obs-point context

## 2) Gate Levels (CI strategy)

- **G0**: build + formatting + minimal unit tests
- **G1**: IR legality gates (must pass on every PR)
  - no-dynamic, flat-types, comb-cycle, logic-depth
- **G2**: cross-backend equivalence gates (merge blocker)
  - C++ vs Verilog/Verilator on curated litmus + examples
- **G3**: system gates (nightly)
  - LinxCore: runner protocol, cosim smoke, trace schema, perf regression

## 3) Verification Matrix (minimum coverage)

A) IR legality
- control-flow lowering correctness (if/for)
- no residual SCF/index
- comb-cycle detection (local + cross-instance)
- logic-depth propagation (local + cross-instance)

B) State semantics
- reg reset/init patterns
- memory tick-read / transfer-write
- RDW old-data default

C) DFX
- probe sampling at tick/xfer
- bundle probe expansion
- trace DSL: instance glob + tags + window/trigger

D) Incremental build
- const param change rebuild scope
- non-semantic edits do not invalidate caches

E) Cosim
- schema id/versioning
- forward-compatible parsing
- mismatch DFX dump workflow

## 4) Multi-agent execution model (batching)

Agents should work in mergeable batches to avoid conflicts:

- **Batch 1**: comb-cycle + depth instance-aware + regression tests (0134/0135)
- **Batch 2**: obs points + mem/reset verifiers + litmus tests (0121/0114/0115)
- **Batch 3**: bundle/flatten/layout_id + probe expansion (0138/0143)
- **Batch 4**: trace DSL + hardened probe maps (0140/0145/0132)
- **Batch 5**: const canonicalization + incremental build caches (0139/0141/0144)
- **Batch 6**: cosim schema + mismatch DFX dump tooling (0142/0146)

Each batch PR MUST attach:
- implemented decision IDs
- gate commands executed
- logs (or pointers under `docs/bringup/gates/logs/<run-id>/...` when applicable)
