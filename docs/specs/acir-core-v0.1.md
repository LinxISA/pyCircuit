# ACIR Core v0.1 Specification

| Field | Value |
| --- | --- |
| Specification | Agentic Circuit Intermediate Representation Core |
| Version | 0.1 |
| Status | Draft for review |
| Namespace | `ac` |
| Global contract epoch | `0.1` |

## Scope

ACIR Core defines the portable MLIR representation for a statically elaborated,
hierarchical, transaction-level computer architecture. It defines structure,
types, interfaces, protocols, resources, address maps, processes, effects, and
verification rules.

ACIR Core does not define a closed component catalog, a command-line interface,
or a concrete C++ API. Those contracts are defined by:

- [Python-to-ACIR Lowering v0.1](python-to-acir-lowering-v0.1.md)
- [Agentic Python and CLI v0.1](agentic-python-cli-v0.1.md)
- [ACIR Standard Library v0.1](acir-stdlib-v0.1.md)
- [gfsim Model Library Contract v0.1](gfsim-runtime-abi-v0.1.md)
- [PTO Trace Schema v0.1](pto-trace-schema-v0.1.md)

## Normative language

The words **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY** are
normative requirements when written in uppercase.

The operation and type spellings in this specification are the public ACIR v0.1
assembly syntax. Producers MUST emit these spellings, and consumers MUST reject
undeclared aliases or compatibility spellings. Assembly examples and syntax
schemata are normative for the constructs they show; explicitly identified
metavariables are not literal source text.

## Core model

### Static topology

The following properties are static architecture topology:

- module definitions and instances;
- instance arrays and ordered instance collections;
- ports, interfaces, endpoints, and typed SSA topology bindings;
- owned queues, resources, and address spaces;
- protocol selection;
- C++ component binding identity;
- time-domain membership;
- hierarchy and observation paths.

The `ac-freeze-topology` pass establishes the static topology boundary. After
that pass, IR MUST NOT add or remove instances, resize collections, change port
signatures, change resource ownership, or change typed SSA topology bindings.

### Dynamic execution

The following properties may change during simulation:

- queue occupancy;
- resource reservations;
- protocol state;
- packet and transaction lifetime;
- local arbitration decisions;
- process continuation state;
- event scheduling and completion;
- storage contents;
- statistics and probes.

### Pure and stateful operations

A **pure operation** has no MLIR memory effects, owns no mutable state, performs
no external I/O, schedules no event, consumes no linear dynamic capability, and
returns values determined only by its operands and static attributes. A pure
operation MAY be zero delay and MAY be re-evaluated, reordered, duplicated, or
eliminated when ordinary SSA rules permit.

A **stateful operation** reads, proposes, arbitrates, commits, or observes an
ACIR abstract resource; owns or accesses mutable simulator state; schedules an
event; suspends a process; or performs external I/O. It MUST declare its MLIR
effects and its state owner. Stateful queue, resource, module, process, protocol,
event, storage, trace, and statistics operations participate in the global-tick
snapshot/proposal/arbitration/commit semantics. A state owner commits at most
once per global tick, even if multiple operations propose changes to it, and
publishes no result or state change earlier than the next global tick.

### Hierarchy

Modules MAY be nested through instances to arbitrary depth. Hierarchy is a
semantic property and MUST survive lowering into the generated C++ ownership
tree.

Module definitions SHOULD remain in the outer MLIR symbol scope. Module-local
objects use inner symbols. Cross-hierarchy references use verified hierarchical
paths rather than lexical nesting of MLIR symbol tables.

### Global tick reference semantics

All dynamic execution occurs in one global epoch whose origin is global tick
`0`. Global time and global ticks are non-negative mathematical integers. An
implementation MAY use a fixed-width representation only if it diagnoses an
overflow before changing architectural state. Floating-point time, host clock
time, and implementation traversal count are not simulation time.

At an active global tick `t`, the reference semantics performs these phases in
order:

1. **Snapshot:** every participant reads the same committed state `S[t]` and
   the events whose exact timestamp is `t`.
2. **Pure evaluation:** zero-delay pure operations evaluate over the static
   Graph-region dependency graph.
3. **Proposal:** stateful operations propose queue, resource, protocol, process,
   module, and event changes without publishing them.
