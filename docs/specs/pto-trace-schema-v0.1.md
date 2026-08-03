# PTO Trace Schema v0.1 Specification

| Field | Value |
| --- | --- |
| Specification | PTO trace JSON and decoded runtime boundary |
| Version | 0.1 |
| Status | Draft for review |
| Schema identity | `pto-trace@0.1` |
| Global contract epoch | `0.1` |

## Purpose

This specification defines the exact trace input consumed by generated gfsim
executables. JSON parsing and schema validation are isolated in the trace
subsystem. Architecture components consume typed decoded transactions and MUST
NOT parse JSON directly.

## Exact schema contract

Conformance requires the exact schema identity `pto-trace@0.1`. Producers and
consumers MUST NOT negotiate a version range, infer compatibility, accept a
nearby version, or apply additive compatibility rules.

The canonical machine-readable envelope is
[`pto-trace.schema.json`](../../schemas/pto-trace.schema.json). This document
defines additional ordering, identity, cursor, and completion constraints that
JSON Schema cannot express.

Every accepted field, opcode, operand kind, attribute, and extension is declared
by the exact machine-readable v0.1 schema selected by the generated binary. An
unknown field or extension is a validation error, even if a consumer could
ignore it. A new optional field, new extension, or changed interpretation
requires a distinct declared schema identity and a global contract-epoch
increment.

## Document envelope

A trace document contains:

```json
{
  "schema": "pto-trace",
  "version": "0.1",
  "contract_epoch": "0.1",
  "metadata": {},
  "records": []
}
```

`schema`, `version`, `contract_epoch`, `metadata`, and `records` are required.
The epoch MUST equal `"0.1"`. Their JSON types and allowed members are closed by
the exact v0.1 schema. Duplicate object keys, numbers outside declared integer
bounds, non-canonical numeric forms where the schema requires integers, and
trailing non-JSON data are errors.

## Metadata

The exact v0.1 metadata object may declare fields defined by its machine-readable
schema, including producer identity, PTO ISA or IR identity, source program
identity, address spaces, data layout, record count, and content hash.

Metadata cannot override record semantics, timing semantics, dependency rules,
identity rules, or runtime behavior. There is no metadata mechanism for
declaring unknown mandatory or optional extensions.

## Root transaction identity

Each record represents one root transaction and MUST contain a `sequence_id`.
`sequence_id` is the canonical identity of that root transaction throughout
decode, scheduling, packets, resources, correlations, statistics, diagnostics,
and completion accounting.

Within one document, `sequence_id` values MUST be unique unsigned integers and
MUST appear in strictly increasing order. They need not be contiguous. A
producer MUST NOT provide a second competing root transaction identifier.

An implementation may expose aliases for presentation, but aliases cannot
replace, merge, reorder, or redefine `sequence_id`.

## Record representation

Each v0.1 record contains exactly the fields permitted by the machine-readable
schema. Its required semantic fields are:

- `sequence_id`: canonical root transaction identity;
- `opcode`: PTO operation or exact normalized operation kind;
- `operands`: ordered typed operand list;
- `dependencies`: ordered list of earlier root `sequence_id` values; and
- `attributes`: opcode-specific metadata with a closed declared shape.

The exact schema may declare source location, an integer issue-time constraint,
stream/thread/tile identity, packetization data, or an expected validation
result. Such fields have only their declared v0.1 meaning.

An illustrative conforming record shape is:

```json
{
  "sequence_id": 42,
  "opcode": "pto.matmul",
  "operands": [
    {"kind": "buffer", "id": "A"},
    {"kind": "buffer", "id": "B"},
    {"kind": "buffer", "id": "C"}
  ],
  "dependencies": [39, 40],
  "attributes": {
    "shape": {"m": 128, "n": 128, "k": 64},
    "dtype": "f16"
  },
  "source": {
    "file": "kernel.pto",
    "line": 17,
    "column": 3
  }
}
```

The example does not open the schema to fields not declared by the canonical
machine-readable definition.

## Operand, tile, and address representation

An operand has a required `kind` and the exact kind-specific fields declared by
v0.1. Initial kinds are `immediate`, `buffer`, `tile`, `address`, `symbol`, and
`record_result`.

Tile and shape data use explicit named dimensions, layout, and data type.
Dynamic dimensions carry concrete values in every affected record. Address
operands identify an ACIR address space declared by the generated model and the
trace metadata. An unresolved or ambiguous address space is a preflight error;
runtime name lookup or model configuration cannot repair it.

After decode, components depend on typed values rather than JSON field spelling.

## Dependencies

Dependencies express logical readiness between root transactions. Every
dependency value MUST equal the `sequence_id` of a record that appears earlier
in the same document. Self-dependencies, forward references, duplicate entries,
external dependencies, symbolic dependency identifiers, and dependency cycles
are invalid in v0.1.

The trace source does not issue a root transaction until every listed root
transaction has reached the dependency-complete state declared by the generated
workload model. Physical queues and resources may add further readiness
conditions but cannot weaken dependency ordering.

## Deterministic child identity

Packets, requests, beats, retries, responses, and other children derived from a
root transaction receive a deterministic child identity tuple:

```text
(sequence_id, static_component_path, phase, local_index, attempt)
```

`sequence_id` is the source record's canonical root identity.
`static_component_path` is the generated stable hierarchy path of the component
that creates the child. `phase` is the statically declared creation phase,
`local_index` is the zero-based ordinal assigned within that phase in committed
Xfer order, and `attempt` is the deterministic zero-based retry number.

