# Python-to-ACIR Lowering v0.1 Specification

| Field | Value |
| --- | --- |
| Specification | Agentic Python source semantics and lowering to ACIR |
| Version | 0.1 |
| Status | Draft for review |
| Source model | Restricted Python AST |
| Semantic intermediate form | `acpy` |
| Portable output | ACIR Core v0.1 |
| Global contract epoch | `0.1` |

## Purpose

This specification defines how ordinary-looking Python architecture code is
captured, validated, normalized, and lowered into ACIR. The intended surface is
assignment plus function call, with function signatures and lexical scopes
providing the information that lower-level construction APIs commonly express
as explicit input, output, and connection declarations.

The frontend is an executable architecture generation language, but it is not a
general Python-to-MLIR compiler. Only the subset defined here has portable ACIR
meaning.

This specification consumes [ACIR Core v0.1](acir-core-v0.1.md) and
[ACIR Standard Library v0.1](acir-stdlib-v0.1.md). Command behavior and
diagnostics conform to
[Agentic Python and CLI v0.1](agentic-python-cli-v0.1.md).

## Normative language

The words **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY** are
normative requirements when written in uppercase.

The public Python decorators, types, call syntax, and keyword names shown in
this specification are exact for the global `0.1` epoch. Implementations MUST
reject aliases or alternate spellings unless this specification lists them.
MLIR examples remain illustrative; their semantics are normative.

## Design principles

The frontend follows these principles:

- architecture dataflow is written as ordinary assignment and function call;
- Python statement order is elaboration order, not simulated execution order;
- module signatures and returns define public dataflow boundaries;
- lexical scopes may define real nested architecture hierarchy;
- static Python values specialize topology, component parameters, and generated
  code;
- symbolic values describe typed architecture `Flow`, `Endpoint`, and
  `ResourceRef` bindings;
- inferred topology never invents buffering, arbitration, replication,
  conversion, or timing behavior;
- all inference decisions are inspectable and source-mapped;
- the same accepted source produces the same canonical ACIR under the same
  declared environment.

The normal Python surface MUST NOT require explicit `ins()`, `outs()`, or
`connect()` calls.

## Authoring model

### System entry

An `@system`-decorated function is a selected architecture entry point. It uses
the same assignment, call, strong-scope, and return semantics as `@module`, but
it has no symbolic parent-facing inputs. Every `@system` parameter is a static
metaprogramming specialization input. Trace selection and simulator-harness
controls are run inputs supplied after build; they are not `@system` parameters
and are not model configuration.

The frontend lowers the specialized body to a root `ac.module` definition and
emits one `ac.system` that selects it. The system's returned values define the
declared simulation results; workload sources, memories, and other external
roles must be instantiated or bound through explicit system providers.

The `--system` CLI option resolves an `@system` symbol. Selecting an undecorated
function or a reusable `@module` directly is an error unless a future adapter
mode explicitly defines its missing root bindings.

### Module functions

An `@module`-decorated Python function defines a reusable hierarchical module.
Its annotated parameters define its public `Flow`, `Endpoint`, `ResourceRef`,
and static specialization parameters. Its annotated return defines its public
results.

```python
@module
def Accelerator(
    trace: TraceStream,
    memory: MemoryTarget,
) -> CompletionStream:
    decoded = TraceDecode(trace)
    scheduled = Scheduler(decoded, depth=16, policy="fifo")
    result = Compute(scheduled, lanes=4, latency=cycles(8))
    completed = DMA(result, memory, channels=2)
    return completed
```

Calling an `@module` function from another architecture function creates one
instance of its definition. It does not execute the module's simulated
behavior during elaboration.

Repeated calls create distinct instances:

```python
left = Compute(requests[0], lanes=2)
right = Compute(requests[1], lanes=2)
```

### External and generated definitions

`@extern_module` declares a typed signature and implementation binding but has
no structural Python body. It lowers to `ac.module.extern` after its signature
has been checked against a registered component schema.

`@generated_module` declares a signature resolved by a versioned static
generator. The generator receives only declared static inputs and emits a
deterministic definition or registered implementation selection. It lowers to
`ac.module.generated`; any generated structural body must pass through ACPy and
the same validation gates as handwritten source.

