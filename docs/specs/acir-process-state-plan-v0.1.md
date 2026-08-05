# ACIR ProcessStatePlan contract design

## Status

Proposed for the ACIR v0.1 implementation. The activation commit, push, and
green push/PR CI freeze this public ProcessStatePlan contract before production
implementation begins. Every
producer, consumer, test, schema, and specification changes together when this
contract changes. Git is the rollback mechanism; no compatibility API,
fallback lookup, legacy spelling, or partial ACSim stage is retained.

The staged implementation plan, this design record, and the corrected Task
12/Task 13 ownership boundary must be committed and pushed before Task 1 edits
production code.

## Scope and ownership

Task 12 analyzes a selected topology-frozen ACIR model without modifying it and
returns one immutable `ProcessStatePlanSet`. It owns:

- the public header `include/acir/Analysis/ProcessStatePlan.h`;
- the private analysis implementation and test hooks;
- the neutral ACIR dialect lowerability helpers;
- `lib/Transforms/LowerProcessState.cpp` and the non-mutating
  `ac-lower-process-state` pass;
- canonical plan verification, serialization, and atomic report publication.

Task 13 consumes the public plan and a separate exact
`BindingResolutionResult`. It does not parse the report, inspect planner
internals, repeat liveness or control analysis, reconstruct helper
specializations, or take ownership of the Task 12 pass.

Task 1 publishes the immutable IDs, records, accessors, verifier, and
serializer. It does not declare `planProcessState` or the atomic writer as
undefined promises. Task 5 adds those declarations and their real definitions
together.

All raw-structure preflight, plan traversal, report traversal, and report
emission are iterative and capability bounded. No hostile-input path uses
recursive region or report traversal.

## Public header conventions

All public declarations live in namespace `acir` in
`include/acir/Analysis/ProcessStatePlan.h`.

Public records are immutable value objects. Constructors are private and are
available only to the private builder/friend implementation. Strings are
returned as `llvm::StringRef`; collections are returned as immutable
`llvm::ArrayRef<T>` views. A tagged union exposes only its active arm. The API
contains no public mutator, builder, parser, corruption hook, callback,
unchecked integer-to-ID conversion, component-name lookup, hierarchy lookup,
C++-name lookup, fallback lookup, runtime descriptor, or
`BindingResolutionResult` parameter.

MLIR types, values, and operation handles remain valid only while the same
unmodified input module and `MLIRContext` remain alive.

## Dense identifier types

The public dense identifiers are distinct strongly typed wrappers around
`uint32_t`:

```cpp
class ProcessCalleeId;
class ProcessValueTypeId;
class ProcessCaptureId;
class ProcessPcId;
class ProcessBlockId;
class ProcessLiveSlotId;
class ProcessWakeId;
class ProcessTransitionId;
```

Every ID exposes:

```cpp
uint32_t value() const;
```

IDs support equality and ordering for immutable record collections. Their
construction remains private.

## Closed enums and serialized spellings

The C++ enumerators and JSON spellings are closed to the following values.

| Enum | C++ enumerator | JSON spelling |
| --- | --- | --- |
| `ProcessWakeKind` | `Condition` | `condition` |
|  | `Resource` | `resource` |
|  | `EventQueue` | `event_queue` |
|  | `NextDelta` | `next_delta` |
| `ProcessSubscriptionSourceKind` | `Capture` | `capture` |
|  | `Value` | `value` |
|  | `Symbol` | `symbol` |
| `ProcessActionKind` | `Original` | `original` |
|  | `Constant` | `constant` |
|  | `ForInitialize` | `for_initialize` |
|  | `ForCondition` | `for_condition` |
|  | `ForIncrement` | `for_increment` |
|  | `ScalarWrap` | `scalar_wrap` |
|  | `ScalarUnwrap` | `scalar_unwrap` |
| `ProcessEmissionClass` | `CopyScalar` | `copy_scalar` |
|  | `Inline` | `inline` |
|  | `Invoke` | `invoke` |
|  | `Wrap` | `wrap` |
|  | `Unwrap` | `unwrap` |
|  | `ForwardOnly` | `forward_only` |
| `ProcessOccurrenceKind` | `Original` | `original` |
|  | `SyntheticLoop` | `synthetic` |
|  | `SyntheticWrapper` | `synthetic` |
|  | `SyntheticConstant` | `synthetic` |
| `ProcessLoopPhase` | `Initialize` | `initialize` |
|  | `Condition` | `condition` |
|  | `Increment` | `increment` |
| `ProcessWrapperDirection` | `Wrap` | `wrap` |
|  | `Unwrap` | `unwrap` |
| `ProcessFrameKind` | `Entry` | `entry` |
|  | `ScfIf` | `scf.if` |
|  | `ScfFor` | `scf.for` |
|  | `ScfWhile` | `scf.while` |
| `ProcessFramePhase` | `Entry` | `entry` |
|  | `Then` | `then` |
|  | `Else` | `else` |
|  | `Merge` | `merge` |
|  | `Header` | `header` |
|  | `Body` | `body` |
|  | `Before` | `before` |
|  | `After` | `after` |
|  | `Exit` | `exit` |
| `ProcessPlannedValueKind` | `Original` | `original` |
|  | `Capture` | `capture` |
|  | `LiveSlot` | `live_slot` |
|  | `Synthetic` | `synthetic` |
|  | `Constant` | `constant` |
| `ProcessValueCoordinateKind` | `Result` | `result` |
|  | `BlockArgument` | `block_argument` |
| `ProcessControlEdgeKind` | `Branch` | `branch` |
|  | `LocalContinue` | `local_continue` |
|  | `Suspend` | `suspend` |
|  | `Terminate` | `terminate` |
| `ProcessTerminateStatus` | `Success` | `success` |
|  | `Failure` | `failure` |
| `ProcessEffectKind` | `Pure` | `pure` |
|  | `Stateful` | `stateful` |
| `ProcessValueTypeKind` | `Value` | `value` |
|  | `Packet` | `packet` |

Frame kind and phase pairs are restricted to this matrix:

| Frame kind | Allowed phases |
| --- | --- |
| `Entry` | `Entry` |
| `ScfIf` | `Then`, `Else`, `Merge` |
| `ScfFor` | `Header`, `Body`, `Exit` |
| `ScfWhile` | `Before`, `After`, `Exit` |

Synthetic `scf.for` occurrences use `ProcessLoopPhase`, not the broader frame
phase enum. All synthetic occurrence JSON arms use `kind: "synthetic"`; their
closed field sets disambiguate loop, wrapper, and constant arms.

## Capability limits

The public limits record uses these exact field names and defaults:

```cpp
struct ProcessStateLimits {
  uint64_t maxProcesses = 1U << 20;
  uint64_t maxProgramCounters = 1U << 20;
  uint64_t maxLiveSlots = 1U << 20;
  uint64_t maxWakeRecords = 1U << 20;
  uint64_t maxCalleeDescriptors = 1U << 20;
  uint64_t maxPlannedOperations = 1U << 20;
  uint64_t maxFairnessWork = 1U << 20;
  uint64_t maxTransitions = 1U << 22;
  uint64_t maxNestedRegionDepth = 512;
  uint64_t maxCanonicalReportBytes = 1U << 24;
};
```

The implementation does not rename, merge, or infer another public limit.
Exact-boundary values succeed. Boundary plus one fails with a diagnostic that
names the capability and configured value.

## Stable call-site and occurrence identities

Call-site and occurrence records retain raw operations only as provenance:

```cpp
class ProcessCallSitePlan {
public:
  mlir::Operation *operation() const;
  llvm::StringRef operationPath() const;
  llvm::ArrayRef<uint64_t> iterationVector() const;
};

class ProcessOriginalOccurrence {
public:
  mlir::Operation *operation() const;
  llvm::StringRef operationPath() const;
  llvm::ArrayRef<ProcessCallSitePlan> callSites() const;
  llvm::ArrayRef<uint64_t> iterationVector() const;
};

class ProcessSyntheticLoopOccurrence {
public:
  const ProcessOccurrenceId &anchor() const;
  ProcessLoopPhase phase() const;
};

class ProcessSyntheticWrapperOccurrence {
public:
  const ProcessOccurrenceId &anchor() const;
  ProcessTransitionId transition() const;
  ProcessLiveSlotId slot() const;
  ProcessWrapperDirection direction() const;
};

class ProcessSyntheticConstantOccurrence {
public:
  const ProcessOccurrenceId &anchor() const;
  uint32_t constant() const;
};

class ProcessOccurrenceId {
public:
  ProcessOccurrenceKind kind() const;
  const ProcessOriginalOccurrence &original() const;
  const ProcessSyntheticLoopOccurrence &syntheticLoop() const;
  const ProcessSyntheticWrapperOccurrence &syntheticWrapper() const;
  const ProcessSyntheticConstantOccurrence &syntheticConstant() const;
};
```

