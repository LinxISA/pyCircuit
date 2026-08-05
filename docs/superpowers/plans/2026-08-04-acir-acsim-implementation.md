# ACIR and ACSim v0.1 Implementation Plan

> Execution contract: use the Superpowers subagent-driven-development workflow
> task by task. Each production change begins with an observed failing test,
> receives requirements and code-quality review, is committed independently,
> and is pushed after integration. Do not batch tasks or retain compatibility
> shims.

**Goal:** Implement the complete ACIR and ACSim v0.1 MLIR surface, strict
verification, deterministic topology freeze, and canonical ACIR-to-ACSim
lowering as the first executable layer of Agentic Circuit.

**Architecture:** A standalone out-of-tree MLIR 22 project defines two dialects
with ODS/TableGen and narrow custom C++ verification. ACIR preserves typed,
hierarchical architecture intent. Freeze and lowering passes resolve all
symbols, topology, ownership, effects, bindings, collections, IDs, activation
edges, and process state before canonical ACSim. ACSim is closed, structured,
and ready for generic C++ emission; component behavior remains outside the
compiler in the later C++ library phase.

**Toolchain:** LLVM/MLIR 22.1.8 pinned to commit
`ca7933e47d3a3451d81e72ac174dcb5aa28b59d1`, C++20, CMake, Ninja,
TableGen, lit/FileCheck, GoogleTest supplied by LLVM, Python 3.11+, Ruff,
Pyright, JSON Schema Draft 2020-12.

## Global test contract

For every operation and type, add at least:

1. one canonical parse/print/parse round-trip;
2. one valid verifier case;
3. one invalid case for every operation-specific invariant;
4. one coverage-ledger entry connecting the public name to tests and code.

Cross-cutting invariants receive focused pass or C++ unit tests. Test files
must state the contract being exercised and use stable diagnostic identifiers.
Tests do not merely grep for implementation details.

Use these standard build commands after Task 1 establishes presets:

```bash
cmake --preset dev-llvm22
cmake --build --preset dev --target acir-opt acir-unit-tests
ctest --preset dev --output-on-failure
cmake --build --preset dev --target check-agentic-circuit
```

The first command must fail clearly if a different MLIR major/minor is selected.

## Task 1: Establish the professional repository and locked toolchain

**Files:**

- Create: `LICENSE`, `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`,
  `SUPPORT.md`, `CHANGELOG.md`, `.gitignore`, `.gitattributes`, `.editorconfig`
- Create: `CMakeLists.txt`, `CMakePresets.json`, `cmake/LLVMToolchain.cmake`
- Create: `cmake/AgenticCircuitConfig.cmake.in`, `cmake/modules/AddACIR.cmake`
- Create: `toolchains/llvm.lock.json`, `pyproject.toml`, `requirements-dev.lock`
- Create: `scripts/bootstrap-dev.sh`, `scripts/check-contracts.py`
- Create: `tests/contracts/test_contracts.py`
- Create: `.github/workflows/ci.yml`, `.github/dependabot.yml`
- Modify: `README.md`

**Red:** Add contract tests that fail because the repository lacks governance
files, exact epoch agreement, schema compilation, Markdown-link integrity,
placeholder rejection, and an exact LLVM lock. Run
`python -m unittest tests.contracts.test_contracts -v` and record the failure.

**Green:** Add the minimal repository files and read-only contract checker.
The LLVM lock records release, commit, source URL, SHA-256 of any downloaded
archive, supported host triples, and package-version policy. CMake finds MLIR
through `MLIR_DIR`, rejects non-22.1.8 packages, enables C++20 without compiler
extensions, exports compile commands, and uses assertions in `dev`.

CI uses `ubuntu-24.04`, pins every action by full commit SHA, caches only
content-addressed LLVM/build inputs, and initially runs contract tests plus a
configure smoke test. It must not silently download unpinned executables.

**Verify:** Run contract tests, validate all eight JSON schemas, configure both
`dev-llvm22` and `release-llvm22`, and inspect `git diff --check`.

