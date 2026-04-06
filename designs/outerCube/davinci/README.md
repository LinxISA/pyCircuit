# Davinci Out-of-Order Processor Core — PyCircuit V5 RTL

## Overview

Davinci is a **single-threaded, 4-wide, out-of-order** processor core targeting
AI inference, HPC, and dense linear algebra. It uses PyCircuit V5 new syntax
(`domain.signal` + `<<=` / `.assign`) for all RTL modules.

**Key characteristics:**
- 12-stage pipeline: F1→F2→D1→D2→DS→IS→EX1–EX4→WB (no retire/ROB)
- 4 instruction domains: Scalar, Vector, Cube (matrix), MTE (memory tile engine)
- Dual RAT: Scalar (32→128 GPRs) + Tile (32→256 physical tiles)
- Reference-counted register freeing (no ROB)
- 8 RAT checkpoints for branch recovery

## Architecture Source

| Document | Content |
|----------|---------|
| `Davinci_supersclar.md` | Full processor architecture (ISA, pipeline, OoO model) |
| `outerCube.md` | outerCube MXU (4096 MACs, dual-mode, multi-format) |
| `tregfile4k.md` | TRegFile-4K (1 MB, 8R+8W, 8-cycle calendar) |

## Module Hierarchy

```
davinci/
├── common/          — Parameters, CDB, TCB, Free List
├── frontend/
│   ├── fetch/       — F1-F2: PC generation, redirect
│   ├── decode/      — D1: 4-wide decoder, domain classification
│   └── rename/      — D2: Scalar RAT, Tile RAT, Checkpoints
├── backend/
│   ├── scalar_rs/   — 32-entry, 6-issue
│   ├── vec_rs/      — 16-entry, tile-tag wakeup
│   ├── scalar_exu/  — 4×ALU + MUL/DIV + BRU
│   ├── lsu/         — Load/store, store-to-load forwarding
│   ├── vec_unit/    — Epoch-pipelined vector execution
│   ├── cube_unit/   — outerCube MXU controller
│   └── mte_unit/    — Memory Tile Engine
├── regfile/         — Scalar PRF, Reference Counter
├── tests/
│   ├── unit/        — Per-module tests
│   └── integration/ — System-level pipeline tests
└── docs/            — MODULE_MAP.md, FEATURE_LIST.md
```

## Implementation Style

All modules use **PyCircuit V5 new syntax**:
- `domain.signal(width=W, reset_value=R, name=N)` for registers
- `signal <<= expr` for unconditional assignment
- `signal.assign(expr, when=cond)` for conditional assignment
- `cas(domain, wire, cycle=0)` for input wrapping
- `mux(cond, true_val, false_val)` for selection

## Running Tests

```bash
cd pyCircuit
PYTHONPATH=compiler/frontend python designs/outerCube/davinci/tests/unit/test_alu.py
PYTHONPATH=compiler/frontend python designs/outerCube/davinci/tests/unit/test_free_list.py
PYTHONPATH=compiler/frontend python designs/outerCube/davinci/tests/unit/test_scalar_rat.py
```

## Implementation Status

See `docs/FEATURE_LIST.md` for detailed per-feature status.

**Phase 1 (Infrastructure):** Complete — parameters, free list, ref counter, CDB, TCB, scalar PRF
**Phase 2 (Frontend):** Complete — fetch, decode, scalar RAT, tile RAT, checkpoints
**Phase 3 (Dispatch):** Complete — scalar RS, vector RS
**Phase 4 (Execution):** Complete — ALU, MUL/DIV, BRU, LSU, vector unit, cube unit, MTE
**Phase 5 (Integration):** In progress — top-level wiring, system tests

## Next Steps

1. Create `davinci_top.py` wiring all modules together
2. Implement remaining RS (LSU, Cube, MTE) with full dispatch routing
3. Add BPU (TAGE + BTB + RAS) for branch prediction
4. Implement L1-I/D cache models
5. Run full MLIR compilation on each module
6. Execute integration test scenarios
