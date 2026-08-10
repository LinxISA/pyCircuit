# ACSim and ACIR-to-gfsim Lowering v0.1 Specification

| Field | Value |
| --- | --- |
| Specification | ACSim construction IR and ACIR-to-gfsim lowering |
| Version | 0.1 |
| Status | Draft for review |
| Global contract epoch | `0.1` |
| ACSim namespace | `acsim` |
| C++ boundary | Same-toolchain C++20 source and templates |
| Generated target | Statically specialized gfsim executable |

## Purpose and authority

This specification defines the target-specific construction IR between frozen
ACIR and generated C++, exact C++20 binding resolution, structured code
generation, and the static dispatch and activation plans used by gfsim.

[ACIR Core v0.1](acir-core-v0.1.md) defines portable architecture semantics.
[ACIR Standard Library v0.1](acir-stdlib-v0.1.md) defines component schemas.
[gfsim Model Library Contract v0.1](gfsim-runtime-abi-v0.1.md) defines runtime
execution. [Agentic Python and CLI v0.1](agentic-python-cli-v0.1.md) defines
artifact publication. [Interface Evolution v0.1](interface-evolution-v0.1.md)
defines hard-break evolution.

The words **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY** are
normative when uppercase. The type and operation names in this specification
are the exact public ACSim v0.1 inventory. Generic MLIR syntax is not an
alternate public spelling.

## Architectural boundary

ACSim is a thin construction IR. It records only:

- concrete C++ type specializations and immutable constructor constants;
- hierarchical ownership and deterministic construction order;
- typed bindings derived from ACIR Graph-region SSA;
- statically shaped homogeneous arrays and fixed heterogeneous collections;
- pure inline-expression graphs;
- generated enum-PC process state;
- stable object and activation-source IDs;
- static dispatch entries and activation adjacency;
- selected static build profile, source maps, and exact fingerprints.

ACSim contains zero queue, storage, resource, arbitration, routing, scheduling,
latency, protocol, decoding, or component semantics. It MUST NOT contain runtime
schema interpretation, component-specific emitter directives, runtime
factories, runtime topology, or generic behavioral fallback code.

Architecture optimization, ownership analysis, fusion legality, zero-delay
cycle checking, and all semantic transformation happen in ACIR. ACSim may only
normalize an already fixed construction plan.

## Input preconditions

Lowering accepts only ACIR for which:

- `ac.contract_epoch` is exactly `"0.1"` and one `ac.system` is selected;
- topology, hierarchy paths, ownership, collection shapes, and model parameters
  are frozen and concrete;
- types, schemas, providers, interfaces, protocols, roles, and policies resolve;
- Graph-region SSA bindings and linearity are verified;
- every component is classified as pure or stateful;
- every `ac.process` passes effect, capture, progress, and suspension checks;
- zero-delay paths are pure, stateless, effect-free, deterministic, and acyclic;
- one static build profile and one exact provider set are selected; and
- every instantiated component is `available` in that provider/build profile.

An unfrozen, partial, unresolved, unavailable, or cross-epoch input fails before
ACSim is created.

## Logical lowering pipeline

```text
verified frozen ACIR
  -> normalize target types and static parameters
  -> resolve exact reusable library bindings and C++ type realizations
  -> lower process suspension to explicit enum-PC state
  -> assign ownership and deterministic construction order
  -> lower homogeneous collections to static arrays
  -> lower Graph-region SSA to typed construction bindings
  -> lower pure operations to inline expression graphs
  -> assign object IDs and activation-source IDs
  -> build static dispatch and activation adjacency
  -> create and verify canonical ACSim
  -> emit deterministic C++20
  -> check concepts, fingerprints, compilation, and link
```

The logical stages are `ac-resolve-gfsim-bindings`,
`ac-lower-process-state`, `ac-lower-to-acsim`, `acsim-verify`,
`acsim-emit-cxx`, and `acsim-check-cxx-contract`. An implementation may combine
stages internally, but `--stop-after`, diagnostics, and validation reports MUST
preserve these boundaries.

## Exact binding resolution

Every declaration that explicitly requests a reusable external or library C++
implementation resolves to exactly one C++20 binding using:

- global contract epoch;
- canonical schema identity and fingerprint;
- provider identity and implementation fingerprint;
- effect classification;
- normalized static arguments and concrete ACIR types;
- interface, role, protocol, and resource contracts;
- functional policy and static build profile; and
- exact toolchain target.

