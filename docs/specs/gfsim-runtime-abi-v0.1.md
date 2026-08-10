# gfsim Model Library Contract v0.1

| Field | Value |
| --- | --- |
| Specification | Graph Flow Simulator model-library and runtime contract |
| Version | 0.1 |
| Status | Draft for review |
| Global contract epoch | `0.1` |
| Contract identifier | `gfsim-cxx20@0.1` |
| Language boundary | Same-toolchain C++20 source and templates |

## Purpose and authority

This specification defines the C++20 model-library contract targeted by ACIR
lowering and the deterministic runtime semantics of generated simulators. The
generated simulator preserves a structured hierarchy of modules, queues,
resources, processes, protocols, probes, and statistics.

Agentic Circuit defines the canonical behavior in this document. The runtime
may reuse implementation concepts and terminology from Linx `tools/model` and
DavinciOO gfsim, including `SimObject`, `Module`, `SimQueue`, `SimSystem`,
`Work`, and `Xfer`. Such reuse does not imply source, binary, trace, timing, or
cycle compatibility with either system.

## C++20 integration boundary

v0.1 is a same-toolchain source/template contract, not a stable binary ABI.
Generated code, the runtime, packet definitions, and component libraries MUST
be compiled together with a compatible C++20 toolchain and build definition.
The contract does not define:

- a dynamic plugin loader;
- runtime discovery or replacement of model implementations;
- a C ABI or cross-compiler object compatibility;
- ABI version negotiation or compatible version ranges; or
- runtime model configuration.

All structural choices, parameters, policies, capacities, bindings, validation
profiles, and instrumentation included in a model are fixed by generated C++ or
compile-time declarations. An implementation MUST reject an exact contract,
schema, or generated-code identity mismatch during compilation or static
preflight. It MUST NOT select the nearest supported version.

## Object model

### `SimObject`

Every runtime object has:

- one owning parent, except the root system;
- a compile-time-assigned stable object ID;
- a stable local name and canonical hierarchy path;
- a statically known runtime kind;
- statically selected lifecycle, Work, Xfer, validation, probe, and statistic
  operations.

Object IDs and hierarchy paths MUST be unique within a generated model.
Pointers and host allocation addresses are never observable identities.

### `Module`

A module owns child modules and local runtime objects. Generated C++ creates one
class or template specialization per specialized ACIR module definition.
Ownership remains hierarchical. Scheduler tables MAY contain non-owning object
IDs or pointers, but MUST NOT replace the ownership tree as the primary model.

### `SimSystem`

The system owns the root module, exact global epoch, event scheduling, phase
barriers, termination state, and deterministic sequencing. Component-owned
resources perform their own arbitration; the system does not impose global
resource arbitration.

## Exact time and epoch

Simulation time is a non-negative, unbounded-in-semantics integer tick. A host
implementation MAY use a bounded integer representation only if overflow is
detected before it changes behavior.

The global epoch is the exact pair `(time, delta)`, where `time` is an integer
tick and `delta` is a non-negative causal-delta index at that tick. Equality,
ordering, scheduling, tracing, and wake decisions use this exact pair. Floating
point time, epsilon comparisons, host clocks, and approximate epoch equality
are forbidden.

The engine is a conservative discrete-event engine with cycle-equivalent
integer-tick semantics. It jumps directly to the earliest pending time when no
work remains at the current time. Components are not polled during skipped
ticks. Cycle-equivalent means that state and observations at integer ticks
match the specified synchronous model; it does not claim compatibility with an
external simulator's cycle definition.

Within one time tick, causal deltas are processed in increasing order. An
effect may schedule a later delta at the same time only when the dependency is
zero-delay, pure, stateless, and effect-free. Independent same-time effects MAY
be reordered only when independence is proven from static ownership and access
information. A traversal-order assumption is not proof of independence.
The runtime declares a finite maximum causal-delta count per tick and MUST
reject a work item or event whose delta is at or beyond that bound before it is
inserted. Reaching the bound is a failed contract result with diagnostic code
`max_deltas_exceeded`.

