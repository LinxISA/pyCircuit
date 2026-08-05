# ACIR ProcessStatePlan Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the complete non-mutating, deterministic, bounded
ProcessStatePlan contract that converts every frozen ACIR process into an
immutable direct input for one later atomic ACIR-to-ACSim conversion.

**Architecture:** The final public API is fixed once, while implementation is
delivered through seven buildable TDD checkpoints. ACIR lowerability lives in a
private Dialect helper so the verifier and Analysis share one authority without
a Dialect-to-Analysis dependency cycle. Analysis is split by identity,
expansion, continuation, wake, liveness, cost, and report responsibilities;
only the façade returns the final immutable plan.

**Tech Stack:** C++20, LLVM/MLIR 22.1.8, MLIR SCF/Func/CF/Arith/Index,
GoogleTest, llvm-lit/FileCheck, LLVM JSON/RFC 8785 helpers, Draft 2020-12 JSON
Schema, Python unittest.

## Global Constraints

- The exact public API, enum spellings, stable paths, type keys, generated
  specialization records, helper payloads, yield-only fixture, private test
  hooks, Task 13 consumer boundary, and lowerability ownership are frozen in
  `docs/superpowers/specs/2026-08-05-acir-process-state-plan-contract-design.md`.
  The complete approved behavior is also recorded in
  `.superpowers/sdd/2026-08-04-acir-acsim-implementation/task-12-brief.md`; no
  task may narrow either source. Task 1 copies the public normative contract
  into the tracked user-facing specification.
- Contract epoch is exactly `"0.1"`; this is a hard break with no legacy API,
  compatibility accessor, fallback lookup, warning path, or partial ACSim
  stage.
- Planning never mutates frozen ACIR, never creates ACSim, and never publishes
  a partial plan or report.
- Public records are value-semantic and immutable. Task 13 consumes direct
  handles, dense IDs, exact types, and exact costs; it does not parse JSON or
  redo liveness, call expansion, symbol resolution, specialization, or control
  reconstruction.
- Current ACIR process syntax has no external process-call mapping. The plan
  contains generated implementation callees only; Task 13 consumes external
  `BindingResolutionResult` independently.
- All hostile-input tree, graph, liveness, call, continuation, path, and report
  traversals are iterative and capability bounded. Maximum admitted nested
  region depth is exactly `512`.
- Default model-wide caps are exact: processes `1U << 20`, PCs `1U << 20`, live
  slots `1U << 20`, wakes `1U << 20`, generated callees `1U << 20`, planned
  operations `1U << 20`, fairness work `1U << 20`, transitions `1U << 22`,
  nested depth `512`, report bytes `1U << 24`.
- Every public C++ API, record arm, report field, verifier branch, boundary,
  pass option, and schema union receives real behavioral coverage. Tests use
  literal hand-derived expectations, not planner helpers or source grep.
- Every task records RED, GREEN, mutation, focused/full verification, and
  self-review in its ignored report. Intermediate commits are reviewed
  checkpoints on the feature branch, not release-compatible subsets.
- The seven commits remain as professional incremental history; Task 7 uses
  the required final subject `feat(lowering): plan ACSim process state` without
  rewriting or squashing earlier reviewed commits.

## Fixed file ownership

- `include/acir/Analysis/ProcessStatePlan.h`: final immutable public records,
  enums, dense IDs, and accessors in Task 1; complete planner/writer façade is
  added only with its real implementation in Task 5.
- `lib/Analysis/ProcessStatePlanInternal.h`: private storage, builders,
  work/cap accounting, and cross-file internal interfaces.
- `lib/Analysis/ProcessStatePlan.cpp`: public façade/accessors, orchestration,
  final plan-set invariant validation; no traversal algorithm.
- `lib/Analysis/ProcessStateIdentity.cpp`: structural paths, original/synthetic
  occurrence IDs, occurrence-qualified planned values, canonical ordering.
- `lib/Analysis/ProcessStateExpansion.cpp`: iterative pure-call expansion,
  constant-loop expansion, dynamic-loop phase actions, forwarding graph.
- `lib/Analysis/ProcessStateContinuation.cpp`: PC/block/frame/control-edge DAG
  construction and deterministic traversal.
- `lib/Analysis/ProcessStateWake.cpp`: captures, suspension/wake/subscription
  planning, declaration resolution, transitions.
- `lib/Analysis/ProcessStateLiveness.cpp`: occurrence-qualified equivalence,
  live slots, stores/loads, wrap/unwrap insertion, realization interning.
- `lib/Analysis/ProcessStateCost.cpp`: exact emitted-operation block costs,
  acyclic maximum-path fairness, overflow/cap checks.
- `lib/Analysis/ProcessStateReport.cpp`: canonical JSON, semantic report
  verification, byte preflight, atomic publication.
- `lib/Dialect/ACIR/ProcessLowerability.h` and `.cpp`: private shared ACIR
  verifier/analysis lowerability preflight; ACIRDialect never links Analysis.
- `lib/Transforms/NormalizeACIRFile.cpp`,
  `lib/Transforms/VerifyACIRFile.cpp`, public
  `include/acir/Transforms/Passes.h`, registration-only
  `include/acir/InitAllPasses.h`, `lib/Transforms/CMakeLists.txt`, and the
  default pipeline in `tools/acir-opt/acir-opt.cpp`, plus isolated factory
  coverage in `unittests/Analysis/ProcessStatePlanVerifierTest.cpp`: Task 2
  owns the complete factory migration and preflight-before-recursion boundary.
- `lib/Transforms/LowerProcessState.cpp`: final non-mutating
  `ac-lower-process-state` pass. Task 13 does not own this file.

## Plan activation gate