Only the active arm getter is valid. Inactive-arm fields are absent from JSON.
The exact JSON arms are:

- original: `{call_sites,iteration_vector,kind,operation_path}`;
- synthetic loop: `{anchor,kind,phase}`;
- synthetic wrapper: `{anchor,direction,kind,slot,transition}`;
- synthetic constant: `{anchor,constant,kind}`.

Every original occurrence preserves the complete outer-to-inner pure-call-site
chain and constant-loop iteration vector in its canonical identity.

## Structural SSA coordinates and planned values

`ProcessValueCoordinate` identifies a physical original operation result or
block argument:

```cpp
class ProcessValueCoordinate {
public:
  ProcessValueCoordinateKind kind() const;
  llvm::StringRef ownerPath() const;
  uint32_t index() const;
};
```

Operation ordinals count every physical original operation in the containing
block, including operations that do not become planned actions. Filtering,
rewriting, or action selection never renumbers them.

Planned values form this closed union. Raw values are provenance only:

```cpp
class ProcessOriginalPlannedValue {
public:
  mlir::Value value() const;
  const ProcessOccurrenceId &occurrence() const;
  const ProcessValueCoordinate &coordinate() const;
  llvm::StringRef path() const;
};

class ProcessCapturePlannedValue {
public:
  ProcessCaptureId capture() const;
};

class ProcessLiveSlotPlannedValue {
public:
  ProcessLiveSlotId slot() const;
};

class ProcessSyntheticPlannedValue {
public:
  const ProcessOccurrenceId &occurrence() const;
  const ProcessValueCoordinate &coordinate() const;
};

class ProcessConstantPlannedValue {
public:
  llvm::StringRef value() const;
};

class ProcessPlannedValue {
public:
  ProcessPlannedValueKind kind() const;
  mlir::Type type() const;
  const ProcessOriginalPlannedValue &original() const;
  const ProcessCapturePlannedValue &capture() const;
  const ProcessLiveSlotPlannedValue &liveSlot() const;
  const ProcessSyntheticPlannedValue &synthetic() const;
  const ProcessConstantPlannedValue &constant() const;
};
```

The exact JSON arms are:

- original: `{coordinate,kind,occurrence,path,type}`;
- capture: `{capture,kind,type}`;
- live slot: `{kind,slot,type}`;
- synthetic: `{coordinate,kind,occurrence,type}`;
- constant: `{kind,type,value}`.

The coordinate object is exactly `{index,kind,owner_path}`. Optional or
inactive fields are absent, never `null`.

Raw `mlir::Value` handles may be retained as provenance, but never define
identity or deduplication.

## Scalar operation description

Copied regionless builtin, arithmetic, and index operations use an explicit
canonical description:

```cpp
class ProcessScalarAttribute {
public:
  llvm::StringRef name() const;
  llvm::StringRef value() const;
};

class ProcessScalarOperationPlan {
public:
  llvm::StringRef name() const;
  llvm::ArrayRef<ProcessScalarAttribute> attributes() const;
  llvm::StringRef properties() const;
};
```

`name` is the fully qualified MLIR operation name. Attribute values and
properties use canonical generic MLIR spelling. Attributes sort bytewise by
`(name, value)`. Empty attributes serialize as `[]`; empty properties serialize
as the string `{}`. The exact JSON object is:

```json
{"attributes":[{"name":"predicate","value":"..."}],"name":"arith.cmpi","properties":"{}"}
```

Synthetic `scf.for` actions are fixed:

- `for_initialize` uses `forward_only` and has no scalar operation object;
- `for_condition` uses `copy_scalar` and records canonical `arith.cmpi` with
  the exact signed-less-than predicate and type attributes;
- `for_increment` uses `copy_scalar` and records canonical `arith.addi`.

Task 13 emits these records directly and performs no loop-shape inference.

## Core immutable records

The following declarations define the immutable public information surface.
Arm-specific getters are valid only for the matching discriminant.

```cpp
class ProcessCapturePlan {
public:
  ProcessCaptureId id() const;
  llvm::StringRef name() const;
  mlir::Value operand() const;
  mlir::Value entryArgument() const;
  mlir::Type type() const;
  llvm::StringRef operandPath() const;
  llvm::StringRef argumentPath() const;
};

class ProcessActionPlan {
public:
  uint32_t id() const;
  ProcessActionKind kind() const;
  ProcessEmissionClass emission() const;
  const ProcessOccurrenceId &occurrence() const;
  mlir::Operation *sourceOperation() const;
  llvm::ArrayRef<uint64_t> iterationVector() const;
  llvm::ArrayRef<ProcessPlannedValue> operands() const;
  llvm::ArrayRef<ProcessPlannedValue> results() const;
  uint32_t cost() const;
  llvm::ArrayRef<mlir::Type> resultTypes() const;
  std::optional<ProcessCalleeId> callee() const;
  const ProcessScalarOperationPlan *scalarOp() const;
};

class ProcessLiveSlotPlan {
public:
  ProcessLiveSlotId id() const;
  llvm::StringRef name() const;
  mlir::Type type() const;
  ProcessValueTypeId storageType() const;
  llvm::ArrayRef<ProcessPlannedValue> memberValues() const;
  std::optional<ProcessCalleeId> wrapCallee() const;
  std::optional<ProcessCalleeId> unwrapCallee() const;
};

class ProcessSubscriptionSourcePlan {
public:
  ProcessSubscriptionSourceKind kind() const;
  mlir::Value value() const;
  mlir::Operation *owner() const;
  mlir::Operation *declaration() const;
  std::optional<ProcessCaptureId> capture() const;
  llvm::StringRef symbol() const;
  llvm::StringRef path() const;
  llvm::StringRef ownerPath() const;
};

class ProcessWakePlan {
public:
  ProcessWakeId id() const;
  ProcessWakeKind kind() const;
  mlir::Operation *operation() const;
  mlir::Value triggeringValue() const;
  mlir::Operation *declaration() const;
  ProcessCalleeId callee() const;
  llvm::StringRef typeKey() const;
  llvm::StringRef operationPath() const;
  llvm::StringRef target() const;
  const ProcessOccurrenceId &occurrence() const;
  llvm::ArrayRef<uint64_t> iterationVector() const;
  llvm::ArrayRef<ProcessSubscriptionSourcePlan> sources() const;
};

class ProcessTransitionStorePlan {
public:
  ProcessLiveSlotId slot() const;
  const ProcessPlannedValue &source() const;
  mlir::Value sourceValue() const;
};

class ProcessTransitionLoadPlan {
public:
  ProcessLiveSlotId slot() const;
  llvm::ArrayRef<ProcessPlannedValue> replacements() const;
};

class ProcessTransitionPlan {
public:
  ProcessTransitionId id() const;
  ProcessPcId sourcePc() const;
  ProcessPcId targetPc() const;
  ProcessWakeId wake() const;
  llvm::ArrayRef<uint64_t> iterationVector() const;
  llvm::ArrayRef<ProcessTransitionStorePlan> stores() const;
  llvm::ArrayRef<ProcessTransitionLoadPlan> loads() const;
};

class ProcessForwardingBindingPlan {
public:
  const ProcessPlannedValue &from() const;
  const ProcessPlannedValue &to() const;
};

class ProcessControlFramePlan {
public:
  ProcessFrameKind kind() const;
  ProcessFramePhase phase() const;
  mlir::Operation *operation() const;
  llvm::StringRef operationPath() const;
  llvm::ArrayRef<ProcessForwardingBindingPlan> bindings() const;
};

class ProcessControlEdgePlan {
public:
  ProcessControlEdgeKind kind() const;
  const ProcessPlannedValue &condition() const;
  ProcessBlockId trueBlock() const;
  ProcessBlockId falseBlock() const;
  llvm::ArrayRef<ProcessForwardingBindingPlan> trueBindings() const;
  llvm::ArrayRef<ProcessForwardingBindingPlan> falseBindings() const;
  ProcessBlockId targetBlock() const;
  llvm::ArrayRef<ProcessForwardingBindingPlan> bindings() const;
  ProcessTransitionId transition() const;
  ProcessTerminateStatus status() const;
};

class ProcessBlockPlan {
public:
  ProcessBlockId id() const;
  ProcessPcId pc() const;
  mlir::Region *originRegion() const;
  mlir::Block *originBlock() const;
  llvm::StringRef path() const;
  llvm::ArrayRef<ProcessControlFramePlan> frames() const;
  llvm::ArrayRef<ProcessTransitionLoadPlan> loads() const;
  llvm::ArrayRef<ProcessActionPlan> actions() const;
  const ProcessControlEdgePlan &edge() const;
  uint64_t cost() const;
};

class ProcessPcPlan {
public:
  ProcessPcId id() const;
  llvm::StringRef name() const;
  llvm::StringRef entryPath() const;
  llvm::ArrayRef<ProcessBlockId> blocks() const;
};

class ProcessStatePlan {
public:
  llvm::StringRef definitionKey() const;
  ac::ProcessOp process() const;
  llvm::ArrayRef<ProcessCapturePlan> captures() const;
  ProcessPcId entryPc() const;
  llvm::ArrayRef<ProcessPcPlan> pcs() const;
  llvm::ArrayRef<ProcessBlockPlan> blocks() const;
  llvm::ArrayRef<ProcessLiveSlotPlan> liveSlots() const;
  llvm::ArrayRef<ProcessWakePlan> wakes() const;
  llvm::ArrayRef<ProcessTransitionPlan> transitions() const;
  uint32_t pcBitWidth() const;
  uint64_t fairnessWork() const;
};
```