Provider registration order, filesystem enumeration, and host object identity
cannot influence selection. Multiple matches are ambiguous. No match produces
`ACLOWER-BINDING-MISSING` and stops before ACSim creation.

On a missing binding, lowering MUST NOT create a stub, choose a similar name,
lower a Python or ACIR behavioral body as substitute, interpret a descriptor at
runtime, defer the error to link time, or add a component-specific emitter
branch. The required repair is to add or correct the reusable C++ library
implementation and binding record.

Generated modules, generated processes, and core IR primitives never resolve
through the binding registry and never receive fabricated binding records.
Their compiler-generated C++ identities are carried by ACSim symbols and
specialization fingerprints. Registry records and the binding lock remain
exclusive to reusable external/library realizations.

An `acsim.type` whose kind is exactly `implementation` is the sole callable
identity for a compiler-generated helper or method. It is not a registry
record, provider request, placement, runtime object, or binding-lock entry.
Both call operations resolve their `callee` by exact canonical symbol only:
`acsim.inline` accepts either a pure `acsim.binding` or an implementation
`acsim.type`, while `acsim.invoke` accepts either a stateful `acsim.binding` or
an implementation `acsim.type`. A generated implementation symbol MUST NOT be
used by both operations in one model; its operation kind closes its effect
classification without mutable effect metadata.

The generic emitter may dispatch on ACSim operation kind and normalized binding
metadata. It MUST NOT dispatch on component name, family, provider namespace,
binding ID, or C++ symbol.

## Generated realization interfaces

An `acsim.module` is one compiler-generated owner class. Its symbol plus
specialization fingerprint is its realization identity. It has no binding
attribute and carries one closed `interface` dictionary containing exactly
`ports`, `resources`, and `results`.

A port record contains exactly `name`, `accessor`, `cardinality`, `delegation`,
`direction`, `interface`, `ownership`, `payload`, `protocol`, `role`, and
`time_domain`. A resource record contains exactly `name`, `accessor`,
`delegation`, `mode`, `ownership`, `resource`, `role`, and `time_domain`. A
result record contains exactly `name` and `cpp_type`. Each list is strictly
name-sorted with unique names; non-empty accessors are unique across the port
and resource lists. Ordered `acsim.export` operations and module results MUST
match the interface names and exact types, roles, delegation, and accessors as
applicable.

`acsim.instance` and `acsim.array` have exactly one realization `target`, which
resolves to a generated `acsim.module` or stateful `acsim.binding`. Their owner
element type names that exact target. `acsim.process` has no binding attribute;
its nested symbol and specialization fingerprint identify its generated state
machine.

## Binding lock

`ac-resolve-gfsim-bindings` emits immutable
`acsim-bindings.lock.json`. Each record contains exactly:

- `binding_schema` equal to `acsim-binding-0.1`;
- `contract_epoch` equal to the string `"0.1"`;
- binding, component-schema, provider, and implementation identities;
- component-schema and provider-implementation fingerprints;
- availability equal to `available`;
- effect equal to `pure` or `stateful`;
- C++ header, target, symbol, concept, and entry points;
- normalized parameters and their C++ mappings;
- construction and ownership requirements;
- typed ports, results, resources, and activation sources; and
- an RFC 8785 canonical-record SHA-256 fingerprint.

Binding metadata cannot contain raw C++ statements, expression fragments,
macros, formatter strings, emitter callbacks, or component behavior.

For each parameter, the lock records its name, ACIR type, canonical value,
ordinal, C++ type, and one mapping:

- `template_argument` for type identity, layout, topology shape, algorithm, or
  dispatch specialization;
- `constexpr_argument` for a compile-time scalar or immutable aggregate; or
- `constructor_constant` for per-instance immutable data known at build time.

A constructor constant is still a build specialization input. It cannot come
from a run manifest, environment variable, runtime configuration map, plugin,
or mutable registry.

## ACSim type inventory