Neither form may inspect symbolic runtime values during generation.

### Component callables

Registered components are exposed as typed Python callables generated from
their `ComponentSchema`. A call has the abstract form:

```python
result = Component(flow, endpoint, resource_ref, *, static_param=value, name="id")
```

Symbolic flow, endpoint, and resource-reference arguments precede `*`. All
component and model parameters are static metaprogramming specialization inputs
and, together with the optional instance `name`, are keyword-only unless a
component schema explicitly defines a positional shorthand. Trace selection and
harness controls are run inputs and MUST NOT appear in a `ComponentSchema`
call signature.

One component call creates exactly one stateful instance unless its schema
declares the callable to be a pure value constructor. The frontend MUST expose
that distinction through schema discovery and type stubs.

### Function parameters

A module parameter is classified from its annotation and declaration:

| Category | Python meaning | ACIR meaning |
| --- | --- | --- |
| Flow | Symbolic immutable dataflow value | `!ac.flow<T>` module argument |
| Endpoint | Interface role handle | `!ac.endpoint<I, R>` module argument |
| ResourceRef | Typed reference to a shared or delegated capability | `!ac.resource_ref<R, Role>` SSA module argument |
| Static parameter | Elaboration-time immutable specialization input | Typed module specialization parameter |

`Flow`, `Endpoint`, and `ResourceRef` are the only public symbolic binding kinds.
`ComponentSchema.binding_kind` MUST classify every symbolic argument as exactly
one of these kinds and controls call binding and ACIR lowering. No model or
component parameter remains runtime-tunable after specialization.

Classification MUST be unambiguous before the function body is lowered.
Unannotated public parameters are errors in portable v0.1 source.

### Returns

`return` defines the module's public symbolic results. A module may return:

- no result with `return` or implicit fallthrough;
- one symbolic value;
- a statically shaped tuple of symbolic values;
- a named result record declared by the component or module schema.

All reachable paths in a module body MUST return compatible shapes and types.
Returning a stateful instance object is forbidden; the return value must be a
declared flow, endpoint, `ResourceRef`, or permitted immutable static value.

### Specialization

Each distinct combination of resolved generic types and static parameters
produces one specialized ACIR module definition. Calls with the same
specialization fingerprint may share that immutable definition but always
create distinct instances and runtime state.

The definition fingerprint is the hash of exact contract epoch, decorator and
callable identity, normalized captured AST, canonical annotations and defaults,
and declared helper/schema identities. It excludes comments, formatting,
non-semantic source paths, hierarchy paths, and host object identities.

The specialization fingerprint hashes the definition fingerprint, all
normalized static arguments, resolved public types, component-schema
fingerprints, helper/provider implementation fingerprints, and frontend build
identity. It excludes instance names and hierarchy paths. Two instances at
different hierarchy paths may therefore share generated specialization code
while retaining distinct ownership, state, object IDs, diagnostics, and probes.

Recursive specialization is permitted only when the frontend proves a finite
specialization graph and a statically bounded elaboration depth.

In this specification, Python JIT means on-demand topology selection, code
generation, and specialization-cache creation or reuse for the requested static
inputs. It does not mean that generated gfsim executes Python callbacks. The
cache key is the complete specialization fingerprint. All Python-dependent work
ends before build; the resulting C++ is fully specialized and has no runtime
model-configuration path.

### Multiple results

Multiple component results may be accessed by tuple destructuring:

```python
hit, miss = CacheLookup(request)
```

or through a generated immutable named result proxy:

```python
lookup = CacheLookup(request)
response = lookup.hit
miss = lookup.miss
```

Integer indexing is permitted only when the result schema is an unnamed fixed
tuple. Dynamic indexing of symbolic result bundles is forbidden.

### Strong scopes

`scope(name)` introduces a semantic hierarchy boundary:

```python
@module
def Chip(trace: TraceStream, memory: MemoryTarget) -> CompletionStream:
    decoded = TraceDecode(trace)

    with scope("frontend"):
        queued = Scheduler(decoded, depth=16)
        issued = Issue(queued, memory)

    return issued
```