**Commit:** `build: establish reproducible LLVM 22 project baseline`

## Task 2: Bootstrap the ACIR/ACSim dialect libraries and driver

**Files:**

- Create: `include/acir/InitAllDialects.h`, `include/acir/InitAllPasses.h`
- Create: `include/acir/Dialect/ACIR/ACIRDialect.td`
- Create: `include/acir/Dialect/ACIR/ACIRDialect.h`
- Create: `include/acir/Dialect/ACSim/ACSimDialect.td`
- Create: `include/acir/Dialect/ACSim/ACSimDialect.h`
- Create: `lib/Dialect/ACIR/ACIRDialect.cpp`
- Create: `lib/Dialect/ACSim/ACSimDialect.cpp`
- Create: `tools/acir-opt/acir-opt.cpp`
- Create: matching `CMakeLists.txt` files under `include/`, `lib/`, and `tools/`
- Create: `test/lit.cfg.py`, `test/lit.site.cfg.py.in`, `test/CMakeLists.txt`
- Create: `test/ACIR/dialect-smoke.mlir`, `test/ACSim/dialect-smoke.mlir`

**Red:** Add smoke tests for explicit registration, unknown operation rejection,
missing `ac.contract_epoch`, wrong epoch, and a minimal canonical module. Run
the lit tests and observe the missing-driver/dialect failure.

**Green:** Implement explicit dialect and pass registries and an `acir-opt`
driver. Do not load all upstream dialects implicitly. Register `builtin`,
`arith`, `index`, `scf`, and `cf` only for contexts and regions allowed by the
normative specs. Add a top-level file verifier requiring exactly epoch `"0.1"`.

**Verify:** Build the driver; run both smoke files; run `acir-opt --help` twice
and compare output; verify no unregistered dialect is accepted.

**Commit:** `feat(ir): bootstrap ACIR and ACSim dialects`

## Task 3: Implement ACIR primitive, unit, aggregate, and named types

**Files:**

- Create: `include/acir/Dialect/ACIR/ACIRTypes.td`
- Create: `include/acir/Dialect/ACIR/ACIRAttributes.td`
- Create: `include/acir/Dialect/ACIR/ACIRTypes.h`
- Create: `lib/Dialect/ACIR/ACIRTypes.cpp`
- Create: `test/ACIR/types-valid.mlir`, `test/ACIR/types-invalid.mlir`
- Create: `unittests/Dialect/ACIR/TypesTest.cpp`

**Red:** Specify round trips and invalid cases for every public type:
`struct`, `packet`, `transaction`, `enum`, `union`, `optional`, `list`,
`vector`, `flow`, `endpoint`, `resource_ref`, interface-only `channel`,
`duration`, `rate`, `event`, `address`, and `resource_token`. Include unresolved
symbols, non-positive vector bounds, illegal nesting, incompatible units,
invalid roles, and forbidden channel placement.

**Green:** Define uniqued storage, parsers/printers, symbol references, exact
integer unit/rate normalization, role enums, protocol/type parameters, and data
layout attributes. Preserve named identities instead of flattening aggregates.

**Verify:** Run focused lit and unit tests plus generic MLIR bytecode/textual
round trips. Run an inventory script proving all public types are covered.

**Commit:** `feat(ir): implement ACIR public type system`

## Task 4: Implement named declarations and record-like executable operations

**Files:**

- Create: `include/acir/Dialect/ACIR/ACIROps.td`
- Create: `include/acir/Dialect/ACIR/ACIRInterfaces.td`
- Create: `include/acir/Dialect/ACIR/ACIROps.h`
- Create: `lib/Dialect/ACIR/ACIROps.cpp`
- Create: `test/ACIR/declarations-valid.mlir`
- Create: `test/ACIR/declarations-invalid.mlir`
- Create: `test/ACIR/records-valid.mlir`, `test/ACIR/records-invalid.mlir`

