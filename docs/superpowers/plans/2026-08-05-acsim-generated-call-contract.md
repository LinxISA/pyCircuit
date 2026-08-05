# ACSim Generated-Call Contract Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Hard-break canonical ACSim so pure and stateful process calls can target either exact external bindings or compiler-generated implementation identities without fabricating binding-lock records.

**Architecture:** Retain the closed ACSim operation inventory. Generalize `acsim.inline` and `acsim.invoke` target resolution to exact `BindingOp` or generated `TypeOp(kind = "implementation")`, permit pure inline calls in process states, and close effect/result legality. This unblocks ProcessStatePlan while keeping generated core helpers outside the registry.

**Tech Stack:** C++20, LLVM/MLIR 22.1.8 ODS, MLIR verifiers, GoogleTest, LLVM lit/FileCheck.

## Global Constraints

- Contract epoch remains exactly `"0.1"`; this is an unreleased hard break with no legacy accessor, attribute, warning, or fallback path.
- `acsim.inline` is always pure; `acsim.invoke` is always stateful.
- External/library callees are exact `acsim.binding` records with matching effect.
- Compiler-generated callees are exact `acsim.type` records with kind exactly `"implementation"`; they never create binding requests or lock records.
- One generated implementation identity has exactly one effect class in a model.
- Lookup is exact symbol resolution only; component names, C++ spellings, hierarchy paths, and fuzzy fallback are forbidden.
- No new public ACSim operation or type is added, so the closed v0.1 inventory count is unchanged.
- C++ API, tests, fixtures, and normative specs migrate completely in the same change.

---

### Task 1: Hard-break ACSim inline/invoke to the generated-call contract

**Files:**

- Modify: `include/acir/Dialect/ACSim/ACSimOps.td`
- Modify: `lib/Dialect/ACSim/ACSimOps.cpp`
- Modify: `test/ACSim/ops-valid.mlir`
- Create: `test/ACSim/generated-call-contract.mlir`
- Modify: `unittests/Dialect/ACSim/OpsTest.cpp`
- Modify: `docs/specs/acsim-gfsim-lowering-v0.1.md`

**Interfaces:**

- Consumes: `ACSim_TypeOp` kind `implementation`, immutable BindingOp effect metadata, current ModelIndex exact symbol resolution, current closed legality and canonical-type verification.
- Produces: `InlineOp::getCalleeAttr()/getCallee()` and `InvokeOp::getCalleeAttr()/getCallee()`; exact BindingOp-or-TypeOp callee verification; process-inline legality; generated implementation effect-consistency verification. Task 12 consumes this contract when assigning each planned action an exact emission class/callee.

- [ ] **Step 1: Write C++ API RED tests**

  Add programmatic builders/parsers that call `InlineOp::getCalleeAttr()` and
  `InvokeOp::getCalleeAttr()`, then assert the attribute name is `callee` and
  the exact target symbol round-trips. The test must fail to compile against
  the current generated API, which exposes only `getBindingAttr()`.

- [ ] **Step 2: Write behavioral lit RED tests**

  In `generated-call-contract.mlir`, cover these exact positive models:

  - module `acsim.inline` -> pure BindingOp, result `!acsim.expr`;
  - module `acsim.inline` -> implementation TypeOp, result `!acsim.expr`;
  - process `acsim.inline` -> implementation TypeOp, once with builtin `i32`
    and once with `!acsim.value` result;
  - process `acsim.invoke` -> stateful BindingOp;
  - process `acsim.invoke` -> implementation TypeOp, including a zero-argument
    wake result.

  Cover these exact negative models and fixed diagnostic categories:

  - inline -> stateful BindingOp;
  - invoke -> pure BindingOp;
  - inline/invoke -> non-implementation TypeOp;
  - unresolved callee and callee resolving to ModuleOp/ProcessOp;
  - one implementation TypeOp used by both inline and invoke;
  - module inline with non-Expr result;
  - process inline with Expr, owner/ref, wake, or other non-scalar/non-Value
    result;
  - invoke with a non-Value/non-Wake result.

  Use the real parser and model verifier. Do not use source grep as primary
  evidence.