4. **Arbitration:** each declared local owner resolves its contested proposals
   using its static policy and deterministic tie-breaks.
5. **Commit:** each state owner accepts or rejects proposals and commits at most
   once for tick `t`. The resulting state and values are observable no earlier
   than tick `t + 1`.

The simulator is event driven: after completing an active tick, it MAY advance
directly to the least timestamp containing a pending event or other scheduled
activation. Skipping inactive integer ticks does not change the reference
result. An event proposed at tick `t` MUST have a timestamp of at least `t + 1`.
No operation observes an uncommitted proposal, arbitration result, or state
change from the same tick. The frozen set of participants, dependencies, state
owners, and arbitration owners is the static execution graph and MUST NOT be
changed by scheduling. This is the ACIR-level definition of the gfsim
snapshot/`Work`/local-arbitration/`Xfer` behavior.

gfsim may refine an active tick into ordered causal deltas. All Work scheduled
at one `(tick, delta)` reads one immutable committed snapshot, and independent
Work may run in any order or concurrently. Delta order is observable only for
proven pure zero-delay dependencies; stateful Xfer results remain unavailable
until the next integer tick. Independent deltas may be reordered only when the
compiler proves that every architectural result, diagnostic, statistic, and
termination classification remains identical.

## System and module structure

### Contract epoch and canonical assembly

Every public ACIR v0.1 file MUST have one outer `builtin.module` with the exact
string attribute `ac.contract_epoch = "0.1"`:

```mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
}
```

The attribute names the complete public syntax and semantic contract, not a
minimum reader version. An ACIR v0.1 parser MUST accept exactly epoch `"0.1"`
and MUST reject a missing, unknown, older, or newer epoch before interpreting
ACIR operations. The toolchain provides no epoch conversion, alias, fallback,
or best-effort compatibility mode; Git history is the rollback mechanism.

Public types use MLIR type syntax, symbol parameters use `@symbol`, and static
literal parameters use canonical MLIR attributes. Module topology uses Graph
region block arguments, typed operation operands and results, and `ac.return`.
The canonical shape is:

```mlir
ac.module @Name(
  %input : !ac.flow<T, @protocol>,
  %service : !ac.endpoint<@Interface, @role>,
  %resource : !ac.resource_ref<@ResourceType, @role>
) -> (!ac.flow<U, @protocol>) graph {
  %output = ac.instance @child of @Child(%input, %service, %resource)
    : (!ac.flow<T, @protocol>,
       !ac.endpoint<@Interface, @role>,
       !ac.resource_ref<@ResourceType, @role>)
      -> !ac.flow<U, @protocol>
  ac.return %output : !ac.flow<U, @protocol>
}
```

`T`, `U`, `@protocol`, `@Interface`, `@ResourceType`, and `@role` above are
metavariables. A concrete file contains concrete types and symbol references.
Operations MUST carry enough operand and result type information to verify the
Graph region without inspecting a C++ binding. Generic MLIR operation syntax is
not a second public ACIR v0.1 spelling and an ACIR interchange parser MUST NOT
accept it as a substitute for the canonical syntax above.

### `ac.system`

`ac.system` selects one complete architecture and identifies:

- root module definition;
- canonical root instance name;
- global tick unit and epoch;
- time domains;
- primary workload process;
- deterministic seed policy;
- enabled instrumentation layers;
- result schema.

One outer `builtin.module` MAY contain multiple systems. A generated simulator
selects exactly one unless it explicitly implements a multi-system harness.

### Module declarations

ACIR defines:

- `ac.module`: hierarchical definition with an ACIR body;
- `ac.module.extern`: signature implemented outside ACIR;
- `ac.module.generated`: signature resolved by a registered generator.

A module signature contains:

- symbol name and visibility;
- typed static parameters;
- named flow ports and interface ports;
- optional time-domain parameter;
- optional reusable implementation binding, present only on declarations that
  explicitly request an external/library implementation;
- contracts and source location.

The structural body of `ac.module` is an MLIR Graph region. Its operations
describe a concurrent ownership and dataflow graph; textual order is canonical
printing and diagnostic order and has no simulated scheduling meaning. Uses
MAY precede their definitions, and the structural graph MAY contain cycles.
Legality depends on the zero-delay rules, not textual dominance.