`ProcessActionPlan::id()` is a dense block-local ordinal. Its cost is exactly
zero or one. `sourceOperation()` is non-null for `Original`, points to the
owning `scf::ForOp` for loop phase actions, and is null for constant and scalar
wrapper actions. `callee()` is engaged exactly for inline, invoke, wrap, or
unwrap. `scalarOp()` is non-null exactly for copy-scalar.

Capture JSON is exactly `argument_path`, `name`, `operand_path`, `ordinal`, and
`type`. Action JSON is exactly `cost`, `emission`, `iteration_vector`, `kind`,
`occurrence`, `operands`, `ordinal`, `result_types`, and `results`, plus
`callee` exactly for inline/invoke/wrap/unwrap and `scalar_op` exactly for
copy-scalar. Optional fields are absent, never `null`.

`ProcessLiveSlotPlan::wrapCallee()` and `unwrapCallee()` are either both
present for a builtin scalar that needs storage conversion or both absent for
aggregate/packet state. Live-slot JSON is exactly `member_values`, `name`,
`ordinal`, `storage_type`, and `type`, plus `wrap_callee` and `unwrap_callee`
together when present.

Wake provenance handles are omitted from JSON. The triggering value is non-null
only for a value-triggered wake; the declaration is non-null only for a
resolved resource or event queue. `target()` is always serialized: condition
value path, exact resource/event symbol, or the empty string for next delta.

Subscription-source JSON is a closed kind union:

- capture: `{capture,kind,path}`;
- value: `{kind,path}` plus `owner_path` only when an owner exists;
- symbol: `{kind,path,symbol}` plus `owner_path` only when an owner or
  declaration exists.

Inapplicable fields are absent. Transition store/load arrays sort by
deduplicated ascending slot ID. Raw store values are provenance only.

Control-edge getters are active-arm-only. Branch JSON is
`{condition,false_bindings,false_block,kind,true_bindings,true_block}`; local
continue is `{bindings,kind,target_block}`; suspend is `{kind,transition}`;
terminate is `{kind,status}`. The exact branch shape is:

```json
{"condition":{},"false_bindings":[],"false_block":0,"kind":"branch","true_bindings":[],"true_block":0}
```

`condition` is one planned-value object; each binding is exactly `{from,to}`;
block fields are dense block IDs. Both binding arrays are always present and
ordered by target block argument. Their count and each `to` coordinate match
the corresponding target arguments exactly. Task 13 never reconstructs
forwarding.

Wake JSON is exactly `callee`, `iteration_vector`, `kind`, `occurrence`,
`operation_path`, `ordinal`, `sources`, `target`, and `type_key`. Transition
JSON is exactly `iteration_vector`, `loads`, `ordinal`, `source_pc`, `stores`,
`target_pc`, and `wake`; a store is `{slot,source}` and a load is
`{replacements,slot}`. Block JSON is exactly `actions`, `cost`, `edge`,
`frames`, `loads`, `ordinal`, `path`, and `pc`. PC JSON is exactly `blocks`,
`entry_path`, `name`, and `ordinal`.

Process JSON contains exactly `blocks`, `captures`, `definition_key`,
`entry_pc`, `fairness_work`, `live_slots`, `pc_bit_width`, `pcs`,
`transitions`, and `wakes`. `process()` and all other raw handles are omitted.

The plan-set-owned generated descriptor tables expose:

```cpp
class ProcessGeneratedCalleePlan {
public:
  ProcessCalleeId id() const;
  llvm::StringRef symbol() const;
  llvm::StringRef cpp() const;
  llvm::StringRef kind() const;
  llvm::StringRef fingerprint() const;
  ProcessEffectKind effect() const;
  llvm::ArrayRef<llvm::StringRef> inputTypeKeys() const;
  llvm::ArrayRef<llvm::StringRef> resultTypeKeys() const;
  ProcessHelperRole role() const;
  const ProcessGeneratedCalleePayload &payload() const;
  llvm::ArrayRef<mlir::Operation *> sourceOperations() const;
  llvm::ArrayRef<mlir::Operation *> declarations() const;
  llvm::ArrayRef<llvm::StringRef> sourcePaths() const;
};

class ProcessValueTypePlan {
public:
  ProcessValueTypeId id() const;
  llvm::StringRef symbol() const;
  llvm::StringRef cpp() const;
  ProcessValueTypeKind kind() const;
  llvm::StringRef fingerprint() const;
  mlir::Type acirType() const;
  const ProcessValueTypePayload &payload() const;
};

class ProcessStatePlanSet {
public:
  llvm::ArrayRef<ProcessStatePlan> processes() const;
  llvm::ArrayRef<ProcessGeneratedCalleePlan> callees() const;
  llvm::ArrayRef<ProcessValueTypePlan> valueTypes() const;
  const ProcessStatePlan *lookupByDefinitionKey(
      llvm::StringRef definitionKey) const;
};
```

`ProcessGeneratedCalleePlan::kind()` always returns `implementation`.
Generated callees are compiler-owned implementation identities, never external
binding records. `ProcessGeneratedCalleePayload` and `ProcessValueTypePayload`
are immutable closed typed unions. Their active arm exposes every field in the
closed payload tables below; Task 13 never parses payload JSON or revisits the
original IR to recover a field. Raw source operations, declarations, and MLIR
types are omitted from JSON; their canonical type spellings and stable paths
are serialized instead. The plan set owns every string and record.

Callee JSON contains exactly `cpp`, `effect`, `fingerprint`, `inputs`, `kind`,
`ordinal`, `payload`, `results`, `role`, `source_paths`, and `symbol`. Value-type
JSON contains exactly `acir_type`, `cpp`, `fingerprint`, `kind`, `ordinal`,
`payload`, and `symbol`; `kind` is exactly `value` or `packet`.

Plans sort bytewise by exact definition key. `lookupByDefinitionKey()` accepts
only the full canonical `@Module::@process` byte sequence and returns the
unique plan or `nullptr`. It rejects partial symbols, component names,
hierarchy-like names, aliases, normalization, C++ names, and fallback guesses.

Dense-record classes expose `id()`. JSON emits `id().value()` as `ordinal`;
there is no redundant `ordinal()` accessor.

## Public façade publication stages

Task 1 declares and defines the immutable record accessors and declares these
fully linkable validation and serialization functions:

```cpp
mlir::LogicalResult verifyProcessStatePlan(
    const ProcessStatePlanSet &plan,
    const ProcessStateLimits &limits = {});

llvm::Expected<std::string> serializeProcessStatePlan(
    const ProcessStatePlanSet &plan,
    const ProcessStateLimits &limits = {});
```

Task 5 adds these declarations and their definitions in the same checkpoint:

```cpp
mlir::FailureOr<ProcessStatePlanSet> planProcessState(
    mlir::ModuleOp module,
    const ProcessStateLimits &limits = {});

llvm::Error writeProcessStatePlanReportAtomically(
    const ProcessStatePlanSet &plan,
    llvm::StringRef path,
    const ProcessStateLimits &limits = {});
```