The frontend MUST outline this block as a nested generated module. It is not
merely a naming prefix.

Scope boundaries infer their signature through capture and escape analysis:

- a symbolic value defined outside and used inside becomes a scope input;
- a symbolic value defined inside and used outside becomes a scope output;
- an internal-only value remains private;
- an immutable static capture becomes a specialized parameter or attribute;
- an `Endpoint` or `ResourceRef` capture becomes a typed SSA role or capability
  input;
- an effectful external object that has no declared ACIR role is rejected.

Scope inputs and outputs are ordered by first source use and first escaping
definition respectively, with source location as a deterministic tie-break.
The inferred signature MUST be visible through frontend inspection.

A fixed tuple, named result, or statically shaped collection crossing a scope
boundary is recursively decomposed into ordered symbolic leaves. The outlined
module exposes those leaves as ports or results, and the parent reconstructs an
immutable source-level bundle. Shape and field metadata remain in ACPy and
source maps. Dynamically shaped collections cannot cross a v0.1 scope boundary.

Nested `scope` blocks recursively create nested hierarchy. Moving a statement
across a strong scope boundary may therefore change ownership and canonical
paths and is not semantics-preserving by default.

## Value model

### Static values

Static values are fully known during elaboration. Portable v0.1 static values
include:

- `None`, booleans, integers, strings, and enums;
- ACIR types, symbols, and unit-bearing values;
- immutable tuples and frozen records of static values;
- statically bounded ranges;
- module and component declarations;
- explicitly declared policy and schema references.

Static values may control Python `if`, `for`, comprehensions, collection shape,
specialization, and name generation.

### Symbolic values

Symbolic values represent architecture objects that do not have a Python value
during elaboration. Their categories include:

- `Flow[T, Protocol]` for a logical typed dataflow edge;
- `Endpoint[I, Role]` for an interface role;
- `ResourceRef[R, Role]` for a resource capability reference;
- immutable named or tuple result bundles;
- process-local runtime SSA values inside `@process` only.

Symbolic values carry type, source location, owner scope, producer, use list,
and a stable frontend identity. Their Python object identity has no semantic
meaning and MUST NOT affect generated names or ordering.

`Flow[T, Protocol]` is a logical edge, not a queue. Both type arguments are
required in the public generic unless a named type alias supplies them.
Capacity, arbitration, latency, and protocol adaptation require explicit
components.

### Prohibited symbolic coercions

Portable module construction MUST reject:

- `bool(symbolic)` and symbolic conditions in Python `if` or `while`;
- arithmetic on symbolic values unless a declared pure ACIR value operation
  implements it;
- iteration over a symbolically sized value;
- hashing or dictionary-key use that depends on symbolic identity;
- implicit conversion to integer, string, bytes, or host collection;
- equality used as Python control flow rather than a declared value operation.

Diagnostics MUST identify the symbolic producer and recommend either static
specialization, an explicit component, or `@process` as appropriate.

## Supported Python subset

### Structural statements

An `@module` body may contain:

- annotated parameters and a declared return annotation;
- simple assignment and annotated assignment;
- tuple or fixed-record destructuring;
- expression calls whose effects are declared by their schemas;
- `return`;
- `with scope(static_name)`;
- statically decidable `if`;
- statically bounded `for` over deterministic iterables;
- list and tuple construction or comprehension with static shape;
- calls to approved pure elaboration helpers;
- `assert` over static values.

Reassignment is permitted as source syntax and normalized to distinct SSA
versions. Mutation of an already constructed architecture object is not
permitted unless an API explicitly models a static builder object whose entire
state is consumed before emission.

### Static control

Python control flow in an `@module` body is elaboration-time control. Conditions
and iteration domains MUST be static.

```python
units = [Compute(input_streams[i], lanes=lanes) for i in range(count)]

if enable_l2:
    output = Cache(units, size=l2_size)
else:
    output = Merge(units)
```

The selected branch and iteration count become part of the build fingerprint.

### Runtime control

Runtime conditions, loops, and state transitions belong in `@process` regions.
They lower to ACIR executable operations and standard MLIR `scf` in an SSACFG
region, including explicit suspension operations for cross-cycle waiting.

