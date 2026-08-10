# Phase 1 Review Repairs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Repair the independent-review blockers in Tasks 12 and 13 so `feature/acir-acsim` satisfies the approved whole-model Phase 1 contract and can merge into `main`.

**Architecture:** Publish one production, immutable process-state planning entry point over the existing expansion/continuation/wake/liveness/cost helpers, then make conversion consume only that public plan and an immutable binding result. Preserve frozen identities, derive deterministic ordering from structure instead of symbol spelling, fingerprint canonical defining content, and publish ACSim only after the complete model has planned successfully.

**Tech Stack:** C++20, LLVM/MLIR 22.1.8, MLIR lit/FileCheck, GoogleTest, CMake/Ninja, Python contract tests.

## Global Constraints

- Global contract epoch remains exactly `0.1`; no compatibility aliases, fallbacks, or nearest-version selection.
- Frozen ACIR remains byte-for-byte unchanged during planning and after every lowering failure.
- Task 13 consumes the public `ProcessStatePlanSet` and immutable `bindings::BindingResolutionResult`; conversion must not include `ProcessStatePlanInternal.h`, reconstruct process analysis, or parse plan reports.
- One complete `acsim.model` is published only after planning, binding, type conversion, ownership expansion, dispatch, and activation checks all succeed.
- Canonical output and fingerprints must not depend on declaration order, pointer values, allocation layout, unordered iteration, or user symbol spelling when spelling is not semantic identity.
- No new dependencies and no Phase 2 runtime/scheduler behavior in this repair branch.
- Every behavior change follows RED -> GREEN -> REFACTOR and each task ends in one reviewed commit.

---

### Task 1: Publish the production process-state planner

**Files:**
- Modify: `include/acir/Analysis/ProcessStatePlan.h`
- Modify: `lib/Analysis/ProcessStatePlan.cpp`
- Modify: `lib/Analysis/ProcessStatePlanInternal.h`
- Modify: `lib/Analysis/ProcessStateLiveness.cpp`
- Modify: `lib/Analysis/ProcessStateWake.cpp`
- Modify: `lib/Analysis/ProcessStateCost.cpp`
- Modify: `lib/Transforms/LowerProcessState.cpp`
- Test: `unittests/Analysis/ProcessStatePlanBasicTest.cpp`
- Test: `unittests/Analysis/ProcessStatePlanControlFlowTest.cpp`
- Test: `unittests/Analysis/ProcessStatePlanWakeTest.cpp`
- Test: `unittests/Analysis/ProcessStatePlanAtomicityTest.cpp`
- Test: `unittests/Analysis/ProcessStatePlanTask13ConsumerTest.cpp`
- Test: `test/Analysis/process-state-pass.mlir`

**Interfaces:**
- Consumes: frozen `mlir::ModuleOp`, `ProcessStateLimits`, and the existing internal expansion/continuation/wake/liveness/cost helpers.
- Produces: `mlir::FailureOr<ProcessStatePlanSet> planProcessState(mlir::ModuleOp module, const ProcessStateLimits &limits = ProcessStateLimits());` in the public header.

- [ ] **Step 1: Add failing public-consumer and control-flow tests**

  Add a public-header-only test that parses a frozen module containing a non-yield-only process, invokes `planProcessState`, and inspects deterministic PC, block, action, wake, transition, and live-slot records. Add focused cases for `scf.if`/bounded `scf.for`, a scalar live across suspension, declaration permutation, and atomic failure.

  ```cpp
  auto plans = planProcessState(*module);
  ASSERT_TRUE(mlir::succeeded(plans));
  ASSERT_EQ(plans->processes().size(), 1u);
  EXPECT_GT(plans->processes().front().blocks().size(), 1u);
  EXPECT_EQ(plans->processes().front().liveSlots().size(), 1u);
  ```

- [ ] **Step 2: Verify RED**

  Run:

  ```sh
  cmake --build --preset dev-llvm22 --target ACIRProcessStatePlanTests acir-opt
  ctest --test-dir build/dev-llvm22 -R ACIRProcessStatePlanTests --output-on-failure
  ```

  Expected: compile failure because `planProcessState` is absent, followed by behavioral failures as the production orchestration is implemented incrementally.

- [ ] **Step 3: Implement the public production orchestration**

  Declare the exact public factory and implement it without exposing mutable builders:

  ```cpp
  mlir::FailureOr<ProcessStatePlanSet>
  planProcessState(mlir::ModuleOp module,
                   const ProcessStateLimits &limits = ProcessStateLimits());
  ```

  For each process in canonical definition-key order, run expansion, continuation, typed wake planning, live-across-suspend storage planning, and bounded fairness-cost planning. Build the immutable set only after every process succeeds. Keep fixture/corruption builders private to unit tests.

