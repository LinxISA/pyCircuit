# Task 11 corrective report: canonical empty binding locks

## Outcome

Task 11 corrective is complete. `acir-opt` now accepts binding resolution with
zero `--ac-binding-registry` arguments while retaining mandatory non-empty
lock output, profile, and target options. A frozen internal-only model produces
the exact two RFC-8785 canonical bytes `[]`; the existing result fingerprint
API returns
`sha256:4f53cda18c2baa0c0354bb5f9a3ecbe5ed12ab4d8e11ba873c2f11161202b945`.

The result API also exposes missing/one lookup by exact external
`resolutionKey`. There is no component-name, hierarchy, binding-spelling, or
fallback lookup.

## Changes

- `tools/acir-opt/BindingOptions.cpp`
  - Deleted the obsolete option error requiring at least one
    `--ac-binding-registry`.
  - Preserved independent validation of `--ac-binding-lock-output`,
    `--ac-binding-profile`, and `--ac-binding-target`.
  - Preserved sorting and parsing for every explicitly supplied registry. Zero
    files simply contribute zero candidates and zero requests; no provider or
    implicit registry is created.
- `include/acir/Bindings/Registry.h`, `lib/Bindings/Registry.cpp`
  - Added `BindingResolutionResult::selectionForResolutionKey`.
  - The method compares only exact `resolutionKey` strings and returns either a
    pointer to the one selected binding or `nullptr`. It performs no allocation
    and has no fallback behavior.
- `test/Bindings/resolve-empty.mlir`
  - Added a real CLI/parser/pass test using a generated `ac.module` with a
    mandatory workload `ac.process` and no registry flag.
  - Proves unchanged frozen ACIR output and absence of ACSim operations.
  - Proves exact `[]` content and exact two-byte lock length.
  - Repeats the same resolution and proves byte-identical lock publication.
  - Independently proves missing lock output, profile, and target remain option
    errors.
- `test/Bindings/resolve-missing.mlir`
  - Added external `ac.module.extern` resolution with no registry files.
  - Proves the failure is precise `ACLOWER-BINDING-MISSING` for key `@Leaf`,
    not an option error, and proves no lock is published.
- `test/Bindings/resolve-valid.mlir`
  - Deleted the obsolete registry-required CLI expectation and retained help,
    provider-permutation, and unchanged-output coverage.
- `unittests/Bindings/CanonicalBindingTest.cpp`
  - Proves exact-key lookup finds the one selection.
  - Proves binding spelling, hierarchy-like spelling, and another exact key are
    missing.
  - Proves empty lookup is missing, the lock is exact `[]`, the SHA-256 is the
    fixed hand-checked vector above, and repeated results have identical bytes
    and fingerprints.

No ACSim ODS/API, process-state planning, or ACIR-to-ACSim lowering file was
modified. The normative lowering specification already states that only
declarations explicitly requesting reusable external/library implementations
resolve through the registry and that generated modules/processes/core IR do
not; no obsolete normative registry-file requirement was found.

## Strict RED evidence

Tests were added before production edits.

### Result lookup API RED

Command:

```console
cmake --build build/dev-llvm22 --target ACIRBindingTests
```

Expected and observed failure: compilation failed at every new lookup assertion
because `BindingResolutionResult` had no member named
`selectionForResolutionKey` (five diagnostics). This directly demonstrated
that the required exact-key result API did not exist.

### Zero-registry CLI RED

Command:

```console
.venv/bin/lit -v \
  build/dev-llvm22/test/Bindings/resolve-empty.mlir \
  build/dev-llvm22/test/Bindings/resolve-missing.mlir
```

Expected and observed failures: 2/2 tests failed. The internal-only invocation
stopped with
`ACLOWER-BINDING-OPTIONS: --ac-binding-registry is required`; the external
no-registry test expected `ACLOWER-BINDING-MISSING` but received the same old
option error. These were behavior failures through the real CLI/parser/pass.

During the first GREEN run, the implementation produced the correct digest but
the newly written hand literal contained an incorrect remembered suffix. The
literal was independently corrected with `printf '[]' | shasum -a 256`; no
production code changed in response to that test-data error.

## Focused GREEN evidence

Command:

```console
cmake --build build/dev-llvm22 --target ACIRBindingTests acir-opt-internal
build/dev-llvm22/bin/ACIRBindingTests \
  --gtest_filter='BindingResolutionResultTest.*:BindingRegistryTest.ExactSelectionIsIndependentOfProviderOrder:BindingLockTest.PublishesAtomicallyOnlyAfterCompleteResolution'
.venv/bin/lit -v \
  build/dev-llvm22/test/Bindings/resolve-empty.mlir \
  build/dev-llvm22/test/Bindings/resolve-missing.mlir \
  build/dev-llvm22/test/Bindings/resolve-valid.mlir
```