There is no public report parser or public plan builder.

## Stable path grammar

All fixed path components are lowercase ASCII tokens. User-defined symbols
retain canonical MLIR symbol spelling. Paths derive only from immutable
original structure or stable plan IDs, never pointer identity, allocation
order, map iteration order, input source locations, or filtered action order.

```text
definition-key          = module-symbol "::" process-symbol
module-symbol           = "@" symbol-name
process-symbol          = "@" symbol-name
function-symbol         = "@" symbol-name

process-root            = definition-key
function-root           = definition-key "/func/" function-symbol
structural-root         = process-root | function-root

operation-path          = structural-root region-step block-step operation-tail
operation-tail          = operation-step (region-step block-step operation-step)*
region-step             = "/r" decimal-index
block-step              = "/b" decimal-index
operation-step          = "/o" decimal-index
result-path             = operation-path "/v" decimal-index
argument-path           = structural-root region-step block-step
                          (operation-step region-step block-step)*
                          "/a" decimal-index

declaration-path     = definition-key "/decl/" declaration-kind "/" decimal-index
declaration-kind     = "capture" | "callee" | "type"

pc-path              = definition-key "/plan/pc/" pc-name
plan-block-path      = pc-path "/b" eight-digit-decimal
live-slot-path       = definition-key "/plan/live/" eight-digit-decimal
wake-path            = definition-key "/plan/wake/" eight-digit-decimal
transition-path      = definition-key "/plan/transition/" eight-digit-decimal
```

Examples:

```text
@Top::@workload/r0/b0/o3
@Top::@workload/func/@helper/r0/b0/o2
@Top::@workload/plan/pc/entry/b00000000
@Top::@workload/plan/live/00000000
```

Structural indices are zero-based decimal indices. A function root is
plan-local: the same physical pure function consumed by another process uses
that process's definition-key prefix. `rN` is a region of the immediately
preceding operation; `bN` is a block of that region. `oN` counts every physical
operation, including structural containers and terminators. Filtering planned
actions never renumbers it. Plan block, live-slot, wake, and transition IDs use
zero-based fixed-width eight-digit decimal suffixes.

The definition key is exactly `@Module::@process` using canonical module and
process symbol spellings.

Every call site serializes exactly:

```json
{"iteration_vector":[],"operation_path":"@Top::@workload/r0/b0/o1"}
```

The closed field set is `{iteration_vector,operation_path}`. The raw operation
handle is omitted. `call_sites` is outermost-to-innermost. A nested call inside
an out-of-line function uses the function-root operation path. The complete
ordered call-site objects and unsigned iteration vectors participate in
occurrence identity.

### Occurrence diagnostic paths

An original occurrence diagnostic path is:

```text
<source-operation-path>/occ/<sha256-of-canonical-occurrence-json>
```

The canonical occurrence object includes the complete call-site chain and
outer-to-inner constant-loop iteration vector. Synthetic identity adds these
exact components before hashing:

```text
synthetic-loop:      anchor=<canonical-occurrence>, phase=<initialize|condition|increment>
synthetic-wrapper:   anchor=<canonical-occurrence>, transition=<8-digit-id>, slot=<8-digit-id>, direction=<wrap|unwrap>
synthetic-constant:  anchor=<canonical-occurrence>, constant=<decimal>
```

The diagnostic path uses the original source operation path as a readable
prefix and the full lowercase 64-hex SHA-256 of canonical occurrence JSON as
the collision-free suffix. Canonical occurrence JSON is UTF-8, sorts object
keys lexicographically, keeps arrays in normative plan order, uses minimal
integer spelling, and contains no insignificant whitespace.

## Canonical dense ordering

Block ordering is independent of IDs. For every PC, compute a private
canonical traversal path iteratively over the already-built acyclic graph:

```text
entry block path          = "entry"
branch true successor     = predecessor-path "/t"
branch false successor    = predecessor-path "/f"
local-continue successor  = predecessor-path "/n"
```

Suspend and terminate have no PC-local successor. When a merge has multiple
candidate traversal paths, retain the bytewise smallest candidate. Propagate
candidates topologically to a fixed point. A missing entry, cycle, duplicate
final traversal path, or unreachable block fails verification. Sort blocks by
`(pc-id.value(), traversal-path bytewise)`, then assign globally dense
`ProcessBlockId` values. `ProcessPcPlan::blocks()` retains this order. The
report block path is
`<definition-key>/plan/pc/<pc-name>/b<global-block-id-as-eight-decimal-digits>`.

Generated callees deduplicate by exact equality of their complete RFC 8785
specialization bytes. Sort the remaining records by unsigned-byte
lexicographic order of those bytes, then assign dense `ProcessCalleeId` values.
Generated value types use the identical rule over complete RFC 8785 value-type
specialization bytes, then receive dense `ProcessValueTypeId` values. Symbols,
C++ spellings, pointers, discovery order, map order, and truncated digests are
not secondary comparators. Equal canonical bytes after deduplication are a
verifier failure.

## Type-key grammar

The type-key namespace is closed:

```text
mlir:<canonical generic MLIR type spelling>
storage:value:<64 lowercase hex SHA-256>
storage:packet:<64 lowercase hex SHA-256>
@acir_wake_condition
@acir_wake_resource
@acir_wake_event_queue
@acir_wake_next_delta
```

`mlir:` identifies an existing builtin, MLIR, or ACIR type represented
directly. `storage:value:` and `storage:packet:` identify entries in the
generated value-type table. Wake keys are four built-in wake types; they do not
create value-type descriptors or accept another prefix. Task 13 maps each
literal directly to `!acsim.wake<@kind>`.

## Generated implementation specialization

Every generated callee uses this canonical specialization record:

```json
{
  "contract_epoch":"0.1",
  "effect":"pure|stateful",
  "inputs":["<type-key>","..."],
  "kind":"implementation",
  "payload":{},
  "results":["<type-key>","..."],
  "role":"<closed-role>",
  "schema":"acir-generated-implementation-0.1",
  "source_paths":["<stable-path>","..."]
}
```

`payload` follows the selected role's closed schema below. Every listed field
participates in specialization identity. Canonical bytes are UTF-8 JSON with
lexicographically sorted object keys, canonical escaping, minimal integer
spelling, arrays in normative order, and no insignificant whitespace.

```text
fingerprint = "sha256:" + lowercase_hex(sha256(canonical-specialization-bytes))
symbol      = "@acir_impl_" + role + "_" + lowercase_hex(digest)
cpp         = "acir::generated::impl_" + role + "_" + lowercase_hex(digest)
```

The full 64-hex digest is never truncated. Before hashing, `source_paths` sorts
bytewise and removes duplicates. It contains every stable original or
synthetic occurrence path requiring the exact specialization. Source paths do
not change payload shape, but do participate in the fingerprint. A built-in
helper with no frozen source/declaration handle uses an empty array; the exact
yield-only `wake_next_delta` descriptor below is that case.

Generated descriptors deduplicate only by the complete specialization record.
A descriptor may not mix pure and stateful use.

### Closed helper roles

The generated role set is exactly:

```cpp
enum class ProcessHelperRole {
  RecordCreate,
  RecordGet,
  RecordWith,
  PacketSerialize,
  PacketDeserialize,
  TraceDecode,
  QueueTrySend,
  QueueTryRecv,
  EventSchedule,
  TraceOpen,
  TraceNext,
  TraceEof,
  TracePosition,
  ContractRequire,
  ContractEnsure,
  ContractAssert,
  Probe,
  StatAdd,
  WakeCondition,
  WakeResource,
  WakeEventQueue,
  WakeNextDelta,
  ScalarWrap,
  ScalarUnwrap
};
```

JSON converts each enumerator to its lower-snake-case spelling, from
`record_create` through `scalar_unwrap`, in the same order.

### Closed helper payloads

No role payload accepts another key. Only the arm matching `role()` is callable.