An `@module` Graph region and an `@process` SSACFG region are distinct semantic
contexts. Values MUST NOT cross between them except through declared ports,
state, queues, resources, events, or other core operations.

### Excluded constructs

Portable v0.1 module construction excludes:

- `async`, `await`, `yield`, and generators;
- `eval`, `exec`, dynamic code generation, and dynamic imports;
- dynamic `getattr`, `setattr`, monkey patching, and descriptor side effects;
- exception-driven architecture construction;
- symbolic Python `while` loops;
- recursion whose depth is not statically proven and bounded;
- nondeterministic filesystem, environment, clock, network, or process access;
- unordered iteration whose canonical order is not explicitly established;
- mutation or aliasing of symbolic collections after consumption;
- arbitrary Python calls without a registered purity or effect contract.

The trusted Python host may technically execute additional code during import.
Such behavior has no portable frontend meaning and remains subject to the CLI
security and determinism rules.

## Source capture and definition discovery

The frontend MUST parse the source AST for each selected decorated definition.
It MUST NOT derive semantics solely from executing overloaded Python operators.
AST capture is required for lexical scope inference, deterministic naming,
source repair, and distinguishing elaboration order from runtime dataflow.

Every captured node records:

- normalized source file identity;
- line and column span;
- enclosing module and scope path;
- stable lexical node index;
- optional macro or generated-wrapper provenance.

Decorators register definitions at Python import time. Elaborating a selected
system resolves the captured AST in an explicit environment containing its
parameters, permitted globals, imported schemas, and approved helpers.

If source text is unavailable, portable lowering MUST fail with a source-capture
diagnostic. A future bytecode frontend would be a distinct, versioned input
format.

## ACPy semantic IR

### Role

`acpy` is a source-oriented semantic IR between Python AST and ACIR. It exists
to make inference, normalization, validation, and diagnostics explicit.

ACPy is not the portable architecture interchange format, but its emitted JSON
is a public inspection and compiler-debugging contract. It MUST validate against
[`acpy.schema.json`](../../schemas/acpy.schema.json), declare exact global epoch
`"0.1"`, and evolve in lockstep with every other public surface. There is no
independent compatibility rule.

### Required information

Every ACPy entity records:

- source span and stable lexical identity;
- enclosing definition and scope;
- resolved static or symbolic type;
- symbol definition and use relationships;
- inferred ownership and effect information;
- links to the resolved component or module schema.

### Conceptual operations

ACPy v0.1 contains exactly these public entity kinds:

- `acpy.system` for a selected root entry;
- `acpy.module` for a captured module definition;
- `acpy.scope` for a strong lexical hierarchy boundary;
- `acpy.arg` for public or inferred arguments;
- `acpy.call` for a resolved component or module invocation;
- `acpy.result` and `acpy.get_result` for result bundles;
- `acpy.bind` for a source name bound to a value version;
- `acpy.static_if` and `acpy.static_for` before specialization;
- `acpy.collection` and `acpy.get_static` for fixed collections;
- `acpy.return` for public or outlined results;
- `acpy.capture` for a scope free variable;
- `acpy.escape` for a value leaving a scope;
- `acpy.process` for a separately validated runtime region.

The canonical JSON representation is normative. A human-readable textual
rendering is not normative and cannot be accepted as an alternate ACPy input.

### ACPy validation

Before lowering to ACIR, the frontend MUST validate:

- all names resolve in lexical scope;
- every expression is classified as static, symbolic, or invalid;
- static control is completely decidable;
- calls resolve to one callable schema;
- assignments and destructuring match result shape;
- captures and escapes are legal and typed;
- symbolic ownership and use rules are satisfiable;
- all retained effects have a declared ACIR representation;
- stable instance names can be assigned without collision.

The CLI MUST be able to emit ACPy in a deterministic machine-readable form.

## AST-to-ACPy conversion

### Expression evaluation

The converter evaluates static expressions in dependency order and represents
symbolic expressions as ACPy operations. It MUST preserve source spans through
constant folding, unrolling, and helper expansion.