Before Task 1, the controller commits this staged plan, the exact tracked
contract-design document, and the corrected Task 12/13 file ownership in
`docs/superpowers/plans/2026-08-04-acir-acsim-implementation.md` with subject
`docs(plan): stage ProcessStatePlan implementation`, pushes it, and requires
push/PR CI green. The implementation starts from that clean pushed base; this
plan is never left as an untracked Task 7 cleanup item.

---

### Task 1: Define the immutable plan API and deterministic baseline

**Files:**

- Read:
  `docs/superpowers/specs/2026-08-05-acir-process-state-plan-contract-design.md`
- Create: `include/acir/Analysis/ProcessStatePlan.h`
- Create: `lib/Analysis/ProcessStatePlanInternal.h`
- Create: `lib/Analysis/ProcessStatePlanTestHooks.h`
- Create: `lib/Analysis/ProcessStatePlan.cpp`
- Create: `lib/Analysis/ProcessStateIdentity.cpp`
- Create: `lib/Analysis/ProcessStateReport.cpp`
- Modify: `lib/Analysis/CMakeLists.txt`
- Create: `schemas/acir-process-state-plan.schema.json`
- Create: `unittests/Analysis/ProcessStatePlanTestSupport.h`
- Create: `unittests/Analysis/ProcessStatePlanBasicTest.cpp`
- Modify: `unittests/Analysis/CMakeLists.txt`
- Modify: `tests/contracts/test_contracts.py`
- Modify: `scripts/check-contracts.py`
- Create: `docs/specs/acir-process-state-plan-v0.1.md`
- Modify: `README.md`
- Modify: `docs/superpowers/plans/2026-08-04-agentic-circuit-roadmap.md`

**Interfaces:**

- Consumes: frozen `mlir::ModuleOp`, ACIR `ProcessOp`, existing stable model
  definition keys and RFC-8785 canonicalization helpers.
- Produces final declarations in namespace `acir`:

```cpp
struct ProcessStateLimits;
class ProcessCalleeId;
class ProcessValueTypeId;
class ProcessCaptureId;
class ProcessPcId;
class ProcessBlockId;
class ProcessLiveSlotId;
class ProcessWakeId;
class ProcessTransitionId;

enum class ProcessWakeKind { Condition, Resource, EventQueue, NextDelta };
enum class ProcessActionKind {
  Original, Constant, ForInitialize, ForCondition, ForIncrement,
  ScalarWrap, ScalarUnwrap
};
enum class ProcessEmissionClass {
  CopyScalar, Inline, Invoke, Wrap, Unwrap, ForwardOnly
};

class ProcessCallSitePlan;
class ProcessOccurrenceId;
class ProcessPlannedValue;
class ProcessGeneratedCalleePlan;
class ProcessValueTypePlan;
class ProcessCapturePlan;
class ProcessSubscriptionSourcePlan;
class ProcessActionPlan;
class ProcessControlFramePlan;
class ProcessControlEdgePlan;
class ProcessBlockPlan;
class ProcessPcPlan;
class ProcessLiveSlotPlan;
class ProcessWakePlan;
class ProcessTransitionPlan;
class ProcessStatePlan;
class ProcessStatePlanSet;

mlir::LogicalResult
verifyProcessStatePlan(const ProcessStatePlanSet &plans,
                       const ProcessStateLimits &limits = ProcessStateLimits());
llvm::Expected<std::string>
serializeProcessStatePlan(const ProcessStatePlanSet &plans,
                          const ProcessStateLimits &limits = ProcessStateLimits());
```

  The header implements the complete identifiers, enum values, getter names,
  return types, active-arm rules, exact lookup, and façade publication stages
  from the contract design's "Dense identifier types" through "Public façade
  publication stages" sections. It has no setters, mutable `ArrayRef`,
  callback/emitter, fallback/component/hierarchy lookup, runtime descriptor,
  test hook, or `BindingResolutionResult` parameter.

- Constructors remain private/friend-only. A private `PlanSetBuilder` creates
  complete hand-derived empty and yield-only fixtures, assigns canonical dense
  IDs only at freeze, and exercises every public getter/report union without a
  temporary public builder. Task 5 adds the final `planProcessState` and atomic
  writer declarations together with their real definitions; Task 1 therefore
  has no undefined public symbol or transient public failure behavior.
- Task 1's private test-hook header publishes the complete frozen
  `ProcessStatePlanCorruptionForTest` enum and
  `cloneProcessStatePlanWithCorruptionForTest`. Verifier RED tests corrupt each
  semantic invariant through that surface before the Task 1 implementation is
  treated as green. Nothing enters `include/acir`.

- [ ] **Step 1: Write API and baseline RED tests**

  Add compile-time concepts proving every final getter exists and forbidden
  mutators/lookups do not. Use the private production builder on real
  parsed/frozen empty and `@Top::@workload` yield-only fixtures. Assert exact
  definition lookup, entry PC `entry`/ordinal `0`, width `1`, one next-delta
  wake and transition targeting entry, no live slots, no generated value
  types, and exactly one generated callee at ID `0`. Its specialization
  preimage and descriptor must equal the contract design's "Exact yield-only
  baseline" literals. The wake `type_key` is exactly
  `@acir_wake_next_delta`. In particular, the descriptor is exactly:

```json
{
  "cpp":"acir::generated::impl_wake_next_delta_63cacba5c3eb82976464804b4aeaa17d43b445733efaddfad7c7bec1ab650269",
  "effect":"stateful",
  "fingerprint":"sha256:63cacba5c3eb82976464804b4aeaa17d43b445733efaddfad7c7bec1ab650269",
  "inputs":[],
  "kind":"implementation",
  "ordinal":0,
  "payload":{"wake_kind":"next_delta","wake_type":"@acir_wake_next_delta"},
  "results":["@acir_wake_next_delta"],
  "role":"wake_next_delta",
  "source_paths":[],
  "symbol":"@acir_impl_wake_next_delta_63cacba5c3eb82976464804b4aeaa17d43b445733efaddfad7c7bec1ab650269"
}
```

  The distinct empty frozen-model fixture still produces literal empty
  canonical bytes:

```text
{"callees":[],"contract_epoch":"0.1","processes":[],"schema":"acir-process-state-plan-0.1","value_types":[]}
```

  Validate the complete closed Draft 2020-12 schema against the full literal
  builder fixture, prove every listed object rejects unknown fields, and change
  repository contract output from `9` to exactly `10` schemas. The branch union
  contains exactly `condition`, `false_bindings`, `false_block`, `kind`,
  `true_bindings`, and `true_block`; both binding arrays are required even when
  empty and preserve target block-argument order.
  Construct two semantically equivalent frozen declaration permutations and
  require byte-identical plan/report records and literal getter observations.
  For every `ProcessStatePlanCorruptionForTest` arm, require the public verifier
  to reject the corrupted immutable plan with the exact invariant diagnostic.

- [ ] **Step 2: Run RED**

```bash
cmake --build build/dev-llvm22 --target ACIRProcessStatePlanTests -j4
```

  Expected: compilation fails because `ProcessStatePlan.h`, dense IDs,
  immutable record accessors and serializer do not exist.

- [ ] **Step 3: Implement the final immutable storage façade**

  Implement private owned storage and public immutable views/accessors. Build
  the contract design's exact path grammar, definition-key ordering, exact-key
  missing/one lookup, PC width, yield-to-entry wake/transition, generated
  next-delta descriptor, empty/yield invariant validation, canonical
  serialization, and complete closed schema. Copy the complete
  approved contract into `docs/specs/acir-process-state-plan-v0.1.md` and link
  it from `README.md`; update the roadmap's specification count and table.

- [ ] **Step 4: Run GREEN and mutations**

```bash
cmake --build build/dev-llvm22 --target ACIRProcessStatePlanTests -j4
build/dev-llvm22/bin/ACIRProcessStatePlanTests \
  --gtest_filter='ProcessStatePlanApiTest.*:ProcessStatePlanBasicTest.*'
.venv/bin/python -m unittest tests.contracts.test_contracts -v
.venv/bin/python scripts/check-contracts.py
```

  Temporarily accept `@workload`, `workload`, or hierarchy guesses in lookup;
  reverse definition ordering; create a resume PC for `yield_sim`; and return PC
  width `0`. Each literal fixture must fail, then restore GREEN.

- [ ] **Step 5: Focused gate and commit**

```bash
ctest --test-dir build/dev-llvm22 \
  -R 'ACIR(ProcessStatePlan|ModelAnalysis)Tests' --output-on-failure
git diff --check
git commit -m "feat(lowering): define process-state plan API"
```

---

### Task 2: Close ACIR lowerability and expand pure continuations

**Files:**

- Create: `lib/Dialect/ACIR/ProcessLowerability.h`
- Create: `lib/Dialect/ACIR/ProcessLowerability.cpp`
- Modify: `lib/Dialect/ACIR/ACIROps.cpp`
- Modify: `lib/Dialect/ACIR/CMakeLists.txt`
- Modify: `lib/Analysis/ModelAnalysis.cpp`
- Modify: `lib/Analysis/ModelAnalysisInternal.h`
- Create: `lib/Analysis/ProcessStateExpansion.cpp`
- Modify: `lib/Analysis/ProcessStatePlanInternal.h`
- Modify: `lib/Analysis/ProcessStatePlan.cpp`
- Modify: `lib/Analysis/CMakeLists.txt`
- Create: `lib/Transforms/NormalizeACIRFile.cpp`
- Create: `lib/Transforms/VerifyACIRFile.cpp`
- Modify: `include/acir/Transforms/Passes.h`
- Modify: `include/acir/InitAllPasses.h`
- Modify: `lib/Transforms/CMakeLists.txt`
- Modify: `tools/acir-opt/acir-opt.cpp`
- Create: `unittests/Analysis/ProcessStatePlanVerifierTest.cpp`
- Create: `test/Analysis/process-state-verifier.mlir`
- Create: `test/Analysis/raw-structure-preflight.mlir`
- Modify: `test/ACIR/process-invalid.mlir`
- Modify: `unittests/Analysis/CMakeLists.txt`
- Modify: `docs/specs/acir-core-v0.1.md`

**Interfaces:**

- Produces the exact private dialect surface from the contract design's
  "Neutral dialect lowerability ownership" section:
  `RawModelStructureLimits`, `preflightRawModelStructure(ModuleOp, ...)`,
  `walkStructuredOperationsIterative(Operation *, function_ref<...>, ...)`,
  `StaticForTripCount`, `analyzeStaticFor(scf::ForOp)`, and
  `verifyProcessLowerability(Operation *, ...)`. It also produces private
  Analysis `ExpandedProcess`/`ExpandedAction` records. Dialect helpers depend
  only on ACIR/MLIR; Analysis consumes them through ACIRDialect.
- `RawModelStructureLimits` has exact defaults: nodes `1U << 20`, edges
  `1U << 22`, and nested region depth `512`. Task 2 moves the concrete
  `NormalizeACIRFilePass` and `VerifyACIRFilePass` into
  `lib/Transforms/NormalizeACIRFile.cpp` and
  `lib/Transforms/VerifyACIRFile.cpp`. Public
  `include/acir/Transforms/Passes.h` declares only
  `createNormalizeACIRFilePass()` and `createVerifyACIRFilePass()`;
  `InitAllPasses.h` contains no concrete file-pass class. Registration and the
  default `acir-opt` pipeline use the factories in normalize-then-verify
  order. Each concrete pass calls raw preflight before any recursive normalize
  or verifier walk. `ACIRTransforms` includes the private helper only through
  its existing private source include root; no public header exposes
  `ProcessLowerability.h`.
