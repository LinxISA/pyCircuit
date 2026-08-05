# Task 2 report: ACIR lowerability and pure-continuation expansion

## Outcome

Implemented the shared private ACIR lowerability/preflight authority, public
out-of-line Normalize/Verify factories, iterative pure-call/static-loop
expansion records, dynamic-loop phase expansion, stable call-site/value
identity propagation, hostile-depth coverage, and the normative `scf.for`
hard break. Commit: `7d327ff7da8acbfa062a771d3dbcc2f1c22b026e`
(`feat(lowering): validate and expand process continuations`).

## Files changed

- `lib/Dialect/ACIR/ProcessLowerability.h` (new)
- `lib/Dialect/ACIR/ProcessLowerability.cpp` (new)
- `lib/Dialect/ACIR/ACIROps.cpp`
- `lib/Dialect/ACIR/CMakeLists.txt`
- `lib/Analysis/ModelAnalysis.cpp`
- `lib/Analysis/ProcessStateExpansion.cpp` (new)
- `lib/Analysis/ProcessStatePlanInternal.h`
- `lib/Analysis/CMakeLists.txt`
- `lib/Transforms/NormalizeACIRFile.cpp` (new)
- `lib/Transforms/VerifyACIRFile.cpp` (new)
- `include/acir/Transforms/Passes.h`
- `include/acir/InitAllPasses.h`
- `lib/Transforms/CMakeLists.txt`
- `tools/acir-opt/acir-opt.cpp`
- `unittests/Analysis/ProcessStatePlanVerifierTest.cpp` (new)
- `unittests/Analysis/CMakeLists.txt`
- `test/Analysis/process-state-verifier.mlir` (new)
- `test/Analysis/raw-structure-preflight.mlir` (new)
- `test/ACIR/process-invalid.mlir`
- `docs/specs/acir-core-v0.1.md`

The listed `ProcessStatePlan.cpp` mutation was unnecessary because Task 1 had
already centralized occurrence/value accessors and hashing; Task 2's new
private storage and expansion implementation live in
`ProcessStatePlanInternal.h` and `ProcessStateExpansion.cpp` without changing
the Task 1 public API or adding `planProcessState` early.

## RED evidence

Tests were written before the production factories/authority.

Command:

```text
cmake --build build/dev-llvm22 --target ACIRProcessStatePlanTests -j4
```

Initial attributable failure after correcting fixture-only syntax:

```text
ProcessStatePlanVerifierTest.cpp:63:19: error: use of undeclared identifier
'createNormalizeACIRFilePass'
ninja: build stopped: subcommand failed.
```

This independently established RED at the public factory boundary before the
factory/preflight implementation existed. Subsequent expansion tests were
added against the private `ExpandedProcess` API and exercised nested calls,
multi-results, two call sites, static iteration identities, synthetic constant
ordinals, and dynamic loop phases.

## Implemented contract

- `RawModelStructureLimits` defaults are nodes `1U << 20`, edges `1U << 22`,
  and nested depth `512`.
- Neutral iterative raw/structured walkers, exact static-for analysis, and
  process lowerability are owned by the private ACIR dialect helper.
- Depth 512 is admitted; depth 513 and programmatically constructed depth
  10,000 fail with the one exact raw-depth diagnostic before Normalize starts.
- Concrete Normalize/Verify pass types are private to their `.cpp` files.
  Registration and the automatic tool pipeline use only the public factories
  in Normalize-then-Verify order.
- Normalize preflights before `normalizeAddressMaps`; Verify preflights before
  epoch, canonical ACSim, type, attribute, location, or region walks.
- Expansion is iterative, emits no `func.call`/`func.return` action, preserves
  outer-to-inner call-site chains, process/function-root paths, original value
  coordinates, full iteration vectors, and explicit argument/result/loop
  forwarding.
- Constant loops produce per-iteration synthetic induction constants with
  dense distinct ordinals. Dynamic suspending loops produce initialize,
  signed-less-than `arith.cmpi`, and `arith.addi` increment phase actions.
- Pure-call function/edge/depth limits, recursion, external declarations,
  effectful bodies, unsupported `cf`, dynamic non-suspending loops,
  non-positive steps, and static trip overflow/cap are rejected.
- ACIR core v0.1 now states the static-trip-or-every-backedge-suspends hard
  break and explicitly denies a compatibility lowering.

## Factory-only proof and depth fixtures