Approved pure helpers may receive only static arguments unless their registered
lowering explicitly accepts symbolic arguments. Helper results must have a
declared static type and deterministic serialization.

### Assignment and SSA normalization

Each assignment target creates a new value version. For example:

```python
x = Decode(trace)
x = Filter(x, predicate="memory")
return x
```

is normalized conceptually to:

```text
x$0 = call Decode(trace)
x$1 = call Filter(x$0, predicate="memory")
return x$1
```

The user-facing source name remains available for diagnostics. Augmented
assignment on a symbolic value is rejected unless it is a declared pure value
operation and has no mutation semantics.

Complex nested expressions are converted to administrative normal form so that
every stateful call has a distinct operation and stable source identity.

### Call resolution

For each call, resolution proceeds in this order:

- resolve the callee to a registered module, component, pure value constructor,
  or approved helper;
- bind positional and keyword arguments to the callable schema;
- classify each symbolic value from `ComponentSchema.binding_kind` as `Flow`,
  `Endpoint`, or `ResourceRef`, and classify every other model argument as a
  static specialization parameter;
- solve generic types and static cardinality expressions;
- validate roles, protocols, units, ownership, and constraints;
- construct the declared result shape;
- assign a stable instance identity when the call is stateful.

Overload resolution MUST be deterministic. If more than one candidate remains,
the frontend reports ambiguity rather than selecting by registration order.

### Port and result inference

Port bindings are inferred from the resolved callable signature, not from
variable names. Python argument position or keyword binds a declared input
whose exact `ComponentSchema.binding_kind` is `flow`, `endpoint`, or
`resource_ref`. Results correspond to declared output ports in schema order and
retain their declared names.

A missing optional port is permitted only when its schema defines the
unconnected behavior. A missing required binding is an error. Extra values are
never silently dropped.

### Stable instance naming

Instance names are chosen by the first applicable rule:

- explicit keyword-only `name=`;
- a simple assignment target when the complete right-hand side is one stateful
  call;
- the callable's canonical short name plus its stable lexical node index and
  complete static expansion path.

Tuple destructuring does not name an instance from one arbitrary result; it
uses the final rule unless `name=` is present. Generated names MUST NOT use host
object identity, line text hashes, or traversal of unordered containers.

Name collisions in one ownership scope are errors. The frontend MAY suggest a
stable explicit name but MUST NOT silently renumber user-specified names.

### Collections

Every statically expanded instance collection MUST be canonicalized. A
homogeneous collection whose elements have the same definition and exact static
specialization lowers to `ac.array`. Every other fixed ordered collection lowers
to `ac.instances`, including collections with heterogeneous definitions or
specializations.

```python
workers = [Compute(inputs[i], lanes=2) for i in range(4)]
```

Canonicalization is semantic and mandatory, not an optimization. Lexical
expansion assigns each element the path segment of its source collection plus
its zero-based static index, such as `workers[0]`, before collection
canonicalization. `ac.array` and `ac.instances` MUST preserve those element
names, source mappings, index order, and canonical hierarchy paths.

A scalar symbolic value does not implicitly broadcast to a collection. A
`Broadcast`, `Fork`, `Map`, or other declared standard component is required.

## Scope inference and outlining

### Free-variable analysis

For each strong scope, the frontend computes lexical definitions, uses,
captures, effects, and escaping values after static unrolling and before ACIR
emission.

A captured symbolic value is legal only when its category can appear in a
module signature. A captured mutable Python object or undeclared external
effect is illegal.

### Escape analysis

A value escapes when it is used by an operation outside its defining scope or
returned by an enclosing module. Every escaping value becomes one outlined
result. Multiple external uses share that one result and remain subject to the
flow linearity rule after SSA binding materialization.

Escaping fixed bundles are flattened recursively according to their declared
field or index order and reconstructed in the parent. This flattening is a
lowering detail recorded in the outlined module schema; it does not create a merge,
broadcast, or runtime container.

Escaping a private state handle, instance handle, queue owner, or non-exportable
resource is an error. The source component must expose a declared output,
endpoint, or delegated resource role instead.

### Outlined definition identity

