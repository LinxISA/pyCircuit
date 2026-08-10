# Phase 3 Binding and Structured C++ Generation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn verified canonical ACSim plus frozen immutable build inputs into deterministic C++20 sources, a same-toolchain compiled executable, exact manifests, and an atomically published immutable build.

**Architecture:** Verified ACSim is converted once into an immutable typed `ModelPlan`; pure generation converts that plan into an ordered `SourceBundle`; build orchestration validates an exact `CompilePlan` and `BuildManifest` before staging, compiling, linking, checking the embedded fingerprint, and publishing. The compiler library is the supported Phase 3 surface, while `acir-cxxgen` is an internal developer and conformance driver; the public `agentic-circuit` CLI remains Phase 4 work.

**Tech Stack:** C++20, LLVM 22 support libraries, MLIR, repository RFC 8785/I-JSON canonicalization, gfsim, GoogleTest, LLVM lit/FileCheck, CMake/CTest, SHA-256.

## Global Constraints

- Global contract epoch is exactly `0.1`; the ACSim namespace and operation inventory remain closed.
- Generation accepts only a module for which `acir::acsim::verifyCanonicalACSimFile` succeeds.
- Canonical ACSim is the sole construction input; frozen ACIR and the binding lock are immutable staged/fingerprint inputs only.
- Every fingerprint is spelled `sha256:` followed by 64 lowercase hexadecimal digits and has a closed, versioned preimage.
- Canonical JSON uses the repository RFC 8785/I-JSON implementation in `acir::bindings`.
- The emitter may branch on plan node kind and normalized binding mapping kind, never component name, family, provider namespace, binding ID, C++ symbol, or catalog entry.
- Generated sources contain no timestamps, random values, host addresses, current working directory, temporary directories, or host-specific absolute source paths.
- Generated ownership is concrete and by value; generated modules use non-owning hierarchy attachment, not erased factories or runtime topology.
- Compiler and linker subprocesses use argument vectors, never shell command strings.
- Published builds are immutable at `<output>/builds/<build-fingerprint>`; `current.json` changes only through sibling-file atomic rename after every gate passes.
- The generated simulator has no Python dependency, schema walker, runtime plugin lookup, descriptor interpreter, component-name emitter branch, coroutine, `std::function` process frame, or hot-path RTTI.
- Each task follows red-green-refactor, runs its focused test first, then the broader affected suite, and ends in one reviewable commit.
- Preserve the unrelated untracked `phase-1-pr-description.md`; never stage or modify it.

---

## File and Responsibility Map

| File | Responsibility |
| --- | --- |
| `include/acir/CodeGen/Manifest.h` | Exact fingerprint types and schema-shaped build-manifest value types. |
| `lib/CodeGen/Manifest.cpp` | Closed validation, canonical JSON, and fingerprint preimages. |
| `include/acir/CodeGen/ModelPlan.h` | Immutable typed representation of verified ACSim construction state. |
| `lib/CodeGen/ModelPlan.cpp` | Canonical ACSim extraction and plan validation. |
| `include/acir/CodeGen/Generator.h` | Ordered source-bundle API and source-contract validator. |
| `lib/CodeGen/Generator.cpp` | Generic module, binding, expression, model, dispatch, and activation emission. |
| `lib/CodeGen/ProcessGenerator.cpp` | Closed enum-PC process-operation emission. |
| `include/acir/CodeGen/Build.h` | Compile-plan, toolchain, build request/result, and staged-build API. |
| `lib/CodeGen/CompilePlan.cpp` | Toolchain preflight and canonical compile-plan construction. |
| `lib/CodeGen/Build.cpp` | Ordered checks, compiler/linker execution, and embedded-fingerprint validation. |
| `lib/CodeGen/BuildInternal.h` | Testable process/filesystem services and failure hooks kept out of the public API. |
| `lib/CodeGen/Staging.cpp` | Contained private stages, immutable cache reuse, and atomic publication. |
| `include/gfsim/object.h` | Shared owning/non-owning deterministic hierarchy index. |
| `tools/acir-cxxgen/*` | Internal stage-aware driver over the library API. |
| `unittests/CodeGen/*` | Contract, extraction, emission, build, staging, and negative unit tests. |
| `test/CodeGen/*` | Golden driver, determinism, extension, forbidden-dependency, and compile/link tests. |
| `docs/implementation/phase-3-audit.md` | Exit-gate evidence and residual-risk audit. |

---

### Task 1: Exact fingerprints and build-manifest contract

**Files:**
- Modify: `include/acir/CodeGen/Manifest.h`
- Modify: `lib/CodeGen/Manifest.cpp`
- Modify: `lib/CodeGen/CMakeLists.txt`
- Modify: `unittests/CodeGen/CodeGenTest.cpp`
- Test fixture: `schemas/build-manifest.schema.json`

**Interfaces:**
- Consumes: `acir::bindings::canonicalizeJson(const llvm::json::Value &)` and LLVM SHA-256.
- Produces: `Fingerprint`, `isValidFingerprint`, `fingerprintCanonicalJson`, `BuildManifest::validate`, `BuildManifest::canonicalJson`, and `BuildManifest::finalizeBuildFingerprint`.

- [x] **Step 1: Replace prototype expectations with failing exact-contract tests**

```cpp
TEST(CodeGenManifestTest, FingerprintsUseNormativeSpelling) {
  EXPECT_EQ(computeFingerprint("hello"),
            "sha256:2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
  EXPECT_TRUE(isValidFingerprint(computeFingerprint("hello")));
  EXPECT_FALSE(isValidFingerprint(std::string(64, '0')));
}

TEST(CodeGenManifestTest, CanonicalManifestMatchesClosedSchemaShape) {
  BuildManifest manifest = makeCompleteManifestFixture();
  ASSERT_THAT_ERROR(manifest.finalizeBuildFingerprint(), Succeeded());
  auto json = manifest.canonicalJson();
  ASSERT_TRUE(static_cast<bool>(json));
  EXPECT_EQ(*json, expectedCanonicalManifestFixture());
  EXPECT_EQ(manifest.schema, "agentic-circuit-build-manifest");
  EXPECT_EQ(manifest.version, "0.1");
}

TEST(CodeGenManifestTest, ManifestRejectsMissingAndNonCanonicalFields) {
  auto manifest = makeCompleteManifestFixture();
  manifest.project.name.clear();
  EXPECT_THAT_ERROR(manifest.validate(), Failed());
  manifest = makeCompleteManifestFixture();
  manifest.sourceFiles[0].path = "../escape.cpp";
  EXPECT_THAT_ERROR(manifest.validate(), Failed());
}
```

- [x] **Step 2: Run the focused tests and confirm the RED state**

Run: `cmake --build --preset dev-llvm22 --target CodeGenTests && build/dev-llvm22/bin/CodeGenTests --gtest_filter='CodeGenManifestTest.*'`

Expected: compilation fails because the schema-shaped API and normative fingerprint spelling do not exist.

- [x] **Step 3: Define the exact schema-shaped manifest types**

