# ACIR Standard Library v0.1 Specification

| Field | Value |
| --- | --- |
| Specification | Agentic Circuit Standard Component Library |
| Version | 0.1 |
| Status | Draft for review |
| Namespace | `ac.std` |
| Global contract epoch | `0.1` |
| C++ language contract | C++20 |

## Purpose

The ACIR Standard Library defines analyzable architecture component schemas and
their statically bound C++20 realizations. It provides reusable interfaces,
protocols, packets, component templates, and policies without extending ACIR
Core for each architecture component.

The library conforms to [ACIR Core v0.1](acir-core-v0.1.md), the
[Python-to-ACIR Lowering v0.1](python-to-acir-lowering-v0.1.md), and the
[gfsim Model Library Contract v0.1](gfsim-runtime-abi-v0.1.md).

The canonical machine-readable component record is
[`component.schema.json`](../../schemas/component.schema.json). This Markdown
specification defines the semantic constraints that JSON Schema alone cannot
express.

## Core and library boundary

ACIR Core defines universal semantics such as modules, channels, protocols,
queues, resources, address spaces, and processes. The Standard Library defines
conventional architecture modules composed from those semantics and registered
through frozen `ComponentSchema` records.

Compute units, schedulers, links, buses, crossbars, routers, DMA engines,
storage, caches, and protocol adapters are component families, not separate
ACIR Core operations.

A component MUST NOT hide a topology-affecting child, queue, route, shared
resource, activation condition, or wakeup condition when doing so prevents
connection, ownership, liveness, or cycle verification.

## Global contract epoch

Every schema, provider, compiler, generated source tree, and runtime participating
in one build MUST declare the exact global contract epoch `"0.1"`. Compatibility is
exact equality; epoch ranges, minimum versions, maximum versions, and additive
compatibility rules are forbidden.

Any change to a frozen component, interface, protocol, packet, policy, probe, or
statistics schema increments the global contract epoch. This includes adding an
optional field, component binding, parameter, result, port, protocol capability,
or guarantee. An epoch change is a hard break: artifacts from different epochs
MUST NOT be linked, loaded, interpreted, or run together.

Implementing a schema already frozen as `declared_unavailable` does not change
the schema and therefore does not increment the epoch. It changes only the
provider/build fingerprint and the component's build-profile availability.

## ComponentSchema contract

The canonical machine-readable `ComponentSchema` has exactly these top-level
fields; providers MUST NOT add provider-specific fields:

- `schema_kind`: the literal `agentic-circuit-component`;
- `schema_version`: the literal `0.1`;
- `contract_epoch`: the string literal `0.1`;
- `canonical_name` and `family`;
- `provider_namespace`;
- `stability`: `experimental`, `provisional`, or `stable`;
- `cpp_binding`;
- `static_parameters`;
- `bindings`;
- `results`;
- `resources`;
- `address_behavior`;
- `protocol_contracts`;
- `effect`;
- `activation`;
- `observation`;
- `schema_fingerprint`.

There is no `runtime_configuration` field. Unknown or duplicate fields are
schema errors. Empty sections MUST be represented by the section's canonical
empty array, object, or `null` value rather than omitted or filled with
placeholders.

### C++ binding

`cpp_binding` has exactly these fields:

- `header`: one repository-relative header path;
- `symbol`: one fully qualified C++ template or class name;
- `language`: the literal `c++20`;
- `concept`: the fully qualified C++20 concept the specialization satisfies;
- `toolchain_target`: the exact canonical toolchain target identifier selected
  by the build profile;
- `functional_policy`: `required`, `optional`, or `none`.

The provider MUST instantiate the declared symbol as ordinary C++20 and prove
the declared concept with a compile-time constraint. The schema fingerprint and
toolchain target MUST match the build manifest exactly. Dynamic plugins,
runtime-loaded descriptors, reflection-based descriptor interpretation, and
runtime component selection are forbidden.

### Static parameters

All model parameters are elaboration-time static. Each `static_parameters`
entry has exactly these fields:

- `name`;
- `acir_type`;
- `required`;
- `default`, which is `null` when no default exists;
- `constraint`;
- `cpp_mapping`: `template_argument`, `constexpr_argument`, or
  `constructor_constant`.

The compiler resolves and validates every parameter before topology freeze.
Generated C++ maps the normalized value to the declared template argument,
`constexpr` argument, or immutable constructor constant. A constructor constant
MUST NOT be mutated after construction. Runtime tuning of model parameters is
outside v0.1.

### Bindings

Each entry in `bindings` defines one Python-callable input and has exactly these
fields:

