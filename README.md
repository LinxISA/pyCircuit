# pyCircuit

`pyCircuit` is a Python-first hardware construction and compilation toolkit built on a PYC MLIR dialect.

You write Python design code, emit MLIR (`.pyc`), run a strict MLIR pass pipeline, then emit:

- Verilog RTL
- C++ cycle models

## Documentation

- `docs/USAGE.md`: frontend design authoring guide
- `docs/COMPILER_FLOW.md`: refactored end-to-end flow + pass-by-pass pipeline reference
- `docs/IR_SPEC.md`: PYC IR contract
- `docs/PRIMITIVES.md`: runtime primitive contracts (`runtime/cpp`, `runtime/verilog`)
- `docs/VERILOG_FLOW.md`: open-source Verilog sim/lint flow
- `docs/LINX_WORKSPACE.md`: Windows + Zybo + Linx workspace notes
- `docs/WSL_UBUNTU_ON_WINDOWS.md`: WSL setup for Linx bring-up

## Refactored Layout (Current)

- Frontend: `compiler/frontend/pycircuit/`
- MLIR compiler: `compiler/mlir/`
- Runtime libraries: `runtime/cpp/`, `runtime/verilog/`
- Flows: `flows/scripts/`, `flows/tools/`
- Designs: `designs/examples/`, `designs/linxcore/`

Legacy paths are no longer canonical (`python/pycircuit`, `pyc/mlir`, `include/pyc`, `tools/`, `scripts/`, `examples/`, `janus/`).

## Quickstart

## 1) Build compiler tools

```bash
flows/scripts/pyc build
```

## 2) Emit MLIR from Python design

```bash
PYTHONPATH=compiler/frontend python3 -m pycircuit.cli emit \
  designs/examples/jit_pipeline_vec.py \
  -o /tmp/jit_pipeline_vec.pyc
```

## 3) Compile MLIR to Verilog / C++

```bash
./build/bin/pyc-compile /tmp/jit_pipeline_vec.pyc --emit=verilog -o /tmp/jit_pipeline_vec.v
./build/bin/pyc-compile /tmp/jit_pipeline_vec.pyc --emit=cpp -o /tmp/jit_pipeline_vec.hpp
```

Useful options:

- `--sim-mode=default|cpp-only`
- `--cpp-only-preserve-ops` (only meaningful with `cpp-only`)
- `--logic-depth=<N>` (default `32`)
- `--out-dir=<dir>` (split-per-module emission + `manifest.json` + `compile_stats.json`)

## 4) Regenerate local outputs / run regressions

```bash
flows/scripts/pyc regen
flows/scripts/pyc test
```

Equivalent flow-runner commands:

```bash
python3 flows/tools/pyc_flow.py doctor
python3 flows/tools/pyc_flow.py regen
python3 flows/tools/pyc_flow.py cpp-test
```

## Frontend Semantics (Refactored)

- JIT-by-default build pattern: `build(m: Circuit, ...)`.
- In design context, plain calls `child(m, ...)` auto-instantiate modules.
- Hardware args become ports; Python literals become specialization parameters.
- For complex specialization objects, use explicit `Circuit.instance(..., params=...)`.
- `@jit_inline` is the explicit inline escape hatch.
- `m.debug(name, signal)` exports stable debug probe ports (`dbg__*`) for TB/traces.

See `docs/USAGE.md` for detailed frontend rules and examples.

## Compiler Pipeline

The exact pass order and per-pass behavior are documented in `docs/COMPILER_FLOW.md`.

Current `pyc-compile` default pipeline ends with strict legality checks:

- no remaining dynamic SCF/index hardware values,
- strict combinational depth check (`--logic-depth`),
- compile stats collection (`Reg/Mem`, `WNS/TNS`, depth).

## Generated Artifact Policy

Generated files are out-of-tree local artifacts and are not checked into git.

Default script output root:

- `.pycircuit_out/examples/...`
- `.pycircuit_out/linxcore/...`

The repository `.gitignore` enforces this policy.

## Linx bring-up shortcuts

Run Linx CPU C++ regression:

```bash
bash flows/tools/run_linx_cpu_pyc_cpp.sh
```

Run LinxCore smoke regression:

```bash
bash designs/linxcore/tests/test_trace_schema_and_mem.sh
```