**Red:** Add tests for `ac.type_scope`, `ac.type_alias`, `ac.struct`, `ac.enum`,
`ac.union`, `ac.packet`, `ac.transaction`, `ac.record.create`, `record.get`,
`record.with`, `packet.serialize`, and `packet.deserialize`. Cover duplicate
fields/enumerants, missing fields, type mismatches, mutation/lifetime rules,
layout ambiguity, unknown aliases, and effect claims.

**Green:** Implement symbol-bearing declarations, field metadata, immutable
record operations, serialization contracts, builders, canonical assembly, and
memory-effect declarations. Packet operations retain semantic packet identity.

**Verify:** Run focused tests and a C++ builder/unit test constructing each op.

**Commit:** `feat(ir): add ACIR named data and packet operations`

## Task 5: Implement protocols, interfaces, roles, and typed topology values

**Files:**

- Modify: `include/acir/Dialect/ACIR/ACIROps.td`
- Modify: `include/acir/Dialect/ACIR/ACIRInterfaces.td`
- Modify: `lib/Dialect/ACIR/ACIROps.cpp`
- Create: `lib/Dialect/ACIR/ACIRInterfaces.cpp`
- Create: `test/ACIR/protocols-valid.mlir`
- Create: `test/ACIR/protocols-invalid.mlir`
- Create: `test/ACIR/interfaces-valid.mlir`
- Create: `test/ACIR/interfaces-invalid.mlir`

**Red:** Cover `ac.interface`, `ac.protocol`, `ac.role`, `ac.state`, `ac.event`,
`ac.transition`, `ac.guarantee`, and `ac.port`. Test role compatibility,
transition determinism, pure guards, payload compatibility, backpressure,
ordering guarantees, channel placement, endpoint cardinality, and unknown
mandatory semantics.

**Green:** Implement protocol declaration regions, transition verification,
typed interface channels, role duality, linear-flow bookkeeping hooks, and
side-effect rejection in guards. Leave `ready_valid` and `request_response`
definitions to the standard-library phase.

**Verify:** Focused lit tests, transition-table unit tests, and deterministic
diagnostic-order tests.

**Commit:** `feat(ir): define ACIR protocol and interface contracts`

## Task 6: Implement hierarchical system, module, instance, and collection IR

**Files:**

- Modify: `include/acir/Dialect/ACIR/ACIROps.td`
- Modify: `lib/Dialect/ACIR/ACIROps.cpp`
- Create: `include/acir/Dialect/ACIR/GraphRegion.h`
- Create: `lib/Dialect/ACIR/GraphRegion.cpp`
- Create: `test/ACIR/hierarchy-valid.mlir`
- Create: `test/ACIR/hierarchy-invalid.mlir`
- Create: `test/ACIR/collections-valid.mlir`
- Create: `test/ACIR/collections-invalid.mlir`

**Red:** Cover `ac.system`, `ac.module`, `ac.module.extern`,
`ac.module.generated`, `ac.instance`, `ac.array`, `ac.instances`, `ac.view`,
and `ac.return`. Test one selected system, symbol resolution, static arguments,
unique ownership, nested modules, homogeneous shapes, bounds, constant
projections, inferred binding cardinality, legal exports, and stable names.

**Green:** Implement Graph-region semantics, module interfaces, specialization
keys, hierarchy ownership, statically shaped nested collections, projections,
exports, and region verification. Do not flatten hierarchy or introduce
`ac.connect`; SSA bindings are the only functional topology edges.

**Verify:** Parse/print, programmatic-builder, symbol-use, nested-hierarchy, and
large-array smoke tests. Confirm instance ordering is independent of hash-map
iteration.

**Commit:** `feat(ir): add hierarchical ACIR graph construction`

## Task 7: Implement queues, resources, addresses, and exact time domains

**Files:**

- Modify: `include/acir/Dialect/ACIR/ACIROps.td`
- Modify: `lib/Dialect/ACIR/ACIROps.cpp`
- Create: `include/acir/Dialect/ACIR/ACIRResources.h`
- Create: `lib/Dialect/ACIR/ACIRResources.cpp`
- Create: `test/ACIR/resources-valid.mlir`
- Create: `test/ACIR/resources-invalid.mlir`
- Create: `test/ACIR/address-time-valid.mlir`
- Create: `test/ACIR/address-time-invalid.mlir`