- [ ] **Step 4: Route the pass through the public factory**

  Replace `PlanSetBuilder::buildYieldOnly` in `LowerProcessState.cpp` with `planProcessState(getOperation(), limits)` and retain `verifyProcessStatePlan` before success.

- [ ] **Step 5: Verify GREEN and canonical atomicity**

  Run:

  ```sh
  cmake --build --preset dev-llvm22 --target ACIRProcessStatePlanTests acir-opt
  ctest --test-dir build/dev-llvm22 -R ACIRProcessStatePlanTests --output-on-failure
  cmake --build --preset dev-llvm22 --target check-acir
  ```

  Expected: analysis unit tests and lit pass; equivalent declaration permutations serialize identically; rejected inputs preserve textual IR and bytecode hashes.

- [ ] **Step 6: Commit**

  ```sh
  git add include/acir/Analysis/ProcessStatePlan.h lib/Analysis lib/Transforms/LowerProcessState.cpp unittests/Analysis test/Analysis/process-state-pass.mlir
  git commit -m "fix(analysis): publish complete process-state planning"
  ```

---

### Task 2: Derive module order from the instantiation DAG

**Files:**
- Modify: `lib/Conversion/ACIRToACSim/ACIRToACSim.cpp`
- Test: `test/Conversion/hierarchy.mlir`
- Test: `test/Conversion/bindings-invalid.mlir`
- Test: `unittests/Conversion/ACIRToACSimTest.cpp`

**Interfaces:**
- Consumes: the complete module inventory and placement targets from conversion planning.
- Produces: deterministic child-before-parent module emission order; lexical symbol order is only a tie-breaker between independent DAG nodes.

- [ ] **Step 1: Add failing adversarial-name and cycle tests**

  Add a valid `@A` parent that instantiates `@Z`, the same graph under declaration permutations, and a real cycle. Assert the valid forms lower identically and the cycle fails with `ACLOWER-OWNERSHIP`.

- [ ] **Step 2: Verify RED**

  ```sh
  cmake --build --preset dev-llvm22 --target check-acir
  ```

  Expected: the valid adversarial-name case fails at the existing lexical-order rejection.

- [ ] **Step 3: Implement canonical topological ordering**

  Remove the `definition >= enclosingModule` rule. Build edges from each concrete module to the concrete modules it instantiates, detect cycles with deterministic diagnostics, and emit a child-before-parent topological order with lexical tie-breaking for ready nodes.

- [ ] **Step 4: Verify GREEN**

  ```sh
  cmake --build --preset dev-llvm22 --target ACIRToACSimTests acir-opt
  ctest --test-dir build/dev-llvm22 -R ACIRToACSimTests --output-on-failure
  cmake --build --preset dev-llvm22 --target check-acir
  ```

- [ ] **Step 5: Commit**

  ```sh
  git add lib/Conversion/ACIRToACSim/ACIRToACSim.cpp test/Conversion unittests/Conversion/ACIRToACSimTest.cpp
  git commit -m "fix(lowering): topologically order module realizations"
  ```

---

### Task 3: Preserve frozen hierarchy identity and owned path storage

**Files:**
- Modify: `lib/Conversion/ACIRToACSim/ACIRToACSim.cpp`
- Test: `test/Conversion/structure.mlir`
- Test: `test/Conversion/hierarchy.mlir`
- Test: `test/Conversion/collections.mlir`
- Test: `unittests/Conversion/ACIRToACSimTest.cpp`

**Interfaces:**
- Consumes: `ac.system root_name`, the frozen owner manifest, and Task 2's deterministic module order.
- Produces: construction/dispatch paths exactly equal to frozen canonical owner paths.

- [ ] **Step 1: Add failing root-name and reallocation tests**

  Add `ac.system ... root @Top as "root"` and assert `root.child` / `root.workload`, never `Top.child`. Add a deep and wide hierarchy that forces `constructionOrder` vector growth and compare every emitted path with the frozen owner manifest.

- [ ] **Step 2: Verify RED**

  ```sh
  cmake --build --preset dev-llvm22 --target check-acir ACIRToACSimTests
  ctest --test-dir build/dev-llvm22 -R ACIRToACSimTests --output-on-failure
  ```

- [ ] **Step 3: Use owned paths and validate against the frozen manifest**

  Seed expansion from the selected system's root name. Pass owned `std::string` values through recursion; never retain a `StringRef` to `constructionOrder.back()` across a `push_back`. Reject any planned path absent from or inconsistent with the frozen owner manifest before emission.