```cpp
using Fingerprint = std::string;

bool isValidFingerprint(llvm::StringRef value);
Fingerprint computeFingerprint(llvm::StringRef bytes);
llvm::Expected<Fingerprint>
fingerprintCanonicalJson(const llvm::json::Value &value);

struct Identity { std::string name; std::string identity; };
struct FileHash { std::string path; Fingerprint sha256; };
struct CompilerIdentity {
  std::string name;
  std::string buildId;
  std::string toolchainTarget;
};
struct ProviderIdentity {
  std::string nameSpace;
  Fingerprint schemaFingerprint;
  Fingerprint implementationFingerprint;
};
struct ComponentSpecialization {
  std::string canonicalName;
  Fingerprint schemaFingerprint;
  Fingerprint specializationFingerprint;
};
struct NamedFingerprint { std::string name; Fingerprint fingerprint; };
enum class ArtifactKind { Acpy, Acir, Acsim, CppSource, CppHeader, Executable, Report };
struct Artifact { std::string path; ArtifactKind kind; Fingerprint sha256; };
enum class ValidationStatus { Passed, Failed };
struct ValidationGate {
  std::string name;
  ValidationStatus status;
  std::optional<Fingerprint> reportSha256;
};
struct SpecializationInput {
  std::string name;
  std::string acirType;
  llvm::json::Value canonicalValue;
};

struct BuildManifest {
  std::string schema = "agentic-circuit-build-manifest";
  std::string version = "0.1";
  std::string contractEpoch = "0.1";
  Identity project;
  Identity system;
  std::vector<FileHash> sourceFiles;
  Fingerprint normalizedAcirSha256;
  CompilerIdentity compiler;
  std::vector<std::string> passPipeline;
  std::vector<ProviderIdentity> providers;
  std::vector<ComponentSpecialization> componentSpecializations;
  std::vector<NamedFingerprint> protocolIdentities;
  std::vector<Artifact> artifacts;
  std::vector<ValidationGate> validationGates;
  std::string buildProfile;
  std::vector<std::string> instrumentationLayers;
  std::vector<SpecializationInput> specializationInputs;
  Fingerprint buildFingerprint;

  llvm::Error validate() const;
  llvm::Expected<std::string> canonicalJson() const;
  llvm::Error finalizeBuildFingerprint();
};
```

`finalizeBuildFingerprint` canonicalizes the complete object with an empty `build_fingerprint`, fingerprints the versioned preimage `{"domain":"agentic-circuit-build-0.1","manifest":...}`, stores it, and revalidates. Semantic sets sort by their schema keys; `pass_pipeline` and `source_files` retain declared order.

- [x] **Step 4: Implement closed validation and canonical serialization**

Use `llvm::json::Object` construction followed by `bindings::canonicalizeJson`; validate exact constants, non-empty identities, enum spellings, unique stable keys, normalized relative paths, and every `sha256:` field before serialization. Link `ACIRCodeGen` publicly to `ACIRBindings`.

- [x] **Step 5: Run focused and complete CodeGen tests**

Run: `cmake --build --preset dev-llvm22 --target CodeGenTests && build/dev-llvm22/bin/CodeGenTests --gtest_filter='CodeGenManifestTest.*' && ctest --test-dir build/dev-llvm22 -R '^CodeGenTests$' --output-on-failure`

Expected: all selected tests pass and the fixture bytes are canonical.

- [x] **Step 6: Commit the exact manifest boundary**

```bash
git add include/acir/CodeGen/Manifest.h lib/CodeGen/Manifest.cpp lib/CodeGen/CMakeLists.txt unittests/CodeGen/CodeGenTest.cpp
git commit -m "feat(codegen): enforce exact build manifest"
```

---

### Task 2: Deterministic non-owning hierarchy attachment

**Files:**
- Modify: `include/gfsim/object.h`
- Modify: `unittests/gfsim/core_test.cpp`

**Interfaces:**
- Consumes: existing `gfsim::Module::addChild` ownership and canonical path refresh.
- Produces: `bool gfsim::Module::attachChild(SimObject &child)` with one shared child-order/traversal index.

- [x] **Step 1: Add failing hierarchy tests**

```cpp
TEST(ModuleHierarchyTest, AttachedByValueChildParticipatesInWalkAndPaths) {
  Module parent("top", 0, nullptr);
  TestObject child("worker", 1, nullptr);
  ASSERT_TRUE(parent.attachChild(child));
  EXPECT_EQ(child.parent(), &parent);
  EXPECT_EQ(child.path(), "top.worker");
  std::vector<ObjectId> ids;
  parent.walk([&](SimObject &object) { ids.push_back(object.id()); });
  EXPECT_EQ(ids, (std::vector<ObjectId>{0, 1}));
}

TEST(ModuleHierarchyTest, DuplicateOrCrossParentAttachmentIsRejected) {
  Module first("first", 0, nullptr);
  Module second("second", 2, nullptr);
  TestObject child("worker", 1, nullptr);
  ASSERT_TRUE(first.attachChild(child));
  EXPECT_FALSE(first.attachChild(child));
  EXPECT_FALSE(second.attachChild(child));
}
```

- [x] **Step 2: Run and observe the missing API**

Run: `cmake --build --preset dev-llvm22 --target GfsimTests && build/dev-llvm22/bin/GfsimTests --gtest_filter='ModuleHierarchyTest.*'`

Expected: compilation fails because `attachChild` is absent.

- [x] **Step 3: Implement one ownership store and one ordered view**

```cpp
bool attachChild(SimObject &child) {
  if (&child == this || child.parent() != nullptr || containsChild(&child))
    return false;
  child.setParent(this);
  child.refreshPathRecursively();
  children_.push_back(&child);
  return true;
}

template <typename T, typename... Args>
T &addChild(Args &&...args) {
  auto owned = std::make_unique<T>(std::forward<Args>(args)...);
  T &result = *owned;
  ownedChildren_.push_back(std::move(owned));
  if (!attachChild(result))
    throw std::logic_error("owned child attachment failed");
  return result;
}
```

Keep `ownedChildren_` for destruction and `children_` as the only traversal/order view. Add only the narrow private setters/friendship required for parent and recursive path refresh.

- [x] **Step 4: Run hierarchy and full gfsim tests**

Run: `cmake --build --preset dev-llvm22 --target GfsimTests && build/dev-llvm22/bin/GfsimTests --gtest_filter='ModuleHierarchyTest.*' && ctest --test-dir build/dev-llvm22 -R '^GfsimTests$' --output-on-failure`

Expected: attached and owned children have identical walk/reset/path behavior; duplicate attachment is rejected.

- [x] **Step 5: Commit the hierarchy API**

```bash
git add include/gfsim/object.h unittests/gfsim/core_test.cpp
git commit -m "feat(gfsim): attach non-owning module children"
```

---

### Task 3: ModelPlan identities, types, runtime rows, and activation

**Files:**
- Create: `include/acir/CodeGen/ModelPlan.h`
- Create: `lib/CodeGen/ModelPlan.cpp`
- Create: `unittests/CodeGen/ModelPlanTest.cpp`
- Modify: `lib/CodeGen/CMakeLists.txt`
- Modify: `unittests/CodeGen/CMakeLists.txt`

**Interfaces:**
- Consumes: `acir::acsim::verifyCanonicalACSimFile(mlir::ModuleOp)` and the exact ACSim v0.1 attributes.
- Produces: `llvm::Expected<ModelPlan> buildModelPlan(mlir::ModuleOp)` and `llvm::Error validateModelPlan(const ModelPlan &)`.

- [x] **Step 1: Add a reusable canonical ACSim test loader and failing extraction test**