The region arguments represent module input flows and imported endpoint or
resource roles. `ac.return` yields the module's exported flows and roles. The
body may contain structural ACIR operations and nested executable regions, but
general branch-based control flow is confined to `ac.process`.

Internal `ac.module` definitions and `ac.process` bodies are compiler-generated
realizations and do not implicitly request registry bindings. Core IR
primitives likewise have no implementation binding. Absence of a reusable
implementation request is not a missing-binding condition.

### Static parameters

ACIR v0.1 supports integer, boolean, string, enum, type, symbol, and unit-bearing
static parameters. Parameters MAY have defaults, expressions, constraints, and
parent-to-child forwarding.

Every system, module, generated-module, external-module, instance, resource,
queue, protocol, and implementation-model parameter is static. Every parameter
MUST be resolved to a concrete value before topology freeze, including
parameters that do not affect type or topology. ACIR v0.1 has no runtime model
configuration, runtime parameter, configuration schema on `ac.system` or module
declarations, or late-bound implementation parameter. Workload inputs, trace
contents, termination limits, logging controls, and simulator diagnostics are
run inputs rather than model parameters and MUST NOT alter frozen ACIR.

### `ac.instance`

An instance has:

- one parent;
- one definition reference;
- resolved static parameters;
- independent runtime state;
- stable local name and identifier;
- canonical hierarchy path.

Two instances of the same definition MUST NOT share mutable state unless that
state is modeled as an explicitly shared child resource.

## Structural collections

### `ac.array`

`ac.array` is an owning, regular, N-dimensional collection of instances. Its
elements have the same definition and interface shape. Index-derived static
parameters are permitted if all resulting elements remain compatible.

Array shape is static after topology freeze. Generated C++ SHOULD preserve the
shape using nested structured containers.

### `ac.instances`

`ac.instances` is an owning, ordered, one-dimensional collection whose elements
implement the same declared interface but MAY use different definitions or
specializations.

This name deliberately distinguishes a structural instance collection from the
value type `!ac.vector<N x T>`.

### `ac.view`

`ac.view` is a non-owning, statically resolved ordered view of instances, ports,
or endpoints. It supports constant selection, slicing, concatenation, zip,
permutation, and element-wise SSA topology binding.

A view MUST resolve before lowering to the simulator object graph.

## Named types

### Type scopes

Named types are declared in `ac.type_scope`. `ac.type_alias` gives a stable
public name to a canonical type.

### Struct

`ac.struct` is a named product type with ordered, named fields. Fields may
contain primitive MLIR types, ACIR structs, enums, tagged unions, optionals,
fixed vectors, fixed arrays, and bounded lists.

A struct is a logical type. Serialized layout is explicit and independent of
host C++ padding.

The verifier rejects duplicate fields, unbounded value recursion, unresolved
public field types, and ambiguous union discriminators.

### Packet

`ac.packet` is a named immutable transport value with an explicit serialization
contract. It MAY declare:

- header, payload, and trailer groups;
- routing and correlation fields;
- priority or QoS fields;
- bounded extension fields;
- maximum serialized size;
- alignment and endianness;
- fragmentation permission;
- integrity metadata.

Packet values have SSA value semantics. After an offer to a channel, the packet
MUST remain stable until it transfers, cancels, or is rejected according to the
protocol.

Timing, bandwidth, and capacity belong to channels and resources, not packet
types.

### Transaction

`ac.transaction` represents a logical architecture operation whose lifetime may
span multiple packets and protocol phases. Packet identity and transaction
identity are distinct.

Examples include a compute request, DMA transfer, memory request/response, and a
decoded PTO operation.

### Tile

Tile is a payload/schema concept. It SHOULD be represented by structs and
transaction fields. It is not a scheduled module unless a particular model
explicitly instantiates a tile-owning resource.

### Record-like operations

Structs, packets, and transactions implement a common `RecordLike` operation
interface. ACIR v0.1 provides shared value operations equivalent to:

- `ac.record.create`;
- `ac.record.get`;
- `ac.record.with`.

Packet serialization is provided by:

