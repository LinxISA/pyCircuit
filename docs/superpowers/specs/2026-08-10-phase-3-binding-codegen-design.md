# Phase 3 binding and structured C++ generation design

Status: approved architecture, pending implementation.

## 1. Purpose

Phase 3 turns verified canonical ACSim into a compiled, fingerprinted gfsim
executable without bypassing the construction IR or interpreting component
metadata at runtime. It completes the compiler boundary between the Phase 1
ACIR/ACSim implementation and the Phase 2 runtime.

This design implements Phase 3 of the
[Agentic Circuit roadmap](../plans/2026-08-04-agentic-circuit-roadmap.md) and
the normative
[ACSim and ACIR-to-gfsim lowering specification](../../specs/acsim-gfsim-lowering-v0.1.md).
Those documents remain authoritative. This document fixes the implementation
architecture, internal APIs, artifact flow, and verification strategy.

## 2. Scope and phase boundary

Phase 3 delivers:

- deterministic provider discovery and exact binding selection;
- immutable binding-lock consumption;
- a typed, immutable compilation plan extracted from verified ACSim;
- hierarchical module and enum-PC process generation;
- generic typed binding, access, dispatch, and activation emission;
- C++20 concept and source-contract checks;
- exact build manifests and cache fingerprints;
- clean staging, compile, link, embedded-fingerprint comparison, and atomic
  publication;
- an internal compiler driver that exposes the logical emit and contract-check
  stages for tests and later composition.

Phase 3 does not add the public `agentic-circuit` CLI. Phase 4 owns that public
surface and calls the Phase 3 library. Phase 3 also does not add component
semantics, a descriptor interpreter, dynamic topology, runtime component
selection, Python integration, or a second construction IR.

## 3. Existing foundation

The implementation builds on these completed contracts:

- `ac-resolve-gfsim-bindings` verifies frozen requests, selects one exact
  candidate, and emits a canonical immutable binding lock;
- `ac-lower-to-acsim` atomically produces verified canonical ACSim with
  ownership, processes, dense runtime rows, and activation edges;
- the ACSim verifier enforces closed operations, types, metadata, ownership,
  effects, runtime expansion, and canonical ordering;
- gfsim supplies typed dispatch thunks, activation adjacency, process runtime,
  components, protocols, trace streaming, diagnostics, and statistics;
- the current CodeGen library supplies useful formatting helpers and prototype
  process, module, dispatch, and manifest code, but its module generator is a
  placeholder and its manifest/staging APIs do not implement the normative
  contracts.

Phase 3 replaces the prototype CodeGen data model rather than preserving an
API that conflicts with the frozen schemas.

## 4. Chosen architecture

The compiler uses five explicit boundaries:

```text
verified canonical ACSim
  -> immutable typed ModelPlan
  -> deterministic SourceBundle
  -> canonical CompilePlan + BuildManifest input record
  -> clean stage, concept check, compile, link, fingerprint check
  -> immutable build directory + atomic current-build pointer
```

Each boundary has one responsibility and a validator. Filesystem mutation
starts only after the model and source bundle validate in memory.

### 4.1 Why a typed plan

Directly walking MLIR while writing files would couple symbol resolution,
ordering, C++ spelling, filesystem errors, and diagnostics. A typed plan makes
canonical order explicit, supports focused tests, and lets source generation
remain a pure function. It is internal compiler state, not a serialized runtime
descriptor and not an additional public IR.

### 4.2 Rejected alternatives

The implementation does not:

- emit files directly during an ACSim walk, because partial traversal failures
  would be mixed with output mutation;
- use JSON as an intermediary model, because that would introduce another
  schema-driven construction boundary;
- use Python or CMake scripts as semantic orchestrators, because Phase 3 must
  provide the reusable compiler contract consumed by Phase 4;
- retain the prototype `generateModuleSource` behavior that fabricates generic
  `SimObject` children.

## 5. Typed model plan

The public compiler header `include/acir/CodeGen/ModelPlan.h` defines immutable
value types. Implementation-only traversal helpers remain under
`lib/CodeGen/`.

The principal API is:

```cpp
llvm::Expected<ModelPlan> buildModelPlan(mlir::ModuleOp canonicalACSim);
llvm::Error validateModelPlan(const ModelPlan &plan);
```

`buildModelPlan` first calls `verifyCanonicalACSimFile`. It never accepts
frozen ACIR, mixed ACIR/ACSim, or a partial `acsim.model`.

`ModelPlan` contains:

- exact model, contract epoch, frozen-ACIR, binding-lock, schema-set, provider,
  profile, and toolchain identities;