The generated module identity derives from the parent definition, lexical scope
path, static specialization fingerprint, and frontend version. Its readable
symbol SHOULD include the scope name; its stable identity MUST not depend on
build directory or host process state.

Two lexically distinct scopes with equal contents remain distinct hierarchy
boundaries unless an explicit deduplication mode proves path and observation
semantics are preserved.

### Ownership

Instances created inside a strong scope are owned by the outlined module.
Queues, resources, address maps, processes, and instrumentation created there
are also owned there unless their schema explicitly models delegation.

Outlining MUST diagnose ownership changes that would make a resource have
multiple owners or make a private object cross a module signature.

## ACPy-to-ACIR lowering

### Module graph region

An `acpy.module` lowers to an `ac.module` whose structural body is an MLIR Graph
region. Operation textual order preserves canonical source order for printing
and diagnostics but has no runtime scheduling meaning.

Conceptually, the earlier example lowers to:

```mlir
ac.module @Accelerator(
  %trace : !ac.flow<!ac.packet<@PtoRecord>>,
  %memory : !ac.endpoint<@MemoryPort, @target>
) -> !ac.flow<!ac.packet<@Completion>> graph {
  %decoded = ac.instance @decoded of @TraceDecode(%trace)
  %scheduled = ac.instance @scheduled of @Scheduler(%decoded)
    {depth = 16, policy = #ac.std.fifo}
  %result = ac.instance @result of @Compute(%scheduled)
    {lanes = 4, latency = 8 cycles}
  %completed = ac.instance @completed of @DMA(%result, %memory)
    {channels = 2}
  ac.return %completed
}
```

`ac.instance` results represent the declared symbolic output flows or exported
roles of the instance, not the mutable C++ instance object.

An `acpy.system` lowers to its specialized root `ac.module` plus `ac.system`.
External and generated declarations lower to `ac.module.extern` and
`ac.module.generated` respectively. All entry and definition forms use the same
resolved type, parameter, port, result, contract, and source-map conventions.

### Symbolic binding materialization

Each symbolic producer-to-consumer use forms a typed logical graph edge.
All three public binding kinds lower through Graph-region SSA results and
operands: `Flow` maps to flow SSA, `Endpoint` maps to endpoint-role SSA, and
`ResourceRef` maps to resource-reference SSA. The
`ComponentSchema.binding_kind` on the destination argument selects the required
SSA type and verification rules. Python lowering MUST NOT emit `ac.connect`;
there is no second connection representation to materialize.

Connection inference may select only a directly compatible binding. It MUST NOT
insert a queue, adapter, arbiter, merge, broadcast, fork, serializer, router, or
time-domain bridge. When direct compatibility fails, the diagnostic SHOULD name
the smallest applicable explicit standard component.

### Flow linearity

`!ac.flow<T>` is linear by default: one produced flow has at most one consuming
component or module result. MLIR Graph regions permit multiple SSA uses, so the
ACIR verifier MUST enforce this stronger semantic rule.

Multiple observation-only uses declared by probes do not consume a flow.
Multiple functional consumers require an explicit `Broadcast`, `Fork`, or other
component whose semantics define replication. Immutable static specialization
values may have multiple uses.

Fan-in likewise requires an explicit `Merge`, arbiter, interconnect, scheduler,
or other declared component. A Python list does not imply merging.

### Scope lowering

Each `acpy.scope` is outlined before final module graph verification:

- captures become ordered module arguments;
- escapes become ordered module results;
- internal calls become instances in the generated definition;
- the parent receives one `ac.instance` of that generated definition;
- source mappings retain both lexical and canonical hierarchy paths.

### Process construction and lowering

`@process` is the only exception that may describe behavior from which the
toolchain generates runtime control. Before topology freeze, the frontend MUST
construct and verify an `ac.process` owned by the surrounding module. Its body
uses an SSACFG region. Python AST validation for `@process` is a separate
context-sensitive subset: runtime branches and loops may lower to `scf`, while
cross-cycle behavior must use explicit ACIR suspension operations.

The frontend MUST reject an operation whose effect is legal in module
construction but illegal in a runtime process, or vice versa.