**Red:** Cover `ac.queue`, `ac.event_queue`, `ac.resource`, `ac.address_space`,
`ac.address_map`, and `ac.time_domain`. Test capacity and latency bounds,
ownership, arbitration, reservation balance, overlap priority, width/range
consistency, exact unit conversion, positive periods, phases, cross-domain
bridges, and overflow/capability rejection.

**Green:** Add typed resource effects and roles, deterministic address maps,
exact rational-to-global-tick normalization, and ownership/arbitration metadata.
All stateful constructs are positive-delay boundaries even if a particular
runtime proposal becomes a no-op.

**Verify:** Focused lit tests plus unit/property tests around integer overflow,
address overlap, and unit normalization.

**Commit:** `feat(ir): model ACIR resources addresses and time`

## Task 8: Implement processes, trace operations, contracts, and observation

**Files:**

- Modify: `include/acir/Dialect/ACIR/ACIROps.td`
- Modify: `include/acir/Dialect/ACIR/ACIRInterfaces.td`
- Modify: `lib/Dialect/ACIR/ACIROps.cpp`
- Create: `test/ACIR/process-valid.mlir`, `test/ACIR/process-invalid.mlir`
- Create: `test/ACIR/trace-valid.mlir`, `test/ACIR/trace-invalid.mlir`
- Create: `test/ACIR/contracts-valid.mlir`
- Create: `test/ACIR/contracts-invalid.mlir`

**Red:** Cover `ac.process`, `try_send`, `try_recv`, `schedule`, `wait_until`,
`wait_for`, `await_event`, `yield_sim`, all five `ac.trace.*` operations,
`require`, `ensure`, `assert`, `probe`, `stat`, `stat.add`, and
`instrumentation`. Test suspension state, live values, allowed `scf` control,
effect resources, unique trace-cursor ownership, probes barred from functional
dataflow, and removable instrumentation semantics.

**Green:** Implement region constraints and effect interfaces for queue,
resource, module, storage, protocol, trace, event-queue, external-I/O, and
statistics resources. Accept upstream `scf`/`arith`/`index` only in the
specified executable contexts.

**Verify:** Focused lit tests, memory-effect unit tests, and an MLIR pass test
proving effectful operations are not incorrectly canonicalized away.

**Commit:** `feat(ir): add ACIR executable and observation operations`

## Task 9: Implement whole-model verification and topology freeze

**Files:**

- Create: `include/acir/Analysis/ModelAnalysis.h`
- Create: `lib/Analysis/ModelAnalysis.cpp`
- Create: `include/acir/Transforms/Passes.td`
- Create: `lib/Transforms/VerifyModel.cpp`
- Create: `lib/Transforms/FreezeTopology.cpp`
- Create: `lib/Transforms/CanonicalizeModel.cpp`
- Create: `test/Transforms/verify-model.mlir`
- Create: `test/Transforms/freeze-topology.mlir`
- Create: `test/Transforms/zero-delay-cycles.mlir`
- Create: `test/Transforms/deterministic-canonicalization.mlir`

**Red:** Encode every item under ACIR `Required verification` and `Freeze
invariants`, including duplicate paths, implicit fan-in/out, linearity,
arbitration ownership, unresolved parameters, bounded payloads, address maps,
zero-delay SCCs, process state, protocol guards, and post-freeze mutation.
Add input-permutation tests whose canonical output must match byte for byte.

**Green:** Build deterministic symbol, ownership, use-cardinality, address,
effect, unit, time-domain, and zero-delay dependency analyses. Assign stable
hierarchy paths and freeze markers using canonical source/symbol order, never
host pointers or unordered-container order.

**Verify:** Run all negative tests, shuffle equivalent declaration orders where
the contract allows it, and compare canonical frozen bytecode hashes.

