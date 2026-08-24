# Agentic Circuit Queue/Var Architecture v0.2 Proposal Manual

| Field | Value |
| --- | --- |
| Specification | Queue/Var authoring, ACIR, gfsim, and PYC refinement proposal |
| Target version | `0.2` |
| Status | Design proposal; implementation is in progress and specified separately |
| Current upgrade-branch contract | `0.2` |
| Primary namespace | `ac` |
| Python frontend tracking | [Issue #9](https://github.com/PTO-ISA/agentic-circuit/issues/9) |
| ACIR and gfsim tracking | [Issue #10](https://github.com/PTO-ISA/agentic-circuit/issues/10) |
| PYC and Verilog tracking | [Issue #11](https://github.com/PTO-ISA/agentic-circuit/issues/11) |

## Status and authority

The executable candidate contract now lives in the
[Queue/Var v0.2 Specification Manual](agentic-circuit-v0.2.md). This proposal
remains the design rationale and future inventory. Where the two differ, the
implementation specification and its machine-readable sources take precedence.

This document records the Agentic Circuit `0.2` programming and lowering model
for design review and implementation planning. The upgrade branch now uses
exact contract epoch `0.2` and rejects epoch `0.1`; the implementation manual
and machine-readable contracts define the executable candidate surface.

The `0.2` proposal becomes normative only after the project lands matching
machine-readable schemas, MLIR ODS definitions, verifiers, runtime behavior,
tests, and an explicit global epoch change. Until then, examples in this manual
are illustrative proposed syntax.

The uppercase words **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and
**MAY** describe intended future requirements. They do not authorize a `0.1`
producer or consumer to emit or accept `0.2` constructs.

## Audience

Read this manual when working on:

- the high-level Python frontend and ACPy representation;
- ACIR Queue/Var types, official opcode definitions, and optimization passes;
- deterministic typed gfsim C++ generation;
- ACIR-to-PYC lowering and external `pycc` integration;
- gfsim-to-RTL refinement and cross-backend validation.

The current contracts remain the source of truth for existing code:

- [Python-to-ACIR Lowering v0.1](python-to-acir-lowering-v0.1.md)
- [ACIR Core v0.1](acir-core-v0.1.md)
- [ACIR Standard Library v0.1](acir-stdlib-v0.1.md)
- [ACSim and gfsim Lowering v0.1](acsim-gfsim-lowering-v0.1.md)
- [gfsim Runtime ABI v0.1](gfsim-runtime-abi-v0.1.md)
- [Interface Evolution v0.1](interface-evolution-v0.1.md)

The imported
[DavinciOO gfsim reference](../../examples/reference/davincioo-gfsim/README.md)
is a concrete topology and behavior oracle. It is not the proposed runtime
semantics. The
[reference generation contract](../../examples/reference/davincioo-gfsim/GENERATION_CONTRACT.md)
describes which parts should become generated topology or reusable policies.

## Goals

The `0.2` design has five primary goals:

- let users write high-level serial Python rather than explicit port wiring;
- infer a static queue-wired architecture from variable def-use and lexical
  scopes;
- make typed `SimQueue<T>` the fundamental stateful communication component;
- separate zero-latency combinational values from stateful queue channels;
- lower one frozen ACIR model to both deterministic gfsim C++ and circuit-level
  PYC IR.

The intended flow is:

```text
high-level serial Python
        |
        v
AST/JIT capture and ACPy
        |
        v
Queue/Var ACIR
        |
        +-------------------------+
        |                         |
        v                         v
ACSim / typed gfsim C++     canonical PYC IR artifact
                                  |
                                  v
                         external pinned pycc
                              |         |
                              v         v
                         PYC C++     Verilog
```

## Non-goals

The proposal does not:

- turn Python into a general-purpose dynamic hardware runtime;
- permit runtime topology mutation;
- permit users to define new ACIR opcodes or private backend providers;
- require gfsim and Verilog to have identical internal cycles or state;
- generate Verilog text directly from ACIR;
- copy the DavinciOO reference scheduler or mutable shared-pointer semantics;
- preserve source compatibility with `0.1`.

## Hard-break policy

Version `0.2` is a global hard break. Git history and release tags preserve
`0.1`; mainline code does not preserve aliases.

The transition MUST:

- increment the complete global contract epoch from `0.1` to `0.2`;
- rename `ac.std.Queue`, `ac.std.Memory`, and related identities to the unified
  `ac.queue`, `ac.memory`, and `ac.*` namespace;
- remove the conceptual distinction between standard and extension
  components;
- reject `0.1` artifacts with an exact epoch-mismatch diagnostic;
- update Python, ACPy, ACIR, ACSim, schemas, capabilities, bindings, manifests,
  generated C++, diagnostics, observations, and documentation in lockstep;
- provide no deprecated alias, compatibility shim, or nearest-version lookup.

## Core mental model

Users write serial-looking Python. The compiler interprets source order as
elaboration and dependency information, not as a request to execute Python
statements once per simulated cycle.

At the architecture graph level:

- a Python stream variable represents a typed queue;
- an official opcode represents a persistent hardware/model building block;
- queue operands and results describe static topology;
- lambda parameters and temporaries represent zero-latency values;
- lexical scopes infer module ownership and queue boundaries;
- runtime conditionals become routing structures;
- runtime loops become bounded feedback graphs.

Application concepts such as `decode`, `dispatch`, `rename`, and `retire` are
not universal ACIR opcodes. They are scope names, token functions, or
compositions of common hardware building blocks.

## Type system

### Payload types from Python structures

Python structures define the compile-time token layout carried by Queue and
Var values.

```python
@ac.struct
class WorkItem:
    value: ac.u32
    route: ac.u2
    remaining: ac.u16
```

The frontend MUST freeze:

- field names and canonical order;
- field types and bit widths;
- nested structure and array extents;
- alignment and serialization rules where required;
- a stable structural identity and fingerprint.

Instances are immutable. Field replacement creates a new value:

```python
next_item = item.with_fields(
    value=item.value + 1,
    remaining=item.remaining - 1,
)
```

The frontend MUST reject in-place mutation:

```python
# Illegal in the proposed subset.
item.remaining -= 1
```

### `ac.var<T>`

`ac.var<T>` represents an immutable, zero-latency combinational value.

It has:

- no capacity or occupancy;
- no push or pop operation;
- no backpressure;
- no value retention across ticks;
- no independent runtime object identity.

Lambda parameters, field projections, arithmetic results, predicates, mux
results, and newly constructed token values are Vars.

```text
!ac.var<i32>
!ac.var<!ac.struct<@WorkItem>>
!ac.array<4 x !ac.var<i16>>
```

The gfsim C++ backend normally emits Vars as local typed values or pure
expressions. PYC lowering emits them as wires, packed values, or combinational
operations.

### `ac.queue<T>`

`ac.queue<T>` represents a finite, typed, stateful token channel.

It has:

- compile-time payload type `T`;
- finite, compile-time capacity;
- latency of at least one tick;
- empty, full, occupancy, push, pop, and backpressure semantics;
- one stable runtime object identity and canonical path;
- committed statistics and observation behavior.

```text
!ac.queue<!ac.struct<@WorkItem>>
```

`ac.queue<T>` MUST NOT support latency zero. Zero-latency computation belongs
to `ac.var<T>` combinational regions. This rule makes the ACIR distinction
between wire-like logic and registered/FIFO state explicit.

### Mutable queue, immutable token

Queue state is mutable; queue payload is immutable.

```text
mutable:
  occupancy, head, tail, pending push/pop, committed state

immutable:
  every token T after construction
```

`peek` reads the immutable committed head without changing occupancy. `pop`
proposes removal of the head. `push` proposes addition of a new immutable
token. Xfer or the corresponding clock edge commits accepted state changes.

An implementation MAY use value copies, moves, an immutable arena, or an
internal reference-counted representation. These optimizations MUST NOT expose
mutable aliasing.

## Compile-time collections

Queue and Var values MAY appear in statically shaped, nested collections:

```text
array<queue<T>>
map<ConstKey, queue<T>>
set<queue<T>>

array<var<T>>
map<ConstKey, var<T>>
set<var<T>>
```

The following information MUST be known before frozen ACIR:

- array extents;
- map key types and the complete key set;
- set membership;
- nested collection shapes;
- element types.

For example:

```python
lanes = ac.array(
    4,
    lambda index: ac.queue(WorkItem, depth=8),
)

engines = ac.map({
    Engine.scalar: scalar_queue,
    Engine.vector: vector_queue,
    Engine.cube: cube_queue,
    Engine.tma: tma_queue,
})
```

Collection membership does not change queue ownership. The enclosing lexical
scope owns physical queues; the collection is a statically shaped aggregate of
handles.

### Static and runtime indexing

Static queue indexing resolves to a concrete queue:

```python
vector_queue = engines[Engine.vector]
lane_zero = lanes[0]
```

Runtime selection MUST NOT produce a dynamic queue pointer. The frontend
lowers it to an official selection, routing, or arbitration building block:

```python
selected = engines[item.route]
```

Conceptually becomes:

```text
ac.select_queue(
  inputs=engines,
  selector=item.route,
) -> queue<WorkItem>
```

Runtime indexing of a Var collection MAY become a combinational mux because
all values and collection members are already statically known.

Set iteration MUST use canonical member identity rather than host hash or
insertion order.

## Official opcode model

### Closed repository-owned inventory

Only the Agentic Circuit repository may define an `ac.opcode`. Users may
instantiate and compose official opcodes, and may write Python helpers that
expand into them, but may not introduce a new opcode identity or private C++,
PYC, or Verilog provider.

Every official opcode definition MUST declare:

- Queue and Var operand/result constraints;
- fixed or variadic port segments;
- payload type relationships;
- specialization constants;
- instance constants;
- state and effect behavior;
- `design`, `verification`, or `observation` role;
- gfsim semantics;
- PYC lowering availability;
- refinement observations;
- positive, negative, and round-trip tests.

The proposed conceptual signature is:

```mlir
ac.opcode @name<
  type parameters,
  const specialization parameters,
  const instance parameters
>(
  queue operands
) -> (
  queue results
)
```

Queue operands and results are SSA handles for instance-to-instance topology
connections; the complete topology remains compile-time static. Constants are
compile-time specialization or construction information. Runtime token data
lives in payload values.

### Common building-block categories

The final minimal inventory remains part of Issue #10. The catalog is expected
to cover these orthogonal categories:

| Category | Candidate building blocks |
| --- | --- |
| Transport and topology | queue, fork, broadcast, join, merge, route, select |
| Combinational token work | transform, predicate, reduce, scan |
| State | state, delay, counter, feedback, memory |
| Resource and scheduling | arbitrate, reserve, release, credit, barrier |
| Boundary | source, sink, observe, assert, probe |

Names in this table are proposals, not active operation spellings.

### Role classification

All opcodes belong to the unified `ac.*` namespace. Role describes hardware
placement, not standardness.

| Role | Meaning | PYC behavior |
| --- | --- | --- |
| `design` | Functional architecture/hardware state | MUST have an official hardware lowering |
| `verification` | Driver, sink, assertion, testbench behavior | MAY lower only outside the synthesizable design boundary |
| `observation` | Probe, statistics, trace | MUST NOT alter functional state or backpressure |

A synthesizable design containing a verification-only leaf MUST fail before
PYC emission or move that leaf to an explicit testbench boundary according to
the opcode contract.

## Python frontend semantics

### Serial source and graph construction

The frontend treats serial Python as a concise graph-construction language.
Each Queue-producing statement describes a persistent dataflow stage, not a
one-time Python function call.

```python
adjusted = source.apply(
    lambda item: item.with_fields(value=item.value + 1)
)
```

Conceptually maps to:

```text
source queue<WorkItem>
        |
        v
official transform building block
        |
        v
adjusted queue<WorkItem>
```

The queue names `source` and `adjusted` are stable logical identities. Lambda
parameter `item` and its arithmetic temporaries are Vars.

### Lexical scopes and inferred boundaries

`with ac.scope()` defines hierarchy and ownership without explicit input or
output declarations.

```python
source = ac.source(WorkItem)

with ac.scope("prepare"):
    adjusted = source.apply(adjust)
    classified = adjusted.apply(classify)

result = classified.apply(execute)
```

The frontend infers:

```text
scope prepare
  borrowed input queues: source
  owned local queues: adjusted
  exported output queues: classified
  child building blocks: adjust transform, classify transform
```

A scope output is any Queue defined in the scope and consumed outside it. A
scope input is any Queue defined outside the scope and consumed inside it.

### Lambda capture

A lambda describes token-level combinational work. Its parameters and captured
runtime Vars become inferred inputs. Captured compile-time values become
specialization or instance constants.

```python
bias = ac.const(7)

adjusted = source.apply(
    lambda item: item.with_fields(value=item.value + bias)
)
```

The lambda MUST NOT allocate queues, mutate topology, perform I/O, inspect
ambient process state, or mutate its input token.

### Static control flow

- `if` over Python constants is evaluated during elaboration.
- `for` over a compile-time range or collection is statically expanded.
- Python helpers may package repeated construction patterns but do not create
  new opcode identities.

```python
lanes = ac.array(
    LANES,
    lambda index: source.apply(
        lambda item: lane_transform(item, index),
    ),
)
```

### Runtime conditionals

A conditional over `ac.var` becomes combinational selection when both branches
produce Vars. When branches direct Queue tokens, it becomes routing and merge
structure.

```python
fast, slow = source.partition(
    lambda item: item.value < THRESHOLD
)
```

Conceptual graph:

```text
source queue
     |
     v
predicate var + route
   /            \
fast queue    slow queue
```

`partition` is illustrative frontend sugar. Frozen ACIR uses the official
common building blocks selected by the opcode catalog.

### Runtime loops

Structured runtime loops are supported only when the compiler can lower them
to a static, bounded feedback graph.

```python
current = source

while current.remaining > 0:
    current = current.apply(step)

result = current.apply(finalize)
```

Conceptual lowering:

```text
                  +----------------------+
                  |                      |
                  v                      |
input/feedback -> predicate -> continue queue -> transform
                      |
                      +-------> done queue
```

The feedback path MUST include at least one latency≥1 Queue. A runtime loop
MUST have statically bounded topology and resources. The frontend MUST reject
an unbounded recursive topology, a zero-latency feedback cycle, or dynamic
queue allocation.

### Multi-consumer queue variables

Queue pop is destructive. The frontend therefore performs use analysis.

- Repeated peek in one atomic rule reads the same immutable head.
- Observation-only use does not consume and does not backpressure.
- Multiple consuming uses insert a strict atomic broadcast.
- Explicit buffered fanout uses the official fork building block.

```python
left = source.apply(left_path)
right = source.apply(right_path)
```

Conceptually becomes:

```text
source
  |
strict atomic broadcast
  |                 |
left input       right input
```

The broadcast pops the source only when every output can accept the token. It
does not introduce hidden buffering.

## Queue effects and atomic firing

### Peek

`peek` returns the current committed head as an immutable Var and does not
change queue state.

Illustrative ACIR:

```mlir
%valid, %token = ac.queue.peek %input
  : !ac.queue<!ac.struct<@WorkItem>>
 -> (!ac.var<i1>, !ac.var<!ac.struct<@WorkItem>>)
```

### Pop and push

`pop` and `push` are queue-state effects. They propose changes during Work and
commit only at the atomic Xfer barrier or the corresponding clock edge.

```mlir
%pop_ok, %token = ac.queue.pop %input
  : !ac.queue<!ac.struct<@WorkItem>>
 -> (!ac.var<i1>, !ac.var<!ac.struct<@WorkItem>>)

%push_ok = ac.queue.push %output, %next
  : (!ac.queue<!ac.struct<@WorkItem>>,
     !ac.var<!ac.struct<@WorkItem>>)
 -> !ac.var<i1>
```

These spellings are illustrative. The final effect representation may use an
atomic region or firing-rule operation rather than public standalone pop/push
operations.

### Default atomic statement

Every Python statement that consumes Queue inputs and produces Queue outputs
forms one atomic firing rule by default.

```python
result = source.apply(transform)
```

Means:

```text
precondition:
  source is not empty
  result is not full

proposal:
  token = peek(source)
  next = transform(token)
  pop(source)
  push(result, next)

commit:
  pop and push both commit, or neither commits
```

### Explicit larger transaction

`with ac.atomic()` combines several statements into one firing rule.

```python
with ac.atomic():
    request = requests.pop()
    credit = credits.pop()
    response = execute(request, credit)
    responses.push(response)
    issued_count = issued_count + 1
```

The two pops, one push, and state update MUST all commit or all reject.

This mechanism generalizes the atomic Queue-to-Queue requirement tracked by
[Issue #8](https://github.com/PTO-ISA/agentic-circuit/issues/8).

## ACIR graph constraints

Frozen ACIR MUST satisfy:

- every Queue has one producer;
- every Queue has one consuming edge unless an official broadcast/fork contract
  says otherwise;
- multiple producers meet through an official merge or arbitration building
  block;
- every cycle contains at least one latency≥1 stateful edge;
- every collection has a compile-time shape and canonical member order;
- every effect belongs to one atomic firing rule;
- every stateful object has a stable ObjectId and canonical path;
- every design leaf resolves to an official repository-owned opcode;
- no Queue, module, opcode, or provider is selected through runtime strings.

## Intermediate optimization

### Var-level optimization

The compiler MAY optimize combinational Var regions with:

- constant folding;
- common-subexpression elimination;
- dead-expression elimination;
- struct field propagation;
- canonical select and mux folding;
- width and type normalization;
- combinational-cycle checking;
- logic-depth analysis.

These transformations preserve token values and do not alter Queue state.

### Queue-level optimization

A Queue is observable state, not an ordinary removable SSA edge. Queue-level
transformations MUST prove preservation of:

- token ordering;
- capacity and backpressure;
- latency and tick visibility;
- atomic firing behavior;
- deadlock behavior;
- committed observations and hierarchy identity.

The compiler MUST NOT fuse or delete a Queue merely because its producer and
consumer are adjacent in the graph.

### Canonical structural lowering

Before backend lowering, the optimizer freezes:

- scope hierarchy;
- static collection members;
- stable Queue and opcode IDs;
- activation adjacency;
- atomic firing groups;
- all payload and collection types;
- all specialization and instance constants.

## gfsim C++ lowering

### Runtime selection

The generated backend targets the Agentic Circuit deterministic runtime, not
the DavinciOO reference scheduler.

It preserves:

- immutable Work snapshots;
- proposal, arbitration, and Xfer phases;
- atomic multi-Queue commit;
- exact epoch and event scheduling;
- static dispatch and activation plans;
- stable identity and hierarchy;
- committed statistics and observations;
- Work-order permutation independence.

It adopts these structural patterns from the reference model:

- typed `SimQueue<T>` specialization;
- parent ownership of interconnect queues;
- child modules borrowing typed Queue references;
- template specialization by payload and static constants;
- queue-wired pop/compute/push module behavior.

### Generated payload

The Python structure:

```python
@ac.struct
class WorkItem:
    value: ac.u32
    route: ac.u2
    remaining: ac.u16
```

Conceptually generates:

```cpp
struct WorkItem {
  std::uint32_t value;
  std::uint8_t route;
  std::uint16_t remaining;
};

gfsim::SimQueue<WorkItem> input_q_;
gfsim::SimQueue<WorkItem> output_q_;
```

The exact C++ layout must follow the frozen ACIR payload contract rather than
ambient compiler padding.

### Generated scope and collection

An array of queues conceptually generates:

```cpp
std::array<gfsim::SimQueue<WorkItem>, 4> lane_queues_;
```

A static map SHOULD generate a dense array plus a compile-time key mapping,
not a hot-path `std::map`. A static set SHOULD become a canonically ordered
array or bit mask rather than a host-order set.

### Generated firing logic

One transform stage conceptually emits:

```cpp
void TransformStage::doWork(gfsim::Epoch epoch) {
  const WorkItem *input = input_q_->peek();
  if (!input || output_q_->isFull())
    return;

  WorkItem next = transform_(*input);
  auto transaction = beginAtomicFiring(epoch);
  transaction.pop(*input_q_);
  transaction.push(*output_q_, std::move(next));
  transaction.propose();
}
```

The code is illustrative. The final runtime API must preserve the same atomic
semantics and may use generated typed thunks instead of a dynamic transaction
object.

### Reference conformance

Issue #10 uses the checked-in DavinciOO reference to prove generated topology
and behavior. The initial gate consumes the 15-record softmax trace and checks:

- record count;
- opcode counts;
- completion and retirement behavior;
- architectural results;
- 453 simulated cycles when the ACIR latency contract matches the reference
  configuration;
- deterministic statistics and observations.

Any deliberate abstraction difference must have an explicit projection rather
than an unexplained mismatch.

## PYC and Verilog lowering

### External artifact boundary

Agentic Circuit owns `ACIR → PYC IR` lowering. The pyCircuit project owns PYC
verification, optimization, C++ simulation, and Verilog emission.

The first integration uses a versioned process boundary:

```text
frozen ACIR
    |
    v
canonical PYC IR file
    |
    v
external pinned pycc
    |              |
    v              v
PYC C++         Verilog
```

Agentic Circuit does not initially link pyCircuit libraries into its compiler
process. The build manifest records the PYC interface version, pyCircuit
commit, `pycc` build identity, input hash, output hashes, and validation gates.

### Type and operation mapping

| ACIR | PYC/RTL realization |
| --- | --- |
| `ac.var<T>` | combinational wire/value |
| immutable structure | fixed packed bundle |
| `ac.queue<T>` depth 1 | registered valid/data/ready channel |
| `ac.queue<T>` depth greater than 1 | FIFO/storage primitive |
| `array<var>` | packed or unpacked wire array |
| `array<queue>` | fixed channel/module array |
| static map | enum-indexed dense structure |
| static set | canonical tuple or bit mask |
| atomic firing | simultaneous handshake and clocked updates |
| route/select | mux, demux, or decoder |
| arbitrate | deterministic arbitration circuit |
| feedback Queue | sequential feedback path |
| state/counter | registers |
| memory | explicit PYC memory primitive |
| scope | PYC module and instance hierarchy |

### Combinational example

The ACIR Var transformation:

```text
next.value = item.value + 1
next.remaining = item.remaining - 1
```

Conceptually becomes Verilog combinational logic:

```verilog
wire [31:0] next_value;
wire [15:0] next_remaining;

assign next_value = item_value + 32'd1;
assign next_remaining = item_remaining - 16'd1;
```

The surrounding Queue maps to registered valid/data/ready behavior. The
combinational Vars do not create extra FIFO stages.

### Design, verification, and observation boundaries

- A design opcode MUST have an official PYC lowering.
- A verification opcode may lower only into the testbench boundary.
- An observation opcode may produce probes or trace logic but MUST NOT alter
  functional state or backpressure.
- Raw Verilog injection and private PYC providers are forbidden.

## Refinement model

The gfsim and PYC/Verilog backends use different internal IR details and may
have different internal cycle structure. They compare a declared semantic
projection.

The refinement gate compares:

- input transaction sequence;
- accepted and completed transaction identity;
- architectural state;
- memory-visible effects;
- completion and retirement order;
- output transaction sequence;
- declared assertion or error behavior.

It does not require equality of:

- internal Queue implementation;
- gfsim delta count;
- internal registers and wires;
- every internal stage cycle;
- abstract gfsim latency and detailed RTL pipeline latency.

PYC C++ and Verilog remain subject to pyCircuit's own cycle-level equivalence
contract because they consume the same circuit-level IR.

## End-to-end example

### Python source

The following example uses only high-level serial source, scopes, structures,
collections, lambdas, and control flow. The spelling is proposed and may change
before implementation.

```python
import agentic_circuit as ac


@ac.struct
class WorkItem:
    value: ac.u32
    route: ac.u2
    remaining: ac.u16


@ac.model
def worker_graph():
    source = ac.source(WorkItem)

    with ac.scope("prepare"):
        prepared = source.apply(
            lambda item: item.with_fields(value=item.value + 1)
        )

    with ac.scope("iterate"):
        current = prepared
        while current.remaining > 0:
            current = current.apply(
                lambda item: item.with_fields(
                    value=item.value * 2,
                    remaining=item.remaining - 1,
                )
            )

    with ac.scope("lanes"):
        lane_inputs = current.partition(
            outputs=4,
            key=lambda item: item.route,
        )
        lane_outputs = ac.array(
            4,
            lambda index: lane_inputs[index].apply(
                lambda item: item.with_fields(
                    value=item.value + index,
                )
            ),
        )

    merged = ac.merge(lane_outputs)
    ac.observe(merged)
    ac.sink(merged)
```

### Inferred hierarchy

```text
/worker_graph
  /source
  /prepare
    /prepared
  /iterate
    /predicate
    /continue
    /feedback
    /done
  /lanes
    /route
    /lane_inputs[0..3]
    /lane_outputs[0..3]
  /merge
  /observe
  /sink
```

### Inferred queue graph

```text
source
  |
prepare transform
  |
prepared
  |
loop predicate <------+ feedback queue
  |                    |
  +-> continue -> step-+
  |
  +-> done
        |
        route by item.route
       /       |       |       \
    lane0    lane1   lane2    lane3
       \       |       |       /
                 merge
                   |
             observe + sink
```

Every edge named as a Queue has latency at least one. Arithmetic and structure
updates inside each transform are Vars and remain combinational.

### Illustrative ACIR

The final ODS syntax remains to be frozen. This excerpt demonstrates intended
types and structure, not exact active assembly syntax.

```mlir
ac.type @WorkItem = !ac.struct<{
  value: i32,
  route: i2,
  remaining: i16
}>

%source = ac.source {
  depth = 8 : i64,
  latency = 1 : i64,
  role = #ac.role<verification>
} : !ac.queue<!ac.struct<@WorkItem>>

%prepared = ac.transform %source {
  output_depth = 8 : i64,
  output_latency = 1 : i64
} : !ac.queue<!ac.struct<@WorkItem>>
  -> !ac.queue<!ac.struct<@WorkItem>> {
^body(%item: !ac.var<!ac.struct<@WorkItem>>):
  %value = ac.get_field %item[value] : !ac.var<i32>
  %one = ac.var.constant 1 : !ac.var<i32>
  %next_value = ac.var.add %value, %one : !ac.var<i32>
  %next = ac.struct_with %item[value = %next_value]
    : !ac.var<!ac.struct<@WorkItem>>
  ac.yield %next
}

%lanes = ac.route %done {
  outputs = 4 : i64,
  key = #ac.field<route>
} : !ac.queue<!ac.struct<@WorkItem>>
  -> !ac.array<4 x !ac.queue<!ac.struct<@WorkItem>>>

%merged = ac.merge %lanes
  : !ac.array<4 x !ac.queue<!ac.struct<@WorkItem>>>
  -> !ac.queue<!ac.struct<@WorkItem>>
```

### Backend expectations

The gfsim backend emits typed Queue members, static collections, child modules,
atomic firing thunks, and activation adjacency. The PYC backend emits fixed
modules, queue channels, feedback storage, route logic, lane instances, merge
arbitration, and observation probes.

Both backends consume the same immutable `WorkItem` payload contract and
produce the same projected output transaction sequence.

## Illegal examples

### Dynamic topology

```python
# Illegal: queue count depends on runtime token data.
for _ in range(item.value):
    queues.append(ac.queue(WorkItem, depth=4))
```

The diagnostic should state that topology and collection shape must be known
before frozen ACIR.

### Mutable payload

```python
# Illegal: token mutation creates backend-visible aliasing.
item.value += 1
```

Use `item.with_fields(value=item.value + 1)`.

### Zero-latency Queue

```python
# Illegal: latency-zero logic must use ac.var.
q = ac.queue(WorkItem, depth=1, latency=0)
```

### Dynamic queue pointer

```python
# Illegal as a structural lookup when index is runtime data.
q = queues[item.route]
```

The frontend may accept equivalent syntax only when it lowers directly to an
official route/select/arbitrate opcode and never materializes a dynamic queue
handle.

### User opcode or backend injection

```python
# Illegal.
@ac.opcode
class PrivateQueue:
    ...

# Illegal.
ac.raw_verilog("assign ...")
```

Users compose repository-owned building blocks; they do not create backend
escape hatches.

### Partial queue transaction

```python
# Illegal semantics if output push may reject after input pop commits.
item = input_queue.pop()
output_queue.push(item)
```

The frontend must place this sequence in one atomic firing or reject it when
the statements cannot be proven to share a transaction.

### Combinational feedback

```python
# Illegal: no stateful latency edge.
value = value.apply(lambda item: item)
```

Runtime loops require an explicit or inferred latency≥1 feedback Queue.

## Diagnostics

Diagnostics should identify the source construct, inferred ACIR object, failed
rule, expected form, and a concrete repair. Examples:

```text
ACPY-QUEUE-001: runtime queue selection cannot produce a queue handle

The expression `engines[item.route]` uses runtime value `item.route` to select
topology. Queue topology must be compile-time fixed.

Use a routing expression that lowers to the official route/select building
block, or use a compile-time key.
```

```text
ACIR-FIRING-002: input pop and output push are not in one atomic firing

Queue `source` may commit a pop while queue `result` rejects its push.

Combine the operations in one statement or wrap them in `with ac.atomic():`.
```

```text
ACIR-CYCLE-003: feedback cycle has no stateful edge

The cycle `/iterate/value -> /iterate/value` contains only `ac.var`
combinational operations. Insert a latency>=1 Queue or remove the feedback.
```

## Determinism and security

Stable output MUST NOT depend on:

- Python hash iteration order;
- host pointer values;
- allocation order;
- C++ Work iteration order;
- ambient checkout path;
- wall-clock time or process ID;
- runtime plugin discovery;
- arbitrary Python or Verilog code execution during simulation.

Canonical ordering uses source occurrence, frozen collection order, stable
object identity, and declared opcode semantics.

Generated gfsim and PYC artifacts MUST contain no Python runtime dependency,
MLIR dependency, runtime schema walker, dynamic topology builder, or component
name dispatch branch.

## Conformance strategy

The implementation must provide machine-checkable coverage for:

- every public Python constructor and supported syntax form;
- every ACIR Queue/Var/collection type;
- every official opcode and role;
- every queue effect and atomic firing form;
- every control-flow lowering;
- every gfsim template specialization pattern;
- every ACIR-to-PYC mapping;
- positive, negative, determinism, replay, install, and refinement behavior.

The three tracking Issues own the delivery gates:

- [Issue #9](https://github.com/PTO-ISA/agentic-circuit/issues/9): Python,
  ACPy, inference, and frontend diagnostics;
- [Issue #10](https://github.com/PTO-ISA/agentic-circuit/issues/10): ACIR
  contract, optimization, gfsim runtime, and C++ generation;
- [Issue #11](https://github.com/PTO-ISA/agentic-circuit/issues/11): PYC
  artifact, external `pycc`, Verilog, and refinement gates.

## Open design items

The following details remain intentionally unresolved and must be frozen by the
tracking Issues before implementation claims conformance:

- exact official opcode names and the minimum orthogonal inventory;
- exact MLIR assembly syntax for Queue, Var, collections, and firing regions;
- exact Python spelling for transform, partition, route, feedback, and sink;
- whether Queue capacity and latency are type parameters, operation attributes,
  or constructor constants in each lowering stage;
- exact immutable payload ABI and packed layout rules;
- exact PYC interface version and canonical artifact envelope;
- exact observation schema used for gfsim-to-PYC refinement;
- exact loop bounds and termination proof requirements;
- exact diagnostic codes and migration report format.

These are open specification work, not permission to introduce local aliases or
backend-specific semantics. Each decision must update this manual, the
machine-readable contract, implementation, and tests together.