Within `@process`:

- local assignment lowers to process-local SSA and block arguments;
- runtime `if` lowers to `scf.if` when its yielded values have compatible types;
- bounded runtime iteration lowers to `scf.for`;
- runtime `while` lowers to `scf.while` only when the body can make progress or
  reaches an explicit suspension point;
- `break` and `continue` lower only when representable by the selected standard
  control-flow operation;
- queue, resource, storage, event, trace, and statistics calls lower to their
  declared effectful ACIR operations;
- `wait_until`, `wait_for`, `await_event`, and `yield_sim` create explicit
  suspension points and continuation state;
- topology constructors, strong scopes, instance arrays, and static parameter
  mutation are forbidden.

A process captures surrounding objects only through declared module-owned state,
ports, queues, resources, events, trace handles, or embedded static
specialization constants. The capture set becomes explicit operands or symbol
references in `ac.process`; arbitrary Python closure state is rejected.

An implementation MUST perform effect verification before applying ordinary
MLIR control-flow canonicalization. It MUST NOT duplicate, speculate, or reorder
queue, resource, storage, trace, event, or suspension operations across their
declared effect boundaries.

After topology freeze, each process containing suspension MUST lower to a
fully static C++ state machine with an explicit enum program counter and
specialized state fields. Generated C++ MUST NOT use C++ coroutines, a bytecode
interpreter, Python callbacks, or polling to implement suspension. Resumption is
scheduled by the relevant queue, event, resource, or time-domain mechanism.

A pure zero-delay operation remains an ACIR operation through verification, but
the C++ generator MUST inline it at its use sites. It MUST NOT allocate a
runtime object, receive a hierarchy path, or enter the simulator registry.

## Lowering pipeline

The normative logical stages are:

```text
Python source and registered schemas
  -> parse and capture decorated AST
  -> validate portable Python subset
  -> resolve static environment and specialize static control
  -> construct typed ACPy
  -> normalize assignments and calls to SSA/ANF
  -> resolve ComponentSchema call signatures and binding_kind values
  -> infer result bundles, captures, escapes, and ownership
  -> outline strong scopes
  -> validate ACPy
  -> create ACIR module Graph regions
  -> materialize Flow, Endpoint, and ResourceRef SSA bindings
  -> construct and verify ac.process regions
  -> canonicalize collections, lexical paths, and symbols
  -> verify ACIR Core
  -> freeze topology
  -> lower suspension to static enum-PC process state machines
  -> lower to ACSim
  -> inline pure zero-delay operations during fully specialized C++ generation
```

An implementation MAY combine stages internally, but diagnostics, inspection,
and stop points MUST preserve these logical boundaries.

## Validation and diagnostics

### Required diagnostic classes

The frontend defines at least these stable code families:

| Code family | Meaning |
| --- | --- |
| `ACPY-SYNTAX-*` | Unsupported or malformed Python construct |
| `ACPY-STATIC-*` | Non-static elaboration expression or nondeterminism |
| `ACPY-SYMBOL-*` | Name, SSA binding, or symbolic coercion error |
| `ACPY-CALL-*` | Callable resolution or argument binding error |
| `ACPY-TYPE-*` | Annotation, generic, unit, or result-shape error |
| `ACPY-SCOPE-*` | Illegal capture, escape, outlining, or ownership |
| `ACPY-NAME-*` | Unstable or duplicate instance identity |
| `ACPY-FLOW-*` | Implicit fan-in, fan-out, or incompatible direct edge |
| `ACPY-EFFECT-*` | Undeclared or context-illegal effect |

Each diagnostic MUST identify the Python source span. When available, it SHOULD
also identify the callable schema field, inferred hierarchy path, producer, and
consumer. Inference failures SHOULD show the attempted binding rather than only
the final ACIR type error.

### Inspectable inference

The frontend inspection form MUST expose:

- source names and normalized SSA versions;
- static values and specialization decisions;
- resolved callable identity and version;
- argument-to-port and result-to-port mappings;
- inferred scope inputs and outputs;
- instance names and ownership paths;
- collection canonicalization decisions;
- every materialized flow, endpoint, and resource-reference SSA binding;
- rejected implicit component requirements.