- [ ] **Step 4: Verify GREEN under sanitizers**

  ```sh
  cmake --build --preset dev-llvm22 --target check-acir ACIRToACSimTests
  ctest --test-dir build/dev-llvm22 -R ACIRToACSimTests --output-on-failure
  cmake --preset asan-llvm22
  cmake --build --preset asan-llvm22 --target ACIRToACSimTests
  ctest --test-dir build/asan-llvm22 -R ACIRToACSimTests --output-on-failure
  ```

- [ ] **Step 5: Commit**

  ```sh
  git add lib/Conversion/ACIRToACSim/ACIRToACSim.cpp test/Conversion unittests/Conversion/ACIRToACSimTest.cpp
  git commit -m "fix(lowering): preserve frozen hierarchy paths"
  ```

---

### Task 4: Fingerprint canonical defining content

**Files:**
- Modify: `lib/Conversion/ACIRToACSim/ACIRToACSim.cpp`
- Modify: `include/acir/Analysis/ModelAnalysis.h`
- Modify: `lib/Analysis/ModelAnalysis.cpp`
- Test: `unittests/Conversion/ACIRToACSimTest.cpp`
- Test: `test/Conversion/hierarchy.mlir`
- Test: `test/Conversion/process-basic.mlir`

**Interfaces:**
- Consumes: canonical module definition descriptors, serialized `ProcessStatePlanSet`, exact resolved binding fingerprints, profile, and target.
- Produces: stable `sha256:` fingerprints that change for semantic definition changes and remain stable for equivalent canonical permutations.

- [ ] **Step 1: Add failing mutation tests**

  Test that changing a module body, process control/wake/live-state plan, component-schema fingerprint, provider-implementation fingerprint, profile, or target changes the applicable specialization fingerprint. Test that repeated equivalent inputs and declaration permutations do not.

- [ ] **Step 2: Verify RED**

  ```sh
  cmake --build --preset dev-llvm22 --target ACIRToACSimTests
  ctest --test-dir build/dev-llvm22 -R ACIRToACSimTests --output-on-failure
  ```

- [ ] **Step 3: Build closed canonical descriptors before hashing**

  Hash canonical JSON descriptors, not pretty-printed IR. Module descriptors include interface, static parameters, ordered body/topology, exports, and referenced realizations. Process descriptors include the canonical serialized plan plus enclosing module specialization. Binding-instance descriptors include the exact resolved record fingerprint, canonical static arguments, profile, and target.

- [ ] **Step 4: Verify GREEN and determinism**

  ```sh
  cmake --build --preset dev-llvm22 --target ACIRToACSimTests check-acir
  ctest --test-dir build/dev-llvm22 -R ACIRToACSimTests --output-on-failure
  scripts/audit5-determinism.sh
  ```

- [ ] **Step 5: Commit**

  ```sh
  git add include/acir/Analysis/ModelAnalysis.h lib/Analysis/ModelAnalysis.cpp lib/Conversion/ACIRToACSim/ACIRToACSim.cpp unittests/Conversion test/Conversion
  git commit -m "fix(lowering): fingerprint canonical realization content"
  ```

---

### Task 5: Complete atomic whole-model ACIR-to-ACSim lowering

**Files:**
- Modify: `include/acir/Conversion/ACIRToACSim/ACIRToACSim.h`
- Modify: `lib/Conversion/ACIRToACSim/ACIRToACSim.cpp`
- Create: `lib/Conversion/ACIRToACSim/TypeConverter.cpp`
- Modify: `lib/Conversion/ACIRToACSim/CMakeLists.txt`
- Create: `test/Conversion/whole-model.mlir`
- Create: `test/Conversion/process-control-flow.mlir`
- Create: `test/Conversion/process-live-values.mlir`
- Modify: `test/Conversion/atomic-failure.mlir`
- Modify: `test/Conversion/process-invalid.mlir`
- Modify: `unittests/Conversion/ACIRToACSimTest.cpp`

**Interfaces:**
- Consumes: Task 1's public process plan, Task 2's DAG order, Task 3's frozen paths, Task 4's fingerprints, and an immutable `bindings::BindingResolutionResult` supplied to conversion planning.
- Produces: one complete canonical `acsim.model`, or no mutation on failure.

- [ ] **Step 1: Add the failing composite whole-model and rollback tests**

  `whole-model.mlir` contains a non-empty module interface/export, nested generated module, homogeneous array, stateful external binding, typed graph edge, process with at least two PCs and one live slot, frozen-root paths, stateful dispatch rows, and at least one non-self activation edge. Run conversion twice and FileCheck identical canonical output. Add a failure after all planning stages and compare the frozen input before/after.

- [ ] **Step 2: Verify RED**

  ```sh
  cmake --build --preset dev-llvm22 --target check-acir ACIRToACSimTests
  ```

  Expected: current stage-boundary rejections and hard-coded yield-only emission fail the new tests.