The complete tuple is the canonical child identity. Hashes, shortened display
forms, generated object IDs, pointer values, host thread order, Work traversal
order, and speculative proposal order cannot define identity. A rejected
proposal does not consume a `local_index`; an accepted child receives its index
at committed Xfer. A retry retains the first four fields and increments only
`attempt`.

## Issue time

If v0.1 declares an issue-time field for an opcode, it is an exact non-negative
integer simulation tick. It is a lower bound on issue and never overrides
dependencies, resource legality, protocol legality, or the global epoch rules.
There is no runtime option that changes its interpretation. A contradictory or
out-of-range value is a validation error.

## Decoded runtime boundary

The trace subsystem produces an exact-version `PtoTraceRecord` containing the
canonical root sequence ID, normalized opcode, typed operands, earlier
dependency IDs, typed attributes, and any other explicitly declared v0.1 data.
`ac.trace.decode` maps it to a declared ACIR transaction type selected statically
by the generated `TraceSourceModel` decoder.

Only the trace subsystem handles JSON objects. Compute, storage, interconnect,
control, and protocol components receive typed transactions or packets.

## Unique cursor ownership and backpressure

Exactly one `TraceSourceModel` owns the cursor for a trace document. No other
object may decode independently, copy cursor authority, seek, or advance the
record stream.

The owner performs the following protocol:

1. Peek the next record without advancing the cursor.
2. Validate and decode it once into a typed root transaction offer.
3. Hold that exact offer and its identity unchanged while downstream
   backpressure rejects or delays transfer.
4. Advance the cursor exactly once when the offer is accepted and committed at
   the Xfer barrier.

Work reevaluation, a wake, a rejected proposal, a retry, a delta transition,
or elapsed simulation time MUST NOT advance the cursor. At most one uncommitted
root offer is associated with the cursor. End of trace becomes observable only
after the final record's transfer commits.

## Streaming and position

Implementations MAY stream the `records` array without materializing the whole
document, provided exact preflight guarantees are preserved. If full dependency
or count validation requires an initial pass, the implementation may spool or
index input deterministically before simulation; simulation state cannot
advance until preflight completes.

Trace position consists of the zero-based next record index, the most recently
committed root `sequence_id` if any, and whether end of trace has been reached.
Peeked but uncommitted records do not change this position.

## Validation

Before simulation, validation checks:

- exact schema identity and closed object members;
- JSON syntax, duplicate keys, and declared numeric bounds;
- required metadata and exact metadata member types;
- unique, strictly increasing root sequence IDs;
- closed opcode, operand, attribute, and source schemas;
- dependencies that resolve only to earlier records;
- bounded packet, operand, dimension, and list fields;
- address-space and data-layout agreement with the generated model;
- exact decoder and target transaction identity; and
- declared record-count and content-hash agreement.

Validation failures use stable `ACTRACE-*` diagnostic codes and include JSON
Pointer, root `sequence_id` when available, expected constraint, actual value,
and source location when declared.

## Validation caps

The generated binary declares finite caps for document bytes, nesting depth,
string bytes, record count, operands per record, dependencies per record,
attribute members, aggregate decoded bytes, and diagnostic count. These caps
are part of the exact consumer contract and are reported before simulation.

Exceeding a representation or safety cap is `failed`. If an explicitly declared
operator cap stops an otherwise valid bounded validation before it can decide
conformance, the run is `incomplete`; it MUST NOT be treated as valid or begin
simulation. Implementations MUST NOT silently truncate records, dependencies,
attributes, strings, diagnostics needed to identify the first failure, or
integer values.

## Versioning and extensions

`pto-trace@0.1` is a distinct local schema identity, but it participates in the
same global epoch as ACIR and the model library. A generated simulator declares
the exact identity tuple it accepts. v0.1 has no additive compatibility rule and
no facility for ignored unknown extensions.

A producer that needs a new field, opcode, operand kind, attribute, extension,
or semantic interpretation must increment the global epoch and replace the
declared schema identity and canonical machine-readable schema. A consumer
either supports that exact identity and epoch or rejects it during preflight.

## Completion and failure accounting

Trace exhaustion alone does not complete a run. The runtime classifies the run:

- `completed` when the final cursor advance committed, every accepted root
  transaction reached its declared terminal state, and the architecture is
  quiescent;
- `incomplete` when a declared execution or operator validation cap, or user
  interruption, stops work without a contract violation; or
- `failed` when trace syntax, schema, decode, dependency, identity, address,
  timing, cap-safety, or another contract check fails.

The result records the classification, exact cursor position, most recently
committed root sequence ID, first failing or unfinished root sequence ID when
known, and a structured diagnostic code.

## Acceptance criteria

The v0.1 trace boundary conforms when:

- the exact closed schema is validated before simulation;
- every root record has one canonical, strictly increasing `sequence_id`;
- dependencies refer only to earlier records in the same document;
- all derived children use the canonical deterministic identity tuple;
- a unique cursor owner holds offers across backpressure;
- the cursor advances only on committed Xfer;
- streaming produces the same records, diagnostics, and position as buffered
  input;
- architecture components consume typed transactions rather than JSON;
- validation caps cannot yield false acceptance or silent truncation; and
- trace state contributes correctly to `completed`, `incomplete`, or `failed`.