```cpp
TEST(ModelPlanTest, ExtractsClosedIdentitiesAndDenseRuntimePlan) {
  auto fixture = parseCanonicalACSimFixture("test/ACSim/ops-valid.mlir");
  ASSERT_TRUE(static_cast<bool>(fixture));
  auto plan = buildModelPlan(**fixture);
  ASSERT_TRUE(static_cast<bool>(plan));
  EXPECT_EQ(plan->contractEpoch, "0.1");
  EXPECT_EQ(plan->runtimeObjects.front().objectId, 0u);
  for (size_t i = 0; i < plan->runtimeObjects.size(); ++i)
    EXPECT_EQ(plan->runtimeObjects[i].objectId, i);
  EXPECT_TRUE(std::is_sorted(plan->activationEdges.begin(),
                             plan->activationEdges.end()));
}

TEST(ModelPlanTest, RejectsNonCanonicalOrMixedInput) {
  auto mixed = parseModule("module { ac.system @top ... }");
  EXPECT_THAT_EXPECTED(buildModelPlan(*mixed), Failed());
}
```

- [x] **Step 2: Run the focused test and confirm the missing types**

Run: `cmake --build --preset dev-llvm22 --target CodeGenTests`

Expected: compilation fails because `ModelPlan.h` and `buildModelPlan` do not exist.

- [x] **Step 3: Define the first immutable plan slice**

```cpp
enum class TypeKind {
  Accessor, Implementation, Interface, Packet, Policy, Protocol, Provider,
  Resource, Role, Schema, TimeDomain, Value, Wake, Payload
};
struct TypePlan {
  std::string symbol;
  TypeKind kind;
  std::string cppType;
  Fingerprint fingerprint;
};
struct RuntimeObjectPlan {
  uint32_t objectId;
  uint32_t activationId;
  std::string targetSymbol;
  std::string hierarchyPath;
  std::vector<uint64_t> indices;
  RuntimeObjectKind objectKind;
  std::string workThunk;
  std::string xferThunk;
  std::string resetThunk;
  std::string validateThunk;
};
struct ActivationEdgePlan {
  uint32_t sourceId;
  uint32_t targetId;
  auto operator<=>(const ActivationEdgePlan &) const = default;
};
struct SourceMapPlan { std::string stableIdentity; std::string source; };
struct ModelPlan {
  std::string modelSymbol;
  std::string contractEpoch;
  Fingerprint frozenAcirFingerprint;
  Fingerprint bindingLockFingerprint;
  Fingerprint providerFingerprint;
  Fingerprint profileFingerprint;
  Fingerprint toolchainFingerprint;
  Fingerprint schemaSetFingerprint;
  std::vector<TypePlan> types;
  std::vector<RuntimeObjectPlan> runtimeObjects;
  std::vector<ActivationEdgePlan> activationEdges;
  std::vector<SourceMapPlan> sourceMap;
};
```

- [x] **Step 4: Extract only after canonical ACSim verification**

Implement a two-phase builder: verify and collect symbols into temporary maps, then materialize sorted value vectors. Reject duplicate symbols, invalid fingerprint spellings, non-dense object IDs, unsorted/duplicate activation edges, missing sources/targets, and activation offsets that do not reconstruct the exact edge list. Never retain `Operation *`, `Attribute`, or `Value` in `ModelPlan`.

- [x] **Step 5: Run focused tests and verifier tests**

Run: `cmake --build --preset dev-llvm22 --target CodeGenTests ACSimOpsTests && build/dev-llvm22/bin/CodeGenTests --gtest_filter='ModelPlanTest.*' && ctest --test-dir build/dev-llvm22 -R '^(CodeGenTests|ACSimOpsTests)$' --output-on-failure`

Expected: canonical fixture extracts identically on repeated runs and malformed input is rejected before plan construction.

- [x] **Step 6: Commit the plan identity boundary**

```bash
git add include/acir/CodeGen/ModelPlan.h lib/CodeGen/ModelPlan.cpp lib/CodeGen/CMakeLists.txt unittests/CodeGen/ModelPlanTest.cpp unittests/CodeGen/CMakeLists.txt
git commit -m "feat(codegen): extract canonical model plan"
```

---

### Task 4: ModelPlan hierarchy, bindings, expressions, and process state

**Files:**
- Modify: `include/acir/CodeGen/ModelPlan.h`
- Modify: `lib/CodeGen/ModelPlan.cpp`
- Modify: `unittests/CodeGen/ModelPlanTest.cpp`

**Interfaces:**
- Consumes: Task 3 `ModelPlan` identities and runtime rows.
- Produces: closed `BindingPlan`, `ModulePlan`, `PlacementPlan`, `ExpressionPlan`, `ProcessPlan`, and typed process-operation variants.

- [x] **Step 1: Add failing full-fixture extraction assertions**

```cpp
TEST(ModelPlanTest, ExtractsHierarchyBindingsExpressionsAndProcesses) {
  auto plan = buildFixturePlan();
  ASSERT_TRUE(static_cast<bool>(plan));
  ASSERT_FALSE(plan->modules.empty());
  EXPECT_TRUE(isStrictlySortedBySymbol(plan->modules));
  EXPECT_TRUE(isStrictlySortedBySymbol(plan->bindings));
  const ProcessPlan &process = plan->modules.front().processes.front();
  EXPECT_GT(process.fairnessWork, 0u);
  EXPECT_TRUE(isDensePcSet(process.states));
  EXPECT_TRUE(isStrictlySortedByOrdinal(process.liveSlots));
  EXPECT_TRUE(std::holds_alternative<TerminatePlan>(
      process.states.back().terminator));
}

TEST(ModelPlanTest, CopiesOnlyStructuredBindingMetadata) {
  auto plan = buildFixturePlan();
  ASSERT_TRUE(static_cast<bool>(plan));
  for (const BindingPlan &binding : plan->bindings) {
    EXPECT_TRUE(isValidCppQualifiedName(binding.cppSymbol));
    EXPECT_TRUE(isValidIncludePath(binding.header));
    EXPECT_EQ(binding.cppSymbol.find(';'), std::string::npos);
  }
}
```

- [x] **Step 2: Run and confirm plan fields are absent**

Run: `cmake --build --preset dev-llvm22 --target CodeGenTests`

Expected: compilation fails on the new closed plan types.

- [x] **Step 3: Add closed plan variants**

```cpp
enum class PlacementKind { GeneratedModule, ExternalStateful, HomogeneousArray };
enum class ParameterMappingKind { TemplateArgument, ConstexprArgument, ConstructorConstant };
struct ParameterPlan {
  std::string name;
  std::string acirType;
  std::string cppType;
  llvm::json::Value canonicalValue;
  uint32_t ordinal;
  ParameterMappingKind mapping;
};
struct BindingPlan {
  std::string symbol;
  std::string bindingId;
  std::string header;
  std::string target;
  std::string cppSymbol;
  std::string conceptName;
  std::string effect;
  Fingerprint recordFingerprint;
  std::vector<ParameterPlan> parameters;
  std::vector<AccessorPlan> accessors;
  std::vector<EntryPointPlan> entryPoints;
};
struct PlacementPlan {
  PlacementKind kind;
  std::string symbol;
  std::string memberName;
  std::string target;
  std::vector<uint64_t> shape;
  std::vector<ConstructorArgumentPlan> constructorArguments;
};
using ProcessOperationPlan = std::variant<ConstantPlan, ArithmeticPlan,
    IndexPlan, InlineCallPlan, InvokePlan, LiveLoadPlan, LiveStorePlan>;
using ProcessTerminatorPlan = std::variant<ContinuePlan, SuspendPlan, TerminatePlan>;
struct PcStatePlan {
  uint32_t ordinal;
  std::string name;
  std::vector<ProcessOperationPlan> operations;
  ProcessTerminatorPlan terminator;
};
struct ProcessPlan {
  std::string symbol;
  std::string className;
  Fingerprint specializationFingerprint;
  uint64_t fairnessWork;
  std::vector<LiveSlotPlan> liveSlots;
  std::vector<PcStatePlan> states;
};
struct ModulePlan {
  std::string symbol;
  std::string className;
  Fingerprint specializationFingerprint;
  std::vector<PlacementPlan> placements;
  std::vector<BindPlan> binds;
  std::vector<ExpressionPlan> expressions;
  std::vector<ProcessPlan> processes;
  std::vector<ExportPlan> exports;
};
```