- `ac.packet.serialize`;
- `ac.packet.deserialize`.

This avoids duplicating create/get/update operations for every record-like type.

### Data layout

ACIR MUST use MLIR data-layout interfaces and SHOULD use `dlti` for byte size,
bit size, alignment, endianness, address width, and serialization width.

Resource capacity and packet serialization MUST use canonical data layout and
MUST NOT depend on host `sizeof`.

## Interfaces and channels

### `ac.interface`

An interface declares roles and a bundle of named channels. Channel directions
are relative to roles.

```text
ac.interface @MemoryPort<Request, Response> {
  role @initiator
  role @target
  channel @request : !ac.channel<Request, @ready_valid>
    from @initiator to @target
  channel @response : !ac.channel<Response, @ready_valid>
    from @target to @initiator
}
```

### Endpoint

`!ac.endpoint<Interface, Role>` is one role of an interface. In concrete
assembly, both parameters are symbol references, for example
`!ac.endpoint<@MemoryPort, @target>`. Endpoint verification recursively checks
the interface's channel bundle. An endpoint value is not a flow, channel, or
resource reference and there is no implicit conversion among these types.

### Flow

`!ac.flow<T, Protocol>` is an immutable logical producer-to-consumer value used
in module topology. `T` is the carried payload or transaction type and
`Protocol` is a concrete `ac.protocol` symbol reference. Neither parameter may
be omitted or inferred in public ACIR v0.1 assembly.

A flow is not a queue, channel implementation, or mutable simulator object. It
does not imply capacity, buffering, arbitration, replication, or delay.

Every flow value has exactly one SSA definition: a module argument or operation
result. It has at most one functional use; use by `ac.return` counts as that
functional use. Observation-only probes do not consume a flow. Replication
requires an explicit component with broadcast or fork semantics. Fan-in
requires an explicit merge, arbitration, scheduling, or interconnect component.

### Channel

`!ac.channel<T, Protocol>` is permitted only as the type of a named `channel`
entry inside an `ac.interface` declaration. It describes that interface field
and declares or inherits:

- carried type;
- handshake protocol;
- ordering guarantee;
- delivery guarantee;
- maximum in-flight count;
- optional latency and bandwidth contract;
- optional time domain.

A channel is not a standalone topology value or edge, cannot appear in a module
signature or as an SSA operand or result, and is not automatically a queue. Any
state affecting capacity or timing must be an explicitly owned queue, resource,
module, or process.

### Resource reference

`!ac.resource_ref<ResourceType, Role>` is a non-owning, typed capability for a
declared resource. In concrete assembly, `ResourceType` and `Role` are symbol
references. The role determines the permitted proposals, observations, and
release or cancellation actions. A resource reference is not a reservation
token and does not transfer ownership.

### Topology binding and cardinality

All module topology is represented once by typed Graph-region SSA operands and
results. No separate connection operation exists. Passing a flow, endpoint, or
resource reference to `ac.instance`, or returning it across a module boundary,
is the topology binding.

Every queue, resource, module instance, process, and other stateful runtime
object has exactly one owning system, module, or owning structural collection.
Every endpoint value has exactly one definition and at most one structural use;
delegation through `ac.return` counts as that use. Multiple peers require an
explicit interconnect, adapter, or replicated endpoint declaration.

Every resource reference resolves to exactly one owned resource. References are
non-owning and MAY have multiple SSA uses only when the referenced resource role
declares shared cardinality and the resource has one deterministic arbitration
owner. A role with exclusive cardinality has at most one structural use.
Reservation tokens remain linear dynamic values: each successful reservation
produces one token consumed exactly once by release or cancellation.

Implicit fan-in, fan-out, arbitration, buffering, conversion, fragmentation,
reassembly, and time-domain bridging are forbidden. These behaviors require
explicit standard-library or user-defined modules.

## Protocol IR

### Definition

`ac.protocol` declares transaction-level handshake behavior as a finite-state
event protocol. It contains:

- roles;
- typed directional events;
- protocol states;
- transitions and priorities;
- atomic transfer points;
- cancellation and retry behavior;
- backpressure mode;
- ordering and delivery guarantees;
- correlation rules;
- liveness and boundedness contracts.