- generated C++ type realizations and exact external binding records;
- specialized modules in canonical symbol order;
- root ownership and deterministic construction/destruction order;
- processes with closed PC sets, live slots, typed operations, wakes,
  transitions, fairness work, and specialization fingerprints;
- runtime objects in dense object-ID order;
- activation edges in sorted, deduplicated source/target order;
- source-map records keyed by stable ACSim identity.

### 5.1 Module and placement plans

A module plan records its generated class name, specialization fingerprint,
closed interface, ordered placements, typed construction relations, pure
expression graph, exports, and processes. Generated-module placements are
ownership wrappers and never receive dispatch rows. Stateful external bindings,
array elements, and processes use the runtime rows already fixed by ACSim.

A placement records one of:

- generated module by-value ownership;
- stateful external binding by-value ownership;
- homogeneous nested `std::array` ownership;
- fixed heterogeneous named ownership.

Every placement carries a stable C++ member name derived from its canonical
symbol, never from a host address or registry order.

### 5.2 Binding plans

External binding plans copy only validated lock metadata already embedded in
canonical ACSim: include, target, concrete type, concept, entry points,
constructor arguments, ownership, accessors, results, resources, ports, and
activation sources. The plan cannot carry raw statements, callbacks, formatter
strings, macros, or behavior.

The generic generator may branch on plan node kind and mapping kind. It must
not branch on component name, family, provider namespace, binding ID, C++
symbol, or catalog entry.

### 5.3 C++ token safety

All C++ identifiers, qualified names, include paths, types, and literal values
pass the same lexical and semantic checks used by binding records. Constructor
values are emitted from typed canonical values:

- template arguments become concrete specialization types;
- constexpr arguments become validated C++ constant tokens;
- constructor constants become immutable constructor arguments;
- JSON strings are escaped as C++ string literals;
- arrays and records become closed aggregate initializers.

No arbitrary JSON string becomes executable C++ text.

## 6. Structured source generation

`include/acir/CodeGen/Generator.h` exposes:

```cpp
llvm::Expected<SourceBundle> generateModelSources(const ModelPlan &plan);
llvm::Error validateSourceBundle(const ModelPlan &plan,
                                 const SourceBundle &bundle);
```

Generation is pure: identical plans produce byte-identical ordered source
files. A `SourceBundle` owns files sorted by normalized relative path. Each file
has a `sha256:` content fingerprint.

### 6.1 File set

Every build emits, even when a file is structurally empty:

```text
include/generated/model.h
include/generated/dispatch.h
include/generated/modules/<specialization>.h
include/generated/processes/<specialization>.h
src/generated/model.cpp
src/generated/modules/<specialization>.cpp
src/generated/processes/<specialization>.cpp
src/generated/main.cpp
```

Names derive from stable specialization identities. Source content contains no
timestamps, random values, pointer values, temporary paths, current working
directory, or host-specific absolute source paths.

### 6.2 Generated modules

Each specialized module becomes one final owner class derived from
`gfsim::Module`. The class declares concrete by-value members, nested arrays,
typed accessors, constructor constants, exports, and processes. Construction
follows plan order; destruction follows C++ reverse member order. No generated
module uses a runtime factory or owns children through an erased component
type.

The Phase 2 hierarchy API gains `Module::attachChild(SimObject &)`, which
registers a non-owning child view, assigns its parent, and refreshes canonical
paths. Generated classes own their concrete members by value and attach them to
the hierarchy after construction. The existing owning `addChild` API remains
for the system root, handwritten models, and tests. Both APIs feed the same
ordered child index, traversal, reset, identity, and path behavior; attaching
one object twice is a preflight error.

### 6.3 Generated processes

Each process becomes one final class derived from
`gfsim::ProcessRuntime<Derived>`. The generator emits:

- the smallest sufficient unsigned PC enum;
- committed/proposed typed live storage;
- the exact `fairness_work` constant;
- an exhaustive `executeProcessStep` switch;
- generic emission for legal builtin, arith, index, cf, `acsim.inline`,
  `acsim.invoke`, live load/store, continue, suspend, and terminate operations;
- explicit invalid-PC failure.

Only process bodies contain architecture-specific generated behavior. External
components retain their reusable Work/Xfer implementations.

### 6.4 Dispatch and activation

The model emits one dense `std::array<gfsim::DispatchRow, N>` in object-ID
order using `gfsim::makeDispatchRow(&typed_member)`. It emits canonical
activation offsets and targets from the plan. Generated module wrappers are not
rows. The generated harness installs both tables without hierarchy scanning.

## 7. Fingerprints and manifests

The existing unprefixed CodeGen fingerprint prototype is replaced by the
normative representation:

```text
sha256:<64 lowercase hexadecimal digits>
```