## Event-driven activation

An object participates in `Work` only when at least one of these conditions is
true:

- one of its declared input epochs changed;
- an event addressed to it is due at the current epoch;
- one of its process subscriptions woke;
- it has a pending commit; or
- it requested an explicit internal wake for the current or a future epoch.

Each input and observable committed value carries its exact last-change epoch.
The scheduler deduplicates multiple wake causes for the same object and epoch.
Idle objects MUST NOT be polled, and delayed operations MUST schedule their
`ready_time`; they MUST NOT decrement countdown state on every tick.

## Work, arbitration, and Xfer

### Work snapshot and proposals

All `Work` executions at the same `(time, delta)` read one immutable committed
snapshot. Each execution writes only to a private proposal buffer associated
with its stable object ID. It MUST NOT expose a proposal, provisional
reservation, or local mutation to another Work execution at that epoch.

Order-independent Work executions MAY run concurrently. The v0.1 reference
implementation is single-threaded, but the API and component contract MUST be
parallel-safe. A future parallel implementation given the same generated model
and trace MUST produce bit-identical committed state, trace output, statistics,
diagnostics, and termination classification.

### Deterministic local arbitration

After all scheduled Work executions finish, a barrier closes proposal
collection. Each owning component arbitrates its proposals locally. Explicit
policy values decide first. Remaining ties use declared stable keys, in order:
port index, instance index, hierarchy path or object ID, then canonical
transaction identity. Monotonic scheduler insertion order MAY be used only for
events already indistinguishable under the model contract.

Arbitration MUST NOT depend on pointer values, hash iteration order, host thread
scheduling, or Work traversal order.

### Xfer barrier and causal continuation

After arbitration, the Xfer barrier atomically commits all accepted proposals
for the epoch, including queue changes, reservations, protocol transfers,
process continuations, event insertion, statistics, and trace-source transfer.
A transfer occurs exactly once at this barrier.

Pure zero-delay evaluation may use increasing causal deltas to respect its
static dependency order. An Xfer state change is stamped with its commit epoch
but is not eligible as an input or activation until at least the next integer
tick. No causal delta exposes a stateful same-tick result.

### Stateful timing rule

A stateful object commits at most once for a given integer tick, regardless of
the number of causal deltas at that tick. Its committed output is eligible no
earlier than the next integer tick. A zero-delay declaration is valid only for
a pure, stateless, effect-free computation. Validation MUST reject zero delay
for queues, storage, resources, protocols with mutable state, processes,
statistics mutations, trace cursors, or any callback with observable effects.

## Static dispatch

Generated code constructs a static dispatch table indexed by stable object ID.
Entries contain typed or type-erased thunks with statically verified signatures
for the operations an object supports. The hot path does not require virtual
dispatch, dynamic type discovery, string lookup, or plugin indirection.

For contract epoch `0.1`, the generated/runtime C++20 boundary is the following
logical layout (the declarations in `gfsim/dispatch.h` are canonical):

```cpp
enum class XferPhase : uint8_t { Arbitrate, Commit };

struct DispatchRow {
  ObjectId id;
  ObjectKind kind;
  void *object;
  void (*work)(void *, Epoch);
  void (*xfer)(void *, Epoch, XferPhase);
  void (*reset)(void *);
  bool (*validate)(const void *, ObjectId, ObjectKind);
};
```

Rows MUST be emitted in ascending dense object-ID order, with `row.id` equal to
the row index. The typed row factory MUST recover exactly the statically known
object specialization from `object`. Installation MUST reject a null pointer,
missing thunk, non-dense ID, kind mismatch, ID mismatch, or failed object
validation. For one immutable Work snapshot, the scheduler invokes all `work`
thunks in ascending row order, then all `xfer(..., Arbitrate)` thunks in that
order, then all `xfer(..., Commit)` thunks in that order. Reset uses the same
ascending row order.

Virtual functions MAY be used outside the hot path as an implementation detail,
but they are not part of the contract and cannot alter deterministic ordering.

## Queues and events

`SimQueue<T>` exposes committed state and private epoch proposals. It provides:

- mandatory entry capacity;
- optional byte capacity using static `PacketTraits<T>` behavior;
- ordered read and write proposals;
- deterministic local arbitration;
- occupancy and watermark statistics; and
- protocol-aware push and pop endpoints.

An enqueue succeeds only if all declared capacities permit it. FIFO data queues
and time-ordered event queues are distinct types.

An event contains `ready_time`, target object ID, event kind, payload, and stable
ordering keys. `ready_time` is an exact integer tick; a same-time event also has
a causal delta assigned by the runtime. Scheduling before the committed epoch
or addressing an object absent from the static dispatch table is an error. The
event queue orders by exact epoch, target object ID, event kind, and payload, in
that order.

## Resources

A resource represents finite service capacity. It supports proposal, local
arbitration, reservation, release, cancellation, and statistics. Its state
includes total capacity, active reservations, owner and root transaction
identity, issue time, and exact `ready_time`.

Reservations commit only at Xfer. Completion schedules an event at
`ready_time`; no resource relies on countdown polling. Resource conservation is
a required validation invariant.

## Processes

Each lowered `ac.process` is a generated enum state machine. Its state contains:

- a program-counter enum value;
- typed live values crossing suspension;
- exact event or input subscriptions;
- termination or failure state; and
- any statically bounded continuation storage.

A process runs after an initial wake or a subscribed event, input epoch change,
or transfer. It executes until it terminates, suspends, fails, or reaches the
static fairness cap. It does not poll its wait condition. A process that can
loop without suspension, bounded progress, or a state change is invalid or
produces a capped-execution diagnostic, according to its static build profile.

## Protocols and packets

Each bound channel has a statically selected protocol descriptor and
protocol-local state. The descriptor defines proposal, transition selection,
validation, and commit behavior. Validation can check legal transitions,
immutable pending offers, exactly-once transfer, credit conservation, in-flight
bounds, request-response correlation, cancellation, and timeout rules.

Every public packet type supplies static `PacketTraits<T>` behavior for its
exact ACIR schema identity, serialized size, maximum size, alignment,
endianness, serialization, deserialization, stable field reflection, and any
declared routing or correlation fields. Native layout may differ from serialized
layout. Offered packet values have immutable observable value semantics.

## Component model-library contract

An external component is a C++20 type or class template compiled into the
generated simulator. It declares an exact contract identity and compile-time
interface. The concrete implementation provides, as applicable:

- static parameter, port, queue, resource, and address-map declarations;
- construction from a generated hierarchy context;
- lifecycle initialization and reset;
- Work proposal generation and Xfer commit thunks;
- process and subscription declarations;
- invariant, probe, and statistic declarations; and
- a static build profile.

`FunctionalPolicy` is an optional static template policy for functional
behavior. When omitted, the component's declared default policy applies. It is
never loaded, replaced, or configured at runtime.

The generated build MUST fail compilation or static preflight if an exact
component contract, packet schema, protocol, policy, or generated layout does
not match. There is no compatibility range or runtime fallback.

## Generated hierarchy and introspection

Parent modules own children and local objects using members, `std::array`, or
equivalent structured containers. Instance collections retain indices and
element paths. Generated static introspection tables expose object kind,
definition and instance identity, ports, bindings, queues, resources, address
maps, static parameters, probes, and statistics.

## Trace source boundary

Only the trace subsystem parses PTO JSON. It produces exact-version
`PtoTraceRecord` values and decoded transactions. Compute, storage,
interconnect, and control components MUST NOT parse JSON.

Exactly one `TraceSourceModel` owns the trace cursor. It peeks the next record
and holds the decoded offer unchanged while downstream backpressure persists.
The cursor advances only when that offer commits at Xfer. Decode, retries,
Work reevaluation, and rejected proposals never advance it. Detailed identity
and dependency rules are defined by the PTO Trace Schema v0.1 specification.

## Static preflight and build profiles

The generated model selects exactly one static build profile. v0.1 defines:

