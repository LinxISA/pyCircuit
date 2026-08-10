# Phase 1 completion audit

Audit of `codex/phase1-review-repair` against the Phase 1 completion audit in
[the ACIR/ACSim implementation plan](../superpowers/plans/2026-08-04-acir-acsim-implementation.md).

Reviewed commit range:
`d74a24ed4cd54f94e7084ae121e2973800b259b2..fd3326c74e933fca4f1c5a42b27a0edd6efee8df`
(111 commits). The individual SHAs are recorded in
[reviewed-commits.txt](reviewed-commits.txt) and rendered into the generated
[spec coverage ledger](spec-coverage.md).

## 1. ODS and normative inventories — PASS

`scripts/check-ir-coverage.py` and `scripts/audit-op-coverage.py` report no
inventory or coverage gaps. All 53 public operations have positive and
negative lit coverage, and the generated ledger is current.

## 2. Verification rules and negative tests — PASS

The ACIR, ACSim, binding, freeze, conversion, and process-state verifier rules
are covered by the negative suites under `test/`. The repair review added or
strengthened atomic conversion failure, whole-model lowering, typed activation,
pure result/export, live scalar wrapper, and public-package consumer coverage.

## 3. Placeholder, legacy, and stale-epoch scan — PASS

The repository contract suite rejects placeholder markers, stale epochs,
broken documentation links, incomplete schemas, and stale IR coverage. All 27
contract and coverage tests pass.

## 4. Clean build and gate matrix — PASS

Fresh out-of-tree builds were configured from this worktree:

- Debug: complete build, 72/72 lit tests, 10/10 CTest suites.
- Release: complete build, 72/72 lit tests, 10/10 CTest suites.
- Contract tests: 27/27.
- IR inventory and coverage gates: pass.
- Repository-wide clang-format dry run: pass.
- Repository-wide configured clang-tidy gate: pass. On macOS the invocation
  includes the active SDK sysroot and libc++ because Homebrew clang-tidy does
  not inherit the Apple compiler driver defaults.
- `git diff --check`: pass.
- Install-to-consumer: the Release package installs, the external consumer
  configures and builds against it, and `process-state-plan-consumer` exits 0.

The clean all-target build exposed a missing transitive
`MLIRControlFlowDialect` link on `ACSimDialect`; it was fixed in `fd3326c`.

## 5. Determinism — PASS

`scripts/audit5-determinism.sh` ran the canonicalize/freeze pipeline five times
over all 11 audit inputs. Each input produced one text hash, one bytecode hash,
and, where applicable, one topology digest. The helper now accepts an `OPT`
override so an audited out-of-tree tool can be selected explicitly.

## 6. Requirements and code-quality review — PASS

The full implementation was reviewed inline as requested. The review found and
repaired the following release blockers:

- production process-state planning did not publish complete continuation,
  liveness, and fairness state;
- scalar live state omitted the normative wrapper callees and explicit
  wrap/unwrap actions;
- hierarchy expansion, module ordering, fingerprints, process bodies, typed
  captures, graph activation, pure results, and atomic publication were
  incomplete;
- installed CMake targets omitted parts of the public lowering/codegen surface;
- the ACSim dialect omitted its ControlFlow link dependency.

The final review found no remaining Phase 1 correctness blocker. Unsupported
topologies continue to fail with explicit lowering diagnostics and remain
scheduled for later roadmap phases rather than being silently accepted.

## 7. Reviewed commits — PASS

The 111 reviewed SHAs are listed in
[reviewed-commits.txt](reviewed-commits.txt). The audit-record commit is outside
that reviewed range by construction.

## Verdict

All seven Phase 1 completion steps pass. Phase 1 is ready to merge to `main`,
and Phase 2 may begin from that merged baseline.