- [x] **Step 4: Extract exact ACSim attributes and region order**

Resolve every symbol through a prebuilt symbol table; convert canonical JSON-valued attributes to owned `llvm::json::Value`; preserve only semantically ordered operation sequences; sort symbol sets by canonical symbol. Validate ownership targets, array ranks and nonzero extents, accessor/type agreement, pure-expression acyclicity, closed PCs, live-slot ordinals/types, wake records, and runtime-row coverage.

- [x] **Step 5: Add independent canonical-parse determinism**

Parse the canonical fixture in two independent MLIR contexts, build both plans, serialize their complete stable identities and nested construction records through a test-only stable printer, and require byte equality. Canonical ACSim itself admits only one declaration order; pre-canonical legal-order permutations remain covered by the end-to-end determinism gate in Task 11. Require a one-byte fingerprint change to change the plan identity there.

- [x] **Step 6: Run the expanded ModelPlan suite**

Run: `cmake --build --preset dev-llvm22 --target CodeGenTests && build/dev-llvm22/bin/CodeGenTests --gtest_filter='ModelPlanTest.*'`

Expected: hierarchy, arrays, bindings, pure expressions, process state, runtime rows, source maps, and activation extract exactly and deterministically.

- [x] **Step 7: Commit the complete typed plan**

```bash
git add include/acir/CodeGen/ModelPlan.h lib/CodeGen/ModelPlan.cpp unittests/CodeGen/ModelPlanTest.cpp
git commit -m "feat(codegen): model hierarchy and process plans"
```

---

### Task 5: Pure structured module and binding generation

**Files:**
- Create: `include/acir/CodeGen/Generator.h`
- Create: `lib/CodeGen/Generator.cpp`
- Create: `unittests/CodeGen/GeneratorTest.cpp`
- Modify: `include/acir/CodeGen/Emitter.h`
- Modify: `lib/CodeGen/Emitter.cpp`
- Modify: `lib/CodeGen/CMakeLists.txt`
- Modify: `unittests/CodeGen/CMakeLists.txt`

**Interfaces:**
- Consumes: complete validated `ModelPlan` from Task 4 and fingerprint API from Task 1.
- Produces: `SourceBundle`, `generateModelSources`, and `validateSourceBundle`.

- [ ] **Step 1: Add failing source-bundle tests**

```cpp
TEST(GeneratorTest, EmitsExactOrderedFileSetAndTypedOwnership) {
  ModelPlan plan = makeGeneratorFixturePlan();
  auto bundle = generateModelSources(plan);
  ASSERT_TRUE(static_cast<bool>(bundle));
  EXPECT_TRUE(isStrictlySortedByPath(bundle->files));
  EXPECT_THAT(paths(*bundle), Contains("include/generated/model.h"));
  EXPECT_THAT(paths(*bundle), Contains("src/generated/main.cpp"));
  EXPECT_THAT(file(*bundle, "include/generated/modules/top.h").content,
              HasSubstr("final : public gfsim::Module"));
  EXPECT_THAT(file(*bundle, "include/generated/modules/top.h").content,
              HasSubstr("ac::std::Queue<"));
  EXPECT_THAT(file(*bundle, "src/generated/modules/top.cpp").content,
              HasSubstr("attachChild(queue_)"));
}

TEST(GeneratorTest, RejectsExecutableCppInStructuredMetadata) {
  ModelPlan plan = makeGeneratorFixturePlan();
  plan.bindings.front().cppSymbol = "Queue; system(\"bad\")";
  EXPECT_THAT_EXPECTED(generateModelSources(plan), Failed());
}
```

- [ ] **Step 2: Run and confirm the pure generator API is missing**

Run: `cmake --build --preset dev-llvm22 --target CodeGenTests`

Expected: compilation fails because `Generator.h` is absent.

- [ ] **Step 3: Define ordered source types**

```cpp
struct GeneratedFile {
  std::string relativePath;
  std::string content;
  Fingerprint fingerprint;
};
struct SourceBundle { std::vector<GeneratedFile> files; };

llvm::Expected<SourceBundle> generateModelSources(const ModelPlan &plan);
llvm::Error validateSourceBundle(const ModelPlan &plan,
                                 const SourceBundle &bundle);
```

- [ ] **Step 4: Implement lexical safety and structured value spelling**

Add private total functions for C++ identifiers, qualified names, include paths, type tokens, integer/boolean/null/string literals, arrays, and records. Each function returns `llvm::Expected<std::string>` and rejects control characters, comments, directives, semicolons, unmatched delimiters, and tokens outside the binding grammar. No emitter entry accepts a raw statement body from a binding record.

- [ ] **Step 5: Generate every module specialization generically**

Emit final classes with by-value generated/external members, nested `std::array` for homogeneous shapes, fixed named members for heterogeneous collections, constructor initialization in plan order, `attachChild` checks, typed accessor/bind calls, pure-expression locals, exports, and process members. Remove the prototype `generateModuleSource` fabrication path and replace raw process-body APIs in public headers with plan-driven internals.

- [ ] **Step 6: Validate exact paths and reproducibility**

`validateSourceBundle` requires the fixed common files plus one header/source pair for every module and process specialization, unique normalized relative paths, strict path order, exact content hashes, no carriage returns, and absence of nondeterministic host tokens supplied by the test fixture.

- [ ] **Step 7: Run generator and complete CodeGen tests**

Run: `cmake --build --preset dev-llvm22 --target CodeGenTests && build/dev-llvm22/bin/CodeGenTests --gtest_filter='GeneratorTest.*' && ctest --test-dir build/dev-llvm22 -R '^CodeGenTests$' --output-on-failure`

Expected: repeated generation produces byte-identical files and fingerprints; malformed tokens fail before output exists.

- [ ] **Step 8: Commit structured module generation**

```bash
git add include/acir/CodeGen/Generator.h lib/CodeGen/Generator.cpp unittests/CodeGen/GeneratorTest.cpp include/acir/CodeGen/Emitter.h lib/CodeGen/Emitter.cpp lib/CodeGen/CMakeLists.txt unittests/CodeGen/CMakeLists.txt
git commit -m "feat(codegen): generate typed module sources"
```

---

### Task 6: Closed enum-PC process generation

**Files:**
- Create: `lib/CodeGen/ProcessGenerator.cpp`
- Modify: `lib/CodeGen/Generator.cpp`
- Modify: `lib/CodeGen/CMakeLists.txt`
- Modify: `unittests/CodeGen/GeneratorTest.cpp`

**Interfaces:**
- Consumes: `ProcessPlan` and closed operation variants from Task 4.
- Produces: process header/source pairs used by `generateModelSources`.

- [ ] **Step 1: Add failing multi-suspension process golden test**