- Original occurrences and original planned values carry complete call-site
  chains, loop iteration vectors, paths/handles, and result/argument
  coordinates. They implement the exact tagged unions, active-arm getters,
  serialized field sets, path grammar, and occurrence hashing frozen in the
  contract design's "Stable call-site and occurrence identities",
  "Structural SSA coordinates and planned values", and "Stable path grammar"
  sections.
- `ProcessLowerability.h` is private under `lib/Dialect/ACIR`. ACIRDialect owns
  the neutral iterative region-depth/structured-operation walk and structured
  lowerability authority. `ACIRAnalysis` adds `${PROJECT_SOURCE_DIR}/lib` only
  as a private include directory and calls that helper; ACIRDialect never links
  Analysis. Existing raw-depth and pure-call logic is moved from
  `ModelAnalysis.cpp`/`ModelAnalysisInternal.h`, not duplicated.
- `unittests/Analysis/ProcessStatePlanVerifierTest.cpp` belongs to the existing
  `ACIRProcessStatePlanTests` target and contains suite
  `ProcessStatePlanNormalizeFactoryTest`. Each case constructs a fresh
  `mlir::PassManager`, calls `enableVerifier(false)`, and adds only
  `createNormalizeACIRFilePass()`. No default pipeline setup and no
  `createVerifyACIRFilePass()` call are permitted in this suite. Its exact
  fixtures are raw models at nested depth `512`, nested depth `513`, and a
  verifier-malformed raw model with `10,000` nested regions. Task 2 adds this
  file to `ACIRProcessStatePlanTests` in `unittests/Analysis/CMakeLists.txt`
  and links `ACIRTransforms` and `MLIRPass` privately if the target does not
  already receive them.

- [ ] **Step 1: Write lowerability and expansion RED tests**

  Cover exact positive-step static `scf.for`; dynamic `scf.for` only when every
  reachable backedge suspends; rejection of dynamic non-suspending,
  non-positive-step, trip-count overflow/cap, unsupported/effectful operations,
  arbitrary cyclic `cf`, recursion, and external function declarations. Cover
  nested pure calls, multiple returns, two call sites, call-site-qualified
  values, full static loop iteration vectors, two distinct constants at one
  anchor, literal process-root/function-root paths and closed call-site JSON,
  pure-call node/edge/depth limits, and depth `512`/`513` plus a `10,000`-deep,
  syntactically parseable but verifier-malformed raw tree.

  Add these three named tests to
  `unittests/Analysis/ProcessStatePlanVerifierTest.cpp`:

  - `ProcessStatePlanNormalizeFactoryTest.Depth512ReachesAndSucceedsThroughIsolatedNormalizePass`;
  - `ProcessStatePlanNormalizeFactoryTest.Depth513FailsRawStructuralPreflightBeforeNormalizeRecursion`;
  - `ProcessStatePlanNormalizeFactoryTest.VeryDeepMalformedFailsRawStructuralPreflightWithoutRecursion`.

  Every test creates its own `mlir::PassManager`, disables pass-manager
  verification with `enableVerifier(false)`, and adds exactly one pass from
  `createNormalizeACIRFilePass()`. Test-local `mlir::PassInstrumentation`
  records pass entry/completion/failure. The depth-`512` raw fixture must
  return success, record only Normalize entry and completion, and assert an
  observable normalization postcondition, proving it reached and completed
  the isolated Normalize pass. The depth-`513` and `10,000`-deep verifier-
  malformed raw fixtures must record only Normalize entry and failure, then
  return failure with the exact first and only diagnostic category, raw
  structural preflight nested-region-depth capability, and exact text
  `whole-model region nesting exceeds ACIR v0.1 capability limit 512`. They
  must not recurse, crash, overflow the stack, start normalization, or emit an
  epoch/canonical-file/downstream verifier diagnostic. The isolated manager
  contains no default `VerifyACIRFilePass`; this unit suite, rather than an
  `acir-opt` invocation, proves factory/pass ordering.

  In `raw-structure-preflight.mlir`, invoke the real internal tool through
  `%acir_opt` in both integration modes for the same three raw-depth fixtures:

  - `%acir_opt --verify-each=false <input>` exercises the automatic default
    normalize-then-verify pipeline;
  - `%acir_opt --verify-each=false --normalize-ac-file <input>` proves the
    registered option is accepted and runs after the automatic default
    normalize-then-verify passes on inputs that reach it.

  The test uses `%split_file` to materialize three named fixtures and contains
  six explicit RUN invocations, one per mode and fixture. `--verify-each=false`
  prevents pass-manager verification from preempting pass-owned preflight, but
  it does not remove the driver's automatic default Normalize and Verify
  passes. Depth `512` must succeed in both modes. Depth `513` and the
  `10,000`-deep malformed fixture must fail in both modes with the same first
  compact raw-depth diagnostic, without a crash, stack overflow, or downstream
  normalization/verifier diagnostic. These CLI runs prove default-pipeline
  safety and registration wiring only; the explicit option is not a
  standalone or factory-isolation proof.

- [ ] **Step 2: Run RED**