| Type | Meaning |
| --- | --- |
| `!acsim.value<@cpp_type>` | Concrete typed C++ value in generated state or a library call |
| `!acsim.expr<@cpp_type>` | Pure effect-free inline expression |
| `!acsim.owner<@realization>` | Unique owning placement of a generated module or stateful library object |
| `!acsim.ref<@realization>` | Statically resolved non-owning reference to that exact realization |
| `!acsim.port<@interface, @role, @payload, @protocol>` | Typed construction-time port |
| `!acsim.resource<@resource, @role>` | Typed construction-time resource capability |
| `!acsim.array<[shape], element_type>` | Statically shaped homogeneous collection |
| `!acsim.object_id` | Dense stable runtime-object ID |
| `!acsim.activation_id` | Dense stable activation-source ID |
| `!acsim.pc<@process>` | Closed enum type for one process |
| `!acsim.wake<@kind>` | Typed process subscription or wake handle |

Static model parameters are attributes, not runtime ACSim values. ACSim has no
dynamic shape, variant owner, opaque component, runtime configuration, plugin,
reflection, or untyped port type.

## ACSim operation inventory

| Operation | Contract |
| --- | --- |
| `acsim.model` | One selected system, exact fingerprints, object registry, and activation plan |
| `acsim.type` | One resolved C++ value, packet, interface, protocol, or policy realization |
| `acsim.binding` | One exact binding-lock record |
| `acsim.module` | One generated owner class for one specialized ACIR module |
| `acsim.instance` | One owned generated submodule or stateful library specialization |
| `acsim.array` | One homogeneous nested static owning collection |
| `acsim.element` | Constant-index projection from a static array |
| `acsim.port` | Typed port projection through a resolved accessor |
| `acsim.resource` | Typed resource-capability projection |
| `acsim.bind` | Exact typed port, resource, export, or pure-view binding |
| `acsim.inline` | Exact pure external-binding or generated-implementation call |
| `acsim.process` | Generated enum-PC process state machine |
| `acsim.live.load` | Typed load of process state live across suspension |
| `acsim.live.store` | Proposed typed process-state update |
| `acsim.invoke` | Exact stateful external-binding or generated-implementation call in a process state |
| `acsim.continue` | Transition to another PC without a wake |
| `acsim.suspend` | Proposed next PC plus exact typed wake registration |
| `acsim.terminate` | Proposed terminal success or failure |
| `acsim.export` | Internal typed value or role exported from a module |
| `acsim.dispatch` | Object ID and exact Work/Xfer/reset/validation thunks |
| `acsim.activate` | Static activation edge to a target object ID |
| `acsim.return` | Ordered module-construction exports |

Process regions may use `builtin`, `arith`, `index`, and `cf` for constants,
pure arithmetic, indexing, and intra-state control flow, and may use the
effect-free `acsim.inline` operation. No other dialect is legal in canonical
ACSim v0.1. Every process block has one PC attribute; an ordinary `cf` edge
cannot cross a suspension boundary.

Changing this inventory changes the public schema and requires a global epoch
increment.

## Verifier invariants

Canonical ACSim has exactly one `acsim.model` with epoch `"0.1"` and exact
frozen-ACIR, binding-lock, provider, profile, toolchain, and schema-set
fingerprints. It contains no unresolved type, symbol, parameter, view,
generator, or component schema.

### Ownership and hierarchy

- Every stateful instance, queue, resource, mutable protocol, trace cursor, and
  process has exactly one owner.
- Every owner is placed once as root, named member, tuple element, or static
  array element.
- Construction precedes every reference and binding; destruction is reverse
  ownership order.
- Non-owning references cannot extend lifetime or cross a boundary that does
  not export the corresponding role.
- Generated C++ preserves the module hierarchy; the scheduler table is only a
  non-owning index and never flattens ownership.
- Hierarchy paths derive from frozen source naming and remain separate from
  specialization and cache fingerprints.
- Owner expansion is deterministic, iterative, and bounded. It includes
  generated-module placements, stateful binding placements, every array
  element, and every process, and drives construction, destruction, paths, and
  recursive child expansion.
- A placement targeting `acsim.module` is an ownership-only wrapper. It remains
  in construction/destruction order and recursively exposes its children but
  receives no runtime object ID, activation ID, or dispatch row.

### Collections

- A homogeneous collection with the same specialization MUST lower to
  `acsim.array`, with concrete non-negative extents and lexicographic indices.
- A heterogeneous or differently specialized `ac.instances` collection MUST
  lower to ordered named members or a fixed tuple plus collection metadata.
- Array elements have identical binding, specialization, interface shape, and
  ownership, and cannot be moved to a different owner.

### Typed bindings

- Every ACIR Graph-region SSA relation lowers exactly once to a typed binding,
  pure dependency, or export chain.
