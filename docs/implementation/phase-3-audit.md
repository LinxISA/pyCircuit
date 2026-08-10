# Phase 3 completion audit

Audit of `codex/phase3-binding-codegen` against Phase 3 in the
[Agentic Circuit roadmap](../superpowers/plans/2026-08-04-agentic-circuit-roadmap.md)
and the detailed
[Phase 3 implementation plan](../superpowers/plans/2026-08-10-phase-3-binding-codegen.md).

Reviewed commit range:
`29064b7373cf0112e5c41a83ae3aaf56fc1111d4..0b1ee58982e65e406cfaea1fa727d686464ac802`
(15 commits before this audit commit).

## 1. Environment

- macOS 26.5.2 (25F84), arm64;
- Apple Clang 21.0.0;
- LLVM/MLIR 22.1.8 from Homebrew;
- CMake 4.2.1, Ninja 1.13.0, and lit 18.1.8;
- C++20 builds through the repository `dev-llvm22`, `asan-llvm22`,
  `ubsan-llvm22`, and `release-llvm22` presets.

## 2. Verification matrix — PASS

The audited tree passed:

- targeted development tests: 126/126 gfsim tests, 50/50 code-generation
  tests, 8/8 CodeGen lit tests, and 8/8 ACSim lit tests;
- complete development build, 10/10 CTest suites, and 80/80 lit tests;
- complete Release build, 10/10 CTest suites, and 80/80 lit tests through
  `check-acir`;
- ASan and UBSan: `GfsimTests` and `CodeGenTests` pass in both presets;
- repository contracts: 18/18 tests and the standalone checker pass;
- IR coverage: 11/11 tests and the read-only ledger checker pass;
- repository-wide C++ formatting dry run and `git diff --check`;
- repository-wide clang-tidy over owned C++ translation units;
- Release installation, external CMake package discovery, consumer build, and
  successful `process-state-plan-consumer` execution.

Apple's ASan runtime does not support the Linux CI gate's
`detect_leaks=1`, so the local ASan run omitted only that option. The external
Homebrew LLVM/MLIR package is not ASan-instrumented; CI and the local run set
`allow_user_poisoning=0` to prevent LLVM allocator annotations from producing
cross-instrumentation false positives. Address checking, strict string checks,
initialization-order checking, and halt-on-error remained enabled. The exact
Linux clang-tidy command also lacks Apple SDK discovery on this host; the local
run injected AppleClang's SDK and libc++ include paths and otherwise used the
CI configuration, file inventory, header filter, and warnings-as-errors policy.

## 3. Normative coverage — PASS

| Contract area | Direct evidence | Principal commits |
| --- | --- | --- |
| Exact manifest and fingerprint schema | `CodeGenManifestTest.*`, schema contract tests | `c3ef6ca` |
| Provider discovery, binding lock, and canonical ACSim input | `ModelPlanTest.*`, `CodeGen/extension-provider.mlir`, publication preflight tests | `9937126`, `217e870`, `2bf6b54` |
| Hierarchical ownership and reusable nested modules | `GeneratorTest.EmitsReusableNestedModulesWithContextDenseIds`, `CodeGen/nested-modules.mlir` | `1d09f09`, `03cfc33`, `2bf6b54` |
| Multidimensional arrays | `GeneratorTest.RecursivelyAttachesMultidimensionalArrays`, `CodeGen/multidimensional-array.mlir` | `03cfc33`, `2bf6b54` |
| Typed bindings, pure expressions, concepts, and exports | `ModelPlanTest.ExtractsHierarchyBindingsExpressionsAndProcesses`, `GeneratorTest.EmitsExactOrderedFileSetAndTypedOwnership`, `CodeGen/full-structured-model.mlir` | `217e870`, `03cfc33`, `2bf6b54` |
| Closed enum-PC processes and scalar operations | `GeneratorTest.EmitsClosedEnumPcProcessWithoutRawFrames`, `GeneratorTest.EmitsTypedScalarOperationsWithoutRuntimeHelpers`, generated compile/run test | `4b4f053`, `2bf6b54` |
| Dense dispatch, activation, and sparse hot work | dispatch generator tests, `GeneratedModelRuntimeTest.*`, gfsim identity tests | `d241a48`, `746e4cd`, `2bf6b54` |
| Same-toolchain compile/link preflight | `BuildTest.CompilePlanIsClosedCanonicalAndArgumentVectorBased`, mismatch and link-input identity tests | `13926ce`, `2bf6b54`, `0b1ee58` |
| Immutable staging, cache reuse, and atomic publication | cache-hit/miss tests and all nine `BuildFailurePoint` boundary injections | `ee65966`, `2bf6b54`, `0b1ee58` |
| Internal stage-aware driver | `CodeGen/driver-stages.mlir`, `CodeGen/driver-errors.mlir` | `1187a72` |
| Extension, determinism, and forbidden dependencies | `CodeGen/extension-provider.mlir`, `CodeGen/determinism.mlir`, `CodeGen/forbidden-generated-dependencies.mlir` | `746e4cd`, `2bf6b54` |

