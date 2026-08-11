# Phase 5 End-to-End Models, PTO Trace Import, and NPU Design

**Status:** Implemented

**Implementation audit:**
[`phase-5-audit.md`](../../implementation/phase-5-audit.md)

**Date:** 2026-08-12

**Contract epoch:** `0.1`

## 1. Purpose

Phase 5 proves the complete Agentic Circuit toolchain with executable models.
It adds a deterministic importer for the PTO CPU-simulation JSONL format used
by DavinciOO, closes runtime statistics and event output, establishes six small
end-to-end golden architectures, and implements the hierarchical superscalar
NPU showcase required by the roadmap.

The public simulator continues to consume only `pto-trace@0.1`. DavinciOO JSONL
is an external source format converted before runtime preflight; it is never a
second accepted simulator wire format.

## 2. Normative authority and scope

The following existing contracts remain authoritative:

- `docs/specs/pto-trace-schema-v0.1.md` and
  `schemas/pto-trace.schema.json` define the only public trace document;
- `docs/specs/gfsim-runtime-abi-v0.1.md` defines committed scheduling,
  statistics, termination, and determinism;
- `docs/specs/acir-stdlib-v0.1.md` defines the component and observation
  inventory;
- `docs/specs/agentic-python-cli-v0.1.md` defines the exact ten-command CLI;
- `docs/specs/interface-evolution-v0.1.md` forbids additive public changes
  without a global epoch increment; and
- `docs/superpowers/plans/2026-08-04-agentic-circuit-roadmap.md` defines the
  Phase 5 examples, NPU showcase, swimlane output, and exit gate.

Phase 5 does not add a public command, option, schema property, component name,
diagnostic catalog entry, or runtime input spelling. Existing catalog entries
may change from `declared_unavailable` to `available` only when their frozen
schemas and semantics remain unchanged, as permitted by the standard-library
contract.

## 3. External trace authority

The imported source format is the PTO CPU-simulation trace writer pinned by
DavinciOO `origin/main` commit
`e73633301cabed0d871ea5ff66e76a91df870aeb`, whose PTO-ISA submodule pins
commit `f6d0567c1cae2d6a7b0ebaf7ad0e3b93f8a39da3`.

The source is UTF-8 JSON Lines. Each non-empty line is one closed instruction
record with exactly these fields:

- `block_idx`: unsigned 64-bit integer;
- `sequence_id`: unsigned 64-bit integer;
- `opcode`: non-empty PTO operation spelling;
- `input_tiles`: ordered tile-descriptor array;
- `scalar_inputs`: ordered scalar-descriptor array; and
- `output_tiles`: ordered tile-descriptor array.

A tile descriptor contains exactly `address`, `shape`, `layout`, and `dtype`.
`address` is a canonical hexadecimal unsigned 64-bit integer, `shape` is a
bounded integer array, and `layout` and `dtype` are non-empty strings. A scalar
descriptor contains exactly `dtype` and string `value`. Source values that
cannot cross the canonical target's portable I-JSON integer boundary are
rejected explicitly; the importer never rounds a semantic integer.

The importer pins this shape rather than accepting whatever a future DavinciOO
checkout happens to emit. Importing a changed external shape requires an
explicit importer update and new provenance evidence; runtime trace acceptance
does not change.

## 4. Importer architecture

The importer is a repository development and fixture-generation tool under
`tools/`. It is not installed, exported from `agentic_circuit`, listed in
capabilities, or added to the public `agentic-circuit` parser.

Reusable conversion logic lives in an internal Python module so tests can call
it without a subprocess. It uses only the standard library and the existing
canonical JSON implementation. The command accepts one input path, one output
path, and an optional stable source-program identity. It emits exactly one
canonical `pto-trace@0.1` JSON document.

The importer performs bounded duplicate-aware JSON parsing, validates every
record before conversion, constructs the complete output in memory, validates
the canonical output against the repository trace contract, writes a private
sibling stage, and atomically replaces the requested output. Importer tests
also pass the emitted bytes through the native gfsim trace parser. Failure
leaves an existing output byte-for-byte unchanged.

### 4.1 Exact record mapping

Each source line maps to one canonical record:

| Canonical field | Mapping |
| --- | --- |
| `sequence_id` | Preserved exactly. Values must be unique and strictly increasing. |
| `opcode` | Preserved exactly after spelling validation. |
| `operands` | Input tiles, scalar inputs, then output tiles, preserving each source order. |
| `dependencies` | Empty. The source format carries no logical dependency list; NPU rename and issue add physical readiness constraints. |
| `attributes.davincioo` | Lossless normalized `block_idx`, tile arrays, scalar array, and parallel operand roles. |
| `issue_time` | Omitted. |
| `source` | Omitted. |