```cpp
TEST(GeneratorTest, EmitsClosedEnumPcProcessWithoutRawFrames) {
  auto bundle = generateModelSources(makeMultiSuspensionPlan());
  ASSERT_TRUE(static_cast<bool>(bundle));
  const auto &header = file(*bundle, "include/generated/processes/top_work.h").content;
  const auto &source = file(*bundle, "src/generated/processes/top_work.cpp").content;
  EXPECT_THAT(header, HasSubstr("enum class Pc : uint8_t"));
  EXPECT_THAT(header, HasSubstr("static constexpr uint64_t kFairnessWork"));
  EXPECT_THAT(source, HasSubstr("switch (static_cast<Pc>(pc))"));
  EXPECT_THAT(source, HasSubstr("ProcessStep::suspend"));
  EXPECT_THAT(source, HasSubstr("ProcessStep::terminate"));
  EXPECT_THAT(source, Not(HasSubstr("std::function")));
  EXPECT_THAT(source, Not(HasSubstr("co_await")));
}
```

- [ ] **Step 2: Run and observe that plan operations are not emitted**

Run: `cmake --build --preset dev-llvm22 --target CodeGenTests && build/dev-llvm22/bin/CodeGenTests --gtest_filter='GeneratorTest.EmitsClosedEnumPcProcessWithoutRawFrames'`

Expected: the test fails because the process files do not contain the structured state machine.

- [ ] **Step 3: Emit the smallest sufficient PC and committed/proposed state**

Choose `uint8_t`, `uint16_t`, or `uint32_t` from the maximum PC ordinal. Emit one committed and proposed field per typed live slot, reset code for entry PC and values, and an exhaustive switch. The default case returns an invalid-PC failure; it never falls through.

- [ ] **Step 4: Emit each closed operation variant with a total visitor**

```cpp
return std::visit(Overloaded{
  [&](const ConstantPlan &op) { return emitConstant(op); },
  [&](const ArithmeticPlan &op) { return emitArithmetic(op); },
  [&](const IndexPlan &op) { return emitIndex(op); },
  [&](const InlineCallPlan &op) { return emitInlineCall(op); },
  [&](const InvokePlan &op) { return emitInvoke(op); },
  [&](const LiveLoadPlan &op) { return emitLiveLoad(op); },
  [&](const LiveStorePlan &op) { return emitLiveStore(op); },
}, operation);
```

Emit `continue`, `suspend`, and `terminate` from their typed terminator variants. Validate operation result dominance, exact types, legal binding effect, wake source IDs, next-PC membership, and state termination before spelling C++.

- [ ] **Step 5: Run positive and negative process tests**

Run: `cmake --build --preset dev-llvm22 --target CodeGenTests && build/dev-llvm22/bin/CodeGenTests --gtest_filter='GeneratorTest.*Process*'`

Expected: multi-suspension state emits exact code; illegal cross-PC values, missing terminators, invalid wake IDs, and effect mismatches return stable errors.

- [ ] **Step 6: Commit process generation**

```bash
git add lib/CodeGen/ProcessGenerator.cpp lib/CodeGen/Generator.cpp lib/CodeGen/CMakeLists.txt unittests/CodeGen/GeneratorTest.cpp
git commit -m "feat(codegen): emit enum pc processes"
```

---

### Task 7: Model harness, dispatch, activation, and embedded identity

**Files:**
- Modify: `lib/CodeGen/Generator.cpp`
- Modify: `unittests/CodeGen/GeneratorTest.cpp`
- Create: `unittests/CodeGen/GeneratedModelCompileTest.cpp`
- Modify: `unittests/CodeGen/CMakeLists.txt`

**Interfaces:**
- Consumes: dense runtime rows and activation edges from `ModelPlan`, `gfsim::DispatchRow`, and hierarchy attachment.
- Produces: `generated/model.h`, `generated/model.cpp`, `generated/dispatch.h`, `generated/main.cpp`, and a readable embedded build fingerprint.

- [ ] **Step 1: Add failing harness and dispatch tests**

```cpp
TEST(GeneratorTest, EmitsDenseDispatchAndCanonicalActivation) {
  auto bundle = generateModelSources(makeGeneratorFixturePlan());
  ASSERT_TRUE(static_cast<bool>(bundle));
  const auto &dispatch = file(*bundle, "include/generated/dispatch.h").content;
  EXPECT_THAT(dispatch, HasSubstr("std::array<gfsim::DispatchRow, 3>"));
  EXPECT_LT(dispatch.find("makeDispatchRow(&model.producer_)"),
            dispatch.find("makeDispatchRow(&model.consumer_)"));
  EXPECT_THAT(dispatch, HasSubstr("kActivationOffsets"));
  EXPECT_THAT(dispatch, HasSubstr("kActivationTargets"));
}

TEST(GeneratedModelCompileTest, MinimalBundleCompilesLinksAndPrintsFingerprint) {
  auto result = compileFixtureBundle(makeMinimalRunnablePlan());
  ASSERT_TRUE(static_cast<bool>(result));
  EXPECT_EQ(runAndCapture(result->executable, {"--build-fingerprint"}),
            result->buildFingerprint + "\n");
}
```

- [ ] **Step 2: Run and observe missing harness behavior**

Run: `cmake --build --preset dev-llvm22 --target CodeGenTests && build/dev-llvm22/bin/CodeGenTests --gtest_filter='GeneratorTest.EmitsDenseDispatchAndCanonicalActivation:GeneratedModelCompileTest.*'`

Expected: tests fail because the complete runnable harness and embedded fingerprint are absent.

- [ ] **Step 3: Emit static construction and runtime tables**

Emit a final root model owning its top module, one `std::array<gfsim::DispatchRow, N>` in object-ID order using `makeDispatchRow(&typed_member)`, and canonical activation offsets/targets. Construct `SimSystem` with the supplied tables; do not walk hierarchy to discover runtime objects.

- [ ] **Step 4: Emit a bounded fingerprint query**

Embed `inline constexpr std::string_view kBuildFingerprint = "sha256:..."` in generated model code and make generated `main` accept only the internal `--build-fingerprint` preflight query plus normal simulator startup. The build library compares the exact newline-trimmed response before publication.

- [ ] **Step 5: Compile and link the minimal generated model**

Use `llvm::sys::ExecuteAndWait` with explicit argument vectors, the configured test compiler, repository include roots, built gfsim library, and a private temporary directory. Record the exact command vector on failure; do not invoke a shell.

- [ ] **Step 6: Run harness, generator, and gfsim tests**

Run: `cmake --build --preset dev-llvm22 --target CodeGenTests GfsimTests && build/dev-llvm22/bin/CodeGenTests --gtest_filter='GeneratorTest.*:GeneratedModelCompileTest.*' && ctest --test-dir build/dev-llvm22 -R '^(CodeGenTests|GfsimTests)$' --output-on-failure`

Expected: the minimal generated executable links and reports the planned fingerprint; dispatch is dense and activation is canonical.

- [ ] **Step 7: Commit the generated harness**

```bash
git add lib/CodeGen/Generator.cpp unittests/CodeGen/GeneratorTest.cpp unittests/CodeGen/GeneratedModelCompileTest.cpp unittests/CodeGen/CMakeLists.txt
git commit -m "feat(codegen): emit static model harness"
```

---

### Task 8: Compile plan and same-toolchain preflight

**Files:**
- Create: `include/acir/CodeGen/Build.h`
- Create: `lib/CodeGen/CompilePlan.cpp`
- Create: `lib/CodeGen/Build.cpp`
- Create: `unittests/CodeGen/BuildTest.cpp`
- Modify: `lib/CodeGen/CMakeLists.txt`
- Modify: `unittests/CodeGen/CMakeLists.txt`

**Interfaces:**
- Consumes: `SourceBundle`, `BuildManifest`, explicit compiler/link inputs, and LLVM process support.
- Produces: `CompilePlan`, `ToolchainIdentity`, `BuildRequest`, `BuildResult`, `createCompilePlan`, and `buildGeneratedModel`.

