# Phase 1 completion audit

Audit of branch `feature/acir-acsim` against the seven-step Phase 1
completion audit defined in
[docs/superpowers/plans/2026-08-04-acir-acsim-implementation.md](../superpowers/plans/2026-08-04-acir-acsim-implementation.md)
("Phase 1 completion audit").

Reviewed commit range:
`d74a24ed4cd54f94e7084ae121e2973800b259b2..6b7c2223ca6fb1b134472cafdba8cb56a442fd9a`
(87 commits). The individual SHAs are recorded in the
[spec coverage ledger](spec-coverage.md) under "Reviewed commits",
generated from [reviewed-commits.txt](reviewed-commits.txt).

## 1. ODS vs normative inventories — PASS

`scripts/check-ir-coverage.py` (gate) and `scripts/audit-op-coverage.py`
both report zero gaps: every ODS operation/type in the ACIR and ACSim
dialects matches the normative v0.1 inventory manifests, and every
operation has positive and negative lit coverage. The committed ledger is
byte-identical to the freshly generated one.

## 2. Verification rules mapped to negative tests — PASS (one gap found and fixed)

Every `Required verification` item, every freeze invariant in
[docs/specs/acir-core-v0.1.md](../specs/acir-core-v0.1.md), every ACSim
verifier rule, and every binding rule was mapped to an observed negative
test. Group-level mapping:

| Rule group | Negative coverage |
| --- | --- |
| Hierarchy, ownership, path assignment | `test/ACIR/hierarchy-invalid.mlir`, `test/ACIR/review-r1-invalid.mlir`, `test/ACIR/review-r2-invalid.mlir` |
| Collections, views, cardinalities | `test/ACIR/collections-invalid.mlir` |
| Interfaces, protocols, roles, port mapping | `test/ACIR/interfaces-invalid.mlir`, `test/ACIR/interfaces-review-r1-invalid.mlir`, `test/ACIR/protocols-invalid.mlir`, `test/ACIR/protocols-review-r1-invalid.mlir`, `test/ACIR/port-mapping-review-r2.mlir` |
| Type and record declarations | `test/ACIR/declarations-invalid.mlir`, `test/ACIR/records-invalid.mlir`, `test/ACIR/types-invalid.mlir` |
| Address spaces, maps, time domains, capability limits | `test/ACIR/address-time-invalid.mlir` (includes the 256-relation limit test) |
| Resources, arbitration, ownership order | `test/ACIR/resources-invalid.mlir`, `test/ACIR/resources-forward-malformed-invalid.mlir`, `test/ACIR/ownership-order-review-r2.mlir` |
| Processes, runtime references, guarantees | `test/ACIR/process-invalid.mlir`, `test/ACIR/runtime-references-invalid.mlir`, `test/ACIR/guarantees-review-r1-invalid.mlir` |
| Trace operations | `test/ACIR/trace-invalid.mlir` |
| Freeze invariants, fan-out/flow cardinality, zero-delay cycles | `test/Transforms/freeze-topology.mlir`, `test/Transforms/verify-model.mlir`, `test/Transforms/zero-delay-cycles.mlir` |
| ACSim op/type verifier rules | `test/ACSim/ops-invalid.mlir`, `test/ACSim/types-invalid.mlir` |
| Conversion and binding rules (`ACLOWER-BINDING-*`) | `test/Conversion/bindings-invalid.mlir`, `test/Conversion/process-invalid.mlir`, `test/Conversion/atomic-failure.mlir`, `test/Bindings/resolve-ambiguous.mlir`, `test/Bindings/resolve-empty.mlir`, `test/Bindings/resolve-missing.mlir` |

Findings:

- **Gap fixed.** The 256-relation mixed-interleave capability limit in
  `lib/Dialect/ACIR/ACIRResources.cpp` had no negative test. Added one to
  `test/ACIR/address-time-invalid.mlir` (33 map entries producing 272
  relations, exceeding the 256 limit); committed as `3c8399e`.
- **Structural note.** The freeze invariant "no implicit fan-in" has no
  dedicated negative test because it is structurally unrepresentable:
  SSA values are single-def, so implicit fan-in cannot be expressed in
  the IR at all. Fan-out is covered (`test/Transforms/verify-model.mlir`
  flow-cardinality checks).

## 3. Placeholder / legacy / stale-epoch scan — PASS

Tracked files scanned for unfinished-work markers, skipped tests, legacy
aliases, component-specific emitter names, and stale epoch strings. The
only hits are the intentional "0.2" negative fixtures used to test epoch
rejection and the pattern strings inside the checker scripts themselves.

## 4. Clean-tree build and gate matrix — PASS

From a fresh clone (`/tmp/acir-audit`, synced to `6b7c222`):

- `dev-llvm22` configure with `-DACIR_ENABLE_ASSERTIONS=ON`: build clean;
  67/67 lit tests, 10/10 unit tests (ctest).
- `release-llvm22`: build clean; 67/67 lit, 10/10 ctest.
- 30/30 contract tests; IR coverage gate; clang-format gate;
  `git diff --check` clean.
- Install-to-consumer flow: consumer project configures, builds, and
  exits 0 against the installed package.

## 5. Deterministic canonicalization and bytecode hashes — PASS

`scripts/audit5-determinism.sh` runs the project pipeline
`ac-canonicalize-model,ac-freeze-topology` five times each over 11 inputs
(4 freezable models including extracted `freeze-topology` /
`deterministic-canonicalization` sections, 7 canonicalize-only valid
models). Across all runs and all inputs the textual IR hash, the
`--emit-bytecode` hash, and the frozen `ac.topology_digest` are each
identical (one unique value per input). The lit test
`test/Transforms/deterministic-canonicalization.mlir` additionally
proves two syntactically different but semantically equal inputs produce
byte-identical bytecode.

## 6. Requirements and code-quality review — PARTIAL (documented limitation)

An independent human review could not be requested from within this
session; a documented self-review was performed instead:

- Full-branch diff scan (87 commits, 251 files) for debug leftovers,
  commented-out code, hardcoded paths, and suppression creep.
- Flagged items verified benign: `llvm::errs()` uses are the
  flag-gated `--acir-test-pass-trace` instrumentation and CLI error
  reporting in `tools/acir-opt/acir-opt.cpp`; `llvm_unreachable`
  additions are enum-exhaustiveness defaults; the five `NOLINT`
  suppressions were reconciled with the format gate in `6b7c222`.
- clang-tidy gate (`.clang-tidy`, `WarningsAsErrors: '*'`) is clean
  across the full tree.

**Limitation:** plan step 6 asks for an *independent* review. This
remains required before merge; this audit does not substitute for it.

## 7. Reviewed commit SHAs recorded — PASS

The reviewed range (87 SHAs) is listed in
[reviewed-commits.txt](reviewed-commits.txt) and rendered into the
"Reviewed commits" section of the generated
[spec coverage ledger](spec-coverage.md). The commit that records the
list is by construction outside the reviewed range.

## Verdict

Steps 1–5 and 7 pass. Step 6 is partially satisfied by a documented
self-review; an independent human requirements/code-quality review is
the remaining gate before Phase 2 may begin.