Observed: 4/4 focused C++ tests passed and 3/3 focused lit tests passed. After
formatting, the complete binding lit directory passed 4/4.

## Boundary and error-path coverage

- Frozen internal-only root with mandatory workload process and zero registry:
  success, unchanged ACIR, no ACSim, exact two-byte `[]` lock.
- Same empty model repeated: byte-identical lock; C++ result test also proves
  identical fingerprint.
- Empty result fingerprint: exact SHA-256 vector for the exact bytes `[]`.
- External reusable implementation request and zero registry: precise
  `ACLOWER-BINDING-MISSING`, exact key reported, no option error, no output.
- Missing lock output: independent `ACLOWER-BINDING-OPTIONS` failure.
- Missing profile: independent `ACLOWER-BINDING-OPTIONS` failure.
- Missing target: independent `ACLOWER-BINDING-OPTIONS` failure.
- Exact external `resolutionKey`: returns the one selected binding.
- Binding spelling without `@`, hierarchy-like spelling, and unrelated key:
  missing; no fallback.
- Empty result lookup: `nullptr` with no allocation or synthesized selection.
- Duplicate-key rejection remains in the existing resolver path.
- Provider permutation remains covered by existing lit and unit tests.
- Atomic publication, no partial output, preservation of an existing lock, and
  temporary cleanup remain covered by
  `BindingLockTest.PublishesAtomicallyOnlyAfterCompleteResolution` and the full
  CTest runs.
- Existing deterministic ordering and resource-cap tests remain green in the
  complete binding test executable and CTest runs.

## Full verification

### Debug build, CTest, and lit

```console
cmake --build build/dev-llvm22 -j4
ctest --test-dir build/dev-llvm22 --output-on-failure
cmake --build build/dev-llvm22 --target check-acir -j4
```

Result: build succeeded; 6/6 CTest tests passed; 55/55 lit tests passed.

### Release build, CTest, and lit

```console
cmake --build build/release-llvm22 -j4
ctest --test-dir build/release-llvm22 --output-on-failure
cmake --build build/release-llvm22 --target check-acir -j4
```

Result: build succeeded; 6/6 CTest tests passed; 55/55 lit tests passed.

### Python and repository contracts

```console
.venv/bin/python -m unittest discover -s tests -p 'test_*.py' -v
.venv/bin/python scripts/check-contracts.py
```

Result: 18 tests ran successfully with one environment-dependent configure
test skipped because `MLIR_DIR` was not supplied; repository contracts reported
OK for 9 schemas, epoch 0.1, and LLVM 22.1.8.

### Touched-production clang analyzer

```console
sdk_path=$(xcrun --show-sdk-path) && \
/opt/homebrew/opt/llvm/bin/clang-tidy -p build/dev-llvm22 \
  -checks='-*,clang-analyzer-*' -warnings-as-errors='*' \
  lib/Bindings/Registry.cpp tools/acir-opt/BindingOptions.cpp \
  --extra-arg=-isysroot --extra-arg="$sdk_path" --extra-arg=-stdlib=libc++
```

Result: both touched production translation units processed with exit 0 and no
diagnostics.

### Formatting, diff, and status

```console
/opt/homebrew/opt/llvm/bin/clang-format -i \
  include/acir/Bindings/Registry.h lib/Bindings/Registry.cpp \
  tools/acir-opt/BindingOptions.cpp \
  unittests/Bindings/CanonicalBindingTest.cpp
git diff --check
git diff --name-only
git status --short --branch
```

Result: formatting completed; `git diff --check` emitted no output. Before the
final commit, the only modified/untracked files were the scoped binding CLI,
result API, binding tests, and this report. Final post-commit status is recorded
by the controller-facing completion message.

## Self-review

- The production change is minimal: one obsolete guard removed and one exact
  lookup method added.
- Zero registry files do not instantiate or reinterpret a registry document;
  empty candidate/request vectors flow through the existing deterministic
  resolver and canonicalizer.
- The canonical empty representation and digest use the existing result API
  and hashing implementation; no special-case serializer or hash path was
  added.
- The lookup returns an element owned by the result and does not allocate,
  normalize, parse, or guess keys.
- The CLI still sorts supplied registry paths, and all existing ordering,
  resource-cap, and atomic-write behavior is unchanged.
- No debug code, temporary artifact, compatibility warning, deprecated flag,
  ACSim change, or downstream lowering work remains.

## Concerns

None. The one skipped Python configure regression is pre-existing and explicitly
environment-gated; both configured Debug and Release builds completed all
native tests and lit tests successfully.