```cpp
class ProcessRecordFieldDescriptor {
public:
  llvm::StringRef name() const;
  llvm::StringRef typeKey() const;
};

class ProcessRecordCreatePayload {
public:
  llvm::ArrayRef<ProcessRecordFieldDescriptor> fields() const;
  llvm::StringRef recordType() const;
};
class ProcessRecordGetPayload {
public:
  llvm::StringRef field() const;
  llvm::StringRef record() const;
  llvm::StringRef result() const;
};
class ProcessRecordWithPayload {
public:
  llvm::StringRef field() const;
  llvm::StringRef record() const;
  llvm::StringRef value() const;
};
class ProcessPacketSerializePayload {
public:
  uint64_t bytes() const;
  llvm::StringRef packet() const;
  llvm::StringRef packetType() const;
};
class ProcessPacketDeserializePayload {
public:
  uint64_t bytes() const;
  llvm::StringRef packet() const;
  llvm::StringRef packetType() const;
};
class ProcessTraceDecodePayload {
public:
  llvm::StringRef entry() const;
  llvm::StringRef result() const;
  llvm::StringRef source() const;
};
class ProcessQueueTrySendPayload {
public:
  llvm::StringRef element() const;
  llvm::StringRef queue() const;
};
class ProcessQueueTryRecvPayload {
public:
  llvm::StringRef element() const;
  llvm::StringRef queue() const;
};
class ProcessEventSchedulePayload {
public:
  llvm::StringRef delay() const;
  llvm::StringRef target() const;
  llvm::StringRef value() const;
};
class ProcessTraceOpenPayload {
public:
  llvm::StringRef source() const;
};
class ProcessTraceNextPayload {
public:
  llvm::StringRef entry() const;
  llvm::StringRef source() const;
};
class ProcessTraceEofPayload {
public:
  llvm::StringRef source() const;
};
class ProcessTracePositionPayload {
public:
  llvm::StringRef source() const;
};
class ProcessContractRequirePayload {
public:
  llvm::StringRef message() const;
};
class ProcessContractEnsurePayload {
public:
  llvm::StringRef message() const;
};
class ProcessContractAssertPayload {
public:
  llvm::StringRef message() const;
};
class ProcessProbePayload {
public:
  llvm::StringRef kind() const;
  llvm::StringRef result() const;
  llvm::StringRef target() const;
};
class ProcessStatAddPayload {
public:
  llvm::StringRef stat() const;
  llvm::StringRef valueType() const;
};
class ProcessWakeConditionPayload {
public:
  ProcessWakeKind wakeKind() const;
  llvm::StringRef wakeType() const;
};
class ProcessWakeResourcePayload {
public:
  ProcessWakeKind wakeKind() const;
  llvm::StringRef wakeType() const;
};
class ProcessWakeEventQueuePayload {
public:
  ProcessWakeKind wakeKind() const;
  llvm::StringRef wakeType() const;
};
class ProcessWakeNextDeltaPayload {
public:
  ProcessWakeKind wakeKind() const;
  llvm::StringRef wakeType() const;
};
class ProcessScalarWrapPayload {
public:
  ProcessWrapperDirection direction() const;
  llvm::StringRef scalar() const;
  llvm::StringRef valueType() const;
};
class ProcessScalarUnwrapPayload {
public:
  ProcessWrapperDirection direction() const;
  llvm::StringRef scalar() const;
  llvm::StringRef valueType() const;
};

class ProcessGeneratedCalleePayload {
public:
  ProcessHelperRole role() const;
  const ProcessRecordCreatePayload &recordCreate() const;
  const ProcessRecordGetPayload &recordGet() const;
  const ProcessRecordWithPayload &recordWith() const;
  const ProcessPacketSerializePayload &packetSerialize() const;
  const ProcessPacketDeserializePayload &packetDeserialize() const;
  const ProcessTraceDecodePayload &traceDecode() const;
  const ProcessQueueTrySendPayload &queueTrySend() const;
  const ProcessQueueTryRecvPayload &queueTryRecv() const;
  const ProcessEventSchedulePayload &eventSchedule() const;
  const ProcessTraceOpenPayload &traceOpen() const;
  const ProcessTraceNextPayload &traceNext() const;
  const ProcessTraceEofPayload &traceEof() const;
  const ProcessTracePositionPayload &tracePosition() const;
  const ProcessContractRequirePayload &contractRequire() const;
  const ProcessContractEnsurePayload &contractEnsure() const;
  const ProcessContractAssertPayload &contractAssert() const;
  const ProcessProbePayload &probe() const;
  const ProcessStatAddPayload &statAdd() const;
  const ProcessWakeConditionPayload &wakeCondition() const;
  const ProcessWakeResourcePayload &wakeResource() const;
  const ProcessWakeEventQueuePayload &wakeEventQueue() const;
  const ProcessWakeNextDeltaPayload &wakeNextDelta() const;
  const ProcessScalarWrapPayload &scalarWrap() const;
  const ProcessScalarUnwrapPayload &scalarUnwrap() const;
};
```

Payload JSON keys are exact:

- record field descriptor: `{name,type_key}`;
- record create: `{fields,record_type}`;
- record get: `{field,record,result}`;
- record with: `{field,record,value}`;
- packet serialize/deserialize: `{bytes,packet,packet_type}`;
- trace decode: `{entry,result,source}`;
- queue try-send/try-recv: `{element,queue}`;
- event schedule: `{delay,target,value}`;
- trace open/eof/position: `{source}`;
- trace next: `{entry,source}`;
- require/ensure/assert: `{message}`;
- probe: `{kind,result,target}`;
- stat add: `{stat,value_type}`;
- wake helpers: `{wake_kind,wake_type}`;
- scalar helpers: `{direction,scalar,value_type}`.

Wake payloads use their matching wake kind and literal wake type. Scalar wrap
serializes direction `wrap`; scalar unwrap serializes `unwrap`. Input type keys,
result type keys, and effect remain separate role semantics and participate in
the specialization digest. Wake helpers are stateful, have no inputs, and
return their matching wake type. Inactive payload arms are absent, never null.

### Payload value domains

All strings are owned UTF-8 bytes. A payload field named `*_type`, `record`,
`result`, `value`, `entry`, `element`, `delay`, or `scalar` is an exact type key
unless the table identifies it as a symbol or string. No role normalizes or
reinterprets a field.

| Role and field | Exact value domain |
| --- | --- |
| `record_create.fields` | Declaration-order field descriptors. Each is `{name,type_key}`; `name` is the exact field attribute and `type_key` is exact. Duplicate names fail. |
| `record_create.record_type` | Created record result type key. |
| `record_get.field` | Exact field-name attribute. |
| `record_get.record` | Record operand type key. |
| `record_get.result` | Selected result type key. |
| `record_with.field` | Exact field-name attribute. |
| `record_with.record` | Record operand/result type key. |
| `record_with.value` | Replacement operand type key. |
| packet `bytes` | Exact non-negative resolved serialized byte width as `uint64_t`; rounding or overflow fails. |
| packet `packet` | Canonical resolved packet `FlatSymbolRefAttr` spelling, including `@`. |
| packet `packet_type` | Exact packet operand/result type key. |
| `trace_decode.entry` | Entry operand type key. |
| `trace_decode.result` | Decoded result type key. |
| trace `source` | Exact source `StringAttr`. Trace-decode source comes from unique planned-entry provenance; missing, conflicting, or ambiguous provenance fails. |
| queue `element` | Send operand or receive result type key. |
| queue `queue` | Canonical resolved queue `FlatSymbolRefAttr`, including `@`. |
| `event_schedule.delay` | Delay operand type key. |
| `event_schedule.target` | Canonical resolved event target `FlatSymbolRefAttr`, including `@`. |
| `event_schedule.value` | Scheduled value operand type key. |
| `trace_next.entry` | Entry result type key. |
| contract `message` | Exact message `StringAttr`, including an empty legal value. |
| `probe.kind` | Exact probe-kind `StringAttr`. |
| `probe.result` | Probe result type key. |
| `probe.target` | Canonical resolved target `FlatSymbolRefAttr`, including `@`. |
| `stat_add.stat` | Canonical resolved stat `FlatSymbolRefAttr`, including `@`. |
| `stat_add.value_type` | Stat-add value operand type key. |
| wake `wake_kind` | Role-fixed `condition`, `resource`, `event_queue`, or `next_delta`. |
| wake `wake_type` | Matching unprefixed literal `@acir_wake_condition`, `@acir_wake_resource`, `@acir_wake_event_queue`, or `@acir_wake_next_delta`. |
| scalar `direction` | `wrap` for scalar-wrap; `unwrap` for scalar-unwrap. |
| scalar `scalar` | Exact `mlir:` key of the builtin scalar. |
| scalar `value_type` | Exact `storage:value:` key of the generated realization. |