- [ ] **Step 1: Add failing canonical-plan and mismatch tests**

```cpp
TEST(BuildTest, CompilePlanIsClosedCanonicalAndArgumentVectorBased) {
  auto plan = createCompilePlan(makeBuildRequest(), makeSourceBundle());
  ASSERT_TRUE(static_cast<bool>(plan));
  EXPECT_EQ(plan->schema, "acsim-compile-plan-0.1");
  EXPECT_TRUE(isValidFingerprint(plan->fingerprint));
  EXPECT_TRUE(allPathsAreNormalizedRelative(*plan));
  EXPECT_TRUE(allCommandsAreArgumentVectors(*plan));
}

TEST(BuildTest, RejectsToolchainOrPrebuiltProvenanceMismatch) {
  BuildRequest request = makeBuildRequest();
  request.prebuiltInputs.front().provenance.compilerBuildId = "different";
  EXPECT_THAT_EXPECTED(preflightBuildRequest(request), Failed());
}
```

- [ ] **Step 2: Run and confirm the build API is absent**

Run: `cmake --build --preset dev-llvm22 --target CodeGenTests`

Expected: compilation fails because `Build.h` and its APIs do not exist.

- [ ] **Step 3: Define explicit build inputs and internal compile plan**

```cpp
struct ToolchainIdentity {
  std::string compilerPath;
  std::string compilerName;
  std::string compilerBuildId;
  std::string targetTriple;
  std::string standardLibrary;
  std::string abiMode;
  std::string objectFormat;
  std::vector<std::string> contractFlags;
  Fingerprint fingerprint;
};
struct CompileCommand { std::vector<std::string> arguments; std::string output; };
struct CompilePlan {
  std::string schema = "acsim-compile-plan-0.1";
  std::vector<std::string> sourceUnits;
  std::vector<std::string> objectOutputs;
  std::vector<std::string> includeRoots;
  std::vector<std::string> definitions;
  std::vector<std::string> compilerFlags;
  std::vector<std::string> linkerFlags;
  std::vector<PrebuiltInput> prebuiltInputs;
  std::string executablePath;
  Fingerprint toolchainFingerprint;
  Fingerprint fingerprint;
  llvm::Error validate() const;
  llvm::Expected<std::string> canonicalJson() const;
};
struct BuildRequest {
  Identity project;
  Identity system;
  mlir::ModuleOp canonicalACSim;
  std::string frozenAcirBytes;
  std::string canonicalACSimBytes;
  std::string bindingLockBytes;
  std::string profile;
  std::vector<std::string> instrumentationLayers;
  std::vector<std::string> providerInputs;
  ToolchainIdentity toolchain;
  std::vector<PrebuiltInput> prebuiltInputs;
  std::vector<std::string> linkInputs;
  std::string outputRoot;
};
struct BuildResult {
  std::string buildDirectory;
  std::string executable;
  Fingerprint buildFingerprint;
  bool cacheHit;
};
llvm::Expected<CompilePlan>
createCompilePlan(const BuildRequest &, const SourceBundle &);
llvm::Expected<BuildResult> buildGeneratedModel(const BuildRequest &);
```

- [ ] **Step 4: Implement exact toolchain and provenance checks**

Canonicalize the supplied binding-lock bytes, fingerprint them, and require exact equality with the fingerprint embedded in ACSim. Hash the supplied frozen ACIR and canonical ACSim bytes and require exact equality with their declared/embedded identities. Validate the selected profile as exactly `fast`, `validated`, or `custom`, and require provider, profile, schema-set, and toolchain identities to match the verified plan before source emission.

Require explicit non-empty toolchain fields, compiler `--version`/target output matching the declared build ID and target, C++20 contract flags, identical ABI/standard-library flags for every source/prebuilt input, and exact provenance fingerprints. If source recompilation is available, a mismatched prebuilt input is omitted and rebuilt; otherwise return `ACLOWER-FINGERPRINT`.

- [ ] **Step 5: Canonicalize and fingerprint the compile plan**

Sort set-like include roots/definitions/prebuilt inputs, retain source and linker order, normalize all staged relative paths, and fingerprint `{"domain":"acsim-compile-plan-0.1","plan":...}` through RFC 8785. Reject shell metacharacters only where the C++ token/path grammar forbids them; argument values remain discrete array elements.

- [ ] **Step 6: Run focused Build tests**

Run: `cmake --build --preset dev-llvm22 --target CodeGenTests && build/dev-llvm22/bin/CodeGenTests --gtest_filter='BuildTest.*'`

Expected: canonical plan bytes and fingerprints are stable; all provenance/toolchain mismatches fail in preflight.

- [ ] **Step 7: Commit the build boundary**

```bash
git add include/acir/CodeGen/Build.h lib/CodeGen/CompilePlan.cpp lib/CodeGen/Build.cpp unittests/CodeGen/BuildTest.cpp lib/CodeGen/CMakeLists.txt unittests/CodeGen/CMakeLists.txt
git commit -m "feat(codegen): plan same toolchain builds"
```

---

### Task 9: Contained staging, immutable cache, and atomic publication

**Files:**
- Create: `lib/CodeGen/BuildInternal.h`
- Create: `lib/CodeGen/Staging.cpp`
- Modify: `include/acir/CodeGen/Build.h`
- Modify: `lib/CodeGen/Build.cpp`
- Modify: `lib/CodeGen/CMakeLists.txt`
- Modify: `unittests/CodeGen/BuildTest.cpp`

**Interfaces:**
- Consumes: validated `BuildRequest`, `SourceBundle`, `CompilePlan`, and provisional `BuildManifest`.
- Produces: internal `BuildServices`/`BuildFailurePoint` injection, clean private stages, exact cache-hit comparison, immutable build publication, and atomic `current.json` selection.

- [ ] **Step 1: Add failure-injection and cache tests**

```cpp
TEST(BuildTest, FailedStagePreservesPublishedBuildAndCurrentPointer) {
  TempOutputRoot output;
  auto first = buildGeneratedModel(makeBuildRequest(output.path()));
  ASSERT_TRUE(static_cast<bool>(first));
  const auto previousCurrent = readFile(output.path() + "/current.json");
  BuildRequest failing = makeBuildRequest(output.path());
  BuildServices services = makeRealBuildServices();
  services.failurePoint = BuildFailurePoint::AfterLink;
  EXPECT_THAT_EXPECTED(buildGeneratedModelForTesting(failing, services),
                       Failed());
  EXPECT_EQ(readFile(output.path() + "/current.json"), previousCurrent);
  EXPECT_TRUE(directoryTreeMatchesManifest(first->buildDirectory));
}

TEST(BuildTest, ExactSecondBuildIsCacheHitAndUnequalInputMisses) {
  TempOutputRoot output;
  auto first = buildGeneratedModel(makeBuildRequest(output.path()));
  auto second = buildGeneratedModel(makeBuildRequest(output.path()));
  ASSERT_TRUE(static_cast<bool>(first));
  ASSERT_TRUE(static_cast<bool>(second));
  EXPECT_TRUE(second->cacheHit);
  BuildRequest changed = makeBuildRequest(output.path());
  changed.instrumentationLayers.push_back("trace");
  auto third = buildGeneratedModel(changed);
  ASSERT_TRUE(static_cast<bool>(third));
  EXPECT_FALSE(third->cacheHit);
}
```

- [ ] **Step 2: Run and confirm staging atomicity is unimplemented**

Run: `cmake --build --preset dev-llvm22 --target CodeGenTests && build/dev-llvm22/bin/CodeGenTests --gtest_filter='BuildTest.FailedStage*:BuildTest.ExactSecond*'`