**Commit:** `feat(ir): verify and freeze deterministic ACIR topology`

## Task 10: Implement the closed ACSim dialect

**Files:**

- Create: `include/acir/Dialect/ACSim/ACSimTypes.td`
- Create: `include/acir/Dialect/ACSim/ACSimOps.td`
- Create: `include/acir/Dialect/ACSim/ACSimTypes.h`
- Create: `include/acir/Dialect/ACSim/ACSimOps.h`
- Create: `lib/Dialect/ACSim/ACSimTypes.cpp`
- Create: `lib/Dialect/ACSim/ACSimOps.cpp`
- Create: `test/ACSim/types-valid.mlir`, `test/ACSim/types-invalid.mlir`
- Create: `test/ACSim/ops-valid.mlir`, `test/ACSim/ops-invalid.mlir`

**Red:** Cover all eleven public ACSim types and all twenty-one public ACSim
operations in the normative inventory. Test closed-dialect legality, exactly
one model, all exact fingerprints, construction order, unique owners, static
arrays, typed bindings, pure expressions, process PCs, wake typing, dispatch
thunks, activation IDs, and module exports.

**Green:** Implement the minimal closed representation. Process regions allow
only `builtin`, `arith`, `index`, and `cf`; ordinary edges cannot cross
suspension boundaries. ACSim contains no dynamic shape, opaque component,
runtime configuration, plugin, reflection, unresolved symbol, or untyped port.

**Verify:** Run inventory coverage, textual/bytecode round trips, invalid
dialect injection tests, and deterministic verifier diagnostics.

**Commit:** `feat(ir): implement canonical ACSim dialect`

### Task 10 corrective: separate generated realizations from library bindings

Hard-break the initial Task 10 representation before conversion work. Generated
`acsim.module` and `acsim.process` realizations use symbols plus specialization
fingerprints and never consume registry BindingRecords. Module interfaces are
closed compile-time construction metadata. Placements have one realization
target. Separate bounded deterministic owner expansion from runtime expansion;
generated-module wrappers participate only in owner expansion, while stateful
library placements and concrete processes receive runtime rows.

**Commit:** `fix(ir): separate generated ACSim realizations`

## Task 11: Implement binding locks and deterministic binding resolution

This task follows the Task 10 corrective and includes the separately reviewed
empty-lock corrective. A canonical empty binding lock is valid when the frozen
model requests no reusable external/library implementations. It MUST NOT create
records for generated modules, generated processes, or core IR primitives.

**Files:**

- Create: `include/acir/Bindings/Binding.h`
- Create: `include/acir/Bindings/Registry.h`
- Create: `lib/Bindings/Binding.cpp`, `lib/Bindings/Registry.cpp`
- Create: `lib/Transforms/ResolveBindings.cpp`
- Create: `schemas/acsim-binding.schema.json`
- Create: `tools/acir-opt/BindingOptions.cpp`
- Create: `test/Bindings/resolve-valid.mlir`
- Create: `test/Bindings/resolve-missing.mlir`
- Create: `test/Bindings/resolve-ambiguous.mlir`
- Create: `unittests/Bindings/CanonicalBindingTest.cpp`

**Red:** Test exact selection, no match, multiple matches, provider-order
permutations, normalization, forbidden raw C++ fragments, unavailable schemas,
epoch mismatch, effect mismatch, profile mismatch, and RFC 8785/SHA-256 lock
stability. Missing bindings must fail before ACSim creation with
`ACLOWER-BINDING-MISSING`.

**Green:** Implement typed immutable records, deterministic registry ordering,
exact selection predicates, canonical JSON, fingerprints, and binding-lock
emission. The registry exposes metadata only; it cannot carry emitter callbacks
or component behavior.

**Verify:** Validate locks against the schema, compare hashes across provider
order permutations, and inspect for forbidden component-name emitter dispatch.

**Commit:** `feat(lowering): resolve exact gfsim bindings`

## Task 12: Plan non-mutating process-state materialization

**Files:**

- Create: process-state analysis interfaces and focused analysis tests
- Do not mutate frozen ACIR or create partial ACSim

