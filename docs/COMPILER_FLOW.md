# pyCircuit Compiler Flow (Refactored)

This document is the source-of-truth for the refactored pyCircuit flow in this repository.

It reflects the current implementation in:

- `compiler/frontend/pycircuit/cli.py`
- `compiler/frontend/pycircuit/jit.py`
- `compiler/mlir/tools/pyc-compile.cpp`
- `compiler/mlir/lib/Transforms/*.cpp`

## 1) End-to-end flow

1. Python frontend elaborates/JIT-compiles a design to MLIR (`.pyc`).
2. `pyc-compile` runs the MLIR pass pipeline.
3. `pyc-compile` emits Verilog or C++ artifacts.
4. Optional flow scripts build and run testbenches from emitted artifacts.

## 2) Repository entrypoints (current)

- Frontend CLI: `python3 -m pycircuit.cli` (module path under `compiler/frontend/`)
- Compiler: `build/bin/pyc-compile`
- Convenience wrapper: `flows/scripts/pyc`
- Flow runner: `python3 flows/tools/pyc_flow.py`
- Runtime libraries:
  - C++: `runtime/cpp/`
  - Verilog primitives: `runtime/verilog/`

Legacy paths (`python/pycircuit`, `pyc/mlir`, `include/pyc`, `tools/`, `scripts/`, `examples/`, `janus/`) are no longer the canonical flow.

## 3) Frontend flow

## 3.1 `pycircuit emit`

`pycircuit emit <design.py> -o <out.pyc>` loads `build(...)` from the Python source and emits MLIR.

Behavior:

- JIT-by-default for `build(m: Circuit, ...)` signatures.
- All non-builder parameters must have defaults (or be overridden by `--param name=value`).
- Multi-module compilation is rooted through `compile_design(...)`, producing one MLIR `module` with multiple `func.func` symbols and `pyc.instance` hierarchy edges.

## 3.2 Call semantics in JIT frontend

Default behavior in design context:

- Plain function call `child(m, ...)` auto-instantiates a module instance.
- Hardware values (`Wire`/`Reg`/`Signal`/`Vec`/`Bundle`) become instance ports.
- Python literal specialization values become specialization parameters.
- Instance names are deterministic callsite names: `<callee>__L<line>__N<idx>` (scope-prefixed).
- For complex specialization objects, use explicit `Circuit.instance(..., params=...)`.

Escape hatch:

- `@jit_inline` forces inline expansion into the current module body.

## 3.3 Debug export contract

`Circuit.debug(name, signal)` exports stable debug outputs named as `dbg__*` ports. These probes are intended for direct TB/tracing visibility and remain observable to optimization passes through normal module outputs.

## 4) `pyc-compile` modes and outputs

## 4.1 Core CLI options

- `--emit=verilog|cpp`
- `--out-dir=<dir>` (split-per-module output + `manifest.json`)
- `-o <file>` (single-file emission)
- `--sim-mode=default|cpp-only`
- `--cpp-only-preserve-ops`
- `--logic-depth=<N>` (default 32)

## 4.2 Mode semantics

- `--sim-mode=default`: normal behavior.
- `--sim-mode=cpp-only`:
  - still runs safety/legality/depth/stats passes;
  - Verilog emission is rejected;
  - comb fusion is ON by default for speed.
- `--cpp-only-preserve-ops`:
  - valid with `--sim-mode=cpp-only`;
  - disables `FuseComb` to keep operation-granular scheduling.

## 4.3 Stats outputs

All compiles report stats in stderr summary and JSON:

- `--out-dir=<dir>` writes `<dir>/compile_stats.json`
- `-o <file>` writes `<file>.stats.json`

Current JSON fields:

- `reg_count`, `reg_bits`
- `mem_count`, `mem_bits`
- `logic_depth_limit`, `max_logic_depth`
- `wns`, `tns`
- `fuse_comb_enabled`

## 5) Default pass pipeline (exact order)

From `compiler/mlir/tools/pyc-compile.cpp`:

```text
Inliner
Canonicalizer
CSE
SCCP
RemoveDeadValues
SymbolDCE
LowerSCFToPYCStatic
EliminateWires
EliminateDeadState
CombCanonicalize
SLPPackWires
CheckCombCycles
PackI1Regs
FuseComb (enabled unless cpp-only + cpp-only-preserve-ops)
Canonicalizer
CSE
RemoveDeadValues
SymbolDCE
CheckFlatTypes
CheckNoDynamic
CheckLogicDepth
CollectCompileStats
```