Expected: tests fail because the build does not stage/publish atomically.

- [ ] **Step 3: Implement strict normalized-relative-path containment**

Keep fault injection out of `include/acir/CodeGen/Build.h`. Define the internal seam exactly as:

```cpp
enum class BuildFailurePoint {
  None, AfterInputValidation, AfterSourceWrite, AfterContractCheck,
  AfterCompile, AfterLink, AfterFingerprintQuery, AfterManifestWrite,
  AfterImmutableRename, BeforeCurrentRename
};
struct BuildServices {
  BuildFailurePoint failurePoint = BuildFailurePoint::None;
  llvm::unique_function<llvm::Error(llvm::ArrayRef<llvm::StringRef>)> execute;
};
llvm::Expected<BuildResult>
buildGeneratedModelForTesting(const BuildRequest &, BuildServices &);
```

Create `normalizeArtifactPath` that rejects empty paths, roots, absolute paths, `.`/`..` components, NULs, and any normalized result outside the private stage. Every write goes through `writeFileExclusive(stageRoot, relativePath, bytes)` and verifies exact length and SHA-256 after close.

- [ ] **Step 4: Implement the ordered private-stage pipeline**

Write frozen ACIR, canonical ACSim, binding lock, every source, `compile-plan.json`, validation reports, and provisional manifest. Run concept/source checks, compile units, link, query the executable fingerprint, finalize artifact hashes and passed gates, then write the final canonical manifest. Bound captured compiler output and strip only explicitly declared stage/source path prefixes in reports.

- [ ] **Step 5: Publish without overwriting immutable state**

Rename the completed private stage to `builds/<build-fingerprint>`. If that directory exists, compare canonical manifest bytes and every artifact hash; equal means cache hit and unequal means `ACLOWER-FINGERPRINT`. Write canonical `current.json` to a sibling temporary file, flush it and its directory as supported, then atomically rename it over the pointer. Cleanup may remove only the exact private stage created by the current invocation.

- [ ] **Step 6: Exercise every injected failure boundary**

Table-drive failure points after input validation, source write, contract check, compile, link, embedded-fingerprint query, manifest write, immutable rename, and before pointer rename. For each, assert the previous build tree and pointer are byte-identical and no immutable directory is mutated.

- [ ] **Step 7: Run Build tests and sanitizer-focused staging tests**

Run: `cmake --build --preset dev-llvm22 --target CodeGenTests && build/dev-llvm22/bin/CodeGenTests --gtest_filter='BuildTest.*'`

Expected: cache and publication are exact, contained, and failure-atomic.

- [ ] **Step 8: Commit staging and publication**

```bash
git add include/acir/CodeGen/Build.h lib/CodeGen/BuildInternal.h lib/CodeGen/Build.cpp lib/CodeGen/Staging.cpp lib/CodeGen/CMakeLists.txt unittests/CodeGen/BuildTest.cpp
git commit -m "feat(codegen): publish immutable generated builds"
```

---

### Task 10: Internal stage-aware `acir-cxxgen` driver

**Files:**
- Create: `tools/acir-cxxgen/CMakeLists.txt`
- Create: `tools/acir-cxxgen/acir-cxxgen.cpp`
- Modify: `tools/CMakeLists.txt`
- Create: `test/CodeGen/driver-stages.mlir`
- Create: `test/CodeGen/driver-errors.mlir`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `buildModelPlan`, `generateModelSources`, source-contract validation, compile-plan construction, build/link, and publication APIs.
- Produces: internal `acir-cxxgen` with exact `--stop-after` stages `model-plan`, `acsim-emit-cxx`, `acsim-check-cxx-contract`, `compile`, `link`, and `publish`.

- [ ] **Step 1: Add failing lit tests for exact stage boundaries**

```mlir
// RUN: acir-cxxgen %s --stop-after=model-plan --output-root=%t.plan | FileCheck %s --check-prefix=PLAN
// RUN: acir-cxxgen %s --stop-after=acsim-emit-cxx --output-root=%t.emit | FileCheck %s --check-prefix=EMIT
// RUN: not acir-cxxgen %s --stop-after=unknown 2>&1 | FileCheck %s --check-prefix=BAD-STAGE
// PLAN: stage=model-plan status=passed
// EMIT: stage=acsim-emit-cxx status=passed
// BAD-STAGE: unknown --stop-after stage 'unknown'
```

- [ ] **Step 2: Run lit and observe the missing tool**

Run: `cmake --build --preset dev-llvm22 --target acir-cxxgen check-acir`

Expected: configuration/build fails because the target and tests are absent.

- [ ] **Step 3: Implement explicit command-line input plumbing**

Require canonical ACSim input and explicit flags/files for frozen ACIR, binding lock, project, system, profile, instrumentation, compiler identity, target, include roots, provider inputs, link inputs, and output root at the first stage that needs each input. Do not read defaults from environment variables or current working directory.

- [ ] **Step 4: Map exact stop stages to library boundaries**

`model-plan` prints a stable plan report; `acsim-emit-cxx` materializes the validated source bundle in a private inspection root; `acsim-check-cxx-contract` records source/concept reports; `compile` creates objects; `link` creates and checks the executable; `publish` performs immutable publication. Every failure prints its `ACLOWER-*` code, stage, stable identity, and bounded detail.

- [ ] **Step 5: Run positive and negative driver tests**

Run: `cmake --build --preset dev-llvm22 --target acir-cxxgen && lit -v build/dev-llvm22/test/CodeGen`

Expected: all exact stage names work, unknown stages fail, and stopping before publication leaves `current.json` absent.

- [ ] **Step 6: Commit the internal driver**

```bash
git add tools/acir-cxxgen tools/CMakeLists.txt test/CodeGen test/CMakeLists.txt
git commit -m "feat(codegen): add internal cxx generation driver"
```

---

### Task 11: End-to-end extension, determinism, forbidden-dependency, and sparsity conformance

**Files:**
- Create: `test/CodeGen/Inputs/extension/extension.schema.json`
- Create: `test/CodeGen/Inputs/extension/extension.binding.json`
- Create: `test/CodeGen/Inputs/extension/extension_provider.h`
- Create: `test/CodeGen/Inputs/extension/extension_provider.cpp`
- Create: `test/CodeGen/extension-provider.mlir`
- Create: `test/CodeGen/determinism.mlir`
- Create: `test/CodeGen/forbidden-generated-dependencies.mlir`
- Create: `unittests/CodeGen/GeneratedModelRuntimeTest.cpp`
- Modify: `unittests/CodeGen/CMakeLists.txt`

**Interfaces:**
- Consumes: complete internal driver and compiler library.
- Produces: proof that a provider extension needs no generic emitter change, equivalent inputs are byte-identical, forbidden mechanisms are absent, and idle objects do not increase active-frontier work.

- [ ] **Step 1: Add the external stateful provider fixture**

Define one `ac.test.Counter` schema and exact binding record whose C++ class satisfies the gfsim component concept, has typed constructor constants, and exposes Work/Xfer/reset/validate entry points. The fixture files are the only extension-specific production inputs.

- [ ] **Step 2: Add failing extension compile/link/run coverage**

```mlir
// RUN: acir-cxxgen %s --binding-lock=%S/Inputs/extension/extension.binding.json \
// RUN:   --provider-source=%S/Inputs/extension/extension_provider.cpp \
// RUN:   --stop-after=publish --output-root=%t.out
// RUN: %t.out/builds/*/bin/model --build-fingerprint | FileCheck %s
// CHECK: sha256:
```