```bash
cmake --build build/dev-llvm22 --target ACIRProcessStatePlanTests -j4
build/dev-llvm22/bin/ACIRProcessStatePlanTests \
  --gtest_filter='ProcessStatePlanNormalizeFactoryTest.*'
cmake --build build/dev-llvm22 --target \
  ACIRModelAnalysisTests acir-opt-internal -j4
build/dev-llvm22/bin/ACIRProcessStatePlanTests \
  --gtest_filter='ProcessStatePlanVerifierTest.*:ProcessStatePlanPureCallTest.*'
.venv/bin/lit -v build/dev-llvm22/test/Analysis/process-state-verifier.mlir
.venv/bin/lit -v build/dev-llvm22/test/Analysis/raw-structure-preflight.mlir
```

  Expected: forbidden loops/calls verify, no expansion API exists, call-site
  values collapse, the public Normalize factory does not exist, or the current
  inline default normalize pass enters recursive normalization before a
  raw-structure preflight. The isolated factory suite is RED independently of
  the CLI default pipeline.

- [ ] **Step 3: Implement iterative shared lowerability and expansion**

  Factor current pure-call checks from `ModelAnalysis.cpp` only where behavior
  remains identical, add the final structured-subset and `scf.for` hard break,
  and run raw iterative depth preflight before recursive MLIR verification.
  Move both concrete file passes out of `InitAllPasses.h`. Declare the two
  factories in `include/acir/Transforms/Passes.h`, register only factories,
  add both factories to the default `acir-opt` pipeline, and add both new
  sources to `ACIRTransforms`. The normalize pass runs preflight before
  `normalizeAddressMaps`; the verify pass runs it before
  `verifyCanonicalACSimFile`, `module.walk`, attribute walks, and every other
  recursive verifier path. A failed preflight signals pass failure without
  entering recursive work.
  Expand calls/returns and constant loops iteratively with explicit forwarding;
  emit no `func` action. Dynamic loops produce exact initialize/condition/
  increment phase actions using signed-less-than `arith.cmpi` and `arith.addi`.
  Amend `docs/specs/acir-core-v0.1.md` in the same commit with the exact static-
  trip or every-backedge-suspends `scf.for` hard break.

- [ ] **Step 4: Run GREEN and mutations**

  Run the focused commands above. Temporarily remove dynamic-loop rejection,
  collapse call-site chains/raw `Value` identities, duplicate/drop a constant
  ordinal, bypass raw depth preflight in either file pass, restore either old
  inline class/direct-construction path, or insert
  `createVerifyACIRFilePass()`/the default pipeline before the Normalize
  factory in the isolated test path. The exact pass-instrumentation trace must
  catch factory/pass ordering independently of the private helper and CLI
  runs; every designated fixture or guard scan must fail, then restore GREEN.

- [ ] **Step 5: Focused gate and commit**

```bash
build/dev-llvm22/bin/ACIRModelAnalysisTests
build/dev-llvm22/bin/ACIRProcessStatePlanTests \
  --gtest_filter='ProcessStatePlanNormalizeFactoryTest.*'
.venv/bin/lit -v \
  build/dev-llvm22/test/ACIR/process-invalid.mlir \
  build/dev-llvm22/test/Transforms/freeze-topology.mlir \
  build/dev-llvm22/test/Analysis/process-state-verifier.mlir \
  build/dev-llvm22/test/Analysis/raw-structure-preflight.mlir
! rg -n 'class (NormalizeACIRFilePass|VerifyACIRFilePass)|make_unique<acir::(NormalizeACIRFilePass|VerifyACIRFilePass)>' \
  include/acir/InitAllPasses.h tools/acir-opt/acir-opt.cpp
! rg -n 'normalizeAddressMaps\(' include/acir/InitAllPasses.h
git commit -m "feat(lowering): validate and expand process continuations"
```

---

### Task 3: Plan PCs, continuations, captures, wakes, and transitions

**Files:**

- Create: `lib/Analysis/ProcessStateContinuation.cpp`
- Create: `lib/Analysis/ProcessStateWake.cpp`
- Modify: `lib/Analysis/ProcessStatePlanInternal.h`
- Modify: `lib/Analysis/ProcessStatePlan.cpp`
- Modify: `lib/Analysis/CMakeLists.txt`
- Create: `unittests/Analysis/ProcessStatePlanControlFlowTest.cpp`
- Modify: `unittests/Analysis/CMakeLists.txt`

**Interfaces:**

- Consumes: complete `ExpandedProcess` from Task 2.
- Produces private `ControlPlan` with dense entry/resume PCs, PC-local DAG
  blocks, closed frames/edges, captures, wakes, subscription sources, and
  transitions. It contains occurrence-qualified values and exact raw handles
  but no liveness slots/cost yet.

- [ ] **Step 1: Write control/wake RED tests**

  One frozen model covers all four suspension kinds plus non-blocking
  `try_send`, `try_recv`, and `schedule`. Assert literal PC names/order, wake
  kinds/type keys/targets, transitions, `yield_sim -> entry`, raw op/value and
  resolved declaration handles, condition subscription leaves, captures,
  nested `scf.if/for/while` frames/forwarding/edges, two branch suspensions,
  trace-cursor forwarding, constant-loop suspension occurrences and nested
  lexicographic iteration vectors, dynamic-loop phase targets, and the exact
  canonical traversal-path/global block-ID order from the contract design.

- [ ] **Step 2: Run RED**

```bash
cmake --build build/dev-llvm22 --target ACIRProcessStatePlanTests -j4
build/dev-llvm22/bin/ACIRProcessStatePlanTests \
  --gtest_filter='ProcessStatePlanControlFlowTest.*:ProcessStatePlanWakeTest.*'
```

- [ ] **Step 3: Implement deterministic iterative control planning**

  Traverse expanded continuations iteratively, create resume PCs in exact
  occurrence order, flatten PC-local DAGs, record frames and forwarding, plan
  captures and exact wake declarations/subscriptions, and create one transition
  per reachable suspension edge. Suspensions are not ordinary actions.