Every `ProcessStatePlanNormalizeFactoryTest` case calls a helper that creates a
fresh `mlir::PassManager`, calls `enableVerifier(false)`, installs test-local
instrumentation, and adds exactly `createNormalizeACIRFilePass()`.

- Depth 512: success; exact trace
  `enter:normalize-ac-file`, `complete:normalize-ac-file`; no diagnostics; an
  address-map permission list is observably normalized to
  `["execute", "write"]`.
- Depth 513: failure; exact trace `enter:normalize-ac-file`,
  `fail:normalize-ac-file`; exactly one diagnostic containing
  `whole-model region nesting exceeds ACIR v0.1 capability limit 512`.
- Depth 10,000 verifier-malformed raw module: same exact failure trace and one
  diagnostic, without recursive Normalize work or a crash.

The lit fixture has six explicit `%acir_opt --verify-each=false` invocations:
default and registered-option modes for each of depths 512, 513, and 10,000.

## GREEN evidence

Focused build and factory tests:

```text
cmake --build build/dev-llvm22 --target ACIRModelAnalysisTests \
  ACIRProcessStatePlanTests acir-opt-internal -j4
build/dev-llvm22/bin/ACIRProcessStatePlanTests \
  --gtest_filter='ProcessStatePlanNormalizeFactoryTest.*'
```

Result: build succeeded; 3/3 factory tests passed.

Focused lowerability/expansion tests:

```text
build/dev-llvm22/bin/ACIRProcessStatePlanTests \
  --gtest_filter='ProcessStatePlanVerifierTest.*:ProcessStatePlanPureCallTest.*'
.venv/bin/lit -v build/dev-llvm22/test/Analysis/process-state-verifier.mlir
.venv/bin/lit -v build/dev-llvm22/test/Analysis/raw-structure-preflight.mlir
```

Result: 3/3 unit tests passed; both lit tests passed.

Focused requested lit gate:

```text
.venv/bin/lit -v \
  build/dev-llvm22/test/ACIR/process-invalid.mlir \
  build/dev-llvm22/test/Transforms/freeze-topology.mlir \
  build/dev-llvm22/test/Analysis/process-state-verifier.mlir \
  build/dev-llvm22/test/Analysis/raw-structure-preflight.mlir
```

Result: 4/4 passed.

## Mutation closure

All temporary changes were restored before final verification.

1. Disabled dynamic-loop rejection. `process-state-verifier.mlir` failed at
   the `DYNAMIC` check because the forbidden loop printed successfully.
2. Cleared original occurrence call-site chains. The nested-call unit failed
   with occurrence call-site size `0` versus expected `2`.
3. Collapsed synthetic constant ordinal to zero. The static-loop unit failed
   with `{0, 0}` versus expected `{0, 1}`.
4. Bypassed Normalize raw preflight. The depth-513 factory test failed because
   Normalize completed successfully, emitted no diagnostic, and recorded
   `complete` instead of `fail`.
5. Bypassed Verify raw preflight. The strict two-source preflight guard failed
   because only one pass retained the required leading check.
6. Inserted `createVerifyACIRFilePass()` before isolated Normalize. The
   depth-512 factory proof failed with exact trace
   `enter:verify-ac-file`, `fail:verify-ac-file`, demonstrating independence
   from CLI/default-pipeline wiring.
7. Reintroduced an inline concrete pass declaration marker. The required
   concrete-pass/direct-construction `rg` guard failed on
   `InitAllPasses.h`.

## Full gates

Fresh final repository-wide gates after the last production edit:

```text
cmake --build build/dev-llvm22 --target check-acir -j4
```

Result: all 58 lit tests passed.

```text
ctest --test-dir build/dev-llvm22 --output-on-failure
```

Result: 7/7 test targets passed, 0 failed (including all model-analysis and
process-state-plan tests).

Strict checks:

```text
git diff --check
! rg -n 'class (NormalizeACIRFilePass|VerifyACIRFilePass)|make_unique<acir::(NormalizeACIRFilePass|VerifyACIRFilePass)>' \
  include/acir/InitAllPasses.h tools/acir-opt/acir-opt.cpp
! rg -n 'normalizeAddressMaps\(' include/acir/InitAllPasses.h
test "$(rg -l 'if \(mlir::failed\(ac::preflightRawModelStructure\(module\)\)\)' \
  lib/Transforms/NormalizeACIRFile.cpp lib/Transforms/VerifyACIRFile.cpp | wc -l | tr -d ' ')" -eq 2
! rg -n 'ProcessLowerability' include
```

Result: all passed. Modified C++ files were run through LLVM
`clang-format`; no whitespace errors or temporary mutation/debug markers
remain.