- [ ] **Step 3: Consume public plans and immutable binding results**

  Remove the private analysis include and `buildYieldOnly`. Plan bindings once into `bindings::BindingResolutionResult`, pass it through realization planning, and use `planProcessState` for all valid processes. Keep all planning data outside the IR until every stage succeeds.

- [ ] **Step 4: Lower complete typed structure and process plans**

  Populate ACSim module interfaces/exports from ACIR signatures and returns. Lower typed instance/array projections and binding edges to the applicable `acsim.port`, `acsim.resource`, `acsim.element`, `acsim.bind`, `acsim.inline`, `acsim.export`, and typed `acsim.return` operations. Emit every plan block/action/load/store/control edge. Derive activation adjacency from graph dependencies and wake/subscription sources; emit self-activation only when required by the plan.

- [ ] **Step 5: Preserve atomic publication**

  Build the replacement model in detached storage, verify it, then replace frozen ACIR in one mutation. Every diagnostic path before final publication must leave the input operation and bytecode hashes unchanged.

- [ ] **Step 6: Verify GREEN**

  ```sh
  cmake --build --preset dev-llvm22 --target ACIRToACSimTests check-acir
  ctest --test-dir build/dev-llvm22 -R ACIRToACSimTests --output-on-failure
  rg -n 'ProcessStatePlanInternal|buildYieldOnly|definition >= enclosingModule' lib/Conversion/ACIRToACSim
  ```

  Expected: all tests pass and the final search returns no matches.

- [ ] **Step 7: Commit**

  ```sh
  git add include/acir/Conversion lib/Conversion/ACIRToACSim test/Conversion unittests/Conversion
  git commit -m "fix(lowering): complete atomic whole-model conversion"
  ```

---

### Task 6: Align the installed public surface and completion audit

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `lib/CMakeLists.txt`
- Modify: `lib/Analysis/CMakeLists.txt`
- Modify: `lib/Conversion/ACIRToACSim/CMakeLists.txt`
- Modify: `tests/install-consumer/main.cpp`
- Modify: `tests/install-consumer/CMakeLists.txt`
- Modify: `docs/implementation/phase-1-audit.md`
- Modify: `docs/implementation/reviewed-commits.txt`
- Modify: `docs/implementation/spec-coverage.md`

**Interfaces:**
- Consumes: the final public analysis/conversion headers and targets.
- Produces: an installed consumer that creates and serializes a production plan; updated independent-review evidence and commit ledger.

- [ ] **Step 1: Add a failing installed-consumer call path**

  Parse a minimal frozen module in `tests/install-consumer/main.cpp`, call `planProcessState`, inspect the returned plan, and serialize it. Link only through installed `AgenticCircuit::` targets.

- [ ] **Step 2: Verify RED against a temporary install**

  ```sh
  cmake --install build/release-llvm22 --prefix /tmp/acir-phase1-prefix
  cmake -S tests/install-consumer -B /tmp/acir-phase1-consumer -GNinja -DAgenticCircuit_DIR=/tmp/acir-phase1-prefix/lib/cmake/AgenticCircuit -DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm -DMLIR_DIR=/opt/homebrew/opt/llvm/lib/cmake/mlir
  cmake --build /tmp/acir-phase1-consumer
  ```

- [ ] **Step 3: Export the exact public dependencies**

  Ensure installed headers have all generated/transitive includes and exported targets propagate their link/include dependencies. Export conversion only if its header remains public; otherwise narrow installation to the headers belonging to exported targets.

- [ ] **Step 4: Run the full completion gate**

  ```sh
  /Users/zhoubot/Documents/agentic-circuit/.venv/bin/python -m unittest tests.contracts.test_contracts tests.contracts.test_ir_coverage -v
  python scripts/check-ir-coverage.py
  cmake --build --preset dev-llvm22 --target check-acir
  ctest --test-dir build/dev-llvm22 --output-on-failure
  cmake --preset release-llvm22
  cmake --build --preset release-llvm22 --target check-acir
  ctest --test-dir build/release-llvm22 --output-on-failure
  git ls-files '*.cpp' '*.h' | xargs clang-format --dry-run --Werror
  scripts/audit5-determinism.sh
  git diff --check
  ```

- [ ] **Step 5: Update audit evidence and commit**

  Record both independent review lanes, the repair commits, and fresh verification outputs. Regenerate the reviewed-commit ledger and spec-coverage ledger using the existing repository scripts.

  ```sh
  git add CMakeLists.txt lib tests/install-consumer docs/implementation
  git commit -m "docs(audit): close Phase 1 independent review"
  ```