- [ ] **Step 4: Run GREEN and mutations**

  Reverse suspension worklist order, drop the second branch suspension, sort
  nested loops by innermost iteration only, and classify a non-blocking action
  as a suspension. Literal counts/order/paths must fail; restore GREEN.

- [ ] **Step 5: Focused gate and commit**

```bash
build/dev-llvm22/bin/ACIRProcessStatePlanTests \
  --gtest_filter='ProcessStatePlanControlFlowTest.*:ProcessStatePlanWakeTest.*'
git commit -m "feat(lowering): plan process continuation control flow"
```

---

### Task 4: Plan typed live state, realizations, emissions, and fairness

**Files:**

- Create: `lib/Analysis/ProcessStateLiveness.cpp`
- Create: `lib/Analysis/ProcessStateCost.cpp`
- Modify: `lib/Analysis/ProcessStatePlanInternal.h`
- Modify: `lib/Analysis/ProcessStatePlan.cpp`
- Modify: `lib/Analysis/CMakeLists.txt`
- Create: `unittests/Analysis/ProcessStatePlanEmissionTest.cpp`
- Create: `unittests/Analysis/ProcessStatePlanCostConsumer.cpp`
- Modify: `unittests/Analysis/CMakeLists.txt`

**Interfaces:**

- Consumes: frozen `ControlPlan` and expanded action/value identities.
- Produces complete final process records: typed live slots/stores/loads,
  generated implementation callees, value/packet type realizations, all action
  variants/emission classes, scalar wrap/unwrap, exact per-block cost, and
  fairness maximum. SCF equivalence classes, canonical live-slot assignment,
  generated payload arms, emitted cost, fairness, and the prohibition on
  `acsim.continue` follow the contract design's "Generated implementation
  specialization", "Generated value-type specialization", and "PC,
  suspension, state, and cost invariants" sections exactly.

- [ ] **Step 1: Write liveness/emission/cost RED tests**

  Cover capture/dead exclusion; occurrence-qualified SCF equivalence; stable
  typed slot representative/order; exact stores/replacements; every action and
  planned-value arm; canonical dense generated-callee/value-type tables;
  complete specialization-byte sorting/dedup; rejection of mixed effects, wrong IDs,
  fabricated external mapping, and result/type mismatch; scalar wrap/store and
  load/unwrap versus aggregate/packet; synthetic loop/wrapper/constant
  occurrence discriminators; literal per-block emitted sequences and
  hand-calculated fairness through a public-plan-only test consumer.
  Include the complete family-20 case: two call sites to one pure callee return
  the same original body value, both results remain live across one suspension,
  and they produce distinct occurrence-qualified values and live slots.

- [ ] **Step 2: Run RED**

```bash
cmake --build build/dev-llvm22 --target \
  ACIRProcessStatePlanTests ACSimOpsTests -j4
build/dev-llvm22/bin/ACIRProcessStatePlanTests \
  --gtest_filter='ProcessStatePlanLivenessTest.*:ProcessStatePlanEmissionTest.*:ProcessStatePlanCostTest.*'
```

- [ ] **Step 3: Implement liveness, realization, and exact cost**

  Use occurrence-qualified union-find/equivalence, never raw `Value`, to create
  live classes. Intern complete generated implementation and value/packet
  realization keys. Insert explicit wrapper actions for scalar slots. Compute
  the exact stored block cost from the contract formula, then the iterative
  PC-local DAG longest path. Emit no plan action corresponding to
  `acsim.continue`; non-suspending control remains an exact `cf.br` or
  `cf.cond_br` edge. Reject fairness zero, overflow, cycles, and cap excess.

- [ ] **Step 4: Run GREEN and mutations**

  Independently omit each emitted cost class; alter wrapper transition/slot/
  direction or callee/value-type ID; merge loop phases; deduplicate by raw
  handle; drop/duplicate constants. Each literal fixture must fail, then restore
  GREEN.

- [ ] **Step 5: Focused gate and commit**

```bash
build/dev-llvm22/bin/ACIRProcessStatePlanTests \
  --gtest_filter='ProcessStatePlanLivenessTest.*:ProcessStatePlanEmissionTest.*:ProcessStatePlanCostTest.*'
git commit -m "feat(lowering): plan typed live state and emissions"
```

---

### Task 5: Enforce all capabilities and atomic report publication

**Files:**

- Modify: `lib/Analysis/ProcessStatePlan.cpp`
- Modify: `lib/Analysis/ProcessStatePlanInternal.h`
- Modify: `lib/Analysis/ProcessStateReport.cpp`
- Modify: `lib/Analysis/ProcessStatePlanTestHooks.h`
- Create: `unittests/Analysis/ProcessStatePlanLimitsTest.cpp`
- Create: `unittests/Analysis/ProcessStatePlanAtomicityTest.cpp`
- Modify: `unittests/Analysis/CMakeLists.txt`

**Interfaces:**

- Completes final behavior of `ProcessStateLimits`,
  `planProcessState`, `verifyProcessStatePlan`, `serializeProcessStatePlan`, and
  `writeProcessStatePlanReportAtomically`.
- Extends the existing private hook header only with
  `ProcessStateReportFailurePointForTest` and
  `ScopedProcessStateReportFailureForTest` for open/write/flush/close/rename.
- Adds the final façade declarations and definitions together, so every public
  symbol links in this checkpoint:

```cpp
mlir::FailureOr<ProcessStatePlanSet>
planProcessState(mlir::ModuleOp model,
                 const ProcessStateLimits &limits = ProcessStateLimits());

llvm::Error writeProcessStatePlanReportAtomically(
    const ProcessStatePlanSet &plans, llvm::StringRef path,
    const ProcessStateLimits &limits = ProcessStateLimits());
```