- `name`;
- `binding_kind`: `flow`, `endpoint`, or `resource_ref`;
- `acir_type`: one exact, fully resolved ACIR type;
- `direction`: `in`, `out`, or `inout`;
- `role`: the exact protocol, endpoint, or resource role;
- `cardinality`: one non-negative integer or normalized static expression;
- `linearity`: `linear`, `affine`, or `unrestricted`;
- `ownership`: `owned`, `borrowed`, or `shared`;
- `delegation`: `forbidden`, `allowed`, or `required`;
- `result_mapping`: the result name receiving the binding, or `null`.

Binding order is callable argument order. Static parameters and the reserved
instance `name` are keyword-only. Variable names, registration order, overload
guessing, and runtime descriptor interpretation MUST NOT influence resolution.

### Results

Each `results` entry has exactly these fields:

- `name`;
- `acir_type`;
- `source_binding`, naming a binding whose `result_mapping` names this result,
  or `null` for a value produced by the component;
- `linearity`;
- `ownership`.

The ordered result list maps to no result, one scalar, a fixed tuple, or a named
immutable result record. Stateful instance objects are never results.

### Functional policy and effects

A component may declare an optional static `FunctionalPolicy` template
parameter. Its permitted selections are:

- `NoFunctionalData`, for timing and structural simulation without functional
  payload state;
- `ReferenceFunctionalPolicy`, for checkable reference semantics;
- `OptimizedFunctionalPolicy`, for an implementation with the same declared
  functional contract and conformance results as the reference policy.

The selected policy is part of the specialization and provider/build
fingerprint. The schema fingerprint identifies the frozen set of permitted
policies and does not vary by selection. A policy is never selected at runtime.

`effect` has exactly these fields:

- `kind`: `pure` or `stateful`;
- `requirements`;
- `guarantees`;
- `observable_effects`;
- `failure_behavior`.

A `pure` declaration MUST lower inline and MUST NOT create a `SimObject`, own
runtime state, schedule an event, suspend, or expose activation/wakeup metadata.
A `stateful` declaration MUST bind a C++20 stateful component template, produce
a `SimObject`, and operate only through event-driven activation. Polling loops
and implicit periodic wakeups are forbidden.

For a stateful component, `activation` has exactly these fields:

- `sources`: the exact endpoint, queue, resource, or timer events that may
  activate the component;
- `predicate`: the static readiness predicate evaluated on activation;
- `wakeup_contract`: the exact events registered when the component suspends;
- `quiescence`: the condition under which no wakeup remains registered.

For a pure component, `activation` MUST be `null`.

### Resources, addresses, protocols, and observation

Resource declarations expose capacity, lanes, issue width, initiation interval,
latency policy, reservation owner, transaction classes, and exported
statistics. Address-aware components expose consumed and produced address
spaces, ranges, translation or interleave behavior, routing, and unmapped
behavior. Protocol contracts declare exact roles, ordering, delivery,
correlation, completion, and failure guarantees.

Observation declarations contain only frozen probe, counter, gauge, and
histogram schemas. Validation hooks may diagnose or terminate on a declared
violation but MUST NOT alter functional behavior.

### Exact schema example

The following record is complete; it contains no omitted fields or placeholders.
The fingerprint is the SHA-256 digest of the RFC 8785 canonical JSON record with
`schema_fingerprint` excluded from the digest input.