**Red:** Test deterministic PC assignment, live-across-suspend discovery,
typed state-slot plans, exact wake boundaries, and rejection of unsupported
control without changing the input IR.

**Green:** Produce one immutable process-state plan consumed by the later atomic
conversion. The analysis is iterative and bounded and leaves frozen ACIR
byte-for-byte unchanged.

**Verify:** Compare plans across canonical declaration permutations and prove
that analysis failure publishes neither mutations nor partial output.

**Commit:** `feat(lowering): plan ACSim process state`

## Task 13: Atomically lower the whole frozen model to ACSim

**Files:**

- Create: `lib/Conversion/ACIRToACSim/ACIRToACSim.cpp`
- Create: `lib/Conversion/ACIRToACSim/TypeConverter.cpp`
- Create: `include/acir/Conversion/ACIRToACSim/ACIRToACSim.h`
- Create: `test/Conversion/whole-model.mlir`
- Create: `test/Conversion/atomic-failure.mlir`
- Create: `test/Conversion/process-basic.mlir`
- Create: `test/Conversion/process-control-flow.mlir`
- Create: `test/Conversion/process-live-values.mlir`
- Create: `test/Conversion/process-invalid.mlir`

Task 12 owns `lib/Transforms/LowerProcessState.cpp`, the non-mutating
`ac-lower-process-state` pass, and the complete public ProcessStatePlan API.
Task 13 consumes that public header and a separate exact
`BindingResolutionResult`; it must not recreate process analysis, report
parsing, or pass ownership.

**Red:** Test one atomic conversion containing generated module interfaces,
nested ownership, homogeneous arrays, library bindings, typed graph edges,
process PCs/state slots, owner/runtime expansions, dispatch, activation,
exports, and stable ordering. Every failure leaves no ACSim model behind.

**Green:** Convert the entire selected frozen model in one transaction using the
non-mutating process-state plan. Publish canonical ACSim only after all
realization, ownership, runtime, interface, and binding checks succeed. There
is no supported structure-first or process-later partial conversion path.

**Verify:** Run whole-model and process tests, deterministic permutations, and
atomic-failure tests. Search conversion code for component-specific dispatch.

**Commit:** `feat(lowering): atomically lower ACIR to ACSim`

## Task 14: Add exhaustive inventory coverage and Phase 1 CI gates

Run this only after the Task 10 realization corrective, Task 11 empty-lock
corrective, non-mutating process-state plan, and atomic whole-model conversion.
These exhaustive Phase 1 gates validate that single corrected path; the former
structure-before-process split is not an alternative pipeline.

**Files:**

- Create: `contracts/acir-v0.1.yaml`, `contracts/acsim-v0.1.yaml`
- Create: `scripts/check-ir-coverage.py`
- Create: `tests/contracts/test_ir_coverage.py`
- Create: `docs/implementation/spec-coverage.md`
- Modify: `.github/workflows/ci.yml`
- Modify: `CMakePresets.json`, `CMakeLists.txt`

**Red:** Add a coverage test that deliberately reports every missing public
type, operation, invariant class, positive test, negative test, and source
symbol. Confirm it also rejects implementation-only public aliases and stale
epoch entries.

**Green:** Populate the generated ledger from ODS records, normative inventory
manifests, lit test annotations, and C++ registration tables. Add CI jobs for
Debug assertions, Release, lit/FileCheck, unit tests, schema/contracts,
clang-format, clang-tidy on owned sources, and clean-install consumer configure.
Task 14, not Task 12, owns installation/export of the ProcessStatePlan public
header and library targets, package configuration, and a separate installed-
tree ProcessStatePlan consumer.

**Verify:** Run every Phase 1 gate locally, configure from a clean build tree,
install to a temporary prefix, build a tiny consumer against the installed
package, build the installed-tree ProcessStatePlan consumer without source-tree
include paths, and perform a fresh-clone CI-equivalent check.

**Commit:** `test(ir): enforce complete ACIR and ACSim coverage`