- `fast`: every required representation verifier, static preflight, memory and
  time safety, capacity checks, protocol legality, unique identity,
  correlation, conservation, and fatal contract checks;
- `validated`: `fast` plus verification after every compiler pass,
  transaction-lifetime checks, deterministic-arbitration checks, address and
  dependency audits, full event provenance, and post-run quiescence checks; and
- `custom`: an explicit pass pipeline and instrumentation set that MUST retain
  every mandatory `fast` check.

The selected profile and instrumentation set are part of the binary fingerprint
and cannot change at runtime. Conformance testing uses `validated`.

Static preflight validates exact runtime, generated-code, packet, protocol, and
trace identities; hierarchy and object-ID uniqueness; bindings; address maps;
capacities; zero-delay purity; process bounds; dispatch-table completeness; and
all other statically declared invariants. No simulation state advances if
preflight fails.

Required runtime invariants include:

- queue entry and byte capacities never underflow or overflow;
- reservations never exceed resource capacity;
- each committed reservation completes, releases, or explicitly cancels;
- each packet or trace transfer commits at most once;
- protocol credit is conserved;
- epochs and event times are monotonic;
- stateful objects commit at most once per tick;
- correlations are unique and complete;
- address routing selects exactly one target unless multicast is static;
- stable object paths and IDs remain unique; and
- equal-priority arbitration follows the declared stable keys.

## No-progress and execution caps

The model declares finite caps for any fairness bound, maximum simulation time,
maximum committed event count, maximum causal deltas per tick, trace record
count, and validation traversal that it enables. Reaching a cap is not success;
it produces an incomplete result unless the triggering condition is a contract
violation, in which case it produces a failed result.

When unfinished work remains and no object is runnable and no future event can
wake it, the runtime reports no progress. The diagnostic includes blocked
objects and processes, subscriptions, queue occupancy, pending offers, protocol
state, reservations, next event if any, trace position, and available
dependency or correlation chains.

## Statistics, replay, and determinism

Statistics and event logs are deterministic and machine-readable. Logging and
probes MUST NOT affect decisions. A run is reproducible from the generated
binary fingerprint, exact trace hash, and statically selected validation and
instrumentation declarations.

Host thread scheduling, pointer values, allocation layout, unordered-container
iteration, wall-clock time, and skipped idle ticks MUST NOT affect committed
state, output ordering, diagnostics, statistics, or termination classification.

## Termination result

Every run produces exactly one classification:

- `completed`: the unique trace cursor reached end of trace, all accepted root
  transactions reached their specified terminal state, and the architecture is
  quiescent with no pending commits, wakes, events, or live reservations;
- `incomplete`: execution stopped at a declared time, event, delta, trace, or
  validation cap, or by user interruption, without a contract violation; or
- `failed`: preflight, decoding, assertion, protocol, resource, dependency,
  timing, determinism, internal runtime, or other contract validation failed.

The run result records the classification, exact final epoch, final trace cursor
position, applicable cap, and structured diagnostic code. `completed` MUST NOT
be reported merely because the event queue is empty.

## Pure C++ requirement

The generated executable may depend on the statically linked gfsim model
library, C++20 standard library, compiled component libraries, and its selected
JSON parser. It MUST NOT require Python, MLIR libraries, an ACIR parser, the
Python frontend, or a dynamic component/plugin service at runtime.

## Acceptance criteria

The v0.1 contract conforms when it can:

- compile a generated hierarchy and exact component set with one C++20
  toolchain;
- build unique object IDs and typed static dispatch tables;
- execute the exact event-driven Work, arbitration, and Xfer barriers;
- jump over idle time while preserving integer-tick results;
- enforce zero-delay purity and once-per-tick stateful commit;
- schedule latency with `ready_time` and processes with subscriptions;
- hold trace records across backpressure and advance only on committed Xfer;
- produce bit-identical reference results under every permitted Work ordering;
- run conformance tests that deterministically permute or seed-randomize Work
  order within one `(time, delta)` and compare all observable outputs;
- enforce the selected static build profile and finite caps; and
- classify every run as `completed`, `incomplete`, or `failed`.
