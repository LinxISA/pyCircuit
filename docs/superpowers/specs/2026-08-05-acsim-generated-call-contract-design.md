# ACSim generated-call contract design

## Status

Approved under the standing project rule to adopt the recommended option for
remaining v0.1 decisions. This is a hard-break correction: all producers,
consumers, tests, and specifications migrate together; no legacy attribute,
lookup, or compatibility path remains.

## Problem

ACIR process regions legally contain scalar `arith`/`index`, pure `func.call`,
record/packet operations, stateful queue/resource/event/trace operations, and
suspension. Canonical ACSim currently permits `acsim.inline` only at module
construction, requires it to reference a pure registry `acsim.binding`, and
requires process `acsim.invoke` to reference a stateful registry binding.

That contract cannot represent either:

- compiler-generated pure helpers for core record/packet operations or scalar
  live-state wrap/unwrap; or
- compiler-generated stateful methods for core queues, resources, event
  queues, trace sources, and wake construction.

Fabricating registry BindingRecords would violate the binding-lock contract:
generated modules, processes, and core primitives never resolve through the
external/library registry.

## Considered approaches

### A. Generalize `acsim.inline` and `acsim.invoke` (selected)

Both operations become exact static calls. A callee is either an external
registry binding with the required effect or a compiler-generated
`acsim.type` of kind `implementation`. The operation kind supplies the effect:
`inline` is pure and `invoke` is stateful. This reuses the existing closed op
inventory and preserves generic C++ call lowering.

### B. Add `acsim.eval` and `acsim.call_generated`

This makes generated/external provenance visually explicit, but expands the
public operation inventory and duplicates call verification/code generation.
It adds no semantic capability over approach A.

### C. Reject or erase ACIR pure/core process operations

This contradicts the ACIR v0.1 process contract and would force code generation
to recover behavior outside canonical ACSim. It is not acceptable.

## Selected public IR contract

### Generated implementation identities

`acsim.type` kind `implementation` is the sole identity for a
compiler-generated callable helper or method. It carries the existing safe C++
spelling and fingerprint. It is not a binding record, provider, placement,
runtime object, or registry request.

The compiler emits deterministic identities for:

- core record/packet helpers;
- pure scalar/value wrap and unwrap helpers;
- generated core-component methods;
- compiler-generated wake construction.

### `acsim.inline`

Hard-break the target attribute/accessor from `binding` to `callee`.

The callee resolves exactly to either:

- an `acsim.binding` whose effect is `pure`; or
- an `acsim.type` whose kind is `implementation`.

`acsim.inline` remains pure, receives no owner/object/dispatch/activation row,
and has no hidden state. It is legal both in a generated module body and inside
an `acsim.process` state.

- In a module body, its result remains `!acsim.expr<@cpp_type>`.
- In a process, its result may be a canonical builtin scalar/index/float or
  `!acsim.value<@cpp_type>`. An expression result is not legal process state.

The current single-result operation is retained. Multi-result pure
`func.call` is expanded by ProcessStatePlan into its callee body and return
forwarding; it is never represented by a multi-result inline call.

### `acsim.invoke`

Hard-break the target attribute/accessor from `binding` to `callee`.

The callee resolves exactly to either:

- an `acsim.binding` whose effect is `stateful`; or
- an `acsim.type` whose kind is `implementation`.

`acsim.invoke` remains process-only and may return only exact
`!acsim.value<...>` or `!acsim.wake<...>` values. A generated implementation
callee is not inserted into the binding lock and does not acquire a separate
runtime object merely because it is callable. Stateful object methods receive
their exact owner/ref/resource arguments; stateless scheduler/wake helpers may
have no object argument.

One generated implementation identity may not be used by both `inline` and
`invoke` in the same model. This closes its effect classification without
adding mutable effect metadata to `acsim.type`.

### Process scalar and live-state boundary

Canonical scalar `arith`, `index`, builtin scalar values, and `cf` remain legal
inside a process. Live slots continue to store only `!acsim.value<...>`.

When a builtin scalar crosses suspension, ProcessStatePlan explicitly inserts
and counts:

- a generated pure wrap inline call before `acsim.live.store`; and
- `acsim.live.load` followed by a generated pure unwrap inline call after
  resumption.

These helpers compile to specialized C++ with no required runtime allocation.
Aggregate/packet values already use `!acsim.value` and need no scalar wrapper.

### Pure `func.call`

ProcessStatePlan resolves the exact pure callee and iteratively expands its
body into the caller plan. Arguments, block arguments, returns, and results are
explicit forwarding bindings. Expansion is acyclic and uses the existing
pure-call node/edge/depth capabilities. `func.call` and `func.return` never
appear in canonical ACSim.

Before freeze, process-reachable pure functions are hard-checked to the same
lowerable structured subset as a process continuation: deterministic blocks,
canonical scalar/core pure operations, structured `scf`, and nested pure calls.
Non-suspending loops require an exact static trip count; arbitrary cyclic `cf`,
external declarations, and effectful or unsupported dialect operations are
rejected. This prevents a pure helper from hiding unbounded Work execution.

The expanded leaf actions are canonical scalar operations or generated
`acsim.inline` calls for core helpers. Task 13 consumes the expansion directly
and performs no call-graph analysis.

## Verification invariants

- Exact target lookup only; no component name, C++ spelling, hierarchy, or
  fallback lookup.
- Binding effect must match the call operation.
- Generated callee must be `acsim.type` kind `implementation`.
- A generated implementation identity has one effect class per model.
- Process `inline` is memory-effect free and cannot target a stateful binding.
- `invoke` cannot target a pure binding.
- Core generated implementations never appear in binding requests or locks.
- Module/process legality, deterministic ordering, fingerprints, source maps,
  capability limits, and no-placeholder rules remain closed.

## ProcessStatePlan handoff

Each planned action records one exact emission class:

- copy legal scalar `builtin`/`arith`/`index` operation;
- generated/external pure `inline` callee;
- generated/external stateful `invoke` callee;
- scalar live-state wrap or unwrap;
- control/continuation action.

The plan carries the exact callee operation handle, effect class, result type,
and emitted-operation cost. Task 13 may only materialize the recorded class and
must assert its emitted count equals the plan.

## Testing

- Positive module and process inline calls to pure BindingOp and generated
  implementation TypeOp.
- Positive process invokes to stateful BindingOp and generated implementation
  TypeOp, including zero-argument wake construction.
- Negative effect mismatch, incompatible TypeOp kind, unresolved/wrong-order
  callee, mixed inline/invoke use of one generated identity, illegal process
  result type, and old `binding` syntax/accessor.
- Scalar wrap/store/load/unwrap sequence verifies and contributes exact
  fairness cost.
- Pure function expansion with multiple returns produces no `func` operation
  in ACSim.
- Every affected ODS builder/accessor and textual API receives C++ and lit
  coverage; no legacy path remains.

## Compatibility

None. The contract epoch remains the still-unreleased v0.1 development epoch,
but the old `binding` operand spelling/accessor is deleted everywhere. Git is
the only rollback mechanism.