- [ ] **Step 1: Write limits/non-mutation/atomicity RED tests**

  Assert all ten default values literally. For each cap, exact boundary passes
  and boundary+1 fails naming capability and configured value. Capture text and
  bytecode before every success/failure and require exact equality including
  freeze seal/digest. Require no partial plan. Exercise planning failure with
  malformed/unsupported frozen IR and exact capability fixtures. Exercise
  serialization failure with semantic corruption and
  `maxCanonicalReportBytes`. Use scoped failure injection only for temporary
  open/write/flush/close/rename; require existing sentinel report bytes and no
  temporary files. Report byte cap must fail during bounded iterative emission
  and before destination replacement.

- [ ] **Step 2: Run RED**

```bash
cmake --build build/dev-llvm22 --target ACIRProcessStatePlanTests -j4
build/dev-llvm22/bin/ACIRProcessStatePlanTests \
  --gtest_filter='ProcessStatePlanLimitsTest.*:ProcessStatePlanAtomicityTest.*'
```

- [ ] **Step 3: Implement shared deterministic accounting and atomic writer**

  Preflight every allocation/work/string/container/report byte before iterative
  emission, build into private temporary storage, return no partial result, and
  define the public `planProcessState` façade by orchestrating Tasks 2--4, then
  publish only by same-directory temporary file plus flush/close/atomic rename.
  Keep filesystem failure injection private, inactive, thread-local, scoped to
  exactly open/write/flush/close/rename, and absent from the public header.

- [ ] **Step 4: Run GREEN and mutations**

  Change each `>` comparison to `>=`, generalize one diagnostic, publish before
  verification, rename directly over destination, and mutate input with a
  temporary attribute. Each exact fixture must fail, then restore GREEN.

- [ ] **Step 5: Focused gate and commit**

```bash
build/dev-llvm22/bin/ACIRProcessStatePlanTests \
  --gtest_filter='ProcessStatePlanLimitsTest.*:ProcessStatePlanAtomicityTest.*'
git commit -m "feat(lowering): bound and atomically publish process plans"
```

---

### Task 6: Expose the non-mutating pass and synchronize the normative contract

**Files:**

- Create: `lib/Transforms/LowerProcessState.cpp`
- Modify: `lib/Transforms/CMakeLists.txt`
- Modify: `include/acir/Transforms/Passes.td`
- Modify: `include/acir/Transforms/Passes.h`
- Modify: `include/acir/InitAllPasses.h` if generated registration requires it
- Create: `unittests/Analysis/ProcessStatePlanReportTest.cpp`
- Create: `test/Analysis/process-state-plan.mlir`
- Create: `test/Analysis/process-state-emission.mlir`
- Create: `test/Analysis/process-state-atomicity.mlir`
- Create: `test/Analysis/process-state-pass.mlir`
- Create: `test/Analysis/Inputs/` invalid report fixtures required by the
  normative closed unions
- Create: `test/Analysis/check-process-state-schema.py`
- Modify: `lib/Analysis/ProcessStatePlanTestHooks.h`
- Modify: `test/CMakeLists.txt`
- Modify: `docs/specs/acir-process-state-plan-v0.1.md`
- Modify: `docs/specs/acsim-gfsim-lowering-v0.1.md`
- Modify:
  `docs/superpowers/specs/2026-08-05-acsim-generated-call-contract-design.md`
- Modify: `docs/superpowers/plans/2026-08-04-acir-acsim-implementation.md`
- Modify: `unittests/Analysis/CMakeLists.txt`

**Interfaces:**

- Produces pass name exactly `ac-lower-process-state`, with an optional report
  output option. No option means a valid non-mutating analysis gate.
- The tracked Task 13 plan no longer owns `LowerProcessState.cpp`; Task 13
  consumes the public plan and separate exact binding result.
- Top-level report object contains exactly `callees`, `contract_epoch`,
  `processes`, `schema`, and `value_types`; every nested object has
  `additionalProperties: false` and uses the exact enum spellings, active-arm
  field sets, descriptor objects, and optional-field rules in the contract
  design's "Closed enums and serialized spellings", "Core immutable records",
  "Generated implementation specialization", "Generated value-type
  specialization", and "Canonical report contract" sections.

- [ ] **Step 1: Write pass/schema/report RED tests**

  Test unknown pass before registration and deferred control/liveness/
  atomicity behavior unavailable through a real pass; exact canonical report output with
  stdout ACIR unchanged; no-option success; failure publishes no report; real
  Draft 2020-12 validation; rejection of unknown fields at every level, wrong
  epoch/schema, unsafe integers, invalid union arms; semantic rejection of
  ordinal/order/uniqueness/cross-reference/cost/definition/specialization
  corruption; repository contract output remains exactly `10` schemas. Task 6
  adds only `validateProcessStateReportBytesForTest` to the existing private
  hook surface.
  The Python schema checker consumes invalid JSON fixtures. Task 6 extends the
  existing private hook header only with
  `validateProcessStateReportBytesForTest`; semantic corruption was published
  and tested in Task 1, while filesystem failure injection was published and
  tested in Task 5. No public report parser or public corruption hook is added.

- [ ] **Step 2: Run RED**

```bash
cmake --build build/dev-llvm22 --target \
  ACIRProcessStatePlanTests acir-opt acir-opt-internal -j4
build/dev-llvm22/bin/ACIRProcessStatePlanTests \
  --gtest_filter='ProcessStatePlanReportTest.*'
.venv/bin/lit -v \
  build/dev-llvm22/test/Analysis/process-state-plan.mlir \
  build/dev-llvm22/test/Analysis/process-state-emission.mlir \
  build/dev-llvm22/test/Analysis/process-state-atomicity.mlir \
  build/dev-llvm22/test/Analysis/process-state-pass.mlir
.venv/bin/python -m unittest tests.contracts.test_contracts -v
.venv/bin/python scripts/check-contracts.py
```