Every tile operand becomes `{"kind":"tile","id":...}`. Its ID is derived
only from the tuple `(block_idx, normalized_address)` using the spelling
`block/<decimal>/tile/<lowercase-hex>`. Input and output references to the same
tuple therefore share identity. The complete address, shape, layout, dtype,
direction, and ordinal remain in `attributes.davincioo`.

Every scalar operand becomes an `immediate` whose `type` is the source dtype and
whose `value` remains the source string. The importer does not guess enum,
floating-point, signedness, or width semantics that are absent from the source
format.

### 4.2 Runtime handoff

Run preflight parses and validates the canonical document once, then moves the
typed `PtoTraceDocument` into the generated model's single statically declared
trace owner before simulation begins. The generated model declares that owner
in static metadata; a non-empty trace with zero owners or any model with more
than one owner fails before state advances. Components beyond the trace source
receive typed records or typed decoded transactions and never receive JSON.

The canonical metadata contains:

- `producer`: the exact DavinciOO producer commit;
- `pto_identity`: the exact pinned PTO-ISA trace-emitter commit;
- `source_program`: a caller-provided stable identity or the raw-input SHA-256;
- `data_layout`: `davincioo-tile-address-v1`;
- `record_count`: the exact number of imported records; and
- `content_hash`: the canonical SHA-256 of the `records` array.

Absolute paths, timestamps, process IDs, checkout locations, and ambient git
state never enter output.

## 5. Runtime observation closure

Phase 4 already exposes `--stats-format json` and `--event-log jsonl`, but the
harness currently publishes empty placeholders. Phase 5 connects those
declared outputs to committed runtime state without adding another output
selection.

Statistics are collected from `SimSystem` after termination and serialized in
the existing stable order `(object_path, statistic_name)`. Counter, gauge, and
histogram fields use the existing `StatSnapshot` contract. Observation cannot
mutate functional state.

Runtime events are proposed during Work and admitted only during Xfer. Each
committed event receives the deterministic key:

```text
(epoch.time, epoch.delta, stable_object_id, local_committed_index)
```

Rejected proposals consume no index. The event log is ordered by that key and
contains Chrome Trace Event compatible objects. Exact simulation time and delta
remain in event arguments; the presentation timestamp is a deterministic
integer derived from the epoch and never from wall-clock time.

The runtime records only already-declared activity: accepted and completed
transactions, queue/scheduler occupancy, stalls, execution intervals,
dependency wakeups, memory activity, and retirement. Object paths come from
the frozen hierarchy.

A repository-local packer wraps the JSONL objects as a Perfetto-compatible
`{"traceEvents":[...]}` document. It performs no inference or reordering. The
public run publication remains `events.jsonl`; the Perfetto document is a
derived example/audit artifact.

## 6. Phase 5A golden examples

Six checked-in examples under `examples/phase5/` exercise distinct contracts:

1. **Producer / queue / consumer** — basic ready-valid flow, queue occupancy,
   completion, and trace exhaustion.
2. **Backpressured pipeline** — retained offers, bounded capacity, rejected
   proposals, and exact cursor advancement.
3. **Request / response memory** — correlation identity, memory latency,
   response ordering, and address validation.
4. **Nested arrays** — deterministic collection expansion, hierarchy paths,
   dense object IDs, and independent lanes.
5. **Multi-time-domain bridge** — exact rational domain advancement and bridge
   ordering using the already-declared `ac.std.TimeDomainBridge` schema.
6. **Suspended process** — process wake, resume, state preservation, and
   termination through the Phase 4 process path.

Each example includes Python source, workspace configuration, DavinciOO JSONL
input when applicable, canonical imported trace, architectural expected result,
statistics expectations, selected event expectations, and hierarchy golden.
Every example runs through:

```text
Python -> ACPy -> ACIR -> frozen ACIR -> ACSim -> generated C++
       -> build manifest -> PTO run manifest -> gfsim -> validated results
```

Equivalent copies under unrelated roots must produce byte-identical canonical
artifacts and observations. Generated build trees and executables are not
checked in.

## 7. Phase 5B hierarchical NPU showcase

The showcase is a typed queue-wired model built from existing standard-library
schemas and provider-local internal packet types. It contains:

```text
npu
|- trace_source
|- frontend
|  |- decode
|  `- dispatch
|- backend
|  |- dependency_tracker
|  |- scalar_issue
|  |- vector_issue
|  |- cube_issue
|  `- tma_issue
|- execution
|  |- scalar_units[]
|  |- vector_units[]
|  |- cube_units[]
|  `- tma_units[]
|- memory
|  |- load_store
|  |- scratchpad
|  `- controller
|- completion
`- retirement
```

The typed decoded instruction preserves root sequence ID, block ID, opcode,
ordered tile/scalar operands, canonical tile identities, engine class, and
committed stage timestamps. Decode is table-driven and rejects an unsupported
opcode before accepting the trace offer.

Dispatch is in trace order. Dependency tracking renames tile destinations and
derives true readiness from earlier committed producers within each block.
Issue queues select the oldest ready instruction using `(sequence_id,
stable_object_id)` and may issue independent instructions out of order.
Execution latency is a frozen static policy by engine class and opcode.
Completion broadcasts destination readiness. Retirement is strictly in root
sequence order per block and produces the architectural result.

The memory path distinguishes local tile identity from global transfer
descriptors retained by the imported attributes. Loads, stores, scratchpad
access, and controller latency use finite declared resources and exact
request/response correlation.

The first architectural corpus uses a bounded committed DavinciOO trace subset
covering scalar, vector, cube, and TMA operations. Larger DavinciOO fixtures are
not copied merely for volume; every checked-in record contributes to an exit
criterion.

## 8. Determinism and legal Work permutations

Tests run the examples and showcase with multiple legal Work iteration orders.
Only proposal evaluation order changes. Arbitration, committed child identity,
queue state, issue selection, retirement, statistics, event ordering, result
JSON, and Perfetto bytes must remain identical.

No identity derives from pointers, allocation order, host thread scheduling,
unordered-container iteration, or speculative proposal order. Cross-domain
events use the global epoch ordering contract.

## 9. Failure behavior

- Import syntax, duplicate keys, unknown fields, invalid values, limits,
  identity, or canonical-output failure is diagnosed before publication.
- Runtime trace preflight still rejects raw JSONL because it is not
  `pto-trace@0.1`.
- Unsupported opcode decode, address ambiguity, dependency inconsistency,
  resource overflow, protocol violation, or retirement mismatch produces a
  failed run with no partial result publication.
- Reaching an explicit execution cap produces incomplete, never completed.
- Trace exhaustion completes only after every accepted instruction retires and
  the hierarchy is quiescent.
- Perfetto packing rejects malformed or out-of-order committed events and does
  not publish a partial document.

## 10. Verification strategy

The implementation follows red-green-refactor per task. Required evidence
includes:

- importer positive, negative, limit, duplicate-key, atomicity, root-
  independence, canonical-hash, and native-parser tests;
- normalized losslessness tests that reconstruct the source row shape from
  canonical attributes;
- statistics and event commit-barrier tests;
- Perfetto metadata, slice, counter, dependency-flow, hierarchy, and byte-
  determinism tests;
- one complete CLI build/run/replay test for every small example;
- NPU decode, rename, dependency, issue, latency, completion, memory, and
  retirement unit tests;
- multi-engine concurrency and dependent/independent instruction tests;
- legal Work-permutation equality for committed state and observations;
- architectural result, hierarchy, statistics, and swimlane goldens;
- equivalent-root and repeated-replay byte equality;
- generated dependency scans proving no Python runtime or dynamic model
  discovery in the simulator; and
- development, release, ASan, UBSan, contract, installation, and coverage-ledger
  gates.

## 11. Delivery sequence

Phase 5A lands the importer, observation closure, Perfetto packer, and six small
goldens before the NPU implementation begins. Phase 5B then adds the NPU in
reviewable vertical slices: typed decode, backend scheduling, execution and
memory, retirement and observations, followed by the complete showcase.

The phase branch is merged only after a combined audit proves every roadmap
exit criterion. No task merges a known failure, skipped required contract,
placeholder assertion, or dual trace parser.

## 12. Acceptance

Phase 5 is complete when:

1. the pinned DavinciOO JSONL shape imports deterministically into validated
   canonical `pto-trace@0.1` without widening runtime input;
2. all six small architectures traverse the complete public pipeline and pass
   architectural, hierarchy, statistics, event, determinism, and replay
   goldens;
3. the hierarchical NPU consumes validated imported PTO trace, demonstrates
   concurrent modules and dependency-aware out-of-order issue with in-order
   retirement, and produces the expected architectural result;
4. legal Work permutations produce identical committed results and event
   bytes;
5. the generated hierarchy is inspectable and the derived swimlane document
   loads as Perfetto-compatible Chrome Trace Event JSON; and
6. the complete development, release, sanitizer, install, contract, coverage,
   reproducibility, and dependency gates pass with no required skip.