## Self-review

- No public ProcessStatePlan API shape changed and no planner/writer entrypoint
  was added early.
- No public header exposes the private lowerability helper.
- ACIRDialect has no Analysis dependency; ACIRAnalysis consumes the helper
  through a private `${PROJECT_SOURCE_DIR}/lib` include path.
- Both file passes preflight before their first recursive operation.
- Registration/default pipeline order is factory-based Normalize then Verify.
- Isolated factory tests contain no Verify factory/default pipeline.
- Depth boundary and exact diagnostic behavior are stable at 512/513.
- Expansion retains physical operation ordinals and complete call-site and
  iteration context; raw `mlir::Value` is provenance, not deduplication.
- No `func` action remains in expanded output; returns are forwarding only.
- Dynamic loop phase spellings are `arith.cmpi`/`slt` and `arith.addi`.
- All mutation changes were restored and the committed worktree contains no
  debug-only code.

## Concern

LLVM 22's textual parser itself stack-faulted on 10,000 recursively nested
custom-syntax `builtin.module` bodies before any pass could run. The internal
tool therefore has a bounded, quote/comment-aware textual delimiter safety
check above depth 2048 that emits the same compact ACIR depth diagnostic before
parsing. Depth 513 still reaches and is rejected by the Normalize factory, and
the isolated factory test proves the actual pass handles a programmatically
constructed 10,000-deep verifier-malformed module without recursion. Thus the
10,000 CLI case is safe and deterministic, but its earliest rejection occurs
at the driver parser-safety boundary rather than inside the default Normalize
pass.

---

# Review fix round 1/5

## Outcome

Closed every SPEC/QUALITY finding from the first review without changing the
Task 1 public API. The production textual depth scanner was removed; process
and callee lowerability/purity now have one private Analysis authority;
expansion consumes its validated immutable records; all depth, call-graph,
expansion, forwarding, and synthetic-action accounting is iterative and
limit-checked.

## Production fixes

- `validatePureProcessCallGraph` is the single reachable-function indexing,
  recursion, function/edge/depth limit, lowerability, and transitive purity
  authority. `ModelAnalysis::verifyPureProcessCalls` delegates to it and
  expansion consumes `ValidatedPureCallGraph` records instead of repeating
  symbol indexing or callee validation.
- Reachable callees use the same `verifyProcessLowerability` contract as
  processes. Dynamic non-suspending `scf.for`, unsupported control flow, and
  effectful bodies are rejected through that shared boundary.
- Suspension guarantees are computed with an explicit bottom-up worklist only
  after the bounded structural walk succeeds. There is no recursive
  `regionGuaranteesSuspension` path.
- Expanded value lookup uses definition-context bindings and longest-prefix
  context resolution. Loop-invariant definitions retain their outer
  occurrence rather than inheriting a consumer iteration.
- Every pending expansion task, synthetic action, operation operand edge, and
  forwarding edge is reserved against exact node/edge budgets before
  insertion. Calls and returns are tasks and therefore consume budget.
- Dynamic loop initialize/condition/increment actions carry explicit planned
  operands/results. Condition uses literal `arith.cmpi`, predicate
  `2 : i64`, properties `{}`, and an `i1` synthetic result; increment uses
  literal `arith.addi`, properties `{}`, and explicit induction/step/result
  values. Initialize and increment forwarding is explicit.
- The production pre-parse delimiter-depth heuristic is gone. The internal
  test tool alone exposes hidden options that materialize nested ModuleOps
  after shallow parsing, optionally add a verifier-invalid `scf.yield`, and
  insert that pass immediately before the exact default/registered factories.
  Hidden pass instrumentation proves precise pipeline reach/order/failure.
- `MLIRFuncDialect` is now an explicit public link dependency of ACIRDialect,
  matching its private lowerability implementation's `func.call` and
  `func.return` type use.

## New regression proof

- Real canonical occurrence JSON replaces hand-built call-site JSON in tests;
  the real SHA-256 occurrence helper proves distinct stable hashes for distinct
  call sites.
- A two-iteration static-loop test proves a loop-invariant definition has the
  same serialized outer occurrence (`iteration_vector:[]`) in both consumers,
  while the iteration-local result occurrences differ.
- Pure-call function, edge, and depth limits each have exact-boundary success
  and one-less failure cases via private injectable limits.
- Expansion node and edge counts have exact-boundary success and one-less
  failure cases using the actual final counters.