- [ ] **Step 3: Implement pass and complete tracked specifications**

  Register the generated pass normally, keep analyses preserved, parse only
  the optional report path, call the public planner/verifier/serializer/writer,
  and never create an operation/attribute. Complete semantic report validation
  against Task 1's schema and the contract design's exact report verifier
  invariants. Synchronize every approved handoff and remove stale future-
  operation-handle/external-process-callee/Task13 file-ownership text.
  Create the deferred process-state plan, emission, atomicity, and pass lit
  fixtures using literal expectations already proven by Tasks 3--5 unit tests.

- [ ] **Step 4: Run GREEN and mutations**

  Add unknown fields to every object/union; corrupt each semantic invariant;
  mutate the pass to create an attribute/ACSim operation; reorder/duplicate
  identities. Each designated test must
  fail, then restore GREEN.

  Run all four deferred lit files together:

```bash
.venv/bin/lit -v \
  build/dev-llvm22/test/Analysis/process-state-plan.mlir \
  build/dev-llvm22/test/Analysis/process-state-emission.mlir \
  build/dev-llvm22/test/Analysis/process-state-atomicity.mlir \
  build/dev-llvm22/test/Analysis/process-state-pass.mlir
```

- [ ] **Step 5: Focused gate and commit**

```bash
cmake --build build/dev-llvm22
ctest --test-dir build/dev-llvm22 --output-on-failure
.venv/bin/lit -v build/dev-llvm22/test
.venv/bin/python -m unittest discover -s tests -v
.venv/bin/python scripts/check-contracts.py
git commit -m "feat(lowering): expose canonical process-state reports"
```

---

### Task 7: Close hostile-input, mutation, consumer, and release verification

**Files:**

- Modify: all ProcessStatePlan unit/lit fixtures only where closure finds a
  real uncovered contract
- Create: `unittests/Analysis/ProcessStatePlanTask13ConsumerTest.cpp`
- Modify: `unittests/Analysis/CMakeLists.txt`
- Modify: `docs/specs/acir-process-state-plan-v0.1.md` only for factual closure
  corrections discovered by testing

**Interfaces:**

- `ACIRProcessStateTask13ConsumerTests` is a source-tree public-header-only
  executable. Its include roots are only `${PROJECT_SOURCE_DIR}/include` and
  `${PROJECT_BINARY_DIR}/include` through linked public target build
  interfaces. It receives no direct or transitive public
  `${PROJECT_SOURCE_DIR}/lib` path, includes only
  `acir/Analysis/ProcessStatePlan.h`, links source-tree public libraries only,
  and cannot inspect original SCF/func bodies, parse report JSON, or guess
  callee/value/owner identities. Task 12 adds no install/export/package rule;
  Task 14 owns installed-tree closure.

- [ ] **Step 1: Write final consumer and adversarial RED tests**

  Materialize a deterministic mock ACSim emission transcript from public plan
  records alone and compare literal operations/IDs/costs. Run all 23 mandatory
  families as a coverage matrix. Add exact 512/513, deep malformed, cap+1,
  declaration permutation, two-call-site live value, nested constant
  suspension, wrapper, and atomic failure corpus if any is still absent.

- [ ] **Step 2: Run all required mutations one final time**

  The report for this task records at least one caught mutation for each class:
  lowerability, occurrence/value identity, suspension ordering, liveness,
  wrapper/realization IDs, every emitted cost class, each cap comparison,
  report semantics/schema, input mutation, and publication atomicity. Restore
  the tree and re-run focused GREEN after every mutation group.

- [ ] **Step 3: Run complete Debug and Release verification**

```bash
cmake --preset dev-llvm22
cmake --build --preset dev-llvm22 -j4
ctest --test-dir build/dev-llvm22 --output-on-failure
.venv/bin/lit -v build/dev-llvm22/test

cmake --preset release-llvm22
cmake --build --preset release-llvm22 -j4
ctest --test-dir build/release-llvm22 --output-on-failure
.venv/bin/lit -v build/release-llvm22/test

.venv/bin/python -m unittest discover -s tests -v
.venv/bin/python scripts/check-contracts.py
```

- [ ] **Step 4: Run production/static/hygiene gates**

  Run clang analyzer on every new/touched production translation unit, strict
  clang-format on owned C++, `git diff --check`, zero tracked
  `.superpowers/**`, exact public-symbol inventory scan, and clean status.
  No warning/noise or disabled mutation may remain.

- [ ] **Step 5: Commit final closure**

```bash
git commit -m "feat(lowering): plan ACSim process state"
```

  Do not push from the implementer. The controller performs independent final
  verification, whole-plan review, push, and push/PR CI monitoring.

## Plan self-review

- Spec coverage: all 23 mandatory test families have exactly one primary task
  owner: Task 1 owns 5/7/8; Task 2 owns 12/15/23 constant expansion; Task 3
  owns 1/2/4/6/16/18; Task 4 owns 3/17/19/20/21/22 plus final constant identity;
  Task 5 owns 9/10/11; Task 6 owns 13/14; Task 7 audits the whole matrix without
  redefining ownership.
- Type consistency: public names are fixed in Task 1; later tasks add behavior,
  never parallel public variants. Dense IDs and occurrence-qualified values
  are the only cross-file identities.
- Dependency consistency: lowerability lives under Dialect; Analysis depends on
  Dialect, not vice versa. Continuation precedes liveness; wrapper insertion
  precedes cost; report/pass follows final semantic plan.
- Deferred-marker scan: the plan contains no deferred implementation marker or
  compatibility behavior. Intermediate unsupported legal actions are explicit
  checkpoint failures removed before Task 7 and are never a release endpoint.