```json
{
  "schema_kind": "agentic-circuit-component",
  "schema_version": "0.1",
  "contract_epoch": "0.1",
  "canonical_name": "ac.std.Sink",
  "family": "workload",
  "provider_namespace": "ac.std",
  "stability": "provisional",
  "cpp_binding": {
    "header": "gfsim/components/sink.hpp",
    "symbol": "gfsim::std::Sink",
    "language": "c++20",
    "concept": "gfsim::StdStatefulComponent",
    "toolchain_target": "ac-gfsim-cxx20-v0.1",
    "functional_policy": "optional"
  },
  "static_parameters": [
    {
      "name": "FunctionalPolicy",
      "acir_type": "!ac.static_policy<ac.std.FunctionalPolicy>",
      "required": false,
      "default": "ac.std.NoFunctionalData",
      "constraint": "ac.std.FunctionalPolicy",
      "cpp_mapping": "template_argument"
    },
    {
      "name": "capacity",
      "acir_type": "index",
      "required": false,
      "default": 1,
      "constraint": "value >= 1",
      "cpp_mapping": "constructor_constant"
    }
  ],
  "bindings": [
    {
      "name": "input",
      "binding_kind": "endpoint",
      "acir_type": "!ac.endpoint<!ac.interface<ac.std.Stream<!ac.packet<ac.std.TraceTransaction>,ac.std.ready_valid>>,consumer>",
      "direction": "in",
      "role": "consumer",
      "cardinality": 1,
      "linearity": "linear",
      "ownership": "borrowed",
      "delegation": "forbidden",
      "result_mapping": null
    }
  ],
  "results": [],
  "resources": [
    {
      "name": "acceptance",
      "class": "queue",
      "capacity": "capacity",
      "lanes": 1,
      "issue_width": 1,
      "initiation_interval": 1,
      "latency_policy": "ac.std.FixedLatency<0>",
      "reservation_owner": "self",
      "transaction_classes": ["ac.std.TraceTransaction"],
      "statistics": ["accepted_transactions", "stalled_cycles"]
    }
  ],
  "address_behavior": {
    "consumes": [],
    "produces": [],
    "ranges": [],
    "translation": "none",
    "routing": "none",
    "unmapped": "not_applicable"
  },
  "protocol_contracts": [
    {
      "protocol": "ac.std.ready_valid",
      "role": "consumer",
      "ordering": "fifo",
      "delivery": "exactly_once_on_transfer",
      "correlation": "none",
      "completion": "transfer",
      "failure": "protocol_violation"
    }
  ],
  "effect": {
    "kind": "stateful",
    "requirements": ["input packet remains stable until transfer"],
    "guarantees": ["each accepted packet is consumed exactly once"],
    "observable_effects": ["accepted_transactions", "stalled_cycles"],
    "failure_behavior": "terminate_with_diagnostic"
  },
  "activation": {
    "sources": ["input.transfer", "acceptance.space_available"],
    "predicate": "input.transferable && acceptance.has_space",
    "wakeup_contract": ["input.transferable", "acceptance.space_available"],
    "quiescence": "input.closed && acceptance.empty"
  },
  "observation": {
    "probes": [],
    "counters": ["accepted_transactions", "stalled_cycles"],
    "gauges": ["queue_occupancy"],
    "histograms": []
  },
  "schema_fingerprint": "sha256:6e329477b42a756c1231326ac3589b067e46ae460c53c2b84ebf537964ce8cbf"
}
```

## C++20 realization contract

Each available component resolves to one schema-declared C++20 template or
class specialization. Generated code includes the declared header, supplies all
template and `constexpr` arguments, supplies immutable constructor constants,
and checks the declared concept. No generated descriptor tables or runtime
schema walkers are used to create components.

The compiler records the exact schema fingerprint, provider/build fingerprint,
toolchain target, normalized static arguments, selected policies, and generated
specialization in the build manifest. Any mismatch is a hard build or preflight
failure.

## Extension ladder

Extensions use the following ladder, stopping at the first level that can
express the required semantics:

1. Composite ACIR module assembled from frozen components and core semantics.
2. Reusable C++20 component template or static policy with a complete frozen
   `ComponentSchema`.
3. Runtime primitive, only when neither composite ACIR nor a reusable component
   or policy can express the required scheduling or state semantics.

There is no emitter-extension branch. Source emission is an implementation
detail of the compiler and is not a component extensibility surface.

Third-party providers use names such as `vendor.package.ComponentName` and MUST
NOT register definitions in `ac.std`. A third-party schema still uses global
contract epoch `"0.1"`; any schema evolution waits for and participates in the next
global epoch.

## Static build profiles

A build profile is selected before schema resolution and is immutable for the
build. v0.1 profiles are `fast`, `validated`, and explicit `custom`. A profile
declares the exact toolchain target, provider/build fingerprint, available
component implementations, enabled functional-policy implementations,
instrumentation layers, validation hooks, and compile/link options. A `custom`
profile retains every mandatory `fast` verification and runtime check.

Profiles MAY statically enable hooks for queue bounds, resource reservation,
protocol legality, lifetime, address routing, credit conservation,
request-response correlation, event-time monotonicity, deterministic
arbitration, and no-progress diagnostics. Profiles MUST NOT change frozen
schemas, component semantics, callable signatures, or topology.

## Shared standard types and protocols

The epoch `0.1` catalog freezes schemas for:

- `ac.std.TileShape` and `ac.std.TileDescriptor`;
- `ac.std.ComputeRequest` and `ac.std.ComputeResponse`;
- `ac.std.MemoryRequest` and `ac.std.MemoryResponse`;
- `ac.std.DmaRequest` and `ac.std.DmaCompletion`;
- `ac.std.RouteHeader`, `ac.std.Status`, and `ac.std.TraceTransaction`.

Tile is a payload/schema concept, not a component family.

The available epoch `0.1` protocols are `ac.std.ready_valid` and
`ac.std.request_response`. Ready-valid transfers one immutable packet exactly
once when producer validity and consumer readiness coincide; an unaccepted
packet remains stable and producer-owned. Request-response defines requester and
responder roles, correlation, maximum statically configured in-flight count,
completion, ordering, and failure behavior.