The forbidden-dependency lit test scans generated output for Python/pybind,
dynamic loading, coroutines, `std::function`, RTTI dispatch, runtime factories,
descriptor or schema/catalog walkers, and runtime topology construction. It
also rejects component-name branches in the generic emitter. The production
source-bundle validator applies the same closed dependency policy before any
compile or publication step.

## 4. Generated-artifact sample

One successful extension-provider publication on the audited development
toolchain produced embedded build fingerprint
`sha256:4ad5e009fa1700840b7b07e2a67c50b9dddb5c839d4527c289b216e020f80fbd`.
Selected SHA-256 file hashes were:

- `build-manifest.json`:
  `bf7442a0d258beac400a18fc05a9fe17c12d5e7b78f3c17263e0d9a67eefc4ff`;
- `compile-plan.json`:
  `fbf1e899dc3a43b40d0fb4c4484dfed633fcccc7508360faa65aaafacbaf5d17`;
- `include/generated/dispatch.h`:
  `8ee6c0f933c202c12ae412cf404c4f44b95b529a00724341d3d91ce349a76265`;
- `src/generated/model.cpp`:
  `3f7efc3c2f10227b2fb7f80df17f504af3b2cb993c26d30b3e56a7aeac413dbc`;
- `bin/model`:
  `fcd7be1e10f9653c282a73f621c435ac9fdc2329fc5014f5e9aa289308fe1285`.

The build fingerprint intentionally includes exact input paths and toolchain
identity, so it is evidence for this invocation rather than a cross-host
golden value. `CodeGen/determinism.mlir` separately publishes the same exact
inputs twice, recursively compares both immutable trees, and compares both
executables' embedded fingerprints.

## 5. Failure atomicity — PASS

`BuildTest.EveryInjectedBoundaryPreservesPublishedState` covers all nine
defined failure points, from post-input validation through the final current
pointer rename. Every injected failure leaves the prior immutable build and
`current.json` unchanged. Cache reuse also verifies the existing manifest and
every declared artifact before reporting an exact hit.

## 6. Residual non-blocking risk

The Phase 2 canonical ACSim lowering can represent multi-block
`cf.cond_br` process control flow, while the Phase 3 model-plan extractor
currently accepts only the closed structured operation variants it can emit.
The lowering also does not yet preserve every generated helper descriptor
needed to reconstruct arbitrary helper-rich process bodies from ACSim alone.
Consequently, the current generator is directly proven for the structured
Phase 3 corpus, but not for every possible canonical multi-block process.
Extending that metadata/lowering boundary belongs with the Phase 5
end-to-end process-model integration gate; it does not weaken the Phase 3 exit
criterion of deterministic C++ generation with no runtime descriptor or schema
dependency.

## Verdict

The Phase 3 roadmap scope and exit gate pass. The branch is ready for a
reviewed merge into `main` and upstream publication.