- Flow payload, endpoint interface/role, resource type/role, protocol,
  direction, cardinality, ownership, delegation, and time domain match exactly.
- No binding uses string lookup, numeric guesses, registration order, runtime
  descriptors, or implicit conversion.
- No queue, adapter, arbitration, merge, fork, router, or bridge is inserted.

### Pure expressions

- Every `acsim.inline` resolves exactly to either an external `acsim.binding`
  whose immutable effect is `pure` or a compiler-generated `acsim.type` whose
  kind is `implementation`. It has no owner, object ID, dispatch row,
  activation source, state, queue, event, or side effect.
- Module-body inline calls produce exactly one `!acsim.expr`. Process-body
  inline calls produce exactly one builtin integer, float, index, or
  `!acsim.value`; an expression, owner, reference, wake, or other aggregate is
  not process state.
- Pure-expression graphs are acyclic and deterministic.
- Activation bypasses the pure graph: a committed upstream source activates
  each downstream stateful consumer, which evaluates the expression from its
  committed snapshot.
- ACIR operations and source maps remain available for diagnostics even though
  the emitted C++ is an inline helper, template call, or `constexpr` expression.

### Processes

- Every ACIR process lowers to one `acsim.process` with a closed explicit PC
  enum, one entry state, typed live slots, exact wakes, and terminal outcomes.
- Continuation and live-state updates commit through Xfer and are never visible
  to the same Work snapshot.
- A control path suspends, terminates, or proves bounded local progress within
  its static fairness cap.
- Suspension is subscription-driven. Coroutines, polling, bytecode
  interpretation, dynamic continuation frames, `std::function`, and
  exception-driven control flow are forbidden.
- Every `acsim.invoke` resolves exactly to either an external `acsim.binding`
  whose immutable effect is `stateful` or a compiler-generated `acsim.type`
  whose kind is `implementation`; each result is exactly `!acsim.value` or
  `!acsim.wake`. A stateless generated wake helper MAY have zero arguments.
- Live slots remain exact `!acsim.value` records. When a builtin scalar crosses
  suspension, ProcessStatePlan inserts a generated pure wrap inline call before
  `acsim.live.store`, then `acsim.live.load` and a generated pure unwrap inline
  call after resumption. These helpers allocate no runtime object and create no
  generated binding record.

### Dispatch and activation

- Runtime expansion is a separate deterministic, iterative, bounded analysis.
  It contains only stateful `acsim.binding`-targeted placements and array
  elements plus one row for every process in every concrete generated-module
  context.
- Every runtime row has one dense object ID and activation ID in canonical path
  order and exactly one typed dispatch row. Generated-module wrapper
  placements never receive a runtime row.
- Binding-targeted rows use the exact Work/Xfer/reset/validate entry points
  from their binding-lock record. Process rows use deterministic
  compiler-generated thunks derived from the enclosing module and process
  realization identities without a registry record.
- Every committed value, event target, subscription, pending commit, and
  internal wake has a complete static activation path.
- Activation-source IDs are dense; target IDs are deduplicated and sorted.
- Idle correctness never depends on polling; delay uses exact `ready_time`.

## Structured C++ generation

Each specialized ACIR module becomes one generated C++ owner class. Owners are
stored by value where practical. Homogeneous collections use nested
`std::array` or an equivalent no-allocation aggregate. Heterogeneous collections
use named members or fixed tuples.

For a stateful component, the generator emits only its include, concrete
specialization, immutable construction arguments, owner placement, typed
accessors and binding calls, dispatch registration, activation adjacency, and
concept/fingerprint assertions. It never emits that component's Work, Xfer,
arbitration, latency, routing, storage, protocol, or functional algorithm.

Reusable implementation resides in the repository:

| Source root | Responsibility |
| --- | --- |
| `runtime/` | epochs, events, queues, resources, process support, trace boundary, validation, dispatch, and observation infrastructure |
| `stdlib/` | standard/provider component templates, policies, packet traits, functional policies, and conformance tests |

Provider source resides under `stdlib/providers/`. Architecture-specific output
exists only in the clean build staging tree.

## Generated process state machines

`@process` is the sole generated behavioral exception. Each process becomes one
final C++ class owned by its enclosing generated module. It stores committed and
proposed PC, typed values live across suspension, exact subscriptions, and
termination state, and uses an explicit switch on committed PC.

