# Davinci OoO Core — Module Map & Implementation Plan

**Architecture source**: `Davinci_supersclar.md`, `outerCube.md`, `tregfile4k.md`
**Style reference**: PyCircuit V5 new syntax (`domain.signal` + `<<=`)
**Method**: `docs/pycircuit_implementation_method.md` (10-step workflow)

---

## Module Hierarchy

```
davinci_top                              ✅ IMPL
├── frontend/
│   ├── fetch/          — F1-F2         ✅ IMPL
│   ├── bpu/            — Bimodal BPU   ✅ IMPL (simplified)
│   ├── ibuf/           — 16-entry FIFO ✅ IMPL
│   ├── decode/         — D1            ✅ IMPL
│   └── rename/
│       ├── scalar_rat  — 32→128        ✅ IMPL
│       ├── tile_rat    — 32→256        ✅ IMPL
│       ├── checkpoint  — 8-slot        ✅ IMPL
│       └── rename.py   — Top-level     ✅ IMPL
├── dispatch/           — 5-domain      ✅ IMPL
├── backend/
│   ├── scalar_rs/      — 32-entry      ✅ IMPL
│   ├── lsu_rs/         — 24-entry      ✅ IMPL
│   ├── vec_rs/         — 16-entry      ✅ IMPL
│   ├── cube_rs/        — 4-entry       ✅ IMPL
│   ├── mte_rs/         — 16-entry      ✅ IMPL
│   ├── scalar_exu/
│   │   ├── alu.py      — 4× ALU       ✅ IMPL
│   │   ├── muldiv.py   — MUL+DIV      ✅ IMPL (DIV added)
│   │   └── bru.py      — Branch        ✅ IMPL
│   ├── lsu/            — Load/Store    ✅ IMPL
│   ├── vec_unit/       — 16-cy epoch   ✅ IMPL
│   ├── cube_unit/      — OPA+drain     ✅ IMPL
│   └── mte_unit/       — LD/ST/GET/PUT ✅ IMPL
├── regfile/
│   ├── scalar_prf      — 128×64b       ✅ IMPL
│   ├── tregfile4k/     — 8R8W 1MB      ✅ IMPL (moved into tree)
│   └── ref_counter     — Scalar+Tile   ✅ IMPL
├── common/
│   ├── cdb             — 6-port        ✅ IMPL
│   ├── tcb             — 4-port        ✅ IMPL
│   ├── free_list       — FIFO          ✅ IMPL
│   └── parameters      — Global params ✅ IMPL
└── tests/
    ├── unit/           — Per-module     ✅ 3 tests
    └── integration/    — System-level   ✅ 4 tests
```

**29/29 modules implemented. All compile to valid MLIR.**

---

## Implementation Order (dependency-driven)

### Phase 1 — Common Infrastructure (no pipeline, standalone testable)

| Order | Module | File | Status |
|-------|--------|------|--------|
| 1.1 | Global parameters | `common/parameters.py` | ✅ IMPL |
| 1.2 | Free list (parameterized) | `common/free_list.py` | ✅ IMPL |
| 1.3 | Reference counter | `regfile/ref_counter.py` | ✅ IMPL |
| 1.4 | CDB | `common/cdb.py` | ✅ IMPL |
| 1.5 | TCB | `common/tcb.py` | ✅ IMPL |
| 1.6 | Scalar PRF | `regfile/scalar_prf.py` | ✅ IMPL |

### Phase 2 — Front-End (in pipeline order)

| Order | Module | File | Status |
|-------|--------|------|--------|
| 2.1 | Fetch unit | `frontend/fetch/fetch.py` | ✅ IMPL |
| 2.2 | BPU (bimodal) | `frontend/bpu/bpu.py` | ✅ IMPL |
| 2.3 | Instruction buffer | `frontend/ibuf/ibuf.py` | ✅ IMPL |
| 2.4 | Decoder | `frontend/decode/decode.py` | ✅ IMPL |
| 2.5 | Scalar RAT | `frontend/rename/scalar_rat.py` | ✅ IMPL |
| 2.6 | Tile RAT | `frontend/rename/tile_rat.py` | ✅ IMPL |
| 2.7 | Checkpoint store | `frontend/rename/checkpoint.py` | ✅ IMPL |
| 2.8 | Rename top | `frontend/rename/rename.py` | ✅ IMPL |

### Phase 3 — Dispatch + Reservation Stations

| Order | Module | File | Status |
|-------|--------|------|--------|
| 3.1 | Scalar RS | `backend/scalar_rs/scalar_rs.py` | ✅ IMPL |
| 3.2 | LSU RS | `backend/lsu_rs/lsu_rs.py` | ✅ IMPL |
| 3.3 | Vector RS | `backend/vec_rs/vec_rs.py` | ✅ IMPL |
| 3.4 | Cube RS | `backend/cube_rs/cube_rs.py` | ✅ IMPL |
| 3.5 | MTE RS | `backend/mte_rs/mte_rs.py` | ✅ IMPL |
| 3.6 | Dispatch | `dispatch/dispatch.py` | ✅ IMPL |

### Phase 4 — Execution Units

| Order | Module | File | Status |
|-------|--------|------|--------|
| 4.1 | ALU (×4) | `backend/scalar_exu/alu.py` | ✅ IMPL |
| 4.2 | MUL/DIV | `backend/scalar_exu/muldiv.py` | ✅ IMPL (MUL+DIV) |
| 4.3 | Branch unit | `backend/scalar_exu/bru.py` | ✅ IMPL |
| 4.4 | LSU | `backend/lsu/lsu.py` | ✅ IMPL |
| 4.5 | Vector unit | `backend/vec_unit/vec_unit.py` | ✅ IMPL |
| 4.6 | Cube unit | `backend/cube_unit/cube_unit.py` | ✅ IMPL |
| 4.7 | MTE unit | `backend/mte_unit/mte_unit.py` | ✅ IMPL |

### Phase 5 — Integration

| Order | Module | File | Status |
|-------|--------|------|--------|
| 5.1 | Core top | `davinci_top.py` | ✅ IMPL |
| 5.2 | Scalar pipeline test | `tests/integration/test_scalar_pipeline.py` | ✅ IMPL |
| 5.3 | Tile pipeline test | `tests/integration/test_tile_pipeline.py` | ✅ IMPL |
| 5.4 | Cube GEMM test | `tests/integration/test_cube_gemm.py` | ✅ IMPL |
| 5.5 | Branch recovery test | `tests/integration/test_branch_recovery.py` | ✅ IMPL |

---

## Key Design Decisions

1. **TRegFile-4K**: Moved into `regfile/tregfile4k/` (PyCircuit V5, 159K chars MLIR).
2. **No precise exceptions**: No ROB, no retire stage. Stores commit OoO.
3. **Two RATs**: Scalar (32→128) and Tile (32→256), both checkpointed.
4. **Reference counting**: Shared logic for scalar and tile physical register freeing.
5. **CDB + TCB**: Separate broadcast networks for scalar (64-bit data) and tile (tag-only) results.
6. **Simplified caches**: L1-I/D and L2 modeled as behavioral (latency-accurate, not cycle-exact RTL).
7. **All RS self-contained**: Each RS implements its own wakeup/select logic (no shared rs_base) for clarity and independent compilation.
8. **BPU simplified**: Bimodal 2-bit counter table instead of full TAGE; sufficient for functional simulation.