## Phase 1 completion audit

Before merging the phase branch:

1. Compare all ODS operation/type names with both normative inventories.
2. Map every `Required verification`, `Freeze invariant`, ACSim verifier rule,
   and binding rule to an observed negative test.
3. Scan tracked files for `TODO`, `TBD`, `FIXME`, `placeholder`, skipped tests,
   legacy aliases, component-specific emitter names, and stale epoch strings.
4. Run Debug assertions, Release, all lit/unit/contract tests, format, tidy, and
   install-consumer checks from clean directories.
5. Run deterministic canonicalization and bytecode hash comparisons repeatedly.
6. Request an independent requirements review and code-quality review.
7. Push the reviewed commits and record their SHAs in the spec coverage ledger.

Only then may Phase 2 begin. Passing Phase 1 is progress toward the roadmap,
not completion of Agentic Circuit v0.1.

## Exact inventory manifest for the coverage gate

Task 14 derives its expected manifest from the following exact public names;
abbreviated family names in earlier task prose do not reduce this list.

ACIR operations:

`ac.system`, `ac.type_scope`, `ac.type_alias`, `ac.module`,
`ac.module.extern`, `ac.module.generated`, `ac.instance`, `ac.array`,
`ac.instances`, `ac.view`, `ac.port`, `ac.return`, `ac.queue`,
`ac.event_queue`, `ac.resource`, `ac.address_space`, `ac.address_map`,
`ac.time_domain`, `ac.struct`, `ac.enum`, `ac.union`, `ac.packet`,
`ac.transaction`, `ac.interface`, `ac.protocol`, `ac.role`, `ac.state`,
`ac.event`, `ac.transition`, `ac.guarantee`, `ac.process`,
`ac.record.create`, `ac.record.get`, `ac.record.with`,
`ac.packet.serialize`, `ac.packet.deserialize`, `ac.try_send`, `ac.try_recv`,
`ac.schedule`, `ac.wait_until`, `ac.wait_for`, `ac.await_event`,
`ac.yield_sim`, `ac.trace.open`, `ac.trace.next`, `ac.trace.decode`,
`ac.trace.eof`, `ac.trace.position`, `ac.require`, `ac.ensure`, `ac.assert`,
`ac.probe`, `ac.stat`, `ac.stat.add`, and `ac.instrumentation`.

ACIR types:

`!ac.struct<@name>`, `!ac.packet<@name>`, `!ac.transaction<@name>`,
`!ac.enum<@name>`, `!ac.union<@name>`, `!ac.optional<T>`, `!ac.list<T>`,
`!ac.vector<N x T>`, `!ac.flow<T, Protocol>`,
`!ac.endpoint<Interface, Role>`, `!ac.resource_ref<ResourceType, Role>`,
`!ac.channel<T, Protocol>`, `!ac.duration<unit>`,
`!ac.rate<numerator, denominator>`, `!ac.event<T>`, `!ac.address<@space>`, and
`!ac.resource_token<@resource>`.

ACSim types:

`!acsim.value<@cpp_type>`, `!acsim.expr<@cpp_type>`,
`!acsim.owner<@realization>`, `!acsim.ref<@realization>`,
`!acsim.port<@interface, @role, @payload, @protocol>`,
`!acsim.resource<@resource, @role>`, `!acsim.array<[shape], element_type>`,
`!acsim.object_id`, `!acsim.activation_id`, `!acsim.pc<@process>`, and
`!acsim.wake<@kind>`.

ACSim operations:

`acsim.model`, `acsim.type`, `acsim.binding`, `acsim.module`,
`acsim.instance`, `acsim.array`, `acsim.element`, `acsim.port`,
`acsim.resource`, `acsim.bind`, `acsim.inline`, `acsim.process`,
`acsim.live.load`, `acsim.live.store`, `acsim.invoke`, `acsim.continue`,
`acsim.suspend`, `acsim.terminate`, `acsim.export`, `acsim.dispatch`,
`acsim.activate`, and `acsim.return`.