Canonical JSON uses the repository RFC 8785/I-JSON implementation. Every
fingerprint function documents its exact preimage and uses a closed,
versioned object.

### 7.1 Fingerprint domains

The implementation separates:

- source fingerprints: exact file bytes;
- specialization fingerprints: definition, static arguments, types, schemas,
  provider implementation, profile, and toolchain;
- toolchain fingerprint: compiler build identity, target, standard library and
  ABI mode, relevant flags, and object format;
- compile-plan fingerprint: ordered source units, definitions, include roots,
  flags, and link inputs;
- build fingerprint: the complete normative build input set;
- manifest fingerprint: canonical final manifest bytes excluding no field
  except a separately declared self-fingerprint field if one is added in a
  later contract epoch.

Hierarchy paths do not participate in specialization or cache identity.

### 7.2 Build manifest

`BuildManifest` becomes a typed representation of
`schemas/build-manifest.schema.json`. It contains every required field and no
legacy aliases:

- schema, version, and contract epoch;
- project and system identities;
- source files and normalized ACIR hash;
- compiler and pass pipeline;
- providers, component specializations, and protocol identities;
- artifacts and validation gates;
- build profile and instrumentation layers;
- specialization inputs and final build fingerprint.

Serialization produces RFC 8785 canonical JSON, validates against the same
closed field rules enforced by the schema, and sorts semantically unordered
collections by their stable keys.

### 7.3 Compile plan

The staged `compile-plan.json` is an internal closed artifact with schema
identity `acsim-compile-plan-0.1`. It records ordered source units, object
outputs, include roots, compile definitions, compiler/linker flags, prebuilt
inputs with provenance, final executable path, toolchain fingerprint, and
compile-plan fingerprint. It is not interpreted by the simulator and does not
become a new public component schema.

## 8. Same-toolchain compilation

`include/acir/CodeGen/Build.h` exposes a library API:

```cpp
llvm::Expected<BuildResult> buildGeneratedModel(const BuildRequest &request);
```

`BuildRequest` supplies explicit project/system identities, verified ACSim,
binding lock, profile, instrumentation, source roots, provider inputs,
toolchain configuration, link inputs, and output root. It contains no ambient
defaults from environment variables or the current directory.

Frozen ACIR is also supplied as immutable bytes for staging and fingerprint
verification. The generator never traverses it or derives construction from
it; verified canonical ACSim remains the sole construction input.

The build implementation:

1. validates all input fingerprints and the selected profile;
2. constructs and validates `ModelPlan`;
3. generates and validates `SourceBundle`;
4. constructs canonical compile and manifest input records;
5. creates a clean private stage;
6. writes frozen ACIR, ACSim, binding lock, sources, plans, and reports;
7. runs generated-source and C++ concept checks;
8. compiles every unit with the declared compiler and identical contract flags;
9. links the declared runtime, standard library, provider, and system inputs;
10. reads the embedded build fingerprint from the executable and compares it
    with the planned value;
11. records artifact hashes and passed validation gates;
12. publishes the immutable build and atomically selects it as current.

Compiler subprocesses use argument vectors, not shell command strings. Their
captured stdout/stderr are bounded and written to deterministic validation
reports after removal of declared non-semantic host-path prefixes.

### 8.1 Prebuilt inputs

A prebuilt runtime, provider, or standard-library object may be reused only
when its provenance record exactly matches the requested compiler, target,
ABI, profile, compile definitions, source fingerprint, and contract epoch.
Otherwise the build is a cache miss or `ACLOWER-FINGERPRINT` error, depending
on whether source recompilation is available.

## 9. Staging, caching, and publication

The output root contains immutable fingerprint-addressed builds:

```text
<output>/builds/<build-fingerprint>/...
<output>/current.json
```

A host-only sibling staging directory may contain a random nonce, but no
staged artifact or fingerprint includes that nonce. After validation, the
stage is renamed to its immutable fingerprint path. If the path already exists,
the implementation compares the complete manifest and artifact hashes and
reports an exact cache hit; it never overwrites the directory.

Publication writes a new canonical current-build pointer to a sibling temporary
file, flushes it, and atomically renames it over `current.json`. Readers observe
either the previous complete build or the new complete build. Failed stages
leave both the previous build and current pointer unchanged.

Path validation rejects absolute artifact paths, `..`, empty normalized
components, and any normalized path escaping the stage. Cleanup targets only
the exact private stage created by the active build.

## 10. Internal compiler driver

`acir-cxxgen` is an internal developer/test driver, not the Phase 4 public CLI.
It accepts canonical ACSim and explicit build inputs, then exposes these stage
boundaries:

- `model-plan`;
- `acsim-emit-cxx`;
- `acsim-check-cxx-contract`;
- `compile`;
- `link`;
- `publish`.

