# Interface Evolution v0.1 Specification

| Field | Value |
| --- | --- |
| Specification | Global public-contract and observable-semantics evolution |
| Version | 0.1 |
| Status | Draft for review |
| Current contract epoch | `0.1` |
| Series format | `0.N` |
| Compatibility rule | Exact epoch equality |

## Purpose

This specification defines how Agentic Circuit public interfaces and observable
semantics evolve during the `v0.x` series. The project uses hard breaks instead
of compatibility layers: one global contract epoch covers the complete
toolchain, and every participant in one invocation MUST declare exactly the
same epoch.

The global epoch replaces independent compatibility claims between individual
layers. A change cannot be compatible merely because one affected format,
library, or command retains its local version.

## Normative language

The terms MUST, MUST NOT, REQUIRED, SHOULD, SHOULD NOT, and MAY are normative.
Text marked non-normative is explanatory only.

## Global contract epoch

The contract epoch has the canonical form `0.N`, where `N` is a non-negative
decimal integer without leading zeroes. The current epoch is `0.1`. The epoch is
one indivisible lockstep version for:

- the Python authoring API and lowering semantics;
- the command-line interface;
- ACPy, ACIR, and ACSim;
- ComponentSchema documents and the public catalog;
- the C++ model library source contract;
- PTO trace documents and decoded trace semantics;
- project, artifact, build, run, and replay manifests;
- statistics, probes, events, and other machine-readable observations;
- runtime-visible ordering, timing, validation, and execution semantics.

Producers and consumers MUST compare the complete canonical epoch string for
exact equality. They MUST NOT use ranges, minimum or maximum versions, major-
only comparison, feature probing as a compatibility substitute, or best-effort
parsing. An epoch mismatch is a compatibility error even when the consumer
could otherwise parse the input.

Every persisted machine-readable artifact MUST expose its contract epoch in a
minimal envelope that can be read before its body. Every executable, library,
provider, and in-memory boundary MUST expose equivalent epoch metadata for
preflight comparison. A consumer MUST reject a mismatched epoch immediately
after reading the envelope or equivalent metadata and MUST NOT parse,
validate, lower, link, execute, replay, or partially consume the body.

“Stable” means stable only within one exact epoch. No `0.N` contract carries a
source, binary, schema, artifact, or behavioral compatibility promise for any
other `0.M` epoch.

## Public-interface inventory

The repository MUST maintain the following items as one public-interface
inventory. An item remains public whether its intended consumer is a person,
script, compiler stage, generated source file, provider, test, or runtime.

### Python authoring and frontend

The inventory includes:

- import paths, exported names, decorators, classes, functions, constants, and
  generated type stubs;
- call signatures, parameter kinds, defaults, accepted value domains, return
  types, exceptions, diagnostics, and source-location behavior;
- static elaboration, name resolution, specialization, hierarchy construction,
  ownership, inference, and Python-to-ACPy lowering semantics;
- any deterministic representation exposed for inspection or tooling.

### Command-line interface

The inventory includes:

- command and subcommand names;
- options, positional arguments, defaults, accepted spellings, and validation
  order;
- exit codes, stdout and stderr separation, diagnostic codes, and all text or
  structured output intended for consumption;
- workspace discovery, output paths, generated file sets, and command side
  effects.

### ACPy, ACIR, and ACSim

The inventory includes:

- operation, type, attribute, symbol, pass, and stage names;
- textual and structured syntax, required and optional fields, defaults, and
  canonical serialization;
- verifier rules, legality rules, lowering results, ownership, effects,
  scheduling, ordering, and determinism;
- inspection surfaces and source-map representations.

### Component schemas and catalog

The inventory includes:

- ComponentSchema envelope and field schemas;
- component, protocol, interface, packet, transaction, policy, type, statistic,
  probe, diagnostic, and extension symbols;
- provider namespaces, callable projections, parameters, ports, results,
  resources, address behavior, effects, guarantees, defaults, and bounds;
- catalog membership, symbol identity, availability state, and discovery
  output.

### C++ model library

The inventory includes the public source contract presented to generated and
handwritten C++ consumers:

- public header paths, namespaces, names, templates, signatures, concepts,
  constants, and macros;
- required base classes, overrides, construction and registration protocols,
  packet and component bindings, and generated-code hooks;
- source-visible types, ownership rules, lifecycle, phase semantics, and
  error behavior;
- any binary or runtime metadata used to prove epoch equality.

### PTO trace and manifests

The inventory includes:

- PTO trace envelopes, records, operands, dependencies, extensions, and
  decoded runtime meaning;
- project, artifact, build, run, replay, capability, and cache-manifest fields;
- required fields, field types, defaults, paths, identities, hashes,
  fingerprints, and canonical encodings;
- preflight validation and rejection behavior.

### Runtime observations

The inventory includes:

- statistics, counters, gauges, histograms, probes, events, logs, diagnostics,
  and termination records;
- hierarchy paths, names, schemas, units, reset behavior, sampling points, and
  aggregation rules;
