# LLVM 22.1.8 Upgrade Gate Summary

## Toolchain

- LLVM/MLIR: `22.1.8`
- leaf base: `2087c00` (`origin/main` after pyCircuit PR #70)
- `pycc --version`: `Homebrew LLVM version 22.1.8`
- `pyc-opt --version`: `Homebrew LLVM version 22.1.8`
- staged toolchain: `.pycircuit_out/toolchain/install-llvm22-upgrade`

## Compatibility checks

- LLVM 22 CMake configure/build/install: pass
- PATH-only LLVM 22 autodetection via `flows/scripts/pyc build`: pass
- CMake configured with LLVM 21.1.8: rejected with `pyCircuit requires LLVM/MLIR 22.x`
- `flows/scripts/pyc build` with LLVM 21.1.8: rejected with `LLVM 22 is required`

## pyCircuit gates

- `pre-commit run --files <changed files>`: pass
- `pytest tests/unit -m unit`: 48 passed
- `pytest tests/system -m system`: 3 passed
- API hygiene: pass
- strict decision status: 147 rows, 0 deferred
- `mkdocs build`: pass with pre-existing missing-page/navigation warnings
- `run_examples.sh`: pass
- `run_sims.sh`: pass
- `run_sims_nightly.sh`: pass
- `run_semantic_regressions_v40.sh`: pass

## Linx integration gates

- `run_linx_cpu_pyc_cpp.sh`: pass (`cycles=129`)
- `run_linx_qemu_vs_pyc.sh`: blocked by an existing `origin/main` gap. PR #70
  removed the LinxCore fallback, but the unchanged primary C++ TB does not
  write `PYC_COMMIT_TRACE`; the gate exits with `pyc trace was not produced`.
  The gate script, CPU runner, and TB are byte-identical to `origin/main`.
- `check_pycircuit_interface_contract.py --strict`: pass (`version=2.0`)
- `check_trace_semver_compat.py --strict`: pass (`version=1.0`)

## Existing repository warning

`docs/updatePLAN.md` is referenced by `AGENTS.md`, `CONTRIBUTING.md`, and
`mkdocs.yml`, but is absent from the pinned `linxisa-v0.58.0` pyCircuit tree.
This pre-existing documentation drift is unrelated to the LLVM upgrade.