PC values are assigned deterministically. The smallest sufficient fixed-width
unsigned underlying type is used. A Work call runs until suspension,
termination, failure, the static fairness cap, or a proven bounded local
continuation. Xfer commits the proposed continuation. No C++ coroutine, host
thread, recursive continuation, interpreter, or dynamically allocated frame is
permitted.

## Static dispatch and activation adjacency

Generated C++ defines one static dispatch table indexed by object ID. Each row
contains a typed object pointer plus exact Work, Xfer, reset, and validation
thunks and a static object kind. A thunk may erase storage to `void *`, but it
recovers exactly one compile-time specialization using `static_cast`.

The emitter sorts rows by the already assigned dense object ID and emits one
`gfsim::makeDispatchRow(&typed_object)` initializer per row. Discovery order,
pointer value, and container iteration order MUST NOT affect the emitted bytes.
The runtime contract uses one Xfer thunk with the explicit phase
`XferPhase::Arbitrate` or `XferPhase::Commit`; this preserves the global
all-arbitration-before-all-commit barrier without adding a dynamically selected
operation. The generated validation thunk checks the row ID and kind against the
typed object before any hot execution begins.

Activation adjacency is emitted as canonical compressed arrays equivalent to
`activation_offsets[source_count + 1]` and
`activation_targets[edge_count]`. A committed source activates only adjacent
object IDs. Due events and process subscriptions address object IDs directly.
The scheduler may deduplicate wake causes for one epoch but cannot scan all
objects to discover work.

Hot execution cannot use component names, hierarchy strings, reflection,
`dynamic_cast`, runtime type discovery, descriptor lookup, unordered
registration, plugin indirection, or runtime policy selection.

## Same-toolchain C++20 source contract

v0.1 defines a source/template contract, not a stable binary or plugin ABI.
Generated code, `runtime/`, `stdlib/`, provider sources, packets, and the harness
are compiled with identical:

- C++20 compiler and standard-library implementation/ABI mode;
- target triple, CPU, features, data layout, and object format;
- exception, RTTI, sanitizer, optimization, and LTO settings;
- public compile definitions; and
- contract epoch, schema, provider, source, profile, and toolchain fingerprints.

Prebuilt object reuse requires complete fingerprint equality. Source
compatibility, semantic version ranges, and matching C++ symbols are
insufficient. No dynamic component ABI, cross-compiler ABI, schema negotiation,
descriptor interpreter, or runtime factory registry exists.

Every used specialization is checked with its declared C++20 concept. Compile
checks prove exact epoch, construction arguments, port/resource accessors,
binding expressions, pure and stateful entry signatures, packet traits,
process state, array extents, build profile, fingerprints, and unique linkage.

## Static build profiles

The build profile is selected before binding resolution and is included in the
binary fingerprint:

| Profile | Required contract |
| --- | --- |
| `fast` | Required representation gates, canonical release pipeline, mandatory runtime checks, and selected static instrumentation |
| `validated` | `fast` plus verification after every pass, exhaustive checks, and complete provenance/reports |
| `custom` | Explicit pass pipeline and static instrumentation, while retaining every mandatory `fast` check |

Instrumentation, validation hooks, functional policy, and compiled probes are
static. A run may select an output path and format, but cannot add, remove,
enable, or disable model instrumentation or validation code.

## Fingerprints and caching

The build fingerprint covers exact contract epoch, normalized frozen ACIR,
ACSim, binding lock, schema set, provider implementations, runtime and stdlib
sources, generated sources, compiler, standard library, target, build profile,
compile definitions, and link inputs.

Hierarchy paths are never used as content/cache identity. A specialization
fingerprint derives from canonical definition identity, normalized static
arguments, resolved type/schema identities, provider implementation, profile,
and toolchain. The same specialization instantiated at different hierarchy
paths has one specialization fingerprint but distinct object IDs and paths.

Fingerprint records use RFC 8785 canonical JSON and lowercase SHA-256 prefixed
with `sha256:`. Any missing or unequal field is a cache miss and, where an
artifact is being linked or executed, a hard error. The final fingerprint is
embedded in the executable and copied to `build-manifest.json`.

## Staged output contract

Compilation uses a clean command-specific staging directory containing frozen
ACIR, canonical ACSim, the binding lock, deterministic generated sources,
compile plan, validation reports, executable, and build manifest. Generated
module and process files are emitted deterministically even when structurally
empty.