- work/xfer phases, scheduling, arbitration, queue and resource behavior,
  packet transfer, trace issue, ordering, timing, progress, termination,
  determinism, and replay semantics;
- any output observable through a supported API, CLI command, artifact, trace,
  manifest, or conforming model execution.

### Contract evidence

Normative documentation, schemas, examples, generated stubs, checked-in
generated files, golden outputs, fixtures, and conformance tests are part of
the inventory because consumers use them to determine or verify the contract.

## Epoch-change triggers

Any merged change to a public inventory item or observable semantic behavior
MUST increment the global epoch. This rule applies to additive, subtractive,
and corrective changes. Triggers include:

- adding, removing, renaming, moving, or aliasing a public symbol;
- adding or removing a command, option, field, operation, type, statistic,
  probe, event, diagnostic, or accepted spelling;
- changing a signature, type, default, requirement, bound, validation rule,
  diagnostic, exit code, serialization, file name, or generated file set;
- changing elaboration, verification, lowering, scheduling, ordering, timing,
  determinism, replay, failure, or termination behavior;
- changing the meaning, units, sampling point, or aggregation of an existing
  observation;
- adding a component, protocol, interface, packet, transaction, policy, type,
  statistic, probe, diagnostic, provider, or extension symbol to the public
  catalog;
- correcting an implementation in a way that changes supported observable
  behavior, even when the old behavior violated another document.

The next epoch MUST be greater than the current epoch. A repository change MUST
use one new epoch for all affected layers; it MUST NOT assign separate epochs
to separate files or subsystems.

The following changes do not require an epoch increment when they leave every
inventory item and every observable semantic unchanged:

- internal refactoring;
- performance changes that do not affect modeled time, ordering, output,
  resource bounds, determinism, or another observable;
- comments and non-normative editorial corrections that do not alter a
  requirement;
- making an already-cataloged implementation available without changing its
  schema, catalog identity, declared semantics, or discovery contract.

The last case MUST update the provider or build fingerprint and availability
metadata. It MUST NOT change the contract epoch by itself. Adding the catalog
symbol or changing its schema is not an availability-only change and MUST bump
the epoch.

## Required hard-break change procedure

A contract-changing merge MUST complete all of the following work atomically:

- select the new global epoch and update every producer, consumer, envelope,
  preflight check, and reported capability to that exact value;
- update the public-interface inventory and all affected normative documents;
- update Python, CLI, ACPy, ACIR, ACSim, ComponentSchema, C++ source contracts,
  PTO trace, manifests, statistics, probes, and runtime semantics wherever the
  change propagates;
- update every in-repository consumer, provider, schema, generator, example,
  fixture, golden output, and conformance test;
- regenerate all derived files from a clean staging directory;
- delete every superseded name, parser, emitter, alias, compatibility shim,
  migration path, fixture, golden, and test;
- invalidate artifacts and caches from every other epoch;
- prove exact-match acceptance and mismatch rejection at each boundary;
- merge the complete change as one repository state in which no supported
  producer or consumer advertises the previous epoch.

A partial transition MUST NOT merge. Intermediate commits MAY be incomplete
inside a private development branch, but every externally consumed or shared
merge result MUST satisfy the complete procedure.

Git history is the only rollback and recovery mechanism for an earlier
contract. The current tree MUST NOT retain old interfaces to support rollback,
migration, mixed deployments, artifact conversion, or dual-version tests. To
use an older contract, check out the corresponding older repository revision
and rebuild all artifacts with that revision.

## Artifact publication and cache invalidation

The epoch is a mandatory input to every artifact identity, specialization key,
build fingerprint, binary fingerprint, replay identity, and cache key. Exact
epoch inequality MUST cause an unconditional cache miss before any reuse or
dependency analysis. Content hashes, matching paths, matching provider
versions, or successful parsing MUST NOT override that miss.

Artifact and cache readers MUST reject entries that omit the epoch, encode it
non-canonically, or differ from the active epoch. Readers MUST NOT infer an
epoch from file contents, file names, timestamps, local tool versions, or a
fallback default.

Generators MUST publish directory-shaped outputs as follows:

- create an empty staging directory separate from the destination;
- generate the complete expected file set in that staging directory;
- validate every staged file, its envelope, its hashes, and the complete file
  inventory;
- atomically replace the destination directory with the validated staging
  directory;
- leave either the previous complete directory or the new complete directory
  visible if publication fails.

Generators MUST NOT update an existing output directory in place, merge staged
files into it, or preserve unrecognized files from an earlier generation. A
successful publication MUST contain exactly the current expected file set and
MUST NOT contain stale generated files from another epoch, provider set,
configuration, or source graph.

## Providers and extensions

Every provider and extension MUST declare one exact target epoch. Loading,
linking, registration, discovery, elaboration, and runtime preflight MUST fail
when that target differs from the active epoch. Providers and extensions MUST
NOT declare epoch ranges or negotiate a closest supported epoch.