The protocol models transaction events rather than RTL wires.

### Transition rules

A protocol has exactly one initial state. Streaming protocols MAY be cyclic.
Finite transactions MAY have terminal states.

Guards MUST be side-effect free. Protocol actions may update only
protocol-local abstract state such as credits, phase, and in-flight correlation
records. General `scf` and component state mutation are forbidden in protocol
definitions.

Overlapping transitions require explicit priority. The verifier rejects
ambiguous transitions.

### Transfer

A transition marked `transfer` identifies atomic ownership transfer. Every
offered packet must be transferred exactly once, explicitly cancelled, rejected,
or retained for retry. Protocol semantics MUST NOT silently lose or duplicate a
packet.

### Backpressure

ACIR v0.1 supports `none`, `accept`, `credit`, `capacity`, and declarative
`custom` backpressure modes. A pending offer declares whether its packet must
remain stable.

### Standard guarantees

Protocols may declare:

- ordering: `fifo`, `per_key`, or `unordered`;
- delivery: `exactly_once`, `at_most_once`, or `best_effort`;
- completion: `on_accept`, `on_response`, or `on_terminal_phase`;
- maximum in-flight count;
- response correlation field.

Protocol adapters MUST NOT silently weaken guarantees.

## Queues, resources, and addresses

### `ac.queue`

`ac.queue` represents finite ordered storage. It declares packet type, mandatory
entry capacity, optional byte capacity, ordering, endpoint protocol, ownership,
and optional watermarks.

If both capacities exist, enqueue succeeds only when both constraints permit
it.

### `ac.event_queue`

An event queue stores time-ordered completion events. Equal-time events use a
stable monotonically assigned sequence number as the final tie-break.

### `ac.resource`

`ac.resource` is a first-class finite service or capacity resource. It declares:

- capacity or lane count;
- issue width;
- initiation interval;
- latency model reference;
- reservation and release rules;
- arbitration owner;
- optional supported transaction classes;
- statistics and contracts.

Compute lanes, link lanes, storage banks, and DMA channels may expose resources
without becoming the same kind of module.

Resource acquisition is proposed before commit. Reservations and releases MUST
be balanced or explicitly abandoned by a declared cancellation path.

### `ac.address_space`

`ac.address_space` gives a stable identity to an address domain. It declares
address width, unit, optional data layout, and parent translation relationship.

### `ac.address_map`

`ac.address_map` maps ranges or interleaved regions in one address space to
target endpoints or child address spaces. It supports:

- base and size ranges;
- priority for intentional overlap;
- interleave granularity and bank selection;
- address offset transformation;
- permissions and transaction-class filters;
- explicit default or unmapped behavior.

The verifier rejects ambiguous overlap unless priority is explicit. Routing
through a bus, crossbar, router, memory controller, or adapter remains an
explicit typed module operand/result binding.

For interleave granularity `g`, bank count `n`, and selected bank `b`, an entry
selects the intersection of `[base, base + size)` with every half-open block
`[q * g * n + g * b, q * g * n + g * b + g)` for each non-negative integer
`q`. The entry base and size need not align to a complete stripe. The target
offset MUST align to `g`, and target range verification MUST use the exact
number of selected addresses, including partial first and last blocks and the
`2^64` endpoint.

The ACIR v0.1 verifier MUST bound general mixed-geometry selector analysis to
256 unique eligible relations per address map. A relation is eligible only
when its address ranges and permission/class selectors overlap and either
priority is absent or both explicit priorities are equal. Different explicit
priorities, equal interleave geometries, and selections that materialize as
zero or one finite interval MUST NOT consume this budget. The verifier MUST
deduplicate a pair reached through multiple selector keys, count against the
canonical normalized entry set, saturate at 257, and complete this preflight
before invoking general integer-relation analysis. If the count exceeds 256,
the verifier MUST emit the fixed diagnostic
`general mixed interleave analysis exceeds ACIR v0.1 limit 256`. This
capability diagnostic MUST NOT depend on input entry order.

## Processes and control flow

### `ac.process`

`ac.process` is a stateful resumable region owned by a module. Its kinds are:

- `control`: local scheduling and orchestration;
- `workload`: trace or workload injection;
- `monitor`: non-functional observation and checking.