Publication occurs only after ACSim verification, generated-source checks,
C++ concept checks, compilation, link, and embedded-fingerprint comparison all
succeed. The complete destination artifact set is replaced atomically. A failed
stage cannot modify the most recent valid build or immutable manifest.

## Diagnostics

| Code | Condition |
| --- | --- |
| `ACLOWER-EPOCH-MISMATCH` | Contract epochs differ |
| `ACLOWER-SCHEMA-MISMATCH` | Schema identity or fingerprint differs |
| `ACLOWER-BINDING-MISSING` | No exact available C++ binding exists |
| `ACLOWER-BINDING-AMBIGUOUS` | Multiple exact candidates remain |
| `ACLOWER-PARAM-PHASE` | Parameter is unresolved, dynamic, or mapped illegally |
| `ACLOWER-TYPE-MISMATCH` | Typed value, port, protocol, resource, result, or C++ type differs |
| `ACLOWER-OWNERSHIP` | Owner, lifetime, delegation, or construction order is invalid |
| `ACLOWER-ARRAY` | Static-array shape, specialization, index, or ownership is invalid |
| `ACLOWER-INLINE-EFFECT` | Purported inline expression has delay, state, suspension, or effects |
| `ACLOWER-PROCESS-STATE` | PC, live state, wake, progress, or suspension is invalid |
| `ACLOWER-ACTIVATION` | Static activation adjacency is incomplete or nondeterministic |
| `ACLOWER-DISPATCH` | Object IDs or dispatch rows are inconsistent |
| `ACLOWER-PROFILE` | Static build profile cannot realize the model |
| `ACLOWER-FINGERPRINT` | Required source/toolchain/schema/provider fingerprint differs |

Diagnostics conform to the shared diagnostic schema and retain the logical
stage, binding, ACIR entity, ACSim operation, source span, hierarchy path,
expected value, actual value, and related objects when available.

## Extension rule

A new component is added by a frozen component schema, an exact binding record,
a concept-conforming C++20 template, provider availability, and conformance
tests. It MUST NOT require an ACIR Core operation, ACSim operation, generic
lowering branch, or emitter branch.

A missing reusable operation requires adding library C++ code. A change to
universal architecture semantics changes ACIR Core. A change to construction
semantics changes ACSim and its generic emitter. Either public schema change is
a global hard break under the interface-evolution specification.

## Performance invariants

- Scheduling work is proportional to active objects, due events, pending
  commits, and traversed activation edges, not total object count per tick.
- Idle time jumps directly to the next due event.
- Stateful objects do not use countdown or condition polling.
- Object and activation IDs are dense and dispatch uses static typed thunks.
- Pure zero-delay operations create no runtime object, dispatch row, event, or
  scheduler activation.
- No hot-path string lookup, reflection, RTTI, descriptor interpretation, or
  dynamic policy selection occurs.
- Static topology storage is constructed or reserved before simulation.
- Stateful owners commit at most once per integer tick and publish no result
  earlier than the next tick.
- Permitted Work reordering produces bit-identical results.

## Conformance tests

Positive tests cover nested hierarchy, multidimensional arrays, heterogeneous
fixed collections, all three binding kinds, pure chains, each executable
component family, multi-suspension processes, every activation cause, static
dispatch, `fast` and `validated` profiles, deterministic regeneration, and exact
cache reuse.

Negative tests cover missing/ambiguous/unavailable bindings, cross-epoch or
fingerprint mismatch, dynamic parameters, concept failure, role/type mismatch,
multiple owners, dynamic arrays, stateful zero delay, effectful pure bindings,
invalid process continuation, polling, incomplete activation, runtime
configuration, plugin metadata, descriptor interpretation, generated component
behavior outside `@process`, and component-specific emitter branching.

The extension test adds a stateful provider component using only schema,
binding, C++ template, provider availability, and tests. Generic lowering and
emitter sources remain unchanged.

The sparsity test compares two models with the same active frontier and event
sequence, one with additional permanently idle objects. After construction,
scheduler invocation count and activation-edge traversal count MUST be equal.

## Acceptance criteria

Conformance requires deterministic frozen-ACIR-to-ACSim lowering; complete
ownership, typed binding, process, dispatch, activation, and fingerprint
verification; fully specialized structured C++; exact C++20 bindings with no
fallback; enum-PC `@process` as the sole generated behavior; no runtime model
configuration; no inactive-object polling; exact same-toolchain compilation;
atomic publication; and extensibility without component-specific compiler code.