Namespacing isolates symbol identity; it does not create a compatibility
exception. Merging any new public provider or extension symbol into the
catalog changes discovery output and MUST increment the global epoch. Changing
an existing provider or extension schema, semantics, required capability, or
observable output also MUST increment the epoch.

A provider or extension MAY vary its private implementation and provider/build
fingerprint within an epoch only when its declared schema and all observable
semantics remain unchanged. An implementation for an already-declared catalog
entry MAY transition from unavailable to available without an epoch increment;
capability discovery MUST report the availability change, and fingerprints
MUST prevent reuse of artifacts built against the unavailable or different
implementation state.

Optional and mandatory extension payloads remain subject to the enclosing
epoch. An extension MUST NOT reinterpret a core field, relax exact equality,
carry an old representation, or act as a transport for a legacy contract.

## Forbidden legacy patterns

The current epoch MUST NOT contain:

- deprecated public names or options;
- aliases from an old name to a new name;
- parsers that accept an old syntax, field, spelling, envelope, or epoch;
- emitters that produce an old syntax, field, spelling, envelope, or epoch;
- compatibility shims, adapters, translators, converters, or dual-mode
  dispatch selected by epoch;
- migrations for old source, configuration, artifacts, traces, manifests,
  schemas, caches, or generated files;
- fallback parsing, format sniffing, missing-epoch defaults, compatibility
  ranges, or warning-and-continue behavior;
- shadow fields that preserve an old meaning alongside a new meaning;
- tests, fixtures, examples, goldens, or documentation that exercise or
  advertise a deleted contract;
- caches or output directories containing files carried forward from a prior
  generation.

A diagnostic that reports an epoch mismatch is not a compatibility layer. It
MUST report the expected and actual epoch and stop at the boundary without
interpreting the rejected body.

## Conformance tests

A conforming repository MUST automate at least the following tests.

### Exact equality

- Each producer emits the active canonical epoch.
- Each consumer accepts artifacts and peers with the exact active epoch.
- Each consumer rejects lower, higher, missing, malformed, and
  non-canonically encoded epochs immediately after the envelope or equivalent
  metadata.
- A mismatch prevents parsing of the body and prevents all downstream side
  effects.

### Lockstep coverage

- Python, CLI, ACPy, ACIR, ACSim, ComponentSchema, the C++ model library, PTO
  trace, manifests, statistics, probes, providers, extensions, and runtime
  preflight report the same epoch.
- No public schema or capability response reports an independent compatibility
  epoch.
- Every artifact edge in the end-to-end pipeline performs an exact equality
  check.

### Legacy removal

- Deleted imports, names, commands, options, fields, syntax, symbols, and
  extension forms fail as unknown input rather than warn or redirect.
- Repository searches and negative tests find no old parsers, emitters,
  aliases, shims, migrations, fixtures, goldens, or documentation.
- The conformance suite contains mismatch-rejection tests but no execution
  tests for an earlier contract.

### Clean generation and publication

- Generation from an empty directory and from a destination polluted with
  stale files produces the same final file inventory and byte-identical
  deterministic outputs where determinism is required.
- Removing a generated input removes its generated outputs from the published
  directory.
- Fault injection before atomic replacement leaves the previous complete
  output visible; fault injection after replacement leaves the new complete
  output visible.
- No successful output mixes epochs or contains an undeclared file.

### Artifacts and caches

- Changing only the epoch changes all artifact identities and cache keys.
- No artifact, object, binary, replay record, or cache entry from another epoch
  is reused.
- A matching content hash or provider version cannot turn an epoch mismatch
  into a cache hit.

### Providers, catalogs, and semantics

- A provider or extension targeting another epoch fails before registration or
  use.
- Adding a catalog symbol fails the change gate unless the global epoch also
  changes.
- Making an already-cataloged implementation available changes the applicable
  provider/build fingerprint and availability output without requiring a
  schema change.
- Golden runtime observations prove that all declared stable behavior is
  invariant within one epoch.
- Any deliberate observable-semantic change fails the change gate unless the
  global epoch also changes and all affected goldens update atomically.

## Acceptance criteria

The interface-evolution contract conforms when:

- one canonical global epoch covers every public contract and observable
  semantic named by this specification;
- every boundary accepts exact equality only and rejects mismatches before
  body processing or side effects;
- every merged public or observable change increments the global epoch;
- a hard break updates all consumers, documents, schemas, generated files,
  fixtures, goldens, and tests in one merge;
- the current tree contains no old names, compatibility code, migrations, or
  tests for a superseded contract;
- Git checkout and a complete rebuild are the only way to run an earlier
  contract;
- artifacts and caches cannot cross epoch or provider/build fingerprint
  boundaries;
- clean staging and atomic directory replacement prevent stale or mixed
  generated output;
- catalog additions bump the epoch, while implementation availability without
  schema or semantic change updates only availability metadata and the
  provider/build fingerprint;
- the complete conformance suite passes from a clean checkout.