The test also records a hash of generic `lib/CodeGen/*.cpp` before/after fixture setup and requires no source modification to recognize `ac.test.Counter`.

- [ ] **Step 3: Add byte-for-byte determinism coverage**

Build two legal canonical input permutations into separate output roots with identical explicit identities. Compare the model-plan report, generated files, compile plan, build manifest, executable fingerprint query, and build fingerprint with `cmp`; normalize no content after generation.

- [ ] **Step 4: Add forbidden-dependency scanning**

Scan generated source and linked dependency metadata for component-name conditionals, `Python`, `pybind`, schema/catalog traversal, `dlopen`/plugin loaders, runtime factories, descriptor interpretation, topology mutation, `co_await`, `std::function`, `dynamic_cast`, and RTTI flags. Allow test fixture names only in generated typed declarations and manifest data, never in generic emitter source branches.

- [ ] **Step 5: Add the active-frontier sparsity test**

```cpp
TEST(GeneratedModelRuntimeTest, PermanentlyIdleObjectsDoNotIncreaseHotWork) {
  auto baseline = runInstrumentedModel(makeActiveFrontierPlan(0));
  auto sparse = runInstrumentedModel(makeActiveFrontierPlan(4096));
  EXPECT_EQ(sparse.schedulerInvocations, baseline.schedulerInvocations);
  EXPECT_EQ(sparse.activationEdgesTraversed, baseline.activationEdgesTraversed);
  EXPECT_EQ(sparse.committedResult, baseline.committedResult);
}
```

- [ ] **Step 6: Run all conformance tests**

Run: `cmake --build --preset dev-llvm22 --target CodeGenTests acir-cxxgen && build/dev-llvm22/bin/CodeGenTests --gtest_filter='GeneratedModelRuntimeTest.*:GeneratedModelCompileTest.*' && lit -v build/dev-llvm22/test/CodeGen`

Expected: extension, determinism, forbidden scans, compile/link/run, and sparsity all pass.

- [ ] **Step 7: Commit Phase 3 conformance coverage**

```bash
git add test/CodeGen unittests/CodeGen/GeneratedModelRuntimeTest.cpp unittests/CodeGen/CMakeLists.txt
git commit -m "test(codegen): enforce phase 3 conformance"
```

---

### Task 12: Phase 3 completion audit, integration, and upstream publication

**Files:**
- Create: `docs/implementation/phase-3-audit.md`
- Modify: `docs/superpowers/plans/2026-08-10-phase-3-binding-codegen.md`
- Inspect: all Phase 3 commits and generated artifacts

**Interfaces:**
- Consumes: every preceding task and the normative Phase 3 roadmap/specification.
- Produces: evidence-backed audit, completed checklist, clean merge into `main`, and pushed `origin/main`.

- [ ] **Step 1: Run the targeted Phase 3 verification matrix**

```bash
cmake --preset dev-llvm22 -DACIR_ENABLE_ASSERTIONS=ON
cmake --build --preset dev-llvm22 --target CodeGenTests GfsimTests acir-cxxgen acir-opt acir-opt-internal
build/dev-llvm22/bin/CodeGenTests
build/dev-llvm22/bin/GfsimTests
lit -v build/dev-llvm22/test/CodeGen
lit -v build/dev-llvm22/test/ACSim
```

Record command, exit status, and relevant test counts in the audit.

- [ ] **Step 2: Run the repository-wide gates**

```bash
cmake --build --preset dev-llvm22
ctest --test-dir build/dev-llvm22 --output-on-failure
lit -v build/dev-llvm22/test
cmake --preset asan-llvm22
cmake --build --preset asan-llvm22 --target GfsimTests CodeGenTests
ctest --test-dir build/asan-llvm22 --output-on-failure -R '^(GfsimTests|CodeGenTests)$'
cmake --preset ubsan-llvm22
cmake --build --preset ubsan-llvm22 --target GfsimTests CodeGenTests
ctest --test-dir build/ubsan-llvm22 --output-on-failure -R '^(GfsimTests|CodeGenTests)$'
cmake --preset release-llvm22
cmake --build --preset release-llvm22
cmake --build --preset release-llvm22 --target check-acir
ctest --test-dir build/release-llvm22 --output-on-failure
.venv/bin/python scripts/check-contracts.py
```

Also run the CI static-analysis and install-consumer commands copied verbatim from `.github/workflows/ci.yml`.

- [ ] **Step 3: Audit normative coverage and generated output**

Map provider discovery/binding lock, canonical ACSim input, hierarchical ownership, arrays, typed bindings, pure expressions, enum-PC processes, dispatch/activation, concepts, manifests, fingerprints, same-toolchain preflight, staging/cache/publication, internal stages, extension, determinism, forbidden dependencies, and sparsity to specific tests and commits. Scan generated outputs and generic emitters for every forbidden mechanism in the Global Constraints.

- [ ] **Step 4: Check repository and commit hygiene**

Run: `git status --short --branch && git log --oneline --decorate main..HEAD && git diff --check main...HEAD && git diff --stat main...HEAD`

Expected: only the preserved user file is untracked; no whitespace errors; each task is a distinct reviewed commit.

- [ ] **Step 5: Write and commit the audit**

Document environment/toolchain, all commands and results, acceptance-criterion mapping, generated-artifact sample hashes, failure-atomicity evidence, and any residual non-blocking risk. Mark every completed plan checkbox only after its corresponding evidence exists.

```bash
git add docs/implementation/phase-3-audit.md docs/superpowers/plans/2026-08-10-phase-3-binding-codegen.md
git commit -m "docs(audit): complete phase 3 verification"
```

- [ ] **Step 6: Review the complete phase diff before integration**

Run: `git diff --check main...HEAD && git status --short --branch && git log --oneline --reverse main..HEAD`

Read every changed production file and every test. Repair and re-run affected gates before proceeding if any acceptance criterion lacks direct evidence.

- [ ] **Step 7: Merge and push only the verified phase**

```bash
git switch main
git merge --no-ff codex/phase3-binding-codegen -m "merge: complete phase 3 binding and codegen"
git push origin main
```

Expected: the merge is clean, `origin/main` advances to the Phase 3 merge commit, and the unrelated `phase-1-pr-description.md` remains untracked and unchanged.

---

## Spec Coverage Index

| Normative requirement | Implemented and proved by |
| --- | --- |
| Deterministic provider/binding identities and immutable lock | Tasks 3, 4, 8, 11 |
| Verified canonical ACSim as sole construction input | Tasks 3, 4, 8 |
| Exact schema and implementation fingerprints | Tasks 1, 3, 4 |
| Hierarchical generated ownership and static arrays | Tasks 2, 4, 5 |
| Generic typed binding and pure-expression generation | Tasks 4, 5, 11 |
| Enum-PC process generation and live/wake state | Tasks 4, 6 |
| Dense dispatch and canonical activation | Tasks 3, 7 |
| C++ concept/source checks and same toolchain | Tasks 7, 8, 10 |
| Exact build manifest and compile-plan fingerprints | Tasks 1, 8 |
| Clean staging, immutable cache, atomic publication | Task 9 |
| Logical `acsim-emit-cxx` and `acsim-check-cxx-contract` stages | Task 10 |
| Extension without generic emitter changes | Task 11 |
| Byte-identical equivalent builds | Tasks 5, 8, 11 |
| No forbidden runtime or generator dependency | Tasks 5, 6, 7, 11 |
| Idle-object sparsity | Task 11 |
| Full phase exit matrix and upstream integration | Task 12 |