- [ ] **Step 3: Run RED and record the expected failures**

  Run:

  ```bash
  cmake --build build/dev-llvm22 --target ACSimOpsTests acir-opt-internal -j4
  .venv/bin/lit -v build/dev-llvm22/test/ACSim/generated-call-contract.mlir
  ```

  Expected: C++ compilation fails because `getCalleeAttr` is absent; lit
  positives fail because process inline and generated TypeOp callees are not
  legal. Negative checks that rely on the new contract must not accidentally
  pass through an unrelated parse error.

- [ ] **Step 4: Hard-break the ODS API**

  In `ACSimOps.td`, rename the `FlatSymbolRefAttr` parameter on both InlineOp
  and InvokeOp from `binding` to `callee`. Keep the existing textual
  `@symbol(args) : (...) -> ...` syntax, single InlineOp result, variadic
  InvokeOp results, and all other public fields unchanged. Regenerate through
  the normal CMake build; do not add compatibility accessors.

- [ ] **Step 5: Implement exact callee resolution**

  Refactor the common lookup into one local helper returning the exact resolved
  `BindingOp` or `TypeOp`. Enforce:

  ```text
  inline + BindingOp => effect == "pure"
  invoke + BindingOp => effect == "stateful"
  inline/invoke + TypeOp => kind == "implementation"
  every other resolved operation => incompatible callee diagnostic
  unresolved => unresolved callee diagnostic
  ```

  Record generated TypeOp use in a deterministic map keyed by canonical symbol;
  reject mixed inline/invoke use with one stable diagnostic. Do not compare C++
  spellings or fingerprints as lookup keys.

- [ ] **Step 6: Close process/module legality and types**

  Permit InlineOp inside ProcessOp closed legality while retaining its Pure
  interface. Enforce context-sensitive result types:

  ```text
  module inline  => exactly ExprType
  process inline => exactly IntegerType, FloatType, IndexType, or ValueType
  invoke         => every result is ValueType or WakeType
  ```

  Keep LiveLoad/LiveStore slot types as exact ValueType records; builtin scalar
  crossing-suspension wrap/unwrap is a later ProcessStatePlan responsibility.
  InlineOp itself must remain effect-free and receive no ownership/runtime row.

- [ ] **Step 7: Migrate all C++ and textual consumers completely**

  Replace every `getBinding*` use for InlineOp/InvokeOp with `getCallee*` and
  update builder calls/fixtures. Do not change `acsim.binding` records or the
  unrelated binding-lock APIs. Build with warnings-as-errors so stale generated
  accessor use is a compile failure.

- [ ] **Step 8: Update normative specifications**

  Amend `acsim-gfsim-lowering-v0.1.md` to define exact external BindingOp versus
  compiler TypeOp implementation callees, process-inline legality, effect
  closure, scalar wrap/unwrap handoff, and the absence of generated binding
  records. Preserve the closed op/type inventory counts.

- [ ] **Step 9: Run focused GREEN and mutation checks**

  Run the focused ACSim C++ API tests and `generated-call-contract.mlir`.
  Temporarily mutate TypeOp callee effect tracking so one implementation may be
  used by both InlineOp and InvokeOp; observe the mixed-effect negative test
  fail, then remove the mutation and restore GREEN. Also temporarily accept a
  stateful BindingOp in InlineOp and prove its negative test fails.

- [ ] **Step 10: Run full verification**

  Run complete Debug/Release build, CTest, and lit; Python contract discovery;
  repository contract checker; clang analyzer on every touched production
  translation unit; clang-format, `git diff --check`, zero tracked
  `.superpowers/**`, and clean status.

- [ ] **Step 11: Commit**

  Create one commit:

  ```bash
  git commit -m "fix(ir): support generated process callees"
  ```

  Do not push. Write RED/GREEN/mutation/full-suite evidence to the assigned
  ignored SDD report path.