## 6) Pass-by-pass reference

1. `Inliner`:
- Inlines `func.call` bodies where legal.
- Normalizes IR before PYC-specific transforms.

2. `Canonicalizer` (first):
- Canonical MLIR simplifications and fold opportunities.

3. `CSE` (first):
- Eliminates duplicate computations.

4. `SCCP`:
- Sparse conditional constant propagation to reduce dead/control-constant logic.

5. `RemoveDeadValues` (first):
- Deletes IR values no longer observable.

6. `SymbolDCE` (first):
- Removes unreferenced symbol ops.

7. `LowerSCFToPYCStatic` (`pyc-lower-scf-static`):
- Lowers `scf.for` to static unrolling.
- Lowers `scf.if` to static structure (`pyc.mux`) or constant-chosen branch.
- Fails if loop bounds/steps are non-constant, induction variable is used dynamically, side-effectful ops appear in staticized regions, or SCF remains.

8. `EliminateWires` (`pyc-eliminate-wires`):
- Removes trivial `pyc.wire` + `pyc.assign` pairs.
- Preserves naming by inserting `pyc.alias` when wire has `pyc.name`.
- Conservative: only safe single-driver cases are rewritten.

9. `EliminateDeadState` (`pyc-eliminate-dead-state`):
- Removes unobservable stateful ops (`pyc.reg`, fifo/mem/cdc primitives).
- Honors `pyc.debug_keep=true` to preserve debug-visible state.

10. `CombCanonicalize` (`pyc-comb-canonicalize`):
- Boolean/mux rewrites on PYC comb ops.
- Examples: redundant mux collapse, i1 mux to logic gates, simple xor/xnor factoring.

11. `SLPPackWires` (`pyc-slp-pack-wires`):
- Current implementation is a conservative scaffold.
- No profitability packing rewrites are applied yet.

12. `CheckCombCycles` (`pyc-check-comb-cycles`):
- Detects combinational wire/assign cycles without sequential breaks.
- Emits explicit cycle path diagnostics and fails compile on cycle.

13. `PackI1Regs` (`pyc-pack-i1-regs`):
- Packs compatible runs of i1 registers (same clk/rst/en) into one wider reg.
- Rebuilds bit extracts and aliases to preserve behavior and naming.

14. `FuseComb` (`pyc-fuse-comb`):
- Fuses runs of fusable combinational ops into `pyc.comb` regions.
- Improves backend code size/compile speed for large designs.
- Disabled only when `--sim-mode=cpp-only --cpp-only-preserve-ops`.

15. `Canonicalizer` (second):
- Cleans IR after PYC structural transforms.

16. `CSE` (second):
- Removes post-fusion/post-pack duplicate expressions.

17. `RemoveDeadValues` (second):
- Removes newly dead intermediate values.

18. `SymbolDCE` (second):
- Removes newly dead symbols after cleanup.

19. `CheckFlatTypes` (`pyc-check-flat-types`):
- Requires only flat hardware types (`iN`, `!pyc.clock`, `!pyc.reset`) at interfaces and op operands/results.
- Fails if aggregates/unsupported types remain.

20. `CheckNoDynamic` (`pyc-check-no-dynamic`):
- Enforces no remaining SCF dynamic control-flow and no `index`-typed hardware values.
- Fails on any residual dynamic constructs.

21. `CheckLogicDepth` (`pyc-check-logic-depth`):
- Computes combinational depth between sequential boundaries.
- Unit cost for comb ops; zero cost for wires/aliases/constants/sequential ops.
- Emits per-function attrs: `pyc.logic_depth.max`, `pyc.logic_depth.wns`, `pyc.logic_depth.tns`.
- Fails compile when any endpoint depth exceeds `--logic-depth`.

22. `CollectCompileStats` (`pyc-collect-compile-stats`):
- Counts registers/memories and bit totals.
- Emits attrs consumed by final stats summary/JSON.

## 7) Non-default pass

`PrunePorts` (`pyc-prune-ports`) exists but is intentionally OFF in default `pyc-compile` pipeline because it changes public module interfaces by deleting unused function arguments and rewriting callsites.

## 8) Out-of-tree generated artifact policy

Generated outputs are local artifacts and must not be checked into git.

Default output root used by flow scripts:

- `.pycircuit_out/examples/...`
- `.pycircuit_out/janus/...`

The repo `.gitignore` enforces this policy.