It supports `--stop-after` for those exact names and can emit the model plan,
generated sources, compile plan, validation reports, and build manifest for
tests. Phase 4 may call the library directly and is not required to spawn this
driver.

## 11. Diagnostics and failure behavior

Phase 3 preserves the normative `ACLOWER-*` diagnostics. Errors carry the
logical stage and stable identity involved. At minimum:

- malformed or noncanonical ACSim: `ACLOWER-FINGERPRINT` or the underlying
  ACSim verifier diagnostic;
- missing/ambiguous binding or lock mismatch: `ACLOWER-BINDING-*`;
- illegal parameter mapping or C++ token: `ACLOWER-PARAM-PHASE`;
- type, role, accessor, or concept mismatch: `ACLOWER-TYPE-MISMATCH`;
- ownership or array inconsistency: `ACLOWER-OWNERSHIP` or `ACLOWER-ARRAY`;
- illegal pure/process emission: `ACLOWER-INLINE-EFFECT` or
  `ACLOWER-PROCESS-STATE`;
- dispatch/activation inconsistency: `ACLOWER-DISPATCH` or
  `ACLOWER-ACTIVATION`;
- profile/toolchain/build identity mismatch: `ACLOWER-PROFILE` or
  `ACLOWER-FINGERPRINT`.

No failure publishes partial output. Compiler and linker failures are reported
at the contract-check stage rather than deferred to simulator execution.

## 12. Verification strategy

Every implementation task follows red-green-refactor and ends in one reviewed
commit.

### 12.1 Unit and golden tests

Tests cover:

- exact typed plan extraction for hierarchy, arrays, fixed collections,
  external bindings, pure expressions, processes, runtime rows, and activation;
- deterministic plan construction under permitted input iteration changes;
- byte-identical generated source bundles and canonical manifests;
- exact manifest-schema acceptance and rejection of missing/extra fields;
- stable fingerprint preimages and cache keys;
- path containment, bounded output, and injected filesystem failures;
- no mutation of a previously published build after any stage failure.

### 12.2 Compile/link tests

Golden models compile and link with the repository's locked C++20 toolchain.
Tests cover nested modules, multidimensional arrays, each binding kind, pure
chains, baseline component families, multi-suspension processes, dispatch, and
activation. The executable reports the embedded fingerprint and passes static
preflight.

Negative tests cover concept failure, toolchain mismatch, fingerprint mismatch,
invalid mappings, illegal generated effects, missing runtime activation,
compilation failure, link failure, and embedded-fingerprint mismatch.

### 12.3 Extensibility and forbidden dependencies

An extension fixture adds a stateful provider component through only a schema,
binding record, provider header/source, and tests. It lowers, generates,
compiles, links, and runs without modifying generic lowering or emitter source.

Automated scans reject generated component-name branches, Python dependencies,
schema walkers, plugin loaders, runtime factories, descriptor interpreters,
dynamic topology, coroutines, `std::function` process frames, and hot-path RTTI.

### 12.4 Determinism and sparsity

Equivalent frozen inputs generated from legal order permutations must produce
byte-identical model plans, source files, compile plans, manifests, and build
fingerprints. The sparsity test confirms that adding permanently idle objects
does not change scheduler invocation or traversed activation-edge counts for an
otherwise identical active frontier.

## 13. Delivery sequence

Implementation proceeds through independently reviewable checkpoints:

1. exact fingerprint and build-manifest contracts;
2. typed ACSim `ModelPlan` extraction and validation;
3. generic module, binding, expression, and process source generation;
4. model harness, dispatch, activation, and embedded fingerprint;
5. compile plan and same-toolchain preflight;
6. clean staging, exact cache reuse, and atomic publication;
7. internal driver and stop-after boundaries;
8. extension, determinism, compile/link, forbidden-dependency, and sparsity
   conformance;
9. Phase 3 completion audit and integration.

## 14. Acceptance criteria

Phase 3 is complete only when:

- verified canonical ACSim is the sole construction input to C++ generation;
- exact binding locks, profiles, provider identities, and toolchain identities
  are validated before emission;
- all specialized modules and processes emit structured, fully typed C++20;
- generated source and manifests are byte-identical for equivalent frozen
  inputs;
- generated simulators compile and link with the declared same toolchain;
- concept, fingerprint, and embedded-fingerprint checks pass before publication;
- failed builds preserve the previous complete published build;
- exact cache hits reuse immutable artifacts and any unequal input misses;
- the extension and sparsity tests pass;
- generated sources and executables contain no forbidden dependency or dynamic
  discovery mechanism;
- the full Debug, Release, sanitizer, static-analysis, contract, determinism,
  install-consumer, and generated-model test matrix passes.