`inputTypeKeys()` and `resultTypeKeys()` preserve exact planned operand/result
order. Payload type keys equal the role-required entries. `sourceOperations()`
contains originating leaf/suspension operations in `sourcePaths()` order.
`declarations()` contains resolved declarations for record, packet, queue,
event, probe, and stat ownership roles and is empty for roles without one.
Arrays sort by paired stable source path, stay length-consistent, and never use
pointer order.

### Role, effect, and emission matrix

| Role | Effect | Emission | Input and result rule |
| --- | --- | --- | --- |
| `record_create` | `pure` | `inline` | Field-value inputs in payload order; one `record_type` result. |
| `record_get` | `pure` | `inline` | One `record` input; one `result`. |
| `record_with` | `pure` | `inline` | Inputs `record,value`; one result equal to `record`. |
| `packet_serialize` | `pure` | `inline` | One `packet_type` input; one exact original result type key. |
| `packet_deserialize` | `pure` | `inline` | One exact original bytes input type key; one `packet_type` result. |
| `trace_decode` | `pure` | `inline` | One `entry` input; one `result`. |
| `queue_try_send` | `stateful` | `invoke` | One `element` input; exact original accepted-result key. |
| `queue_try_recv` | `stateful` | `invoke` | No SSA input; exact original element and received-flag result keys. |
| `event_schedule` | `stateful` | `invoke` | Inputs `value,delay` in original order; no result. |
| `trace_open` | `stateful` | `invoke` | No SSA input; exact original cursor result key. |
| `trace_next` | `stateful` | `invoke` | Original cursor input; cursor, `entry`, advanced-flag results in original order. |
| `trace_eof` | `stateful` | `invoke` | Original cursor input; exact EOF result. |
| `trace_position` | `stateful` | `invoke` | Original cursor input; exact position result. |
| `contract_require` | `stateful` | `invoke` | Exact condition input; no result. |
| `contract_ensure` | `stateful` | `invoke` | Exact condition input; no result. |
| `contract_assert` | `stateful` | `invoke` | Exact condition input; no result. |
| `probe` | `stateful` | `invoke` | No SSA input; one `result`. |
| `stat_add` | `stateful` | `invoke` | One `value_type` input; no result. |
| `wake_condition` | `stateful` | suspend-edge wake invoke | No helper input; one `@acir_wake_condition` result. |
| `wake_resource` | `stateful` | suspend-edge wake invoke | No helper input; one `@acir_wake_resource` result. |
| `wake_event_queue` | `stateful` | suspend-edge wake invoke | No helper input; one `@acir_wake_event_queue` result. |
| `wake_next_delta` | `stateful` | suspend-edge wake invoke | No helper input; one `@acir_wake_next_delta` result. |
| `scalar_wrap` | `pure` | `wrap` | One `scalar` input; one `value_type` result. |
| `scalar_unwrap` | `pure` | `unwrap` | One `value_type` input; one `scalar` result. |

Condition subscriptions stay in the wake record, not helper operands. Wake
helpers are never ordinary actions; their invoke cost belongs to the suspend
edge. No role changes effect or emission. Mixed-effect deduplication fails.

## Generated value-type specialization

Every generated value type uses this canonical specialization record:

```json
{
  "acir_type":"<canonical ACIR type spelling>",
  "contract_epoch":"0.1",
  "kind":"value|packet",
  "payload":{},
  "schema":"acir-generated-value-type-0.1"
}
```

The complete public value/packet payload surface is:

```cpp
enum class ProcessValueTypeMemberKind { Field, Element };
enum class ProcessStorageSignedness { Signless, Signed, Unsigned };

class ProcessValueTypeMemberPlan {
public:
  ProcessValueTypeMemberKind kind() const;
  llvm::StringRef name() const;
  std::optional<uint32_t> index() const;
  uint64_t offsetBits() const;
  uint64_t widthBits() const;
  std::optional<ProcessStorageSignedness> signedness() const;
  llvm::StringRef encoding() const;
  llvm::StringRef typeKey() const;
};

class ProcessStorageValuePayload {
public:
  llvm::ArrayRef<ProcessValueTypeMemberPlan> members() const;
  uint64_t widthBits() const;
  llvm::StringRef encoding() const;
};

class ProcessStoragePacketPayload {
public:
  llvm::ArrayRef<ProcessValueTypeMemberPlan> members() const;
  uint64_t widthBits() const;
  uint64_t bytes() const;
  llvm::StringRef encoding() const;
};

class ProcessValueTypePayload {
public:
  ProcessValueTypeKind kind() const;
  const ProcessStorageValuePayload &value() const;
  const ProcessStoragePacketPayload &packet() const;
};
```

Members retain declared semantic order. A field member requires a non-empty
name and no index. An element requires an empty name and a zero-based index.
Offsets and widths are exact non-negative bit counts and every member range is
contained by the enclosing width. Signedness is present only for integer
storage and serializes `signless`, `signed`, or `unsigned`. Encoding is the
non-empty canonical generic MLIR spelling of the physical representation.
`typeKey()` is the exact nested type key and is never reconstructed from the
encoding. Packet width satisfies `width_bits == bytes * 8` without overflow.

Exact payload JSON shapes are:

- field member: `{encoding,kind,name,offset_bits,type_key,width_bits}` plus
  `signedness` for integer storage;
- element member: `{encoding,index,kind,offset_bits,type_key,width_bits}` plus
  `signedness` for integer storage;
- value payload: `{encoding,members,width_bits}`;
- packet payload: `{bytes,encoding,members,width_bits}`.

Member `kind` is `field` or `element`. Optional fields are absent, never null.
`ProcessValueTypePlan::kind()` equals `payload().kind()`. Task 13 does not infer
a layout fact from the original IR.

Every field participates in the digest. Identity spellings are:

```text
fingerprint   = "sha256:" + lowercase_hex(digest)
value symbol  = "@acir_value_" + lowercase_hex(digest)
packet symbol = "@acir_packet_" + lowercase_hex(digest)
value cpp     = "acir::generated::value_" + lowercase_hex(digest)
packet cpp    = "acir::generated::packet_" + lowercase_hex(digest)
value key     = "storage:value:" + lowercase_hex(digest)
packet key    = "storage:packet:" + lowercase_hex(digest)
```

## PC, suspension, state, and cost invariants

The entry PC is ID `0`, name `entry`. Every reachable non-terminal suspension
occurrence except `ac.yield_sim` receives one resume PC in canonical occurrence
order. Resume names are `pc00000001`, `pc00000002`, and so on. A constant-loop
suspension receives a distinct occurrence, PC, wake, transition, and remaining
continuation for every lexicographically ordered full iteration vector.
`ac.yield_sim` resumes at entry and creates no empty resume PC.

PC storage width is the smallest positive integer width that represents every
PC ID. A one-PC process uses width `1`.

Suspension mapping is exact:

| ACIR operation | Wake kind | Wake type key | Target |
| --- | --- | --- | --- |
| `ac.wait_until` | `condition` | `@acir_wake_condition` | exact condition value |
| `ac.wait_for` | `resource` | `@acir_wake_resource` | exact resolved resource |
| `ac.await_event` | `event_queue` | `@acir_wake_event_queue` | exact resolved event queue |
| `ac.yield_sim` | `next_delta` | `@acir_wake_next_delta` | entry PC |

Every wake references one generated stateful wake callee. A suspension is not
also an ordinary action. `try_send`, `try_recv`, and `schedule` are
non-blocking; they create no PC or wake.

SCF forwarding equivalence unions exactly:

- each `scf.if` yielded operand with its corresponding result;
- each `scf.for` init operand, region iter-argument, yielded operand, and
  result at the same coordinate;
- each `scf.while` init operand, before-region argument, `scf.condition`
  forwarded operand, after-region argument, yielded operand, and result at the
  same coordinate.

Every class member has the same `mlir::Type`. Equivalence uses only
occurrence-qualified `ProcessPlannedValue` identities. Raw callee-body values
are provenance, never identity or deduplication keys. A slot exists exactly
when a non-capture equivalence class is defined before a suspension and used by
the continuation after resumption. Captures and dead values are excluded.
Classes are globally deduplicated per process; members sort by canonical
planned-value serialization and deduplicate. The smallest canonical member
names the class. Classes sort bytewise by their complete ordered member-list
serialization, then receive dense IDs and names `live00000000`,
`live00000001`, and so on.

Builtin scalar state records wrap plus store before suspension and load plus
unwrap after resumption. Aggregate and packet state records only store/load and
has no wrapper callee IDs.

Exact block cost is:

```text
block_cost =
    sum(entry ProcessTransitionLoadPlan emissions, each 1)
  + sum(explicit scalar_unwrap actions, each 1)
  + sum(copy_scalar, inline, invoke, for_condition, and for_increment
        leaf actions, each 1)
  + if edge is suspend:
      sum(explicit scalar_wrap actions, each 1)
    + sum(ProcessTransitionStorePlan emissions, each 1)
    + 1 generated wake-producing invoke
    + 1 acsim.suspend
    otherwise:
      1 for the emitted cf.cond_br, cf.br, or process termination
```

`for_initialize` is forward-only and costs zero. `func.call`, `func.return`,
SCF containers/yields/frames, forwarding bindings, every other forward-only
action, `acsim.type`, and `acsim.binding` cost zero. Expanded pure-call leaf
actions count individually. Suspension exists only through its wake,
transition, and suspend edge; it is not an ordinary action.

Every `ProcessBlockPlan::cost()` stores this exact count. Each PC-local graph is
acyclic. `fairnessWork()` is the iterative maximum sum of block costs over
every path in every PC-local DAG. Constant-loop copies count once per
occurrence. A suspension-guaranteed loop charges one local segment because its
backedge crosses `acsim.suspend`. Fairness zero, arithmetic overflow, a graph
cycle, or a result above `maxFairnessWork` fails. Task 13 asserts its emitted
count equals the stored block cost and writes `fairnessWork()` directly.

Task 13 emits no `acsim.continue` for ACIR v0.1. Non-suspending control uses
`cf.br` or `cf.cond_br` inside the current PC; PC changes occur only through a
planned suspension.

## Exact yield-only baseline

The minimal frozen yield-only model contains:

- exactly one process;
- exactly one generated callee at ID `0`;
- no generated value types;
- no live slots;
- one `next_delta` wake and one transition, both using callee `0` where the
  record applies;
- the entry PC/block and resume structure required by the closed schema;
- deterministic empty arrays for every empty collection.

The generated callee specialization preimage is exactly this single-line
UTF-8 byte sequence with no trailing newline:

```json
{"contract_epoch":"0.1","effect":"stateful","inputs":[],"kind":"implementation","payload":{"wake_kind":"next_delta","wake_type":"@acir_wake_next_delta"},"results":["@acir_wake_next_delta"],"role":"wake_next_delta","schema":"acir-generated-implementation-0.1","source_paths":[]}
```

Its SHA-256 digest is:

```text
63cacba5c3eb82976464804b4aeaa17d43b445733efaddfad7c7bec1ab650269
```

The exact generated descriptor is:

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

No model, test, plan, or report may describe the yield-only callee table as
empty. A separate frozen model containing no process still serializes empty
`processes`, `callees`, and `value_types` arrays.

## Canonical report contract

The top-level JSON object is closed and contains exactly:

```json
{
  "callees":[],
  "contract_epoch":"0.1",
  "processes":[],
  "schema":"acir-process-state-plan-0.1",
  "value_types":[]
}
```

An empty frozen model serializes to these exact bytes with no trailing newline:

```text
{"callees":[],"contract_epoch":"0.1","processes":[],"schema":"acir-process-state-plan-0.1","value_types":[]}
```

The Draft 2020-12 schema closes every object with
`additionalProperties: false`, uses the enum spellings in this record, and
uses closed discriminated unions for occurrences, planned values, subscription
sources, actions, frames, and edges. Integer fields are non-negative JSON-safe
integers. Arrays remain in semantic canonical order; object keys sort
lexicographically.

The report contains stable values only. Raw pointers and MLIR handles are never
serialized. The C++ verifier additionally enforces dense IDs, reference
closure, ordering, uniqueness, definition keys, frame phase legality, type
keys, descriptor specialization, effects, costs, capability bounds, and all
relationships that JSON Schema cannot express.

Serialization is iterative and enforces `maxCanonicalReportBytes` while
producing bytes. Complete plan verification precedes serialization;
serialization precedes filesystem replacement. Publication writes a
same-directory temporary file, flushes and closes it, then atomically renames
it. Any failure preserves an existing destination byte-for-byte and removes
temporary artifacts.

Adding the plan schema changes repository contract validation from nine to
exactly ten schemas.

## Private negative-test surface

JSON fixture validation, semantic corruption, and publication failure
injection stay private in:

```text
lib/Analysis/ProcessStatePlanTestHooks.h
```

Publication is monotonic:

- Task 1 creates the header and adds `ProcessStatePlanCorruptionForTest` plus
  `cloneProcessStatePlanWithCorruptionForTest`; its verifier unit tests cover
  every frozen enum arm before implementation is treated as green.
- Task 5 adds `ProcessStateReportFailurePointForTest` and
  `ScopedProcessStateReportFailureForTest` for filesystem publication only.
- Task 6 adds `validateProcessStateReportBytesForTest` for invalid JSON bytes.

No task republishes or renames an earlier symbol. The complete private API is:

```cpp
enum class ProcessStatePlanCorruptionForTest {
  DuplicateOrdinal,
  NonDenseOrdinal,
  DanglingReference,
  DuplicateIdentity,
  UnsortedCanonicalOrder,
  CostMismatch,
  DefinitionKeyMismatch,
  CalleeSpecializationMismatch,
  ValueTypeSpecializationMismatch,
  EffectMismatch,
  IdKindMismatch,
  WrongTypeKey,
  InvalidFramePhase,
  InvalidEdgeBinding,
  InvalidWakeCallee
};

ProcessStatePlanSet cloneProcessStatePlanWithCorruptionForTest(
    const ProcessStatePlanSet &plan,
    ProcessStatePlanCorruptionForTest corruption);

mlir::LogicalResult validateProcessStateReportBytesForTest(
    llvm::StringRef bytes);

enum class ProcessStateReportFailurePointForTest {
  OpenTemporary,
  WriteTemporary,
  FlushTemporary,
  CloseTemporary,
  RenameTemporary
};

class ScopedProcessStateReportFailureForTest {
public:
  explicit ScopedProcessStateReportFailureForTest(
      ProcessStateReportFailurePointForTest point);
  ~ScopedProcessStateReportFailureForTest();

  ScopedProcessStateReportFailureForTest(
      const ScopedProcessStateReportFailureForTest &) = delete;
  ScopedProcessStateReportFailureForTest &operator=(
      const ScopedProcessStateReportFailureForTest &) = delete;
  ScopedProcessStateReportFailureForTest(
      ScopedProcessStateReportFailureForTest &&) = delete;
  ScopedProcessStateReportFailureForTest &operator=(
      ScopedProcessStateReportFailureForTest &&) = delete;
};
```

Nothing in this surface appears in a public header. The scoped injection is
thread-local, installs exactly one filesystem failure point, and restores the
prior state in its destructor. Planning failure uses real malformed or
unsupported frozen IR or an exact capability fixture. Serialization failure
uses semantic corruption or `maxCanonicalReportBytes`. Planning and
serialization are not failure-point enum arms.

Invalid JSON fixtures use `validateProcessStateReportBytesForTest`; they do not
justify a public parser.

## Task 13 consumer gate

The separate `ACIRProcessStateTask13ConsumerTests` executable proves that Task
13 can lower using Task 12's source-tree public header alone. The target:

- receives only `${PROJECT_SOURCE_DIR}/include` and
  `${PROJECT_BINARY_DIR}/include` through linked public target build
  interfaces;
- receives no direct `${PROJECT_SOURCE_DIR}/lib` path, and no transitive target
  exposes it publicly;
- links source-tree public library targets only;
- does not include `ProcessStatePlanTestHooks.h`;
- does not parse report JSON or inspect planner implementation;
- does not inspect original SCF or Func bodies to recover missing semantics;
- emits a literal deterministic mock lowering transcript from public records,
  dense IDs, specialization records, bindings, and costs.

Task 12 adds no install rule, export set, package configuration, or installed-
tree consumer. Task 14 owns install/export closure and its separate installed-
tree consumer.

## Neutral dialect lowerability ownership

The private neutral helpers live in:

```text
lib/Dialect/ACIR/ProcessLowerability.h
lib/Dialect/ACIR/ProcessLowerability.cpp
```

Their private API is:

```cpp
struct RawModelStructureLimits {
  uint64_t maxNodes = 1U << 20;
  uint64_t maxEdges = 1U << 22;
  uint64_t maxNestedRegionDepth = 512;
};

mlir::LogicalResult preflightRawModelStructure(
    mlir::ModuleOp module,
    const RawModelStructureLimits &limits);

mlir::LogicalResult walkStructuredOperationsIterative(
    mlir::Operation *root,
    llvm::function_ref<mlir::LogicalResult(mlir::Operation *)> visitor,
    const RawModelStructureLimits &limits);

struct StaticForTripCount;

mlir::FailureOr<StaticForTripCount> analyzeStaticFor(mlir::scf::ForOp op);

mlir::LogicalResult verifyProcessLowerability(
    mlir::Operation *processLikeOp,
    const RawModelStructureLimits &limits);
```

The dialect layer owns iterative region/block/operation traversal, raw nested
depth accounting, structured-control shape, constant `scf.for` arithmetic, and
operation-local lowerability. `ModelAnalysis` owns module symbol resolution,
definition/reference indexing, call-graph relations, whole-model uniqueness,
and cross-definition semantics. The planner consumes both authorities without
duplicating either.

`ACIRAnalysis` may include the private dialect helper through a private source
include path. `ACIRDialect` never links Analysis; there is no dependency cycle.

Task 2 moves both file-entry passes out of `InitAllPasses.h` so normalization
cannot recurse before raw-structure preflight:

- create `lib/Transforms/NormalizeACIRFile.cpp` and
  `lib/Transforms/VerifyACIRFile.cpp`;
- declare exactly these public factories in
  `include/acir/Transforms/Passes.h`:

```cpp
std::unique_ptr<mlir::Pass> createNormalizeACIRFilePass();
std::unique_ptr<mlir::Pass> createVerifyACIRFilePass();
```

- remove both concrete inline pass classes from
  `include/acir/InitAllPasses.h`; that header retains registration only, and
  its two registration lambdas return the corresponding factories;
- replace both direct `make_unique<NormalizeACIRFilePass>()` and
  `make_unique<VerifyACIRFilePass>()` constructions in the default
  `acir-opt` pipeline with the factories, in normalize-then-verify order;
- define each non-exported concrete pass in its matching `.cpp`, include the
  public factory declaration and private lowerability helper only from that
  source, and add both sources to `ACIRTransforms`.

`NormalizeACIRFilePass` calls `preflightRawModelStructure` before
`normalizeAddressMaps` or any other recursive normalization walk.
`VerifyACIRFilePass` calls the same preflight before epoch/canonical-file
verification, `module.walk`, attribute walks, or any other recursive verifier
walk. Failure signals pass failure immediately; neither pass starts its
recursive work after preflight fails. No public header exposes or includes
`ProcessLowerability.h`.

`ACIRTransforms` uses `${PROJECT_SOURCE_DIR}/lib` privately. No public header
includes a private dialect header, and no private source include path becomes
public.

`NormalizeACIRFilePass`, `VerifyACIRFilePass`, `verifyModel`,
canonicalization/freeze entry points, and `planProcessState` run
`preflightRawModelStructure` before MLIR normalize/verifier paths that may
recursively traverse malformed or deep IR. Existing node/edge/depth
diagnostics and exact-boundary behavior remain unchanged. Depth `512` is
admitted; depth `513` and a `10,000`-deep syntactically parseable but
verifier-malformed raw model fail with one compact deterministic depth
diagnostic before recursive normalization or verification.

Task 2 independently proves the public Normalize factory boundary in
`unittests/Analysis/ProcessStatePlanVerifierTest.cpp`, built into target
`ACIRProcessStatePlanTests`, under suite
`ProcessStatePlanNormalizeFactoryTest`. The suite contains exactly these
depth cases:

- `Depth512ReachesAndSucceedsThroughIsolatedNormalizePass`;
- `Depth513FailsRawStructuralPreflightBeforeNormalizeRecursion`;
- `VeryDeepMalformedFailsRawStructuralPreflightWithoutRecursion`.

Each case constructs a fresh `mlir::PassManager`, calls
`enableVerifier(false)`, and adds only `createNormalizeACIRFilePass()`. It does
not use `acir-opt` pipeline setup, add a default pass, or add
`createVerifyACIRFilePass()`. The raw fixtures have nested depths `512`, `513`,
and `10,000`; the last is syntactically parseable but verifier-malformed. The
suite installs test-local `mlir::PassInstrumentation` and requires the exact
pass trace. The depth-`512` case records only Normalize entry and completion,
succeeds, and asserts an observable post-normalization fact. The depth-`513`
and `10,000`-deep cases record only Normalize entry and failure, then fail
through the raw structural preflight nested-region-depth capability with the
exact first and only diagnostic
`whole-model region nesting exceeds ACIR v0.1 capability limit 512`. They do
not enter recursive normalization, crash, overflow the stack, or emit an
epoch, canonical-file, or other downstream verifier diagnostic. This suite is
the factory-only ordering proof; private-helper unit coverage is not a
substitute.

Task 2 adds `ProcessStatePlanVerifierTest.cpp` to
`ACIRProcessStatePlanTests` in `unittests/Analysis/CMakeLists.txt` and gives
that target private `ACIRTransforms` and `MLIRPass` links if they are not
already present. No separate or ambiguous factory-test target is created.

Task 2 adds lit coverage through the real `acir-opt-internal` executable. The
lit substitution `%acir_opt` resolves to that binary. For each of the depth
`512`, depth `513`, and `10,000`-deep verifier-malformed fixtures, the test invokes
both `%acir_opt --verify-each=false <input>` to exercise the automatic default
normalize/verify pipeline and
`%acir_opt --verify-each=false --normalize-ac-file <input>` to exercise
registered option wiring after the automatic default passes. Disabling
pass-manager verification is part of the hostile-input fixture: it prevents a
recursive pass-manager verifier from preempting pass-owned preflight, but it
does not remove the driver's default Normalize and Verify passes. Depth `512`
succeeds in both modes. The depth-`513` and `10,000`-deep fixtures fail in both
modes with the same bounded raw-depth diagnostic and do not crash, overflow the
stack, or emit a downstream normalize/verifier diagnostic first. Because
`acir-opt` always inserts its default Normalize and Verify passes before the
command-line pipeline, the explicit `--normalize-ac-file` invocation is only
default-pipeline/registration integration coverage; it is not standalone or
factory-isolation evidence.

The exact focused factory command is:

```bash
cmake --build build/dev-llvm22 --target ACIRProcessStatePlanTests -j4
build/dev-llvm22/bin/ACIRProcessStatePlanTests \
  --gtest_filter='ProcessStatePlanNormalizeFactoryTest.*'
```

RED must be observed before the public factory/preflight implementation is
available. GREEN requires all three literal outcomes above. Mutation closure
must bypass the Normalize preflight and, separately, insert the Verify factory
or the driver's default pipeline before the isolated Normalize factory; the
exact instrumentation trace and literal outcomes must catch both mutations
independently of private-helper and CLI coverage.

Task 2 owns extraction from `ModelAnalysis.cpp` and its internal helper,
creation of the neutral dialect helper, affected `ACIROps` verification, and
the normative `acir-core-v0.1.md` hard-break wording. A non-suspending
`scf.for` is legal only when lower, upper, and positive step give an exact
finite static trip count. Otherwise every reachable backedge must suspend.
There is no compatibility path.

## Staged implementation invariants

The seven reviewed commits remain incremental history and are not squashed.
The stages preserve these boundaries:

- immutable schema, record accessors, verifier/serializer, exact yield-only
  callee `0`, semantic-corruption hooks/verifier tests, and declaration-
  permutation determinism;
- neutral raw lowerability, model-analysis ownership repair, process verifier,
  static/dynamic loop contract, pure-call/constant-loop expansion, stable
  occurrence identities, and normative ACIR wording;
- explicit PC-local CFG, continuations, captures, frames, branch forwarding,
  wakes, subscriptions, and transitions, proven by unit/GTest TDD only;
- occurrence-qualified liveness, slots, exact generated callee/value-type
  registries, scalar wrappers, action emission classes, cost/fairness, and
  distinct two-call-site live values, proven by unit/GTest TDD only;
- all ten limits, complete planner façade, non-mutation, bounded iterative
  serialization, atomic report writer, and scoped filesystem failure hooks,
  proven by unit/GTest TDD only;
- private byte validation plus pass/CLI, all deferred process-state lit files,
  closed schema, report integration, and tracked specification synchronization;
- source-tree public-header-only Task 13 consumer, hostile-input and mutation closure,
  Debug/Release verification, static analysis, formatting, and repository
  hygiene.

No stage publishes a release-compatible subset. The final stage uses commit
subject `feat(lowering): plan ACSim process state` without rewriting earlier
reviewed commits.