It may contain permitted `scf`, `arith`, `index`, pure `func.call`, and ACIR
runtime operations.

Python loops that create topology execute during elaboration. Runtime loops may
remain as `scf` inside a process.

An `scf.for` is lowerable only when its lower bound, upper bound, and positive
step define an exact finite static trip count within the ACIR v0.1 capability
limit, or when every reachable backedge suspends. A dynamic non-suspending
`scf.for`, a non-positive static step, or a static trip-count overflow is a
hard verification error; there is no compatibility lowering.

Every `ac.process` body MUST be constructed and verified for allowed operations,
effects, ownership, suspension, and captured types before topology freeze.
Continuation lowering MUST run only after topology freeze, when captures,
hierarchy paths, and owned state are final. Continuation lowering MUST NOT add,
remove, rebind, or resize topology.

### Suspension

ACIR defines:

- `ac.wait_until`;
- `ac.wait_for`;
- `ac.await_event`;
- `ac.yield_sim`.

Communication uses non-blocking `ac.try_send`, `ac.try_recv`, and `ac.schedule`.
`ac.try_issue` is a frontend or standard-library convenience and is not a core
operation.

An `scf.while` loop MUST NOT busy-wait for simulation state. Process lowering
converts suspension points into explicit continuation state.

## Units and time

ACIR distinguishes cycles, physical time, bytes, bits, entries, packets,
transactions, bytes per cycle, and transactions per cycle.

Unit-bearing attributes reject incompatible arithmetic. A plain integer is not
accepted where the unit would be ambiguous.

Before emitting ACIR, Python specialization computes one exact positive
rational quantum shared by every declared physical time and domain period, then
scales every value to a non-negative integer global tick. Rounding, truncation,
floating-point approximation, and per-domain epsilon rules are forbidden. An
implementation publishes a finite maximum tick-scale capability and rejects a
model when exact common scaling exceeds it or would overflow a declared bound.

`ac.time_domain` has a concrete positive integer `period` in global ticks and a
concrete non-negative integer `phase` measured from global epoch tick `0`.
Domain cycle `n` occurs at global tick `phase + n * period`, using exact integer
arithmetic. A time domain does not imply an RTL clock signal. Cross-domain
topology requires an explicit bridge.

Only pure, stateless, effect-free operations may have zero delay. Queues,
resources, modules with state, processes, protocol state, storage, event
operations, and all other stateful operations introduce a tick boundary and
cannot publish a same-tick result. A structural Graph region MAY contain cycles,
but the verifier MUST reject every strongly connected component in the
zero-delay dependency graph that contains a cycle, including a self-loop.
Therefore every legal structural cycle contains a state or positive-delay
boundary. ACIR v0.1 defines no fixed-point combinational iteration.

## Effects

Runtime operations implement MLIR side-effect interfaces using abstract
resources for queue state, resource reservations, module state, storage state,
protocol state, trace position, event queues, external I/O, and statistics.

An operation with any such effect is stateful even when its accepted proposal
does not change state in a particular tick. An operation claiming to be pure
MUST have no effect on these resources and no hidden C++ state.

Passes MUST NOT reorder, duplicate, eliminate, or speculate effectful operations
unless their declared effects prove the transformation valid.

## Contracts and observation

ACIR supports `ac.require`, `ac.ensure`, and `ac.assert` for capacities,
protocols, latency, ordering, packet fields, ownership, address maps, and
topology constraints.

`ac.probe` observes internal state by stable hierarchy path without creating a
functional connection.

`ac.stat` declares counters, gauges, histograms, and event logs. Instrumentation
may be grouped in removable `ac.instrumentation` layers.

## Freeze invariants

After `ac-freeze-topology`:

- definitions, instances, collections, ports, SSA topology bindings, and model
  parameters are concrete;
- types, protocols, interfaces, and address spaces resolve;
- static parameters are concrete;
- each stateful runtime object has exactly one owner;
- hierarchy paths are assigned;
- address maps are deterministic;
- contested resources have an arbitration owner;
- no implicit fan-in or fan-out remains;
- every linear flow has at most one functional consumer;
- every endpoint and exclusive resource role satisfies its use cardinality;
- every shared resource role has one arbitration owner;
- no forbidden zero-delay cycle remains;
- dynamic operations cannot mutate topology.