Canonical JSON is the normative inspection format. A human-readable rendering
and annotated source view are recommended.

## CLI integration

The CLI MUST support these frontend artifacts:

```text
agentic-circuit elaborate architecture.py --emit=acpy -o build/main.acpy.json
agentic-circuit elaborate architecture.py --emit=acir -o build/main.ac.mlir
agentic-circuit check architecture.py --stop-after=acpy-verify --json
```

`--emit=acpy` emits a deterministic, source-oriented representation suitable
for diagnostics and tests. It is not accepted as the default long-term input to
the compiler.

The build manifest records source hashes, the exact global epoch, Python
version, approved helper/provider versions, all specialization inputs, and the
ACPy and ACIR artifact hashes.

## Complete hierarchy example

```python
@module
def Cluster(
    requests: Flow[Request, ReadyValid],
    memory: Endpoint[MemoryPort, Target],
    *,
    lanes: Static[int] = 4,
) -> Flow[Completion, ReadyValid]:
    with scope("dispatch"):
        decoded = Decode(requests)
        split = Fork(decoded, outputs=lanes)
        scheduled = [
            Scheduler(split[i], depth=8, name=f"scheduler_{i}")
            for i in range(lanes)
        ]

    with scope("execute"):
        completed = [
            Compute(scheduled[i], lanes=1, latency=cycles(8), name=f"lane_{i}")
            for i in range(lanes)
        ]
        merged = Merge(completed, policy="round_robin")
        stored = DMA(merged, memory, channels=2)

    return stored
```

The resulting architecture has `dispatch` and `execute` as nested module
instances. `requests`, `lanes`, `memory`, `scheduled`, and `stored` cross only
the boundaries implied by capture and escape analysis. `Fork` and `Merge` are
explicit because replication and fan-in are architectural behavior.

## Acceptance criteria

The Python-to-ACIR frontend conforms to v0.1 when it can:

- lower assignment-and-call module code without visible input/output or
  connection builders;
- deterministically distinguish static and symbolic expressions;
- derive module interfaces from annotations and returns;
- derive strong-scope interfaces from captures and escapes;
- resolve component calls entirely from machine-readable schemas;
- preserve hierarchy, ownership, source maps, and stable instance names;
- represent structural modules as ACIR Graph regions and processes as SSACFG
  regions with explicit suspension;
- canonicalize every expanded collection to `ac.array` or `ac.instances` while
  preserving lexical index paths;
- lower `Flow`, `Endpoint`, and `ResourceRef` bindings only as ACIR SSA;
- generate fully specialized C++ with enum-PC suspension state machines and no
  coroutine, interpreter, polling, or runtime model configuration;
- reject implicit fan-in, fan-out, buffering, adaptation, and domain crossing;
- expose ACPy and all inference decisions as deterministic JSON;
- report frontend errors at Python source before C++ generation;
- emit equivalent canonical ACIR for equivalent deterministic source and
  declared specialization inputs.

## Non-normative design references

The assignment-and-call surface follows the useful part of
[Chisel functional module creation](https://www.chisel-lang.org/docs/explanations/functional-module-creation):
a module can present a function-like interface while still creating hardware or
architecture structure. Agentic Circuit does not adopt Chisel's RTL wire model.

The typed source-oriented intermediate form is informed by
[JAX tracing](https://docs.jax.dev/en/latest/tracing.html) and
[JAXPR](https://docs.jax.dev/en/latest/jaxpr.html), while requiring AST capture
and strong hierarchy semantics that ordinary tracing does not provide.

The structural/executable region split follows the distinction between
[MLIR Graph and SSACFG regions](https://mlir.llvm.org/docs/LangRef/). ACIR adds
flow linearity and architecture ownership rules beyond base MLIR SSA validity.

[Amaranth assignment semantics](https://amaranth-lang.org/docs/amaranth/v0.5.1/guide.html)
were reviewed as a counterexample for this abstraction level: ordered
last-assignment-wins behavior is appropriate for RTL signal construction but is
not the meaning of sequential Python statements in an ACIR module graph.