- Dynamic phase tests assert every operand/result arity and type plus exact
  scalar name, attribute name/value/count, and properties.
- A programmatic 10,000-deep `scf.if` suspension shape produces exactly one
  depth-limit diagnostic before summary analysis, without recursive failure.
- Callee lit/unit coverage includes dynamic non-suspending loops, unsupported
  `cf`, external/recursive callees, and an effectful reachable callee.
- The six parser-safe CLI cases cover default and registered pipelines at
  depths 512, 513, and verifier-malformed 10,000, with exact enter/complete or
  enter/fail traces and no downstream pass entry after preflight failure.

## Verification

Fresh pre-commit verification after the final production edit:

```text
cmake --build build/dev-llvm22 -j4
```

Result: full build succeeded.

```text
ctest --test-dir build/dev-llvm22 --output-on-failure
```

Result: 7/7 test targets passed, 0 failed.

```text
ACIR_TEST_EXEC_ROOT=$PWD/build/dev-llvm22/test \
ACIR_TOOLS_DIR=$PWD/build/dev-llvm22/bin \
LLVM_TOOLS_DIR=/opt/homebrew/opt/llvm/bin \
.venv/bin/lit -v test
```

Result: 58/58 lit tests passed.

`git diff --check` passed, the removed scanner symbol and obsolete duplicated
purity traversal patterns are absent, and LLVM `clang-format` was applied to
all modified C++ sources and private headers.

## Remaining concerns

None. The hostile CLI coverage now reaches the real pass pipeline only after a
safe shallow parse and is compiled exclusively into `acir-opt-internal`; the
public driver has no equivalent option, materializer, trace hook, or textual
depth heuristic.

## Round 2 follow-up fixes (2026-08-06)

- Dynamic `scf.for` expansion now emits the missing ordered
  `scf.yield`-to-region-iter-argument backedges. A literal two-carry fixture
  checks all six initialization/backedge/exit value IDs and canonical
  occurrences, and its oracle rejects dropped or reordered edges.
- Expansion cap tests use hand-counted literals: 4 nodes/4 edges for the call
  fixture and 14 nodes/12 edges for the dynamic-loop fixture. The latter
  separately accounts for 11 physical tasks, 3 synthetic actions, 10 operand
  edges, and 2 forwarding edges.
- Expanded SSA bindings are keyed by canonical scoped context and resolved by
  bounded ancestor iteration lookup. The 512-iteration work oracle records
  1,536 total probes and at most 2 probes per lookup, avoiding history scans.
- Validated pure functions remain sorted and are now queried by binary search;
  the admitted 1,024-function chain requires at most 11 probes. Duplicate
  function symbols are rejected explicitly and have a regression test.
- Reachable-callee effect validation is explicit iterative postorder. Local
  interfaces are queried only on non-recursive-effect operations; structured
  operations aggregate already-computed child summaries. An 8,192-deep pure
  `scf.if` nest succeeds, then fails with exactly one diagnostic after an
  effectful leaf is inserted.
- Raw-depth failures compare the normalized complete output stream with
  `diff -u`, covering both default and explicitly registered pipelines at
  depths 513 and 10,000; unexpected additional diagnostics are rejected.

## Round 2 mutation evidence

- Removing physical-task node accounting produced 3 instead of 14 nodes;
  removing synthetic-action accounting produced 11 instead of 14.
- Removing physical operand accounting produced 2 instead of 12 edges;
  removing one forwarding reservation produced 11 instead of 12.
- Replacing the bounded binding-work result with cumulative work produced a
  1,536-probe maximum instead of the required maximum of 2.
- Restoring linear function lookup produced 1,024 probes instead of at most
  11.
- Calling recursive memory-effect interfaces at every structured node made
  the hostile test take 16.5 seconds versus 1.8 seconds for iterative local
  aggregation, exposing the quadratic recursive traversal.
- Injecting one additional CLI output line made the exact-stream lit test fail
  with the unexpected line in the unified diff.

## Round 2 verification

- `git diff --check` passed.
- `cmake --build build/dev-llvm22 --target check-acir -j2` passed all 58 lit
  tests.
- `ctest --test-dir build/dev-llvm22 --output-on-failure -j2` passed all 7
  test targets, including `ACIRProcessStatePlanTests`.

## Round 2 remaining concern

The separately configured ASan executable aborts while constructing the first
`MLIRContext` with an LLVM allocator `use-after-poison`, before any task test
body runs. The non-ASan LLVM 22 development build passes all repository gates;
this toolchain-level ASan incompatibility was not changed in this task.