Schemas for `ac.std.fifo_push_pop`, `ac.std.credit`,
`ac.std.fire_and_forget`, and `ac.std.event_notification` are frozen but are
`declared_unavailable` in the initial build profile.

## Shared standard interfaces

The catalog freezes the parameterized schemas:

- `ac.std.Stream<T, Protocol>`;
- `ac.std.RequestResponse<Request, Response, Protocol>`;
- `ac.std.MemoryPort<Request, Response>`;
- `ac.std.ComputePort<Request, Response>`;
- `ac.std.ControlPort<Command, Status>`;
- `ac.std.EventPort<Event>`;
- `ac.std.TracePort<Transaction>`.

All type and protocol arguments resolve statically before topology freeze.

## Two-tier component catalog

Epoch `0.1` freezes complete schemas for every component named below, independent
of implementation availability. The initial executable baseline is:

- `ac.std.TraceSource`;
- `ac.std.Queue`;
- `ac.std.Scheduler`;
- `ac.std.Compute`;
- `ac.std.Link`;
- `ac.std.Memory`;
- `ac.std.Sink`;
- `ac.std.ready_valid`;
- `ac.std.request_response`.

Each baseline component MUST pass schema, C++20 concept, compile/link,
determinism, activation/wakeup, protocol, and family-specific conformance tests
in a profile before that profile marks it `available`.

The initial build profile records the following frozen component schemas as
`declared_unavailable`:

- control: `Arbiter`, `Dispatcher`, `Scoreboard`, `DependencyTracker`;
- interconnect: `Bus`, `Crossbar`, `Router`, `Switch`;
- transport: `Dma`, `Packetizer`, `Reassembler`, `LoadStore`;
- storage: `RegisterFile`, `Scratchpad`, `Cache`, `Tlb`,
  `MemoryController`;
- synchronization: `Fork`, `Join`, `Broadcast`, `Barrier`;
- adaptation: `ProtocolAdapter`, `WidthAdapter`, `TimeDomainBridge`,
  `AddressTranslator`, `MemoryManagement`;
- workload: `TrafficSource`.

Instantiation of a `declared_unavailable` component or protocol MUST fail during
schema resolution with a hard availability diagnostic. The compiler MUST NOT
substitute another component, generate a stub, defer failure to link time, or
interpret a runtime descriptor.

When an implementation passes conformance, a build profile may mark that frozen
schema `available`. This availability is represented only by the changed
provider/build fingerprint; the schema and schema fingerprint remain unchanged.
If implementation requires any schema modification, it waits for a global
contract epoch increment.

There is no requirement that one provider implement the complete declared
catalog. Conformance is claimed for a specific build profile and its explicit
available implementation set.

## Standard policies

The catalog freezes static policy schemas for FIFO, round-robin,
fixed-priority, weighted-round-robin, and age-based arbitration; FIFO,
priority, dependency-aware, and oldest-ready scheduling; fixed, linear-byte,
and table latency; direct, address-range, table, and XY routing; single-bank and
interleaved storage; and direct-mapped, set-associative, FIFO-replacement, and
LRU-replacement cache behavior.

A policy is a C++20 type, template argument, or `constexpr` value satisfying its
declared concept. Callback objects and runtime policy selection are forbidden.
Policies without a conforming implementation remain `declared_unavailable`.

## Standard observations

Where applicable, frozen schemas use these shared names:

- `accepted_transactions`;
- `completed_transactions`;
- `rejected_transactions`;
- `stalled_cycles`;
- `busy_cycles`;
- `queue_occupancy` and `queue_occupancy_peak`;
- `bytes_transferred`;
- `arbitration_conflicts`;
- `protocol_violations`;
- `latency_cycles`.

Their types and meanings are part of the epoch contract. Adding or changing an
observation requires a global epoch increment.

## Acceptance criteria

The epoch `0.1` Standard Library conforms when:

- every catalog entry has one complete, unique, machine-readable frozen schema;
- all artifacts declare global contract epoch `"0.1"` and exact schema,
  provider/build, and toolchain fingerprints;
- every available component binds statically to its declared C++20 symbol and
  concept with all parameters resolved before topology freeze;
- pure components lower inline without `SimObject` creation;
- stateful components pass activation/wakeup and event-driven execution checks;
- the initial executable baseline and ready-valid/request-response protocols
  pass conformance in the selected build profile;
- unavailable instantiation fails during schema resolution;
- generated gfsim preserves hierarchy, ownership, deterministic behavior,
  validation hooks, and frozen observations;
- generated binaries contain no dynamic component plugin loader or runtime
  descriptor interpreter.