## Public v0.1 inventory

### Structural operations

- `ac.system`
- `ac.type_scope`
- `ac.type_alias`
- `ac.module`
- `ac.module.extern`
- `ac.module.generated`
- `ac.instance`
- `ac.array`
- `ac.instances`
- `ac.view`
- `ac.port`
- `ac.return`
- `ac.queue`
- `ac.event_queue`
- `ac.resource`
- `ac.address_space`
- `ac.address_map`
- `ac.time_domain`

### Type and protocol declarations

- `ac.struct`
- `ac.enum`
- `ac.union`
- `ac.packet`
- `ac.transaction`
- `ac.interface`
- `ac.protocol`
- `ac.role`
- `ac.state`
- `ac.event`
- `ac.transition`
- `ac.guarantee`

### Executable operations

- `ac.process`
- `ac.record.create`
- `ac.record.get`
- `ac.record.with`
- `ac.packet.serialize`
- `ac.packet.deserialize`
- `ac.try_send`
- `ac.try_recv`
- `ac.schedule`
- `ac.wait_until`
- `ac.wait_for`
- `ac.await_event`
- `ac.yield_sim`
- `ac.trace.open`
- `ac.trace.next`
- `ac.trace.decode`
- `ac.trace.eof`
- `ac.trace.position`

### Contracts and observation

- `ac.require`
- `ac.ensure`
- `ac.assert`
- `ac.probe`
- `ac.stat`
- `ac.stat.add`
- `ac.instrumentation`

### Public types

- `!ac.struct<@name>`
- `!ac.packet<@name>`
- `!ac.transaction<@name>`
- `!ac.enum<@name>`
- `!ac.union<@name>`
- `!ac.optional<T>`
- `!ac.list<T>`
- `!ac.vector<N x T>`
- `!ac.flow<T, Protocol>`
- `!ac.endpoint<Interface, Role>`
- `!ac.resource_ref<ResourceType, Role>`
- `!ac.channel<T, Protocol>` (interface declarations only)
- `!ac.duration<unit>`
- `!ac.rate<numerator, denominator>`
- `!ac.event<T>`
- `!ac.address<@space>`
- `!ac.resource_token<@resource>`

### Public file attributes

- `ac.contract_epoch = "0.1"`

## Required verification

An ACIR verifier MUST diagnose:

- a missing or non-`"0.1"` `ac.contract_epoch`;
- unresolved or duplicate symbols;
- duplicate stable hierarchy paths;
- incompatible interface roles, types, or protocols;
- ambiguous protocol transitions;
- illegal packet mutation or lifetime;
- implicit fan-in or fan-out;
- a flow or exclusive endpoint/resource role with too many functional uses;
- a resource reference whose role, referent, or shared cardinality is invalid;
- a `!ac.channel` type outside an `ac.interface` channel declaration;
- multiple owners for a queue or resource;
- contested resources without arbitration ownership;
- unbalanced resource reservation paths detectable statically;
- ambiguous address-map overlap;
- an address map that exceeds the 256-relation general mixed-interleave
  verification capability;
- address width or address-space mismatch;
- incompatible or ambiguous units;
- unresolved template or generator bindings;
- any non-concrete model parameter at topology freeze;
- unbounded packet payloads without an explicit permitted bound;
- a zero-delay stateful operation or zero-delay pure strongly connected cycle;
- invalid process suspension state;
- side effects in protocol guards;
- probes used as functional dataflow;
- topology mutation after freeze.

## Determinism

For identical frozen ACIR, workload and trace inputs, seed, and initial state,
the generated simulator MUST produce identical architectural results and
statistics.

Host scheduling, pointer addresses, unordered-container iteration, and
filesystem enumeration MUST NOT affect simulation behavior.

## Versioning and extension

ACIR files identify their language version. Version `0.1` is experimental and
does not promise compatibility with later `0.x` versions.

New component families do not require new core operations. Domain extensions
SHOULD use namespaced interfaces, types, attributes, modules, and registered
verification interfaces before proposing new ACIR core constructs.

Unknown mandatory semantics MUST NOT be silently ignored.
