# Davinci Out-of-Order Processor Core

## 1. Overview & Design Philosophy

The Davinci core is a **single-threaded, 4-wide, out-of-order** processor targeting AI inference, HPC, and dense linear algebra workloads. It executes a unified instruction stream containing four instruction domains — scalar, vector, cube (matrix), and memory-tile-engine (MTE) — on a shared pipeline front-end with distributed back-end execution units.

**Key design decisions:**

- **64-bit RISC scalar ISA** resembling ARM / RISC-V, providing general-purpose control flow and address computation.
- **Cube ISA** for tile-level outer-product-accumulate operations on the outerCube MXU (see `outerCube.md`).
- **Vector ISA** for element-wise SIMD operations (see [pto-isa docs](https://github.com/hw-native-sys/pto-isa/tree/main/docs/isa)).
- **MTE ISA** for bulk tile transfers (memory ↔ TRegFile-4K) and scalar element access (GPR ↔ TRegFile-4K via TILE.GET / TILE.PUT).
- **No precise interrupts or exceptions.** The core executes run-to-completion kernels only. This eliminates the Reorder Buffer (ROB) and in-order retirement, yielding a simpler, lower-power OoO engine equivalent to Tomasulo's original algorithm (IBM 360/91, 1967).
- **RAT + checkpoint** recovery for branch mispredictions; **reference-counted** physical register freeing (no ROB lifetime tracking).
- **TRegFile-4K** (1 MB, 8R+8W, 4 KB tiles) as the **physical tile register file** for vector, cube, and MTE operands (see `tregfile4k.md`). 32 architectural tile registers (T0–T31) are renamed into 256 physical tile slots by the **Tile RAT**, mirroring the scalar rename scheme. There is no separate vector register file.

### 1.1 Key Parameters

| Parameter | Value |
|-----------|-------|
| Scalar ISA width | **64-bit** RISC (ARM / RISC-V style) |
| Architectural GPRs | **32** (X0–X31), 64-bit |
| Physical GPRs | **128** (P0–P127), 64-bit |
| Architectural tile regs | **32** (T0–T31), 4 KB each (used by vector, cube, and MTE) |
| Physical tile regs | **256** (PT0–PT255) in TRegFile-4K |
| TRegFile-4K capacity | 256 × 4 KB = **1 MB** |
| Fetch / decode width | **4** instructions / cycle |
| Scalar issue width | **6** (4 ALU + 1 MUL/DIV + 1 branch) |
| Vector issue width | **1** instruction / cycle |
| Cube issue width | **1** CUBE instruction / cycle |
| MTE issue width | **2** TILE.LD/ST per cycle |
| Pipeline depth (scalar) | **12** stages (fetch-to-writeback) |
| Branch predictor | Hybrid TAGE + BTB + RAS |
| RAT checkpoints | **8** (max in-flight branches) |
| Reservation station entries | Scalar: 32, LSU: 24, Vector: 16, Cube: 4, MTE: 16 |
| L1-I cache | **64 KB**, 4-way, 64 B line |
| L1-D cache | **64 KB**, 4-way, 64 B line, non-blocking (8 MSHRs) |
| L2 cache (core-private) | **512 KB**, 8-way, 64 B line |
| Cube MXU | 4096 base MACs, 8 banks, dual-mode A/B |
| Clock target | ≥ **1.5 GHz** (5 nm) |
| Peak FP16 throughput | **12.3 TFLOPS** (cube) |
| Peak FP8 throughput | **24.6 TOPS** (cube) |
| Peak MXFP4 throughput | **98.3 TOPS** (cube) |

---

## 2. ISA Summary

The Davinci core fetches, decodes, renames, and dispatches instructions from four domains within a single instruction stream. All instructions use a **32-bit fixed-width** encoding.

### 2.1 Scalar ISA

A 64-bit RISC instruction set with ARM / RISC-V style operations.

| Category | Instructions | Operands | Latency (cycles) |
|----------|-------------|----------|-------------------|
| Integer ALU | ADD, SUB, AND, OR, XOR, SLL, SRL, SRA, SLT, MOV | 2 src GPR, 1 dst GPR | 1 |
| Immediate ALU | ADDI, ANDI, ORI, XORI, SLLI, SRLI, SRAI, LUI | 1 src GPR + imm, 1 dst GPR | 1 |
| Multiply | MUL, MULH, MULHU | 2 src GPR, 1 dst GPR | 4 (pipelined) |
| Divide | DIV, DIVU, REM, REMU | 2 src GPR, 1 dst GPR | 12–20 (non-pipelined) |
| Compare & branch | BEQ, BNE, BLT, BGE, BLTU, BGEU | 2 src GPR + offset | 1 (resolve) |
| Jump | JAL, JALR | 1 src GPR + offset, 1 dst GPR | 1 |
| Load | LB, LH, LW, LD, LBU, LHU, LWU | 1 src GPR + offset, 1 dst GPR | 4 (L1 hit) |
| Store | SB, SH, SW, SD | 2 src GPR + offset | 4 (L1 hit) |
| System | FENCE, NOP, HALT | — | varies |

**Architectural registers:** X0 (hardwired zero) through X31, plus a program counter (PC). Condition flags are not used; branches compare register values directly (RISC-V style).

**Encoding (32-bit):**

```
  31       25 24  20 19  15 14  12 11   7 6     0
 ┌──────────┬──────┬──────┬──────┬──────┬────────┐
 │  funct7  │  rs2 │  rs1 │funct3│  rd  │ opcode │  R-type
 └──────────┴──────┴──────┴──────┴──────┴────────┘

 ┌─────────────────┬──────┬──────┬──────┬────────┐
 │    imm[11:0]    │  rs1 │funct3│  rd  │ opcode │  I-type
 └─────────────────┴──────┴──────┴──────┴────────┘
```

### 2.2 Vector ISA

The vector unit executes SIMD instructions that operate on **tile registers** in TRegFile-4K. Each 4 KB tile is treated as a 2D matrix of elements laid out in **64 × 512-bit rows**. Vector instructions process all 64 rows over one 8-cycle TRegFile read epoch (512 B/cy); compute is pipelined within the read epoch and results are written back in the subsequent 8-cycle write epoch.

**Tile-as-vector-register model.** There is no separate vector register file. Vector operands are **architectural tile registers** (T0–T31), which the Tile RAT renames to physical tile slots (PT0–PT255) in TRegFile-4K. A full-tile vector instruction has **16-cycle latency** (8-cycle read epoch + 8-cycle write epoch), but is **epoch-pipelined** at **1 tile per 8 cycles** throughput — the write epoch of one instruction overlaps the read epoch of the next.

#### 2.2.1 Tile Dimensions (4 KB tile, 512-bit row)

| Element type | Width | Columns/row | Rows | Elements/tile | Tile size | Notes |
|-------------|-------|-------------|------|---------------|-----------|-------|
| FP64 / INT64 | 8 B | 8 | 64 | 512 | 4 KB | |
| FP32 / INT32 / UINT32 | 4 B | 16 | 64 | 1024 | 4 KB | |
| FP16 / BF16 / INT16 / UINT16 | 2 B | 32 | 64 | 2048 | 4 KB | |
| FP8 (E4M3 / E5M2) / INT8 / UINT8 | 1 B | 64 | 64 | 4096 | 4 KB | |
| MXFP4 | ½ B | 128 | 64 | 8192 | 4 KB | 32-element groups share an 8-bit scale (stored in separate scale tile) |
| HiFP4 | ½ B | 128 | 64 | 8192 | 4 KB | Similar to MXFP4 with different exponent encoding |

Supported element types are encoded in the instruction's `funct3` field (primary) and a 1-bit `funct7` subfield `W` (wide format selector):

| funct3 | W | Type | Abbreviation | Element width |
|--------|---|------|-------------|---------------|
| 000 | 0 | FP64 | .f64 | 8 B |
| 001 | 0 | FP32 | .f32 | 4 B |
| 010 | 0 | FP16 | .f16 | 2 B |
| 011 | 0 | BF16 | .bf16 | 2 B |
| 100 | 0 | FP8 (E4M3) | .f8 | 1 B |
| 101 | 0 | INT32 | .i32 | 4 B |
| 110 | 0 | INT16 | .i16 | 2 B |
| 111 | 0 | INT8 | .i8 | 1 B |
| 100 | 1 | MXFP4 | .mxf4 | ½ B (+ shared 8-bit scale per 32 elements) |
| 101 | 1 | HiFP4 | .hf4 | ½ B (+ shared 8-bit scale per 32 elements) |

MXFP4 / HiFP4 tiles pack **128 elements per 512-bit row** (2 elements per byte). The associated **scale tile** contains one FP8 scale factor per 32-element group; for a 128-column tile this is 4 scales per row, stored in a separate tile operand. Vector instructions on MXFP4/HiFP4 data typically take a data tile and a scale tile as inputs (see VDEQUANT, VQUANT).

#### 2.2.2 Vector Instruction Encoding (32-bit)

All vector instructions use a **32-bit fixed-width** encoding. Tile register fields are 5 bits (T0–T31). Scalar register fields (for tile-scalar variants) are 5 bits (X0–X31).

```
  R-type (tile-tile):
  31       25 24  20 19  15 14  12 11   7 6     0
 ┌──────────┬──────┬──────┬──────┬──────┬────────┐
 │  funct7  │  Ts2 │  Ts1 │funct3│  Td  │ opcode │
 │ (op)     │ (5b) │ (5b) │(type)│ (5b) │ VEC    │
 └──────────┴──────┴──────┴──────┴──────┴────────┘

  S-type (tile-scalar):
 ┌──────────┬──────┬──────┬──────┬──────┬────────┐
 │  funct7  │  Xs  │  Ts1 │funct3│  Td  │ opcode │
 │ (op+S)   │ (5b) │ (5b) │(type)│ (5b) │ VEC    │
 └──────────┴──────┴──────┴──────┴──────┴────────┘

  T-type (ternary, 3-source):
 ┌──────────┬──────┬──────┬──────┬──────┬────────┐
 │  funct7  │  Ts3 │  Ts2 │funct3│  Td  │ opcode │
 │ (op)     │ (5b) │ (5b) │(type)│ (5b) │ VEC    │
 └──────────┴──────┴──────┴──────┴──────┴────────┘
   Ts1 is implicitly Td (accumulate-in-place) or encoded in funct7 subfield.

  U-type (unary):
 ┌──────────┬──────┬──────┬──────┬──────┬────────┐
 │  funct7  │ 00000│  Ts1 │funct3│  Td  │ opcode │
 │ (op)     │      │ (5b) │(type)│ (5b) │ VEC    │
 └──────────┴──────┴──────┴──────┴──────┴────────┘
```

#### 2.2.3 Complete Vector Instruction List

All instructions below operate on **4 KB tiles** (T0–T31 architectural, renamed to PT0–PT255 via Tile RAT). Latency column shows full instruction latency including TRegFile read and write epochs. All full-tile → tile instructions are **16 cycles** (8 read + 8 write) and **epoch-pipelined** at 1 tile/8 cy throughput unless noted otherwise.

**Category A — Elementwise Arithmetic (tile × tile → tile)**

| Mnemonic | PTO origin | Operands | Semantics | Types | Latency |
|----------|-----------|----------|-----------|-------|---------|
| VADD | TADD | Td, Ts1, Ts2 | Td[i,j] = Ts1[i,j] + Ts2[i,j] | FP64/32/16/BF16/FP8, INT32/16/8 | 16 |
| VSUB | TSUB | Td, Ts1, Ts2 | Td[i,j] = Ts1[i,j] − Ts2[i,j] | same | 16 |
| VMUL | TMUL | Td, Ts1, Ts2 | Td[i,j] = Ts1[i,j] × Ts2[i,j] | same | 16 |
| VMIN | TMIN | Td, Ts1, Ts2 | Td[i,j] = min(Ts1[i,j], Ts2[i,j]) | same | 16 |
| VMAX | TMAX | Td, Ts1, Ts2 | Td[i,j] = max(Ts1[i,j], Ts2[i,j]) | same | 16 |
| VDIV | TDIV | Td, Ts1, Ts2 | Td[i,j] = Ts1[i,j] / Ts2[i,j] | FP64/32/16/BF16 | 16 |
| VREM | TREM | Td, Ts1, Ts2 | Td[i,j] = remainder(Ts1[i,j], Ts2[i,j]) | INT32/16/8 | 16 |
| VFMOD | TFMOD | Td, Ts1, Ts2 | Td[i,j] = fmod(Ts1[i,j], Ts2[i,j]) | FP64/32/16/BF16 | 16 |
| VADDC | TADDC | Td, Ts1, Ts2, Ts3 | Td[i,j] = Ts1[i,j] + Ts2[i,j] + Ts3[i,j] | FP32/16/BF16, INT32/16 | 16 |
| VSUBC | TSUBC | Td, Ts1, Ts2, Ts3 | Td[i,j] = Ts1[i,j] − Ts2[i,j] + Ts3[i,j] | same | 16 |
| VFMA | — | Td, Ts1, Ts2, Ts3 | Td[i,j] = Ts1[i,j] × Ts2[i,j] + Ts3[i,j] | FP64/32/16/BF16 | 16 |
| VPRELU | TPRELU | Td, Ts1, Ts2 | Td[i,j] = Ts1[i,j]≥0 ? Ts1[i,j] : Ts1[i,j]×Ts2[i,j] | FP32/16/BF16 | 16 |

**Category B — Elementwise Arithmetic (tile × scalar → tile)**

| Mnemonic | PTO origin | Operands | Semantics | Types | Latency |
|----------|-----------|----------|-----------|-------|---------|
| VADDS | TADDS | Td, Ts1, Xs | Td[i,j] = Ts1[i,j] + Xs | FP64/32/16/BF16, INT32/16/8 | 16 |
| VSUBS | TSUBS | Td, Ts1, Xs | Td[i,j] = Ts1[i,j] − Xs | same | 16 |
| VMULS | TMULS | Td, Ts1, Xs | Td[i,j] = Ts1[i,j] × Xs | same | 16 |
| VDIVS | TDIVS | Td, Ts1, Xs | Td[i,j] = Ts1[i,j] / Xs | FP64/32/16/BF16 | 16 |
| VMINS | TMINS | Td, Ts1, Xs | Td[i,j] = min(Ts1[i,j], Xs) | FP64/32/16/BF16, INT32/16/8 | 16 |
| VMAXS | TMAXS | Td, Ts1, Xs | Td[i,j] = max(Ts1[i,j], Xs) | same | 16 |
| VANDS | TANDS | Td, Ts1, Xs | Td[i,j] = Ts1[i,j] & Xs | INT32/16/8 | 16 |
| VORS | TORS | Td, Ts1, Xs | Td[i,j] = Ts1[i,j] \| Xs | INT32/16/8 | 16 |
| VXORS | TXORS | Td, Ts1, Xs | Td[i,j] = Ts1[i,j] ^ Xs | INT32/16/8 | 16 |
| VSHLS | TSHLS | Td, Ts1, Xs | Td[i,j] = Ts1[i,j] << Xs | INT32/16/8 | 16 |
| VSHRS | TSHRS | Td, Ts1, Xs | Td[i,j] = Ts1[i,j] >> Xs | INT32/16/8 | 16 |
| VLRELU | TLRELU | Td, Ts1, Xs | Td[i,j] = Ts1[i,j]≥0 ? Ts1[i,j] : Ts1[i,j]×Xs | FP32/16/BF16 | 16 |
| VADDSC | TADDSC | Td, Ts1, Xs, Ts2 | Td[i,j] = Ts1[i,j] + Xs + Ts2[i,j] | FP32/16/BF16 | 16 |
| VSUBSC | TSUBSC | Td, Ts1, Xs, Ts2 | Td[i,j] = Ts1[i,j] − Xs + Ts2[i,j] | FP32/16/BF16 | 16 |
| VEXPAND | TEXPANDS | Td, Xs | Td[i,j] = Xs (broadcast scalar to all elements) | all | 16 |
| VAXPY | TAXPY | Td, Ts1, Xs, Ts2 | Td[i,j] = Ts1[i,j] × Xs + Ts2[i,j] | FP64/32/16/BF16 | 16 |

**Category C — Elementwise Unary (tile → tile)**

| Mnemonic | PTO origin | Operands | Semantics | Types | Latency |
|----------|-----------|----------|-----------|-------|---------|
| VABS | TABS | Td, Ts1 | Td[i,j] = \|Ts1[i,j]\| | FP64/32/16/BF16, INT32/16/8 | 16 |
| VNEG | TNEG | Td, Ts1 | Td[i,j] = −Ts1[i,j] | same | 16 |
| VNOT | TNOT | Td, Ts1 | Td[i,j] = ~Ts1[i,j] | INT32/16/8 | 16 |
| VRELU | TRELU | Td, Ts1 | Td[i,j] = max(0, Ts1[i,j]) | FP64/32/16/BF16 | 16 |
| VRECIP | TRECIP | Td, Ts1 | Td[i,j] = 1/Ts1[i,j] | FP64/32/16/BF16 | 16 |
| VRSQRT | TRSQRT | Td, Ts1 | Td[i,j] = 1/√Ts1[i,j] | FP64/32/16/BF16 | 16 |
| VSQRT | TSQRT | Td, Ts1 | Td[i,j] = √Ts1[i,j] | FP64/32/16/BF16 | 16 |
| VEXP | TEXP | Td, Ts1 | Td[i,j] = e^Ts1[i,j] | FP32/16/BF16 | 16 |
| VLOG | TLOG | Td, Ts1 | Td[i,j] = ln(Ts1[i,j]) | FP32/16/BF16 | 16 |

**Category D — Elementwise Logic & Shift (tile × tile → tile)**

| Mnemonic | PTO origin | Operands | Semantics | Types | Latency |
|----------|-----------|----------|-----------|-------|---------|
| VAND | TAND | Td, Ts1, Ts2 | Td[i,j] = Ts1[i,j] & Ts2[i,j] | INT32/16/8 | 16 |
| VOR | TOR | Td, Ts1, Ts2 | Td[i,j] = Ts1[i,j] \| Ts2[i,j] | INT32/16/8 | 16 |
| VXOR | TXOR | Td, Ts1, Ts2 | Td[i,j] = Ts1[i,j] ^ Ts2[i,j] | INT32/16/8 | 16 |
| VSHL | TSHL | Td, Ts1, Ts2 | Td[i,j] = Ts1[i,j] << Ts2[i,j] | INT32/16/8 | 16 |
| VSHR | TSHR | Td, Ts1, Ts2 | Td[i,j] = Ts1[i,j] >> Ts2[i,j] | INT32/16/8 | 16 |

**Category E — Compare & Select**

| Mnemonic | PTO origin | Operands | Semantics | Types | Latency |
|----------|-----------|----------|-----------|-------|---------|
| VCMP | TCMP | Td, Ts1, Ts2 | Td = packed predicate mask from Ts1 cmp Ts2 (cmp mode in funct7) | all | 16 |
| VCMPS | TCMPS | Td, Ts1, Xs | Td = packed predicate mask from Ts1 cmp Xs | all | 16 |
| VSEL | TSEL | Td, Ts1, Ts2, Tmask | Td[i,j] = Tmask[i,j] ? Ts1[i,j] : Ts2[i,j] | all | 16 |
| VSELS | TSELS | Td, Ts1, Xs, Tmask | Td[i,j] = Tmask[i,j] ? Ts1[i,j] : Xs | all | 16 |

**Category F — Type Conversion**

| Mnemonic | PTO origin | Operands | Semantics | Types | Latency |
|----------|-----------|----------|-----------|-------|---------|
| VCVT | TCVT | Td, Ts1 | Td[i,j] = convert(Ts1[i,j]), type in funct7 | any → any | 16 |
| VQUANT | TQUANT | Td, Ts1, Ts_scale | Quantize: Td = round(Ts1 / Ts_scale) | FP32→INT8/FP8 | 16 |
| VDEQUANT | TDEQUANT | Td, Ts1, Ts_scale | Dequantize: Td = Ts1 × Ts_scale | INT8/FP8→FP32 | 16 |

**Category G — Row Reduction (tile → column-vector tile)**

Row reductions read a full tile (64 rows) and produce a **column-vector result** — one scalar per row stored in column 0 of the destination tile (remaining columns are zero). The result tile is written in a write epoch.

| Mnemonic | PTO origin | Operands | Semantics | Types | Latency |
|----------|-----------|----------|-----------|-------|---------|
| VROWSUM | TROWSUM | Td, Ts1 | Td[i,0] = Σ_j Ts1[i,j] | FP64/32/16/BF16 | 16 |
| VROWPROD | TROWPROD | Td, Ts1 | Td[i,0] = Π_j Ts1[i,j] | FP32/16/BF16 | 16 |
| VROWMAX | TROWMAX | Td, Ts1 | Td[i,0] = max_j Ts1[i,j] | FP64/32/16/BF16, INT32/16/8 | 16 |
| VROWMIN | TROWMIN | Td, Ts1 | Td[i,0] = min_j Ts1[i,j] | same | 16 |
| VROWARGMAX | TROWARGMAX | Td, Ts1 | Td[i,0] = argmax_j Ts1[i,j] | FP32/16, INT32/16 | 16 |
| VROWARGMIN | TROWARGMIN | Td, Ts1 | Td[i,0] = argmin_j Ts1[i,j] | same | 16 |

**Category H — Column Reduction (tile → row-vector tile)**

Column reductions produce a **row-vector result** — one scalar per column stored in row 0 of the destination tile.

| Mnemonic | PTO origin | Operands | Semantics | Types | Latency |
|----------|-----------|----------|-----------|-------|---------|
| VCOLSUM | TCOLSUM | Td, Ts1 | Td[0,j] = Σ_i Ts1[i,j] | FP64/32/16/BF16 | 16 |
| VCOLPROD | TCOLPROD | Td, Ts1 | Td[0,j] = Π_i Ts1[i,j] | FP32/16/BF16 | 16 |
| VCOLMAX | TCOLMAX | Td, Ts1 | Td[0,j] = max_i Ts1[i,j] | FP64/32/16/BF16, INT32/16/8 | 16 |
| VCOLMIN | TCOLMIN | Td, Ts1 | Td[0,j] = min_i Ts1[i,j] | same | 16 |
| VCOLARGMAX | TCOLARGMAX | Td, Ts1 | Td[0,j] = argmax_i Ts1[i,j] | FP32/16, INT32/16 | 16 |
| VCOLARGMIN | TCOLARGMIN | Td, Ts1 | Td[0,j] = argmin_i Ts1[i,j] | same | 16 |

**Category I — Row Broadcast-Expand (column-vector × tile → tile)**

Each instruction broadcasts a per-row scalar from `Ts2` (column 0) across all columns of `Ts1`, applying the specified operation. These are key building blocks for softmax, layer-norm, and similar row-wise normalization kernels.

| Mnemonic | PTO origin | Operands | Semantics | Types | Latency |
|----------|-----------|----------|-----------|-------|---------|
| VROWEXPAND | TROWEXPAND | Td, Ts1 | Td[i,j] = Ts1[i,0] (broadcast col-0) | all | 16 |
| VROWEXPADD | TROWEXPANDADD | Td, Ts1, Ts2 | Td[i,j] = Ts1[i,j] + Ts2[i,0] | FP32/16/BF16 | 16 |
| VROWEXPSUB | TROWEXPANDSUB | Td, Ts1, Ts2 | Td[i,j] = Ts1[i,j] − Ts2[i,0] | FP32/16/BF16 | 16 |
| VROWEXPMUL | TROWEXPANDMUL | Td, Ts1, Ts2 | Td[i,j] = Ts1[i,j] × Ts2[i,0] | FP32/16/BF16 | 16 |
| VROWEXPDIV | TROWEXPANDDIV | Td, Ts1, Ts2 | Td[i,j] = Ts1[i,j] / Ts2[i,0] | FP32/16/BF16 | 16 |
| VROWEXPMAX | TROWEXPANDMAX | Td, Ts1, Ts2 | Td[i,j] = max(Ts1[i,j], Ts2[i,0]) | FP32/16/BF16 | 16 |
| VROWEXPMIN | TROWEXPANDMIN | Td, Ts1, Ts2 | Td[i,j] = min(Ts1[i,j], Ts2[i,0]) | FP32/16/BF16 | 16 |
| VROWEXPEXPDIF | TROWEXPANDEXPDIF | Td, Ts1, Ts2 | Td[i,j] = exp(Ts1[i,j] − Ts2[i,0]) | FP32/16/BF16 | 16 |

**Category J — Column Broadcast-Expand (row-vector × tile → tile)**

Same as row-expand but broadcasts a per-column scalar from `Ts2` (row 0) across all rows.

| Mnemonic | PTO origin | Operands | Semantics | Types | Latency |
|----------|-----------|----------|-----------|-------|---------|
| VCOLEXPAND | TCOLEXPAND | Td, Ts1 | Td[i,j] = Ts1[0,j] (broadcast row-0) | all | 16 |
| VCOLEXPADD | TCOLEXPANDADD | Td, Ts1, Ts2 | Td[i,j] = Ts1[i,j] + Ts2[0,j] | FP32/16/BF16 | 16 |
| VCOLEXPSUB | TCOLEXPANDSUB | Td, Ts1, Ts2 | Td[i,j] = Ts1[i,j] − Ts2[0,j] | FP32/16/BF16 | 16 |
| VCOLEXPMUL | TCOLEXPANDMUL | Td, Ts1, Ts2 | Td[i,j] = Ts1[i,j] × Ts2[0,j] | FP32/16/BF16 | 16 |
| VCOLEXPDIV | TCOLEXPANDDIV | Td, Ts1, Ts2 | Td[i,j] = Ts1[i,j] / Ts2[0,j] | FP32/16/BF16 | 16 |
| VCOLEXPMAX | TCOLEXPANDMAX | Td, Ts1, Ts2 | Td[i,j] = max(Ts1[i,j], Ts2[0,j]) | FP32/16/BF16 | 16 |
| VCOLEXPMIN | TCOLEXPANDMIN | Td, Ts1, Ts2 | Td[i,j] = min(Ts1[i,j], Ts2[0,j]) | FP32/16/BF16 | 16 |
| VCOLEXPEXPDIF | TCOLEXPANDEXPDIF | Td, Ts1, Ts2 | Td[i,j] = exp(Ts1[i,j] − Ts2[0,j]) | FP32/16/BF16 | 16 |

**Category K — Data Movement & Permute (tile → tile)**

| Mnemonic | PTO origin | Operands | Semantics | Types | Latency |
|----------|-----------|----------|-----------|-------|---------|
| VEXTRACT | TEXTRACT | Td, Ts1, Xrow, Xcol | Extract sub-tile from Ts1 at offset (Xrow, Xcol) → Td | all | 16 |
| VINSERT | TINSERT | Td, Ts1, Xrow, Xcol | Insert Ts1 into Td at offset (Xrow, Xcol) (RMW) | all | 16 |
| VCONCAT | TCONCAT | Td, Ts1, Ts2 | Concatenate Ts1 and Ts2 along row or col (mode in funct7) | all | 16 |
| VFILLPAD | TFILLPAD | Td, Ts1, Xval | Copy Ts1 to Td; pad outside valid region with scalar Xval | all | 16 |
| VGATHER | TGATHER | Td, Ts1, Tidx | Gather: Td[i,j] = Ts1[Tidx[i,j]] | all | 16 |
| VGATHERB | TGATHERB | Td, Ts1, Tidx | Gather by byte offset: Td[i,j] = Ts1[byte_at(Tidx[i,j])] | all | 16 |
| VSCATTER | TSCATTER | Td, Ts1, Tidx | Scatter: Td[Tidx[i,j]] = Ts1[i,j] | all | 16 |
| VPACK | TPACK | Td, Ts1, Ts2 | Pack two narrow-type tiles into one wider tile | INT16→8, FP16→8 | 16 |
| VCI | TCI | Td | Generate contiguous integer sequence: Td[i,j] = i×cols+j | INT32/16/8 | 16 |
| VTRI | TTRI | Td, Xdiag | Generate triangular mask: Td[i,j] = (j ≤ i+diag) ? 1 : 0 | INT32/16/8 | 16 |

**Category L — Partial-Tile Operations**

Partial operations handle tiles with mismatched valid regions (out-of-bounds elements treated as identity).

| Mnemonic | PTO origin | Operands | Semantics | Types | Latency |
|----------|-----------|----------|-----------|-------|---------|
| VPARTADD | TPARTADD | Td, Ts1, Ts2 | Partial add (identity=0 outside valid region) | FP32/16/BF16 | 16 |
| VPARTMUL | TPARTMUL | Td, Ts1, Ts2 | Partial mul (identity=1 outside valid region) | FP32/16/BF16 | 16 |
| VPARTMAX | TPARTMAX | Td, Ts1, Ts2 | Partial max (identity=−∞ outside valid region) | FP32/16/BF16 | 16 |
| VPARTMIN | TPARTMIN | Td, Ts1, Ts2 | Partial min (identity=+∞ outside valid region) | FP32/16/BF16 | 16 |

**Category M — Complex / Multi-Cycle**

| Mnemonic | PTO origin | Operands | Semantics | Types | Latency |
|----------|-----------|----------|-----------|-------|---------|
| VSORT32 | TSORT32 | Td, Tidx, Ts1 | Sort each 32-element block with index tracking | FP32/16, INT32 | 16–32 |
| VMRGSORT | TMRGSORT | Td, Ts1, Ts2 | Merge two sorted sequences | FP32/16, INT32 | 16–32 |
| VHISTO | THISTOGRAM | Td, Ts1, Tidx | Accumulate histogram bins from Ts1 using index Tidx | INT32/16 | 16 |
| VIMG2COL | TIMG2COL | Td, Ts1, Xcfg | Image-to-column transform for convolution | FP32/16/BF16 | 16 |

#### 2.2.4 PTO ISA Cross-Reference

The following PTO ISA instructions map to other Davinci instruction domains (not the vector unit):

| PTO instruction | Davinci mapping | Domain |
|----------------|----------------|--------|
| TLOAD | TILE.LD | MTE (§2.4.1) |
| TSTORE / TSTORE_FP | TILE.ST | MTE (§2.4.1) |
| MGATHER | TILE.GATHER | MTE (§2.4.1) |
| MSCATTER | TILE.SCATTER | MTE (§2.4.1) |
| TPREFETCH | TILE.LD (prefetch hint) | MTE (§2.4.1) |
| TMOV / TALIAS | TILE.MOVE (rename-only) | MTE (§2.4.2) |
| TTRANS | TILE.TRANSPOSE | MTE (§2.4.2) |
| TMATMUL / TMATMUL_ACC / TMATMUL_BIAS | CUBE.OPA + CUBE.DRAIN | Cube (§2.3) |
| TMATMUL_MX | CUBE.OPA (MXFP4/HiFP4 mode) | Cube (§2.3) |
| TGEMV / TGEMV_ACC / TGEMV_BIAS | CUBE.OPA (M=1 row) | Cube (§2.3) |
| TGEMV_MX | CUBE.OPA (MXFP4 GEMV) | Cube (§2.3) |
| TSYNC | FENCE | Scalar (§2.1) |
| TASSIGN | Software-managed (Tile RAT) | — |
| TFREE | Reference counting (§10.5) | — |
| TPUSH / TPOP | TILE.ST / TILE.LD to stack | MTE + software |
| TSUBVIEW / TRESHAPE | Software tile aliasing | — |
| TSETFMATRIX / TSET_IMG2COL_* | CSR writes (cube/MTE config) | Scalar |
| TPRINT | Debug hook (not in hardware ISA) | — |
| TGET_SCALE_ADDR | Software address computation | — |

#### 2.2.5 Instruction Count Summary

| Category | Count | Description |
|----------|-------|-------------|
| A. Elementwise (tile×tile) | 12 | Arithmetic binary/ternary |
| B. Tile-scalar | 16 | Arithmetic with GPR scalar |
| C. Unary | 9 | abs, neg, not, relu, transcendental |
| D. Logic & shift | 5 | Bitwise binary |
| E. Compare & select | 4 | Predicated operations |
| F. Type conversion | 3 | cvt, quant, dequant |
| G. Row reduction | 6 | Per-row reduce to column-vector |
| H. Column reduction | 6 | Per-column reduce to row-vector |
| I. Row broadcast-expand | 8 | Per-row scalar broadcast with op |
| J. Column broadcast-expand | 8 | Per-column scalar broadcast with op |
| K. Data movement & permute | 10 | Extract, insert, gather, scatter |
| L. Partial-tile | 4 | Mismatched valid region ops |
| M. Complex / multi-cycle | 4 | Sort, histogram, img2col |
| **Total** | **95** | |

Full per-instruction specification: see [pto-isa docs](https://github.com/hw-native-sys/pto-isa/tree/main/docs/isa).

### 2.3 Cube ISA

Tile-level instructions that drive the outerCube MXU. Each `CUBE.OPA` consumes tile registers and executes all K-loop OPA steps internally.

| Instruction | Operands | Function |
|-------------|----------|----------|
| CUBE.CFG | mode, fmt [, Mactive] | Set operating mode (A/B) and data format |
| CUBE.OPA | zd, Ta, Tb, Rn | Outer product accumulate: iterate over Nb B-tiles |
| CUBE.DRAIN | zd, Tc | Drain accumulator buffer to tile register(s) |
| CUBE.ZERO | zd | Zero accumulator buffer (1 cycle) |
| CUBE.WAIT | zd | Stall until pending drain completes |

Supported formats: FP16, BF16, FP8 (E4M3/E5M2), MXFP4, HiFP4. All accumulate into FP32.

Full cube ISA specification: see `outerCube.md` §6.

### 2.4 MTE ISA (Memory Tile Engine)

The MTE bridges three domains: **memory ↔ TRegFile-4K** (bulk tile transfers) and **scalar GPR ↔ TRegFile-4K** (single-element access). All MTE instructions flow through both the Scalar RAT and Tile RAT at rename.

#### 2.4.1 Bulk Tile Transfer Instructions

| Instruction | Operands | Function |
|-------------|----------|----------|
| TILE.LD | Td, [Rbase] | Contiguous load: 4 KB from address Rbase → tile Td |
| TILE.LD | Td, [Rbase], Rs | Strided load: rows at stride Rs → tile Td |
| TILE.ST | [Rbase], Ts | Contiguous store: tile Ts → 4 KB at address Rbase |
| TILE.ST | [Rbase], Ts, Rs | Strided store: tile Ts → rows at stride Rs |
| TILE.GATHER | Td, [Rbase], Tidx | Gather: indexed load using index tile (element offsets in Tidx) |
| TILE.SCATTER | [Rbase], Ts, Tidx | Scatter: indexed store using index tile (element offsets in Tidx) |
| TILE.ZERO | Td | Zero tile register Td |
| TILE.COPY | Td, Ts | Copy tile Ts → Td (allocates new physical tile, copies data) |

#### 2.4.2 Tile Manipulation Instructions

| Instruction | Operands | Function |
|-------------|----------|----------|
| TILE.MOVE | Td, Ts | Move tile Ts → Td (rename-only, zero-copy; see move elimination below) |
| TILE.TRANSPOSE | Td, Ts, fmt | Transpose tile Ts with element format fmt → tile Td |

**TILE.MOVE Td, Ts** — Logically copies tile Ts to Td, but is implemented as **move elimination** at the rename stage: the Tile RAT entry for Td is simply updated to point to the same physical tile as Ts. No data is copied, no physical tile is allocated from the free list, and no execute stage is needed. The instruction completes in **zero cycles** (handled entirely at D2 rename).

```
  Rename (D2) for TILE.MOVE Td, Ts:
    1. Read Tile RAT[Ts] → PT_src (current physical tile for Ts)
    2. Read Tile RAT[Td] → PT_old (old physical tile for Td, becomes orphan)
    3. Write Tile RAT[Td] ← PT_src  (Td now aliases same physical tile as Ts)
    4. Increment refcount(PT_src)     (one more architectural name maps to it)
    5. Mark PT_old as orphan; if refcount(PT_old)==0 → free to tile free list
    6. No RS entry allocated; no execute stage; instruction retires at D2
    7. Ready bit for Td inherits ready state of PT_src
```

After TILE.MOVE, Td and Ts share the same physical tile. This is safe under rename: the next instruction that writes to either Td or Ts will allocate a fresh physical tile at that point, naturally "splitting" the alias. TILE.MOVE is critical for avoiding unnecessary 4 KB copies in tile register spill/fill sequences and data routing between pipeline stages.

**TILE.TRANSPOSE Td, Ts, fmt** — Reads tile Ts, transposes the 2D element matrix according to the element format `fmt`, and writes the result to tile Td. The transpose treats the 4 KB tile as a 2D matrix with dimensions determined by the element width:

| fmt (funct3) | Element width | Tile layout (rows × cols) | Transpose block |
|-------------|--------------|---------------------------|-----------------|
| 000 (FP64) | 8 B | 64 × 8 | 8 × 8 (8 blocks of 8 rows) |
| 001 (FP32) | 4 B | 64 × 16 | 16 × 16 (4 blocks of 16 rows) |
| 010 (FP16) | 2 B | 64 × 32 | 32 × 32 (2 blocks of 32 rows) |
| 011 (BF16) | 2 B | 64 × 32 | 32 × 32 (2 blocks of 32 rows) |
| 100 (FP8) | 1 B | 64 × 64 | 64 × 64 (1 block, full tile) |
| 101 (INT32) | 4 B | 64 × 16 | 16 × 16 (4 blocks of 16 rows) |
| 110 (INT16) | 2 B | 64 × 32 | 32 × 32 (2 blocks of 32 rows) |
| 111 (INT8) | 1 B | 64 × 64 | 64 × 64 (1 block, full tile) |

The transpose operates on **square sub-blocks** whose dimension equals the number of elements per 512-bit row. For FP8/INT8 the entire tile is one 64×64 block and transposes in-place. For FP16/BF16/INT16, the 64 rows are split into two 32-row halves, each transposed as a 32×32 block. The MTE unit contains a dedicated **transpose buffer** (4 KB SRAM) that accumulates rows during the read epoch and emits transposed rows during the write epoch.

```
  TILE.TRANSPOSE encoding (32-bit):
  ┌──────────┬──────┬──────┬──────┬──────┬────────┐
  │  funct7  │ 00000│  Ts  │ fmt  │  Td  │ opcode │
  │ 0100010  │ (5b) │ (5b) │(3b)  │ (5b) │ 10xxxxx│
  └──────────┴──────┴──────┴──────┴──────┴────────┘
```

#### 2.4.3 Scalar ↔ Tile Element Access Instructions

| Instruction | Operands | Function |
|-------------|----------|----------|
| TILE.GET | Rd, Ts, Ridx | Read single element: element at index Ridx in tile Ts → scalar GPR Rd |
| TILE.PUT | Td, Rs, Ridx | Write single element: scalar GPR Rs → element at index Ridx in tile Td |

**TILE.GET Rd, Ts, Ridx** — Reads one element from tile Ts at the position specified by scalar register Ridx. The element is zero-extended to 64 bits and written to scalar destination GPR Rd. The element data type (FP16, FP32, FP64, INT8, etc.) is encoded in the instruction's `funct3` field, which determines element width and the extraction offset within the 512-bit row. Ridx encodes a linear element index: `row = Ridx / elements_per_row`, `col = Ridx % elements_per_row`.

**TILE.PUT Td, Rs, Ridx** — Writes the lower bits of scalar GPR Rs into tile Td at the element position specified by Ridx. This is a **read-modify-write** operation on the tile: the rename stage treats Td as both source (old mapping, read) and destination (new physical tile, write). The MTE unit copies the source physical tile to the destination physical tile, then overwrites the single element. The element data type is encoded in `funct3`.

```
  TILE.GET encoding (32-bit):
  ┌──────────┬──────┬──────┬──────┬──────┬────────┐
  │  funct7  │ Ridx │  Ts  │funct3│  Rd  │ opcode │
  │ 0100000  │ (5b) │ (5b) │ type │ (5b) │ 10xxxxx│
  └──────────┴──────┴──────┴──────┴──────┴────────┘
       Ts: architectural tile register (T0–T31)
       Ridx: scalar GPR holding element index
       Rd: scalar GPR destination
       funct3: element type (000=FP64, 001=FP32, 010=FP16, 011=BF16, 100=FP8, 101=INT32, 110=INT16, 111=INT8)

  TILE.PUT encoding (32-bit):
  ┌──────────┬──────┬──────┬──────┬──────┬────────┐
  │  funct7  │  Rs  │ Ridx │funct3│  Td  │ opcode │
  │ 0100001  │ (5b) │ (5b) │ type │ (5b) │ 10xxxxx│
  └──────────┴──────┴──────┴──────┴──────┴────────┘
       Td: architectural tile register (T0–T31) — read-modify-write
       Rs: scalar GPR holding element value
       Ridx: scalar GPR holding element index
       funct3: element type
```

Every MTE instruction flows through both the **Scalar RAT** (for address/data operands) and the **Tile RAT** (for tile operands) at the D2 rename stage:

| Instruction | Scalar RAT | Tile RAT source(s) | Tile RAT destination | Result bus |
|-------------|-----------|---------------------|----------------------|------------|
| TILE.LD Td, [Rbase] | Rbase → P-reg lookup | — | Td → allocate new PT | TCB |
| TILE.LD Td, [Rbase], Rs | Rbase, Rs → P-reg lookups | — | Td → allocate new PT | TCB |
| TILE.ST [Rbase], Ts | Rbase → P-reg lookup | Ts → PT lookup | — | — |
| TILE.ST [Rbase], Ts, Rs | Rbase, Rs → P-reg lookups | Ts → PT lookup | — | — |
| TILE.GATHER Td, [Rbase], Tidx | Rbase → P-reg lookup | Tidx → PT lookup | Td → allocate new PT | TCB |
| TILE.SCATTER [Rbase], Ts, Tidx | Rbase → P-reg lookup | Ts, Tidx → PT lookups | — | — |
| TILE.ZERO Td | — | — | Td → allocate new PT | TCB |
| TILE.COPY Td, Ts | — | Ts → PT lookup | Td → allocate new PT | TCB |
| **TILE.MOVE Td, Ts** | — | Ts → PT lookup | **Td → alias PT(Ts)** (no alloc) | **— (rename-only)** |
| **TILE.TRANSPOSE Td, Ts, fmt** | — | Ts → PT lookup | Td → allocate new PT | TCB |
| **TILE.GET Rd, Ts, Ridx** | Ridx → P-reg lookup; **Rd → allocate new P-reg** | Ts → PT lookup | — | **CDB** (scalar) |
| **TILE.PUT Td, Rs, Ridx** | Rs, Ridx → P-reg lookups | **Td → PT lookup (old)** | **Td → allocate new PT** | TCB |

Key observations:
- **TILE.MOVE** is handled entirely at D2 rename (**move elimination**): Tile RAT[Td] is pointed to the same physical tile as Ts. No free-list allocation, no RS entry, no execute stage, no result bus. Zero-cycle latency.
- **TILE.TRANSPOSE** allocates a new physical tile and requires a full read-then-transpose-then-write pass through the MTE's transpose buffer.
- **TILE.GET** produces a **scalar GPR result** (broadcast on CDB), while consuming a tile source. It requires both a Tile RAT source lookup and a Scalar RAT destination allocation.
- **TILE.PUT** is a **read-modify-write** on the tile: the rename stage looks up the old physical tile mapping as a source AND allocates a new physical tile as a destination. The MTE unit copies the old tile contents to the new tile, then overwrites the single element.

After rename, MTE RS entries carry physical scalar register tags (from Scalar RAT) and physical tile tags (from Tile RAT). The MTE unit maintains a large outstanding request buffer to maximize memory-level parallelism.

### 2.5 Instruction Domain Identification

The 7-bit opcode field encodes the instruction domain:

| Opcode[6:5] | Domain | Decode path |
|-------------|--------|-------------|
| 00, 01 | Scalar | Scalar rename → Scalar RS |
| 10 | Vector / MTE | Tile RAT rename → Vector RS or MTE RS |
| 11 | Cube | Tile RAT rename → Cube RS |

---

## 3. Top-Level Block Diagram

```
 ┌──────────────────────────────────────────────────────────────────────────────────────┐
 │  DAVINCI CORE                                                                        │
 │                                                                                      │
 │  ┌─────────────────────────────── FRONT-END ──────────────────────────────────────┐  │
 │  │                                                                                │  │
 │  │   ┌──────────┐    ┌───────────┐    ┌──────────────────────────────────┐        │  │
 │  │   │  Branch   │───▶│  Fetch    │───▶│  Instruction Buffer (16 entries) │        │  │
 │  │   │ Predictor │    │  Unit     │    │  4-wide dequeue                  │        │  │
 │  │   │ TAGE+BTB  │    │ (L1-I)   │    └──────────┬───────────────────────┘        │  │
 │  │   │ +RAS      │    └──────────┘               │ 4 instr/cy                     │  │
 │  │   └──────────┘                                ▼                                │  │
 │  │                              ┌─────────────────────────────────┐               │  │
 │  │                              │  Decode + Rename (4-wide)       │               │  │
 │  │                              │  ┌───────┐  ┌──────────────┐  │               │  │
 │  │                              │  │Scalar │  │  Tile RAT     │  │               │  │
 │  │                              │  │ RAT   │  │  (32→256)     │  │               │  │
 │  │                              │  └───┬───┘  └──────┬───────┘  │               │  │
 │  │                              │  ┌───┴─────────────┴────────┐ │               │  │
 │  │                              │  │ Free Lists + Ref Counters  │ │               │  │
 │  │                              │  └───────────────────────────┘ │               │  │
 │  │                              │  ┌───────────────────────────┐ │               │  │
 │  │                              │  │ Checkpoint Store (8 slots) │ │               │  │
 │  │                              │  └───────────────────────────┘ │               │  │
 │  │                              └──────────────┬──────────────────┘               │  │
 │  └─────────────────────────────────────────────┼──────────────────────────────────┘  │
 │                                                │ renamed µops                        │
 │  ┌─────────────────────────── DISPATCH ────────┼──────────────────────────────────┐  │
 │  │                                             ▼                                  │  │
 │  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐        │  │
 │  │  │Scalar RS │  │  LSU RS  │  │Vector RS │  │ Cube RS  │  │  MTE RS  │        │  │
 │  │  │(32 entry)│  │(24 entry)│  │(16 entry)│  │(4 entry) │  │(16 entry)│        │  │
 │  │  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘        │  │
 │  └───────┼──────────────┼─────────────┼─────────────┼──────────────┼──────────────┘  │
 │          │              │             │             │              │                  │
 │  ┌───────┼──────────── EXECUTE ───────┼─────────────┼──────────────┼──────────────┐  │
 │  │       ▼              ▼             ▼             ▼              ▼              │  │
 │  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐        │  │
 │  │  │ 4× ALU   │  │  Load /  │  │  Vector  │  │outerCube │  │   MTE    │        │  │
 │  │  │ 1× MUL   │  │  Store   │  │  Unit    │  │   MXU    │  │  Engine  │        │  │
 │  │  │ 1× BRU   │  │  Unit    │  │(512b DP) │  │(4096 MAC)│  │(LD/ST/  │        │  │
 │  │  └─────┬────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘  │G/S)     │        │  │
 │  │        │             │             │             │         └────┬────┘        │  │
 │  └────────┼─────────────┼─────────────┼─────────────┼──────────────┼─────────────┘  │
 │           │             │             │             │              │                  │
 │  ┌────────┼──────────── COMPLETE (CDB) ─────────────┼──────────────┼──────────────┐  │
 │  │        ▼             ▼             ▼             ▼              ▼              │  │
 │  │  ┌──────────────────────────────────────────────────────────────────────┐      │  │
 │  │  │  Common Data Bus (CDB) — 6 write-back ports                         │      │  │
 │  │  │  broadcast tag + data → wakeup RS entries + write physical RF        │      │  │
 │  │  └──────────────────────────────────────────────────────────────────────┘      │  │
 │  └───────────────────────────────────────────────────────────────────────────────┘  │
 │                                                                                      │
 │  ┌──────────────── REGISTER FILES ───────────────────────────────────────────────┐  │
 │  │                                                                                │  │
 │  │  ┌──────────────────┐  ┌──────────────────────────────────────────────────┐   │  │
 │  │  │ Scalar Physical   │  │ TRegFile-4K (unified tile/vector data RF)       │   │  │
 │  │  │ Register File     │  │ 256×4KB = 1MB                                   │   │  │
 │  │  │ 128×64b           │  │ 8R+8W, 512B/cy/port, 8-cycle epoch calendar     │   │  │
 │  │  │ 12R+6W ports      │  │ used by: Vector + Cube + MTE                    │   │  │
 │  │  └──────────────────┘  └──────────────────────────────────────────────────┘   │  │
 │  └────────────────────────────────────────────────────────────────────────────────┘  │
 │                                                                                      │
 │  ┌──────────────── MEMORY SUBSYSTEM ─────────────────────────────────────────────┐  │
 │  │                                                                                │  │
 │  │  ┌──────────┐  ┌──────────┐  ┌───────────────────────┐                        │  │
 │  │  │  L1-I    │  │  L1-D    │  │  L2 Cache (512 KB)    │───▶  External Bus /   │  │
 │  │  │  64 KB   │  │  64 KB   │  │  8-way, 64B line      │      NoC Interface    │  │
 │  │  │  4-way   │  │  4-way   │  └───────────────────────┘                        │  │
 │  │  └──────────┘  └──────────┘                                                    │  │
 │  │                      ▲                                                          │  │
 │  │                      │ scalar LD/ST + MTE tile LD/ST                            │  │
 │  └──────────────────────┼──────────────────────────────────────────────────────────┘  │
 └──────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Pipeline Overview

The Davinci core uses a **12-stage** scalar pipeline. There is **no retire/commit stage** because the design does not require precise architectural state.

### 4.1 Pipeline Stages

```
 F1 → F2 → D1 → D2 → DS → IS → EX1 → EX2 → EX3 → EX4 → WB → (no retire)
 ├── Fetch ──┤├─ Decode/Rename ─┤├DS┤├IS┤├──── Execute ────────┤├WB┤
```

| Stage | Name | Function |
|-------|------|----------|
| F1 | Fetch-1 | Send PC to L1-I cache and branch predictor |
| F2 | Fetch-2 | Receive 4 instructions from I-cache; apply BTB/TAGE prediction |
| D1 | Decode | Decode 4 instructions; identify domain (scalar/vector/cube/MTE) |
| D2 | Rename | Read Scalar RAT + Tile RAT; allocate physical registers/tiles; intra-group bypass; checkpoint both RATs on branch |
| DS | Dispatch | Allocate reservation station entry; write operand tags/data |
| IS | Issue | Select oldest ready instruction per functional unit; read physical RF |
| EX1–EXn | Execute | Variable latency: ALU=1cy, MUL=4cy, LD=4cy(L1 hit), VEC=16cy(2 epochs), MTE=8–72cy(epoch+mem), DIV=12–20cy |
| WB | Writeback | Broadcast result on CDB; write to physical RF; wakeup dependent RS entries |

### 4.2 Pipeline Timing (Scalar ALU Instruction)

```
  Cycle:  0    1    2    3    4    5    6    7
  ─────  ────  ────  ────  ────  ────  ────  ────
  i0:    F1   F2   D1   D2   DS   IS   EX1  WB
  i1:    F1   F2   D1   D2   DS   IS   EX1  WB
  i2:    F1   F2   D1   D2   DS   IS   EX1  WB
  i3:    F1   F2   D1   D2   DS   IS   EX1  WB
         └──── 4-wide ────────┘
```

### 4.3 Execution Latencies by Domain

| Domain | Operation | Stages | Latency (cycles) | Pipelined |
|--------|-----------|--------|-------------------|-----------|
| Scalar | ALU (add, logic, shift) | EX1 | **1** | yes |
| Scalar | MUL | EX1–EX4 | **4** | yes |
| Scalar | DIV | EX1–EX(12–20) | **12–20** | no |
| Scalar | Branch resolve | EX1 | **1** | yes |
| LSU | Load (L1 hit) | EX1–EX4 | **4** | yes |
| LSU | Load (L2 hit) | EX1–EX(12) | **12** | yes |
| LSU | Store | EX1–EX4 | **4** (addr+data) | yes |
| Vector | VADD/VMUL/VFMA (full tile) | 2 epochs (16 cy) | **16** (8 read + 8 write, compute hidden) | epoch-pipelined |
| Vector | VROWSUM/VROWMAX/VCOLSUM/… (reduce) | 1 epoch + reduce | **16** (8 read + reduce + 8 write) | no |
| Cube | CUBE.OPA (N steps) | 19 + N | **N + 18** (first tile) | epoch-pipelined |
| MTE | TILE.LD (contiguous, L2 hit) | mem + 1 write epoch | **72** (64 mem + 8 TRegFile write) | yes (across ports) |
| MTE | TILE.ST (contiguous, L2) | 1 read epoch + mem | **72** (8 TRegFile read + 64 mem write) | yes (across ports) |
| MTE | TILE.COPY | 2 epochs | **16** (8 read + 8 write) | epoch-pipelined |
| MTE | TILE.ZERO | 1 write epoch | **8** (write zeros, no read) | yes |
| MTE | TILE.GATHER (L2 hit) | mem + 1 write epoch | **72–128** (variable mem + 8 TRegFile write) | partially |
| MTE | TILE.SCATTER (L2) | 1 read epoch + mem | **72–128** (8 TRegFile read + variable mem) | partially |
| MTE | TILE.MOVE (rename-only) | — (D2) | **0** (move elimination, no execute) | — |
| MTE | TILE.TRANSPOSE | 2 epochs | **16** (8 read + 8 write via transpose buffer) | no |
| MTE | TILE.GET (element → GPR) | 1 read epoch + extract | **9** (8 TRegFile read epoch + 1 extract) | no (port occupied 8 cy) |
| MTE | TILE.PUT (GPR → element, RMW) | 2 epochs | **16** (8 read + 8 write), **8** with copy elision | no |

### 4.4 Branch Misprediction Penalty

```
  Branch enters pipeline at F1 → resolved at IS/EX1 (stage 6).
  Misprediction penalty = 6 cycles (flush F1–IS, restore RAT checkpoint).

  Recovery sequence:
    Cycle c:     Branch resolves as mispredicted at EX1
    Cycle c+1:   Flash-restore RAT checkpoint; flush all younger instructions
    Cycle c+2:   Redirect fetch to correct target
    Cycle c+3:   First correct instruction enters F1
    ...
    Cycle c+8:   First correct instruction reaches EX1
```

---

## 5. Front-End: Fetch & Branch Prediction

### 5.1 Fetch Unit

The fetch unit delivers up to **4 aligned instructions per cycle** from the L1 instruction cache.

| Parameter | Value |
|-----------|-------|
| Fetch width | **4** instructions / cycle (16 bytes) |
| Fetch alignment | 16-byte aligned fetch block |
| Instruction buffer | **16** entries (4-cycle decoupling) |
| L1-I cache | **64 KB**, 4-way set-associative, 64 B line |
| L1-I latency | **2** cycles (F1 + F2) |
| I-TLB | 64 entries, fully associative |

**Fetch pipeline:**

```
  F1: PC → I-TLB + L1-I tag lookup + BTB lookup + TAGE index
  F2: L1-I data return (4 instructions) + TAGE prediction + RAS check
      → push into instruction buffer (up to 16 entries)
      → if predicted-taken: redirect PC at end of F2
```

### 5.2 Branch Predictor

The branch predictor uses a **hybrid scheme** combining three components.

#### 5.2.1 TAGE Predictor (Conditional Branches)

| Parameter | Value |
|-----------|-------|
| Base predictor | 4K-entry bimodal (2-bit saturating counters) |
| Tagged tables | 5 tables: T1(512), T2(512), T3(1K), T4(1K), T5(1K) |
| History lengths | 4, 8, 16, 32, 64 (geometric series) |
| Tag width | 8–12 bits per entry |
| Total storage | ~20 KB |
| Prediction accuracy | ~95% (typical workloads) |

#### 5.2.2 Branch Target Buffer (BTB)

| Parameter | Value |
|-----------|-------|
| Entries | **2048** |
| Associativity | 4-way set-associative |
| Tag | partial PC (upper bits) |
| Target | full 64-bit target address |
| Hit latency | 1 cycle (available end of F1) |

#### 5.2.3 Return Address Stack (RAS)

| Parameter | Value |
|-----------|-------|
| Depth | **16** entries |
| Push | on JAL/JALR to link register |
| Pop | on JALR from link register (return pattern) |
| Speculative management | checkpoint RAS top-of-stack pointer with RAT checkpoints |

### 5.3 Fetch Redirect Priorities

```
  Priority (highest to lowest):
    1. Branch mispredict redirect (from EX1)  — flush + restart
    2. BTB/TAGE taken-branch redirect (from F2) — next-cycle redirect
    3. Sequential PC+16 (default)
```

---

## 6. Decode & Rename

### 6.1 Decode Stage (D1)

The decode stage processes **4 instructions per cycle**, identifying each instruction's domain, opcode, source/destination registers, and immediate values.

| Function | Detail |
|----------|--------|
| Decode width | **4** instructions / cycle |
| Domain classification | Opcode[6:5] → scalar, vector, cube, MTE |
| Immediate extraction | Sign-extend and format-dependent extraction |
| Branch detection | Identify branch instructions for checkpoint allocation |

Instructions that cannot be expressed as a single micro-op (e.g., certain complex addressing modes) are **cracked** into 2 micro-ops at D1, consuming 2 dispatch slots.

### 6.2 Rename Stage (D2)

The rename stage performs **register renaming** for both scalar registers and tile registers using two independent Register Alias Tables (RATs). The Scalar RAT maps 32 architectural GPRs to 128 physical GPRs. The Tile RAT maps 32 architectural tile registers to 256 physical tile slots in TRegFile-4K.

#### 6.2.1 Scalar RAT

| Parameter | Value |
|-----------|-------|
| Architectural registers | 32 (X0–X31) |
| Physical registers | **128** (P0–P127) |
| RAT storage | 32 entries × 7 bits = **224 bits** |
| Read ports | **8** (2 sources × 4 decode slots) |
| Write ports | **4** (1 destination × 4 decode slots) |

Each RAT entry contains:
- Physical register index (7 bits)
- Ready bit (1 bit): set when result has been written to the physical RF

#### 6.2.2 Tile RAT (Vector, Cube, MTE Operands)

All three tile-consuming domains (vector, cube, MTE) share a single **Tile RAT** that renames 32 architectural tile registers (T0–T31) to 256 physical tile slots (PT0–PT255) in TRegFile-4K. This eliminates WAW and WAR hazards on tile operands through renaming, exactly as the scalar RAT does for GPRs.

| Parameter | Value |
|-----------|-------|
| Architectural tile registers | 32 (T0–T31) |
| Physical tile registers | **256** (PT0–PT255), 4 KB each in TRegFile-4K |
| Tile RAT storage | 32 entries × 8 bits = **256 bits** |
| Read ports | **8** (up to 3 source tiles × 4 decode slots, shared/muxed) |
| Write ports | **4** (1 destination tile × 4 decode slots) |

Each Tile RAT entry contains:
- Physical tile index (8 bits)
- Ready bit (1 bit): set when the producing operation has finished writing the physical tile

Tile RAT operation mirrors scalar RAT operation: at D2, destination tile operands are allocated a fresh physical tile from the tile free list, the old physical tile mapping is marked orphan, and source tile operands are looked up to obtain the current physical tile index and ready bit. Tile instructions dispatched to Vector RS, Cube RS, and MTE RS carry **physical tile tags** (8 bits each) rather than architectural indices.

#### 6.2.3 Intra-Group Bypass Logic

When 4 instructions are renamed simultaneously, later instructions in the group may depend on earlier ones. Hardware **priority-encoded comparators** detect these intra-group dependencies for both scalar and tile RATs:

```
  Scalar example:
    Rename slot 0:  X5 → P40  (destination)
    Rename slot 1:  reads X5  → comparator detects match → bypass P40
    Rename slot 2:  X5 → P41  (re-definition)
    Rename slot 3:  reads X5  → comparator detects slot 2 match → bypass P41

    4 slots × 2 sources × 3 older slots = 24 comparators (7-bit each)
    + 8 bypass MUXes (select forwarded phys-reg vs scalar RAT read)

  Tile example:
    Rename slot 0:  TILE.LD T10  → PT200  (destination)
    Rename slot 1:  VADD dst=T10 → PT201  (re-definition)
    Rename slot 2:  reads T10    → comparator detects slot 1 match → bypass PT201

    4 slots × 3 tile sources × 3 older slots = 36 comparators (8-bit each)
    + 12 bypass MUXes (select forwarded phys-tile vs Tile RAT read)
```

#### 6.2.4 Free Lists

| Parameter | Value |
|-----------|-------|
| **Scalar free list** | FIFO, 96 entries (128 physical − 32 architectural) |
| Scalar dequeue rate | up to 4 per cycle |
| Scalar enqueue rate | up to 4 per cycle (from ref-count freeing) |
| **Tile free list** | FIFO, 224 entries (256 physical − 32 architectural) |
| Tile dequeue rate | up to 4 per cycle |
| Tile enqueue rate | up to 4 per cycle (from tile ref-count freeing) |

At reset, the scalar free list is initialized with P32–P127 (first 32 pre-assigned to X0–X31). The tile free list is initialized with PT32–PT255 (first 32 pre-assigned to T0–T31).

**Stall condition:** If either free list cannot supply enough physical registers for the current decode group, the rename stage stalls the pipeline.

#### 6.2.5 Checkpoint Storage (Branch Recovery)

| Parameter | Value |
|-----------|-------|
| Checkpoint slots | **8** (supports 8 in-flight unresolved branches) |
| Checkpoint size | Scalar RAT (224b) + Tile RAT (256b) + scalar free-list head (7b) + tile free-list head (8b) + RAS pointer (4b) = **~499 bits** |
| Flash-copy latency | **1 cycle** (parallel bit-copy from both active RATs) |
| Flash-restore latency | **1 cycle** (parallel bit-copy to both active RATs) |

Both the Scalar RAT and Tile RAT are checkpointed. On branch misprediction, both RATs are restored in parallel, along with both free-list head pointers.

**Checkpoint lifecycle:**

```
  Branch decoded at D2:
    1. Allocate checkpoint slot (round-robin)
    2. Flash-copy: active Scalar RAT + active Tile RAT → checkpoint[i]
    3. Save scalar free-list head, tile free-list head, and RAS top pointer
    4. Tag the branch's RS entry with checkpoint ID

  Branch resolved correctly at EX1:
    1. Deallocate checkpoint slot → available for reuse

  Branch mispredicted at EX1:
    1. Flash-restore: checkpoint[i] → active Scalar RAT + active Tile RAT
    2. Restore scalar free-list head pointer (reclaim speculatively allocated GPRs)
    3. Restore tile free-list head pointer (reclaim speculatively allocated tiles)
    4. Restore RAS pointer
    5. Flush all pipeline stages after D2
    6. Redirect fetch to correct target
```

**Stall condition:** If all 8 checkpoint slots are occupied, the rename stage stalls on the next branch instruction until an older branch resolves.

---

## 7. Dispatch & Issue

### 7.1 Dispatch (DS Stage)

After rename, each micro-op is dispatched to the appropriate **reservation station** based on its domain and operation type. Dispatch is **in-order** (preserving program order for dependency tracking), but issue from reservation stations is **out-of-order**.

| Reservation Station | Serves | Entries | Issue width |
|---------------------|--------|---------|-------------|
| **Scalar RS** | 4× ALU, 1× MUL/DIV, 1× BRU | **32** | 6 (4 ALU + 1 MUL + 1 BRU) |
| **LSU RS** | Load unit, Store unit | **24** | 2 (1 load + 1 store) |
| **Vector RS** | Vector ALU/FMA | **16** | 1 |
| **Cube RS** | CUBE.OPA, CUBE.CFG, CUBE.DRAIN, CUBE.ZERO, CUBE.WAIT | **4** | 1 |
| **MTE RS** | TILE.LD, TILE.ST, TILE.GATHER, TILE.SCATTER, TILE.GET/PUT, TILE.COPY, TILE.TRANSPOSE | **16** | 2 |

#### 7.1.1 Reservation Station Sizing Rationale

The number of entries in each RS is chosen to satisfy: **(1)** absorb the execution latency of its functional units so that new instructions are not stalled waiting for RS slots, **(2)** provide enough window for out-of-order issue to find independent instructions, and **(3)** stay within area and wakeup-logic power budgets. The core principle: RS entries ≈ **dispatch rate × average occupancy time**, with headroom for dependent chains and dispatch bursts.

**Scalar RS — 32 entries:**
- The front-end dispatches up to **4 instructions/cycle**. In typical AI/HPC kernels, ~60–70% of instructions are scalar (address computation, loop control, branch), giving ~2.5–3 scalar dispatches/cycle.
- ALU latency is **1 cycle** (result available next cycle), so independent ALU chains drain quickly. However, **MUL (4 cy)** and **DIV (12–20 cy)** are multi-cycle and block their pipeline slot while in-flight. A single DIV can occupy an issue port for up to 20 cycles.
- With 6 issue ports, up to 6 instructions leave the RS per cycle, but dependent chains create bubbles. The RS needs enough depth to look past these stalls and find independent operations.
- Sizing: ~3 dispatch/cy × ~8 cy average occupancy (mix of 1-cy ALU and 4-cy MUL, with occasional 20-cy DIV) ≈ 24 entries minimum. Rounded up to **32** to tolerate bursty dispatch and long DIV chains.
- Wakeup cost: 32 entries × 2 sources × 6 CDB ports = **384 tag comparators** (7-bit each) — acceptable at 5 nm.

**LSU RS — 24 entries:**
- Load/store instructions make up ~20–30% of a typical mix, so ~0.8–1.2 dispatches/cycle.
- **Load latency is 4 cycles** (L1 hit) but **12+ cycles** on L1 miss (L2 hit), and hundreds of cycles on DRAM access. The LSU RS must buffer many outstanding loads to exploit **memory-level parallelism (MLP)**.
- The L1-D cache supports **8 MSHRs** (miss-status holding registers) — up to 8 cache misses can be in flight simultaneously, each occupying an RS slot for 12+ cycles.
- Sizing: 8 MSHR-bound loads (occupying slots for ~12 cy each) + steady-state L1-hit loads and stores ≈ **24 entries**. This keeps the memory subsystem saturated with overlapping misses while allowing hit-path traffic to proceed without stalling dispatch.
- The 2-port issue (1 load + 1 store/cycle) prevents store traffic from blocking load throughput.

**Vector RS — 16 entries:**
- Vector instructions have **high latency (16 cycles)** with epoch-pipelined throughput of **1 tile per 8 cycles**. They are less frequent than scalar ops but arrive in bursts during vector-heavy code regions.
- At 1 issue per 8 cycles, the RS drains slowly. But the 4-wide front-end can dispatch vector instructions at up to 4/cycle during vector-heavy phases.
- The RS must: (a) absorb front-end bursts (16 entries ÷ 4/cy = 4 cycles of burst buffering), (b) provide enough look-ahead window to find independent vector ops past dependent chains (16-instruction window).
- The 95-instruction vector ISA includes diverse categories (elementwise, reduce, broadcast-expand, permute), and a typical softmax or layer-norm kernel interleaves 5–10 dependent vector instructions — the 16-entry window can look past 1–2 such chains.
- Area-efficient: tile-domain RS entries are only ~80 bits each (no 64-bit data capture), so 16 entries ≈ **160 bytes**.

**Cube RS — 4 entries:**
- Cube instructions are **very long-latency** (CUBE.OPA: N+18 cycles, typically 26–82 cy) but **extremely infrequent** — a single CUBE.OPA encodes an entire K-loop of outer-product-accumulate steps spanning thousands of MAC operations.
- A typical GEMM kernel issues 1 CUBE.OPA per ~100+ scalar/MTE instructions. Software double-buffers tile loads around cube execution, so the cube RS is rarely the dispatch bottleneck.
- The RS only needs to hold: the currently-executing CUBE.OPA, the next queued CUBE.OPA (overlapping with tile loads for the next iteration), plus associated CUBE.CFG/CUBE.DRAIN/CUBE.WAIT control instructions.
- Sizing: **4 entries** suffices because the instruction stream rarely has >2–3 cube instructions queued. Additional entries would waste area (including 8-bit tile tag comparators) with no throughput benefit since the cube pipeline executes 1 instruction at a time.

**MTE RS — 16 entries:**
- MTE instructions span a wide latency range: **TILE.LD: 72 cy** (L2 hit, potentially hundreds from DRAM), **TILE.ST: 72 cy**, **TILE.COPY/TRANSPOSE: 16 cy**, **TILE.ZERO: 8 cy**, **TILE.GET: 9 cy**.
- The primary design driver is **memory-level parallelism for tile loads**: the programmer (or compiler) schedules many TILE.LD instructions ahead of the CUBE.OPA that consumes the loaded tiles. With up to **7 available write ports** and a **32-entry outstanding request buffer**, the MTE can service many concurrent tile loads.
- At 2 issues/cycle, the MTE RS can launch 2 tile operations per cycle (e.g., 1 TILE.LD + 1 TILE.ST on separate ports).
- Sizing: 7 concurrent TILE.LDs (one per write port) + several TILE.STs and local tile ops (GET/PUT/COPY/TRANSPOSE) + headroom for dispatch bursts ≈ **16 entries**. This provides enough scheduling window to overlap tile loads with stores and local operations, maximizing TRegFile-4K port utilization.

**Summary — entries vs. area:**

| RS | Entries | Entry width | Storage | Comparators | Dominant sizing factor |
|----|---------|------------|---------|-------------|----------------------|
| Scalar | 32 | ~170 bits | ~680 B | 384 (7b × 6 CDB) | Multi-cycle MUL/DIV latency + DIV blocking |
| LSU | 24 | ~170 bits | ~510 B | 288 (7b × 6 CDB) | L1 miss latency (MLP) + 8 MSHRs |
| Vector | 16 | ~80 bits | ~160 B | 64 (8b × 4 TCB) | 16-cy epoch latency + dispatch bursts |
| Cube | 4 | ~80 bits | ~40 B | 16 (8b × 4 TCB) | Infrequent instructions, 1 in-flight |
| MTE | 16 | ~80 bits | ~160 B | 64 (8b × 4 TCB) + 96 (7b × 6 CDB) | TILE.LD MLP + 7 write ports |
| **Total** | **92** | — | **~1550 B** | **~912** | |

### 7.2 Reservation Station Entry Format

Each RS entry stores all information needed to issue an instruction once operands are ready:

**Scalar / LSU RS entry:**
```
 ┌─────────┬──────┬──────┬──────┬──────┬──────┬──────┬──────┬──────┬──────┬──────┐
 │ valid   │ age  │ op   │ psrc1│ rdy1 │data1 │ psrc2│ rdy2 │data2 │ pdst │ ckpt │
 │ (1b)    │(6b)  │(8b)  │(7b)  │(1b)  │(64b) │(7b)  │(1b)  │(64b) │(7b)  │(3b)  │
 └─────────┴──────┴──────┴──────┴──────┴──────┴──────┴──────┴──────┴──────┴──────┘
   ~170 bits per scalar entry
```

**Tile-domain RS entry (Vector / Cube / MTE):**
```
 ┌──────┬──────┬──────┬───────┬──────┬───────┬──────┬───────┬──────┬───────┬──────┬───────┬──────┐
 │valid │ age  │ op   │ptsrc1 │trdy1 │ptsrc2 │trdy2 │ptsrc3 │trdy3 │ptdst1 │ptdst2│ pscalar│ ckpt │
 │(1b)  │(6b)  │(8b)  │(8b)   │(1b)  │(8b)   │(1b)  │(8b)   │(1b)  │(8b)   │(8b)  │(7b+1b)│(3b)  │
 └──────┴──────┴──────┴───────┴──────┴───────┴──────┴───────┴──────┴───────┴──────┴───────┴──────┘
   ~80 bits per tile-domain entry (no 64-bit data capture — tiles are too large)
```

Tile-domain RS entries carry **physical tile tags** (8-bit, from Tile RAT) plus ready bits. They do not capture tile data (4 KB) in the RS; instead, issue waits until the Tile RAT ready bit is set, then reads directly from TRegFile-4K at execute time. The `pscalar` field holds a scalar physical register tag for instructions that also consume a scalar operand (e.g., TILE.LD base address).

| Field | Width | Description |
|-------|-------|-------------|
| valid | 1 | Entry is occupied |
| age | 6 | Sequence number for oldest-first selection |
| op | 8 | Micro-op code |
| psrc1, psrc2 (scalar RS) | 7 each | Physical scalar source register tags |
| ptsrc1–ptsrc3 (tile RS) | 8 each | Physical tile source tags (from Tile RAT) |
| rdy / trdy | 1 each | Source operand ready |
| data1, data2 (scalar RS only) | 64 each | Captured scalar operand data (CDB snoop) |
| pdst / ptdst | 7 or 8 | Physical destination tag (scalar 7b, tile 8b) |
| ckpt | 3 | Checkpoint ID (for branch recovery) |

### 7.3 Wakeup Logic

When an execution unit broadcasts a result on the **Common Data Bus (CDB)**, every reservation station entry compares its source tags against the CDB tag:

```
  CDB broadcast: (tag=P40, data=0x1234)

  For each RS entry:
    if (psrc1 == P40 && !rdy1):  rdy1 ← 1;  data1 ← 0x1234
    if (psrc2 == P40 && !rdy2):  rdy2 ← 1;  data2 ← 0x1234

  Hardware: N entries × 2 sources × 7-bit comparators × C CDB ports
  Scalar RS: 32 × 2 × 6 = 384 comparators (CDB has 6 write-back ports)
```

An instruction becomes **ready to issue** when `rdy1 && rdy2` (both operands available).

### 7.4 Select Logic

Each functional unit's select logic picks the **oldest ready** instruction from its reservation station every cycle:

```
  Select priority: lowest age value among entries with (valid && rdy1 && rdy2)

  Per cycle:
    Scalar RS → select up to 4 ALU + 1 MUL + 1 BRU (6 instructions)
    LSU RS    → select 1 load + 1 store
    Vector RS → select 1 vector op
    Cube RS   → select 1 cube op
    MTE RS    → select up to 2 tile ops
```

**Issue conflicts:** If multiple ready instructions target the same functional unit type and only one slot is available, the oldest wins. Younger instructions remain in the RS for the next cycle.

### 7.5 Dispatch Stall Conditions

The dispatch stage stalls the entire front-end if any of these conditions hold:

| Condition | Recovery |
|-----------|----------|
| Target RS is full | Wait for RS entry to be freed (instruction issues or is flushed) |
| Scalar free list empty | Wait for scalar physical register to be freed (ref-count → 0, orphan) |
| Tile free list empty | Wait for physical tile to be freed (tile ref-count → 0, orphan) |
| All checkpoint slots occupied | Wait for an in-flight branch to resolve |

---

## 8. Execution Units

### 8.1 Scalar Unit

The scalar unit contains **6 functional units** sharing the Scalar RS.

#### 8.1.1 ALU (×4)

Four identical single-cycle ALUs handle integer arithmetic, logic, shift, and compare operations.

| Parameter | Value |
|-----------|-------|
| Count | **4** symmetric ALUs |
| Operations | ADD, SUB, AND, OR, XOR, SLL, SRL, SRA, SLT, SLTU, LUI, AUIPC |
| Latency | **1** cycle |
| Throughput | **4** ops / cycle |
| Input width | 64-bit |

#### 8.1.2 MUL/DIV Unit (×1)

| Parameter | Value |
|-----------|-------|
| MUL latency | **4** cycles (pipelined, 1 MUL issued/cycle) |
| MUL operations | MUL, MULH, MULHU, MULHSU, MULW |
| DIV latency | **12–20** cycles (non-pipelined, blocks MUL during execution) |
| DIV operations | DIV, DIVU, REM, REMU, DIVW, DIVUW |

#### 8.1.3 Branch Unit (×1)

| Parameter | Value |
|-----------|-------|
| Latency | **1** cycle (compare + resolve) |
| Operations | BEQ, BNE, BLT, BGE, BLTU, BGEU, JAL, JALR |
| On correct prediction | Deallocate checkpoint; no pipeline impact |
| On mispredict | Flash-restore RAT; flush pipeline stages F1–IS; redirect fetch |
| Mispredict penalty | **6** cycles (front-end refill) |

### 8.2 Load/Store Unit

The LSU handles all scalar memory operations with a **simplified** design enabled by the no-exception guarantee.

#### 8.2.1 Architecture

```
  ┌──────────────────────────────────────────────────────────┐
  │  Load/Store Unit                                          │
  │                                                          │
  │  LSU RS (24 entries) ──┬──▶ Load Pipeline  (EX1–EX4)    │
  │                        └──▶ Store Pipeline (EX1–EX4)    │
  │                                                          │
  │  ┌─────────────────┐    ┌─────────────────┐             │
  │  │ Load Queue       │    │ Store Buffer     │             │
  │  │ (16 entries)     │    │ (16 entries)     │             │
  │  │ addr + tag       │    │ addr + data      │             │
  │  └────────┬────────┘    └────────┬────────┘             │
  │           │  store-to-load       │                       │
  │           │◀─ forwarding ────────┘                       │
  │           ▼                      ▼                       │
  │      ┌──────────────────────────────┐                    │
  │      │        L1-D Cache (64 KB)    │                    │
  │      │    4-way, 64B line, 8 MSHRs  │                    │
  │      └──────────────────────────────┘                    │
  └──────────────────────────────────────────────────────────┘
```

#### 8.2.2 Key Parameters

| Parameter | Value |
|-----------|-------|
| Load pipeline latency | **4** cycles (address calc + TLB + cache access + align) |
| Store pipeline latency | **4** cycles (address calc + TLB + write to store buffer) |
| Load queue entries | **16** |
| Store buffer entries | **16** |
| Store-to-load forwarding | Full forwarding when address and size match |
| L1-D MSHRs | **8** (non-blocking, 8 outstanding misses) |
| D-TLB | 64 entries, fully associative |

#### 8.2.3 Simplified Store Commit

Because the Davinci core does not support precise exceptions, **stores can commit to cache out-of-order** once their address and data are resolved. The store buffer serves only as a write-combining queue, not as a speculative buffer waiting for in-order retirement.

The store buffer still provides **store-to-load forwarding**: when a load's address matches a store buffer entry, the data is forwarded directly, avoiding a cache read. Address ambiguity (unknown store addresses) causes the load to wait until all older stores have their addresses computed.

### 8.3 Vector Unit

The vector unit executes SIMD operations on tile data stored in TRegFile-4K. There is **no separate vector register file**; vector operands are architectural tile registers (T0–T31) renamed to physical tile slots (PT0–PT255) by the Tile RAT.

| Parameter | Value |
|-----------|-------|
| Datapath width | **512 bits** (one 512-bit row per sub-cycle) |
| Operand source | **TRegFile-4K** physical tile slots (PT0–PT255, via Tile RAT rename) |
| Element types | FP64(×8), FP32(×16), FP16(×32), BF16(×32), FP8(×64), INT32(×16), INT16(×32), INT8(×64) |
| ALU/FMA pipeline depth | **4** stages per row (multiply → add → accumulate → normalize) |
| TRegFile read epoch | **8** cycles per source tile (512 B/cy, delivers 8 rows/cy to vector unit) |
| TRegFile write epoch | **8** cycles per destination tile (512 B/cy from vector unit) |
| **Full-tile instruction latency** | **16** cycles (8-cycle read epoch + 8-cycle write epoch; compute is pipelined and hidden) |
| **Throughput (epoch-pipelined)** | **1 tile per 8 cycles** — the write epoch of instruction N overlaps with the read epoch of instruction N+1 on different ports |
| Max source operands | **3** tile registers (e.g., VFMA: Ta × Tb + Tc; read on parallel ports) |
| Max destination operands | **2** tile registers |
| Reduce latency | **16** cycles (1 read epoch + tree reduce + 1 write epoch; column/row-vector result tile) |

**Vector pipeline timing (epoch-pipelined):**

```
  Instruction i0: VADD T30, T10, T20
  Instruction i1: VMUL T31, T30, T11  (depends on i0's result T30)
  Instruction i2: VSUB T0,  T31, T12  (depends on i1's result T31)

  Port usage (R = read port, W = write port):

  Epoch    │ 0 (cy 0–7)     │ 1 (cy 8–15)    │ 2 (cy 16–23)   │ 3 (cy 24–31)
  ─────────┼─────────────────┼─────────────────┼─────────────────┼────────────────
  i0       │ R: T10,T20 read │ W: T30 write    │                 │
  i1       │                 │ R: T30,T11 read │ W: T31 write    │
  i2       │                 │                 │ R: T31,T12 read │ W: T0 write

  Observations:
  - i0 write epoch (1) overlaps with i1 read epoch (1) — different ports, no conflict
  - i1 can read T30 starting at epoch 1 because i0 completes T30 at end of epoch 1
    (write-port data is available for read-port next-epoch access via TRegFile bypass)
  - Sustained throughput: 1 vector tile op every 8 cycles (1 epoch)
  - Latency per instruction: 16 cycles (2 epochs from issue to completion)
```

For **independent** instructions (no data dependency), the overlap is straightforward — read and write use separate ports. For **dependent** instructions (i1 reads i0's output), TRegFile-4K's **write-to-read epoch bypass** allows i1 to begin reading a tile in the epoch immediately after i0 finishes writing it, achieving back-to-back issue with zero bubble.

For a **3-source VFMA** (Ta × Tb + Tc), all three source tiles are read simultaneously on three read ports during the same read epoch. The write epoch remains 8 cycles, so total latency is still **16 cycles**, and epoch-pipelined throughput is still **1 per 8 cycles**.

For **row/column reductions** (VROWSUM, VROWMAX, VCOLSUM, VCOLMAX, etc.), the source tile is read over an 8-cycle read epoch; the reduction tree accumulates across columns (for row-reduce) or rows (for col-reduce) per cycle. The reduced result — a column-vector or row-vector — is written to the destination tile in an 8-cycle write epoch, giving **16 cycles** total latency.

#### 8.3.1 TRegFile-4K Port Usage

The vector unit accesses TRegFile-4K through read and write ports shared with cube and MTE. See the comprehensive **TRegFile-4K Port Allocation** table in §9.2 for the full per-port mapping across all three units.

A vector instruction occupies one read port per source physical tile for 8 cycles (one epoch) and one write port per destination physical tile for the next 8 cycles (second epoch). Because read and write use separate ports, the write epoch of instruction N overlaps with the read epoch of instruction N+1 — this is **epoch-pipelining**, giving sustained throughput of **1 tile-wide vector op per 8 cycles**.

The vector unit processes data as it streams from TRegFile-4K: each cycle, 512 B arrives per read port (8 rows of 512 bits), and the vector ALU pipeline processes these rows. Results are buffered and written back through a write port during the write epoch.

On completion (end of write epoch, cycle 15), the vector unit sets the **Tile RAT ready bit** for the destination physical tile and broadcasts on the TCB, waking up dependent instructions.

### 8.4 Cube Unit (outerCube MXU)

The cube unit is the outerCube Matrix Unit, a large-scale outer-product accumulation engine. Full specification is in `outerCube.md`.

#### 8.4.1 Summary

| Parameter | Value |
|-----------|-------|
| Base MAC units | **4096** (8 banks × 8 rows × 64 columns) |
| Modes | **Mode A** (K-parallel, 8-bank reduction) / **Mode B** (M-parallel, independent) |
| Formats | FP16, BF16, FP8 (E4M3/E5M2), MXFP4, HiFP4 |
| MAC scaling | FP16: 4096 / FP8: 8192 / MXFP4: 32768 MACs/cycle |
| Accumulator | 32-bit FP32, ping-pong (2 × 16 KB = 32 KB) |
| Pipeline | 19 stages: 8 (OF) + 1 (MUL) + 1 (RED) + 1 (ACC) + 8 (AD) |
| Staging SRAM | A double-buffer (8 KB) + B double-buffer (32 KB) = 40 KB baseline |
| Peak FP16 @ 1.5 GHz | **12.3 TFLOPS** |
| Peak FP8 @ 1.5 GHz | **24.6 TOPS** |
| Peak MXFP4 @ 1.5 GHz | **98.3 TOPS** |

#### 8.4.2 Cube Instruction Dispatch

Cube instructions (CUBE.OPA, CUBE.DRAIN, etc.) are dispatched to the **Cube RS** (4 entries) after Tile RAT rename. Each CUBE.OPA is a long-running instruction that occupies the MXU for many cycles (N + 18, where N = Nb × S OPA steps). While the MXU is busy, the Cube RS holds subsequent cube instructions until the current one completes.

A CUBE.OPA may reference a range of architectural tile registers (e.g., T[Tb]..T[Tb+Na−1]). At dispatch, the Tile RAT translates each architectural tile index to a physical tile index. For multi-tile operands, the cube RS stores a base physical tile index plus a **tile address table** (up to 16 entries) holding the physical indices of all tiles in the range; the cube pipeline controller uses these physical indices to program TRegFile-4K port addresses.

The cube unit reads tile data from TRegFile-4K ports R0 (A operand) and R1–R4 (B operand), and drains results via W0 (C output). Port interactions are managed by the cube pipeline controller, which issues epoch-aligned physical tile addresses to TRegFile-4K's pending registers (see `tregfile4k.md` §3).

### 8.5 MTE Unit (Memory Tile Engine)

The MTE unit is the **bridge between three domains**: memory ↔ TRegFile-4K (bulk tile transfers) and scalar GPR ↔ TRegFile-4K (single-element access via TILE.GET/TILE.PUT). All MTE instructions go through full **dual-RAT rename** at D2: scalar operands are renamed via the Scalar RAT, and tile operands are renamed via the Tile RAT. Instructions that produce a new tile (TILE.LD, TILE.ZERO, TILE.COPY, TILE.GATHER, TILE.PUT) allocate a fresh physical tile from the tile free list. TILE.GET produces a scalar GPR result and broadcasts on the CDB.

#### 8.5.1 Architecture

```
  ┌──────────────────────────────────────────────────────────────────┐
  │  Memory Tile Engine (MTE)                                        │
  │                                                                  │
  │  MTE RS (16 entries) ──┬──▶ Load Tile Pipeline                  │
  │                        ├──▶ Store Tile Pipeline                 │
  │                        ├──▶ Gather Pipeline                     │
  │                        ├──▶ Scatter Pipeline                    │
  │                        ├──▶ TILE.GET Pipeline (tile→GPR)        │
  │                        └──▶ TILE.PUT Pipeline (GPR→tile, RMW)   │
  │                                                                  │
  │  ┌──────────────────────────────┐                                │
  │  │ Outstanding Request Buffer   │  Tracks up to 32 in-flight    │
  │  │ (32 entries)                 │  tile transfers for MLP        │
  │  └──────────────┬───────────────┘                                │
  │                 │                                                │
  │  ┌──────────────▼───────────────┐  ┌─────────────────────────┐  │
  │  │ Address Generation Unit      │  │ Data Assembly / Scatter  │  │
  │  │ (contiguous, strided, index) │  │ (pack / unpack for G/S)  │  │
  │  └──────────────┬───────────────┘  └──────────┬──────────────┘  │
  │                 │                              │                 │
  │                 ▼                              ▼                 │
  │  ┌──────────────────────────────────────────────────┐           │
  │  │  L2 / Memory Interface (high-bandwidth path)     │           │
  │  │  64 B/cy (1 cache line/cy) sustained              │           │
  │  └──────────────────────────────────────────────────┘           │
  │                 │                              │                 │
  │                 ▼                              ▼                 │
  │  ┌──────────────────────────────────────────────────┐           │
  │  │  TRegFile-4K Write Ports (W1–W7 for TILE.LD)    │           │
  │  │  TRegFile-4K Read Ports (R5–R7 for TILE.ST)     │           │
  │  └──────────────────────────────────────────────────┘           │
  │                                                                  │
  │  ┌──────────────────────────────────────────────────┐           │
  │  │  Scalar GPR ↔ Tile Element Path                  │           │
  │  │  TILE.GET: TRegFile read port → extract → CDB    │           │
  │  │  TILE.PUT: CDB snoop → tile copy + insert → write│           │
  │  └──────────────────────────────────────────────────┘           │
  └──────────────────────────────────────────────────────────────────┘
```

#### 8.5.2 Key Parameters

| Parameter | Value |
|-----------|-------|
| TILE.LD TRegFile write | **8 cycles** per write port (512 B/cy × 8 cy = 4 KB) |
| TILE.LD total latency (L2 hit) | **72 cycles** (64 cy memory fetch + 8 cy TRegFile write epoch) |
| TILE.ST TRegFile read | **8 cycles** per read port (512 B/cy × 8 cy = 4 KB) |
| TILE.ST total latency (L2) | **72 cycles** (8 cy TRegFile read epoch + 64 cy memory write) |
| Available write ports | W1–W7 (**7** ports, minus ports used by cube drain) |
| Available read ports | R5–R7 (**3** ports, minus ports used by cube operands) |
| Max concurrent TILE.LD | up to **7** (1 per write port), limited by memory BW |
| Max concurrent TILE.ST | up to **3** (1 per read port) |
| Outstanding request buffer | **32** entries (supports deep memory-level parallelism) |
| Gather/scatter | Uses index tile (Tidx) for non-contiguous access patterns |
| L2 → MTE bandwidth | **64 B/cy** (1 cache line/cy) → 1 tile in **64 cycles** from L2 |
| TILE.COPY / TILE.TRANSPOSE latency | **16 cycles** (8 cy TRegFile read epoch + 8 cy write epoch) |
| TILE.ZERO latency | **8 cycles** (1 write epoch, no read needed) |
| **TILE.GET latency** | **9 cycles** (8 cy TRegFile read epoch + 1 cy element extract → CDB) |
| **TILE.PUT latency** | **16 cycles** (8 cy read epoch + 8 cy write epoch); **8 cy** with copy elision |
| TILE.GET throughput | **1 per 8 cycles** (read port occupied for full epoch even for single element) |
| TILE.PUT throughput | **1 per 16 cycles** (read + write port, 2 epochs); **1 per 8 cy** with elision |

#### 8.5.3 MTE Rename → Issue → Execute Flow (Bulk Transfer)

```
  D2 (Rename):
    TILE.LD T10, [X5]
      Scalar RAT: X5 → P40 (physical scalar for base address)
      Tile RAT:   T10 → PT200 (allocate new physical tile from tile free list)
                  old mapping PT10 marked orphan
      Tile RAT ready[PT200] ← 0

  DS (Dispatch):
    MTE RS entry: {op=TILE.LD, pscalar=P40, srdy=<from Scalar RAT>, ptdst=PT200, ckpt=...}

  IS (Issue):
    Wait for pscalar P40 ready (CDB wakeup from scalar ALU)
    → read base address from scalar physical RF

  EX (Execute — memory fetch + 1 TRegFile write epoch):
    Memory phase (≈64 cycles from L2):
        MTE Address Gen: compute contiguous address range from base address
        MTE Data Path:   request 64 cache lines from L2 (64 B/cy)
        MTE Buffer:      accumulate 4 KB in outstanding request buffer
    TRegFile write epoch (8 cycles):
        Reserve write port, program reg_idx = PT200
        Write 512 B/cy × 8 cy = 4 KB to physical tile slot PT200
    Total TILE.LD latency (L2 hit): 64 + 8 = **72 cycles**

  Complete:
    Tile RAT ready[PT200] ← 1
    TCB broadcast: PT200
    → wake dependent instructions in Vector RS, Cube RS, MTE RS
    Decrement tile refcount for any source tiles
```

MTE bulk operations incur both **memory latency** and **TRegFile epoch latency**. For TILE.LD, the MTE first fetches 4 KB from memory (64 cache lines at 64 B/cy = 64 cycles from L2), buffers the data, then writes to TRegFile-4K in one 8-cycle write epoch using the **physical tile index** (from Tile RAT) as the `reg_idx` address — total latency: **memory + 8 cycles**. For TILE.ST, the MTE first reads the tile from TRegFile in one 8-cycle read epoch, then writes the data to memory — total: **8 cycles + memory**. The MTE controller issues physical `reg_idx` addresses to port pending registers and sequences data transfer across each 8-cycle epoch.

#### 8.5.4 TILE.GET / TILE.PUT Execution Flow (Element Access)

**TILE.GET Rd, Ts, Ridx** — scalar ← tile element:

```
  D2 (Rename):
    Scalar RAT: Ridx → P50 (lookup index);  Rd → P60 (allocate new scalar dest)
    Tile RAT:   Ts → PT180 (lookup source tile)

  DS (Dispatch):
    MTE RS entry: {op=TILE.GET, pscalar=P50(Ridx), srdy, pdst=P60(Rd), ptsrc1=PT180(Ts), trdy}

  IS (Issue):
    Wait for P50 ready (CDB wakeup) AND PT180 ready (TCB wakeup)
    → read index value from scalar RF; compute row_group = row / 8, row_off, col

  EX (Execute, 9 cycles):
    Cycles 1–8: TRegFile read epoch — reserve read port for physical tile PT180
                port reads 512 B/cy × 8 cy (full tile streamed out);
                capture the 512-B chunk at cycle (row_group+1) containing target row
    Cycle 9:    extract element from captured 512-bit row based on col and
                funct3 (element type), zero-extend to 64 bits

  Complete:
    CDB broadcast: (tag=P60, data=element_value)
    → wakeup dependent scalar RS entries; write to scalar physical RF
    Decrement tile refcount for PT180
```

**TILE.PUT Td, Rs, Ridx** — tile element ← scalar (read-modify-write):

```
  D2 (Rename):
    Scalar RAT: Rs → P70 (lookup data), Ridx → P71 (lookup index)
    Tile RAT:   Td old mapping → PT180 (source, for tile copy)
                Td new mapping → PT210 (allocate from tile free list)
                PT180 marked orphan; ready[PT210] ← 0

  DS (Dispatch):
    MTE RS entry: {op=TILE.PUT, pscalar=P70(Rs), pscalar2=P71(Ridx),
                   ptsrc1=PT180(Td_old), ptdst=PT210(Td_new)}

  IS (Issue):
    Wait for P70, P71 ready (CDB) AND PT180 ready (TCB)

  EX (Execute, 16 cycles — 2 full TRegFile epochs):
    Read epoch (cycles 1–8):
        Reserve read port for physical tile PT180
        Read 512 B/cy × 8 cy = 4 KB (full source tile)
        Buffer tile data in MTE internal SRAM; overwrite target element
        at (row, col) derived from Ridx with scalar value from Rs
    Write epoch (cycles 9–16):
        Reserve write port for physical tile PT210
        Write modified tile 512 B/cy × 8 cy = 4 KB to PT210

    Copy elision optimisation (8 cycles):
        When PT180 refcount=0 and is orphaned at rename, the copy is
        skipped. PT210 reuses PT180's storage. Only the target element
        is overwritten in-place during a single write epoch (8 cy).

  Complete:
    Tile RAT ready[PT210] ← 1
    TCB broadcast: PT210
    → wake dependent tile-domain RS entries
    Decrement tile refcount for PT180; if orphan and refcount=0 → free PT180
```

TILE.GET occupies a TRegFile read port for a full 8-cycle epoch (even though only one 512-B chunk is needed), plus 1 cycle for element extraction — **9 cycles** total. TILE.PUT requires two full epochs (8 cy read + 8 cy write = **16 cycles**) because it is a read-modify-write on the tile. With copy elision (PT_old orphaned, refcount=0), the read epoch is skipped and only the write epoch is needed — reducing latency to **8 cycles**.

#### 8.5.5 TILE.MOVE (Move Elimination) and TILE.TRANSPOSE

**TILE.MOVE Td, Ts** — Handled entirely at the D2 rename stage with **zero-cycle latency**:

```
  D2 (Rename):
    TILE.MOVE T5, T10
      Tile RAT[T10] → PT180 (source physical tile)
      Tile RAT[T5]  → PT50  (old destination mapping, marked orphan)
      Tile RAT[T5]  ← PT180 (Td now aliases same physical tile as Ts)
      refcount(PT180) += 1   (extra architectural name)
      ready[T5] = ready[PT180]  (inherit readiness)
      → No RS entry allocated. No execute. No TCB broadcast.
      → Instruction completes immediately at D2.

  If PT50 is orphan and refcount==0 → free PT50 to tile free list
```

TILE.MOVE does not consume any execute-stage resources, TRegFile-4K ports, or memory bandwidth. It is the preferred way to "rename" tiles between software pipeline stages (e.g., double-buffering schemes where the next iteration's input tiles become the current iteration's operand tiles). Because Td and Ts share the same physical tile after TILE.MOVE, the next write to either architectural register will naturally allocate a new physical tile at rename time.

**TILE.TRANSPOSE Td, Ts, fmt** — Full read-transpose-write through a dedicated 4 KB transpose buffer:

```
  D2 (Rename):
    Tile RAT: Ts → PT180 (source lookup); Td → PT220 (allocate new PT from free list)
    ready[PT220] ← 0

  DS (Dispatch):
    MTE RS entry: {op=TILE.TRANSPOSE, ptsrc1=PT180, ptdst=PT220, fmt}

  IS (Issue):
    Wait for PT180 ready (TCB wakeup)

  EX (Execute, 16 cycles):
    Cycles 1–8:   read 64 rows (512 B/cy) from PT180 via TRegFile read port
                   → store into 4 KB transpose buffer, rearranging elements
    Cycles 9–16:  write 64 transposed rows (512 B/cy) from transpose buffer
                   → PT220 via TRegFile write port

  Complete:
    Tile RAT ready[PT220] ← 1
    TCB broadcast: PT220
    Decrement tile refcount for PT180
```

The MTE transpose buffer (4 KB SRAM) holds one full tile during the transpose. The buffer uses a column-major write / row-major read pattern (or vice versa) to produce the transposed layout. For element types with fewer than 64 elements per row (e.g., FP32: 16 per row), the transpose operates on multiple independent square sub-blocks within the buffer (see §2.4.2 for block dimensions per format).

Port allocation is shared dynamically among Vector, Cube, and MTE — see the comprehensive **TRegFile-4K Port Allocation** table in §9.2 for the full mapping of R0–R7 and W0–W7 across all three units and cube activity scenarios.

#### 8.5.6 MTE RS Entry Fields per Instruction

Each MTE RS entry carries both scalar and tile physical tags obtained at D2 rename. The `pdst` field holds a scalar physical destination tag for TILE.GET (CDB result), or is unused. TILE.MOVE does not allocate an RS entry (handled at rename).

| Instruction | pscalar (7b) | pscalar2 (7b) | ptsrc1 (8b) | ptsrc2 (8b) | ptdst (8b) | pdst (7b) | Result bus |
|-------------|-------------|---------------|-------------|-------------|------------|-----------|------------|
| TILE.LD Td, [Rbase] | Rbase P-tag | — | — | — | PT(Td) | — | TCB |
| TILE.LD Td, [Rbase], Rs | Rbase P-tag | Rs P-tag | — | — | PT(Td) | — | TCB |
| TILE.ST [Rbase], Ts | Rbase P-tag | — | PT(Ts) | — | — | — | — |
| TILE.ST [Rbase], Ts, Rs | Rbase P-tag | Rs P-tag | PT(Ts) | — | — | — | — |
| TILE.GATHER Td, [Rbase], Tidx | Rbase P-tag | — | PT(Tidx) | — | PT(Td) | — | TCB |
| TILE.SCATTER [Rbase], Ts, Tidx | Rbase P-tag | — | PT(Ts) | PT(Tidx) | — | — | — |
| TILE.ZERO Td | — | — | — | — | PT(Td) | — | TCB |
| TILE.COPY Td, Ts | — | — | PT(Ts) | — | PT(Td) | — | TCB |
| **TILE.MOVE Td, Ts** | — | — | — | — | — | — | **— (no RS entry)** |
| **TILE.TRANSPOSE Td, Ts** | — | — | PT(Ts) | — | PT(Td) | — | TCB |
| **TILE.GET Rd, Ts, Ridx** | Ridx P-tag | — | PT(Ts) | — | — | **P(Rd)** | **CDB** |
| **TILE.PUT Td, Rs, Ridx** | Rs P-tag | Ridx P-tag | **PT(Td_old)** | — | **PT(Td_new)** | — | TCB |

Issue conditions:
- **Scalar readiness:** `srdy` bits for pscalar/pscalar2 must be set (wakeup via CDB)
- **Tile readiness:** `trdy` bits for ptsrc1/ptsrc2 must be set (wakeup via TCB)
- Both conditions satisfied → issue to MTE execute pipeline
- **TILE.MOVE:** never enters RS; completes at D2 rename (move elimination)

Completion behavior:
- **TILE.LD, TILE.GATHER, TILE.ZERO, TILE.COPY, TILE.TRANSPOSE, TILE.PUT:** broadcast destination physical tile tag on **TCB**, set `Tile RAT ready[ptdst] ← 1`, decrement tile ref-counts for consumed source tiles.
- **TILE.GET:** broadcast scalar result on **CDB** (tag=pdst, data=extracted element), set `Scalar RAT ready[pdst] ← 1`, decrement tile ref-count for source physical tile.
- **TILE.ST, TILE.SCATTER:** no result bus broadcast; only decrement ref-counts on completion.
- **TILE.MOVE:** no result bus, no ref-count changes at completion (handled at rename: refcount increment for shared PT, orphan marking for old Td mapping).

---

## 9. Register Files

### 9.1 Scalar GPR Physical Register File

| Parameter | Value |
|-----------|-------|
| Physical registers | **128** (P0–P127), 64-bit each |
| Total storage | 128 × 8 B = **1 KB** |
| Read ports | **12** (8 from rename lookup + 4 from issue/execute) |
| Write ports | **6** (4 ALU + 1 MUL/LSU + 1 TILE.GET), matched to CDB ports |
| Implementation | Flip-flop array (small enough for full-speed multi-port) |
| Bypass network | 6-source → 12-sink forwarding MUXes |

**Bypass network:** When a result is broadcast on the CDB in the same cycle that an issuing instruction reads the physical RF, the bypass network forwards the CDB data directly to the execution unit input, avoiding a 1-cycle read-after-write penalty.

**Register lifecycle:**

```
  Allocate:  free list dequeue → assigned as destination at D2
  Write:     execution unit writes result at WB stage
  Read:      issuing instructions read at IS stage (or snoop from CDB)
  Orphan:    a later instruction remaps the same architectural register
  Free:      orphan AND reference count = 0 → return to free list
```

### 9.2 TRegFile-4K (Unified Tile/Vector Data Register File)

The TRegFile-4K is the **physical tile register file** for all non-scalar operands: vector, cube, and MTE. It holds 256 physical tile slots; the Tile RAT maps 32 architectural tile registers (T0–T31) into these 256 physical slots, enabling full out-of-order renaming. There is no separate vector register file. Full specification is in `tregfile4k.md`.

| Parameter | Value |
|-----------|-------|
| Tile size | **4 KB** (4096 B) |
| Physical tile count | **256** (PT0–PT255) |
| Architectural tile count | **32** (T0–T31, renamed by Tile RAT) |
| Total capacity | 256 × 4 KB = **1 MB** |
| SRAM banks | **64** (1R1W each, 256 × 512-bit) |
| Bank groups | **8** groups × 8 banks |
| Read ports | **8** (R0–R7), 512 B/cy each |
| Write ports | **8** (W0–W7), 512 B/cy each |
| Calendar | 8-cycle synchronized epoch |
| Per-port throughput | 1 tile (4 KB) / 8 cycles |
| Aggregate read BW | 8 × 512 B/cy = **4 KB/cy** |
| Aggregate write BW | 8 × 512 B/cy = **4 KB/cy** |
| Addr acceptance | 1 `reg_idx` / port / 8 cycles (epoch-aligned, zero-bubble chaining) |

**TRegFile-4K Port Allocation (8R / 8W) across Vector, Cube, and MTE units:**

Each port is epoch-locked: once a `reg_idx` is latched, the port is occupied for 8 cycles (one full epoch). The allocation varies with cube activity and data format.

| Port | Cube active — MXFP4 / HiFP4 | Cube active — FP16 / BF16 / FP8 | Cube idle |
|------|------------------------------|----------------------------------|-----------|
| **R0** | **Cube A** operand (1 tile/epoch) | **Cube A** operand (1 tile/epoch) | Vector / MTE — free |
| **R1** | **Cube B** operand | **Cube B** operand | Vector / MTE — free |
| **R2** | **Cube B** operand | **Cube B** operand | Vector / MTE — free |
| **R3** | **Cube B** operand | free → **Vector / MTE** | Vector / MTE — free |
| **R4** | **Cube B** operand | free → **Vector / MTE** | Vector / MTE — free |
| **R5** | **Vector** src / **MTE** read | **Vector** src / **MTE** read | Vector / MTE — free |
| **R6** | **Vector** src / **MTE** read | **Vector** src / **MTE** read | Vector / MTE — free |
| **R7** | **Vector** src / **MTE** read | **Vector** src / **MTE** read | Vector / MTE — free |
| **W0** | **Cube C** drain (accum → tile) | **Cube C** drain (accum → tile) | Vector / MTE — free |
| **W1** | **Vector** dst / **MTE** write | **Vector** dst / **MTE** write | Vector / MTE — free |
| **W2** | **Vector** dst / **MTE** write | **Vector** dst / **MTE** write | Vector / MTE — free |
| **W3** | **Vector** dst / **MTE** write | **Vector** dst / **MTE** write | Vector / MTE — free |
| **W4** | **Vector** dst / **MTE** write | **Vector** dst / **MTE** write | Vector / MTE — free |
| **W5** | **Vector** dst / **MTE** write | **Vector** dst / **MTE** write | Vector / MTE — free |
| **W6** | **Vector** dst / **MTE** write | **Vector** dst / **MTE** write | Vector / MTE — free |
| **W7** | **Vector** dst / **MTE** write | **Vector** dst / **MTE** write | Vector / MTE — free |

**Per-unit port usage by MTE instruction:**

| MTE Instruction | Read port(s) | Write port(s) | Epochs | Notes |
|-----------------|-------------|---------------|--------|-------|
| TILE.LD | — | 1 W port | 1 write | Data streamed from memory into write port |
| TILE.ST | 1 R port | — | 1 read | Data streamed from read port to memory |
| TILE.COPY | 1 R port | 1 W port | 1 read + 1 write | Can epoch-pipeline read/write via same-phase pair |
| TILE.ZERO | — | 1 W port | 1 write | Write port driven with zeros, no read needed |
| TILE.GATHER | — | 1 W port | 1 write | Gathered data from memory written to tile |
| TILE.SCATTER | 1 R port | — | 1 read | Tile data read and scattered to memory |
| TILE.GET | 1 R port | — | 1 read (full epoch) | Only 1 row captured; port still occupied 8 cy |
| TILE.PUT | 1 R port | 1 W port | 1 read + 1 write | RMW: read old tile, write modified tile |
| TILE.PUT (elided) | — | 1 W port | 1 write | Copy elision: in-place element overwrite |
| TILE.TRANSPOSE | 1 R port | 1 W port | 1 read + 1 write | Read → transpose buffer → write |
| TILE.MOVE | — | — | — | Rename-only, no port usage |

**Per-unit port usage by Vector instruction:**

| Vector Instruction | Read port(s) | Write port(s) | Epochs | Notes |
|--------------------|-------------|---------------|--------|-------|
| 2-source (VADD, VMUL, …) | 2 R ports | 1 W port | 1 read + 1 write | Epoch-pipelined: write overlaps next read |
| 3-source (VFMA) | 3 R ports | 1 W port | 1 read + 1 write | All 3 sources read in same epoch |
| Reduce (VROWSUM, VCOLMAX, …) | 1 R port | 1 W port | 1 read + 1 write | Result is column/row-vector tile |

**Per-unit port usage by Cube:**

| Cube Phase | Read port(s) | Write port(s) | Epochs | Notes |
|------------|-------------|---------------|--------|-------|
| OPA operand fetch (OF) | R0 (A) + R1–R4 (B) | — | 1 read epoch each | 5 ports reserved; R3–R4 free for FP16/BF16/FP8 |
| Accumulator drain (AD) | — | W0 | 1 write epoch per output tile | Ping-pong: drain overlaps next OF |

**Available ports summary (maximum concurrent operations):**

| Scenario | Free R ports | Free W ports | Max concurrent MTE reads | Max concurrent MTE writes | Max concurrent VEC ops |
|----------|-------------|-------------|--------------------------|---------------------------|------------------------|
| Cube active (MXFP4) | R5–R7 (**3**) | W1–W7 (**7**) | 3 TILE.ST | 7 TILE.LD | 1 (2-src) or 1 (3-src needs R5–R7) |
| Cube active (FP16) | R3–R7 (**5**) | W1–W7 (**7**) | 5 TILE.ST | 7 TILE.LD | 2 (2-src) or 1 (3-src) |
| Cube idle | R0–R7 (**8**) | W0–W7 (**8**) | 8 TILE.ST | 8 TILE.LD | 3 (2-src) or 2 (3-src) |

**Physical tile lifecycle:**

```
  Allocate:  tile free list dequeue → assigned as destination at D2 (Tile RAT update)
  Write:     execution unit (vector/cube-drain/MTE-load) writes 4 KB to physical tile slot
  Read:      issuing instructions read at execute time (physical tile idx → TRegFile-4K port)
  Orphan:    a later instruction remaps the same architectural tile register
  Free:      orphan AND tile reference count = 0 → return to tile free list
```

See `tregfile4k.md` for calendar rotation, bypass rules, and scheduling constraints.

---

## 10. Out-of-Order Execution Model

The Davinci core implements a **ROB-less out-of-order** execution model. Because the core does not need to maintain precise architectural state (no interrupts, no exceptions), it dispenses with the Reorder Buffer entirely. This section describes how instructions flow through the core and how correctness is maintained.

### 10.1 Core Principles

1. **OoO dispatch, OoO execution, OoO completion.** An instruction's result is committed to the physical register file as soon as execution completes. There is no in-order retirement stage.
2. **False dependencies (WAW, WAR) eliminated by register renaming.** Both the Scalar RAT (32→128) and Tile RAT (32→256) assign each destination to a unique physical register/tile, so no instruction ever overwrites another's live data.
3. **True dependencies (RAW) resolved by tag-based wakeup.** Scalar instructions wait for source tags on the CDB. Tile-domain instructions wait for Tile RAT ready bits signaling physical tile completion.
4. **Branch recovery via RAT checkpoints.** On mispredict, both the Scalar RAT and Tile RAT are flash-restored in 1 cycle; all younger instructions are flushed.
5. **Physical registers freed by reference counting.** No ROB means no retirement-based freeing; instead, a register (scalar or tile) is freed when it is both *orphaned* (no longer the current mapping for any architectural register) and its reference count reaches zero.

### 10.2 Instruction Lifecycle

```
  ┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐
  │Fetch│──▶│Decode│──▶│Rename│──▶│ Disp│──▶│Issue│──▶│ Exec│──▶│  WB │
  │ F1-2│   │  D1  │   │  D2  │   │ DS  │   │ IS  │   │EX1-n│   │     │
  └─────┘   └─────┘   └─────┘   └─────┘   └─────┘   └─────┘   └─────┘
                         │                                         │
                    Allocate P-reg                          Write P-reg
                    Update RAT                             Broadcast CDB
                    Checkpoint (branch)                    Wakeup dependents
                    Increment ref-counts                   Decrement ref-counts
                                                           Free orphans (refcnt=0)
```

Detailed per-stage actions:

| Stage | Actions |
|-------|---------|
| **Fetch (F1–F2)** | PC → I-cache + branch predictor; receive 4 instructions |
| **Decode (D1)** | Decode opcode, identify domain, extract fields |
| **Rename (D2)** | Read Scalar RAT / Tile RAT for sources (get physical tags + ready bits); allocate physical dest from appropriate free list; update RAT; increment ref-counts for source physical regs/tiles; if branch: allocate + flash-copy both RAT checkpoints |
| **Dispatch (DS)** | Write entry into target RS with opcode, source tags, ready bits, dest tag |
| **Issue (IS)** | Select oldest ready entry; read physical RF for operands not yet captured |
| **Execute (EX)** | Compute result (variable latency per unit) |
| **Writeback (WB)** | Scalar: broadcast (tag, data) on CDB; write result to physical RF; set ready bit in Scalar RAT; wakeup dependent RS entries. Tile: write data to physical tile in TRegFile-4K; set ready bit in Tile RAT; wakeup dependent tile RS entries. Both: decrement ref-counts for source regs/tiles; if source is orphan and refcnt=0: free it |

### 10.3 Register Alias Table (RAT) Operation

The two RATs (Scalar and Tile) are the core state machines of the processor. In the absence of a ROB, the RATs are the **definitive mapping** from architectural to physical registers.

**Scalar RAT rename example (4-wide, single cycle):**

```
  Instruction stream:
    i0:  ADD  X5, X2, X3
    i1:  MUL  X6, X5, X7
    i2:  SUB  X5, X8, X9
    i3:  ADD  X10, X5, X6

  Before rename:
    Scalar RAT: X2→P2, X3→P3, X5→P5, X7→P7, X8→P8, X9→P9

  Rename (all in one cycle at D2):
    i0: src1=P2, src2=P3, dst=P40 (new);  RAT: X5→P40;  P5 marked orphan
    i1: src1=P40 (bypass from i0), src2=P7, dst=P41;  RAT: X6→P41
    i2: src1=P8, src2=P9, dst=P42 (new);  RAT: X5→P42;  P40 marked orphan
    i3: src1=P42 (bypass from i2), src2=P41 (bypass from i1), dst=P43;  RAT: X10→P43

  After rename:
    Scalar RAT: X2→P2, X3→P3, X5→P42, X6→P41, X7→P7, X8→P8, X9→P9, X10→P43
    Orphans: P5 (old X5), P40 (old X5, transient within group)
    Free when: orphan AND refcount=0
```

**Tile RAT rename example:**

```
  Instruction stream:
    i0:  TILE.LD  T10, [X5]            (scalar src X5, tile dst T10)
    i1:  TILE.LD  T20, [X6]            (scalar src X6, tile dst T20)
    i2:  VADD     T10, T10, T20        (tile src T10, T20; tile dst T10)
    i3:  TILE.ST  [X7], T10            (scalar src X7, tile src T10)

  Before rename:
    Tile RAT: T10→PT10, T20→PT20

  Rename (all in one cycle at D2):
    i0: scalar src=<from Scalar RAT>, tile dst=PT100 (new);  Tile RAT: T10→PT100;  PT10 orphaned
    i1: scalar src=<from Scalar RAT>, tile dst=PT101 (new);  Tile RAT: T20→PT101;  PT20 orphaned
    i2: tile src1=PT100 (bypass i0), tile src2=PT101 (bypass i1),
        tile dst=PT102 (new);  Tile RAT: T10→PT102;  PT100 orphaned
    i3: tile src=PT102 (bypass i2);  no tile dst (store)

  After rename:
    Tile RAT: T10→PT102, T20→PT101
    Tile orphans: PT10 (old T10), PT20 (old T20), PT100 (old T10, transient)
    Free when: orphan AND tile refcount=0
```

### 10.4 Common Data Bus (CDB)

The CDB is a broadcast network connecting execution unit outputs to reservation stations and the physical register file.

| Parameter | Value |
|-----------|-------|
| CDB ports | **6** (4 ALU + 1 MUL/LSU + 1 TILE.GET) |
| Broadcast width | 7-bit tag + 64-bit data per port |
| Snoop points | All RS entries (32+24+16+4+16 = 92 entries × 2 scalar sources) |

The CDB carries scalar physical register tags and 64-bit data. Instructions that produce tile results use the **Tile Completion Bus (TCB)** instead. The CDB is used by:
- Scalar ALU, MUL/DIV, Branch, LSU results (5 ports)
- **TILE.GET** that produces a scalar GPR result (shared port 6)

**Tile Completion Bus (TCB):** A lightweight broadcast network (8-bit physical tile tag, no data payload) with **4 ports** supporting up to 4 simultaneous tile completions per cycle. TCB port allocation:

| TCB port | Source |
|----------|--------|
| TCB0 | Vector unit (VADD, VMUL, VFMA, ... destination tile) |
| TCB1 | Cube unit (CUBE.DRAIN destination tile) |
| TCB2 | MTE unit — TILE.LD / TILE.GATHER / TILE.ZERO / TILE.COPY / TILE.PUT completion (port 1) |
| TCB3 | MTE unit — TILE.LD / TILE.GATHER / TILE.ZERO / TILE.COPY / TILE.PUT completion (port 2) |

Each tile-domain RS entry compares its physical tile source tags against all 4 TCB tags for wakeup, mirroring CDB wakeup for scalar RS entries. MTE instructions that do not produce a tile destination (TILE.ST, TILE.SCATTER) do not broadcast on the TCB.

Each CDB port can broadcast one result per cycle. When a result is broadcast:

1. **Wakeup:** Every scalar/LSU RS entry compares its source tags against the broadcast tag. On match, the ready bit is set and data is captured.
2. **RF write:** The physical register file captures the data at the destination tag.
3. **RAT status:** The ready bit for the physical register is set in the RAT status table.

### 10.5 Physical Register Freeing (Reference Counting)

Without a ROB, the processor cannot use retirement to determine when a physical register is dead. Instead, it uses a **reference counting** scheme, applied identically to both scalar physical registers and physical tile registers:

```
  Per physical scalar register (128 entries):
    ┌──────────┬──────────┬───────────┐
    │ orphan   │ refcount │ state     │
    │ (1 bit)  │ (4 bits) │           │
    └──────────┴──────────┴───────────┘

  Per physical tile register (256 entries):
    ┌──────────┬──────────┬───────────┐
    │ orphan   │ refcount │ state     │
    │ (1 bit)  │ (3 bits) │           │
    └──────────┴──────────┴───────────┘

  State machine (same for both):
    MAPPED:   RAT points to this register; refcount tracks in-flight readers
    ORPHAN:   RAT no longer points here (remapped); refcount may be > 0
    FREE:     orphan AND refcount == 0 → returned to free list
```

Tile refcount is 3 bits (max 7 concurrent readers per physical tile), which suffices because the Vector RS (16), Cube RS (4), and MTE RS (16) issue at most a few readers per tile simultaneously. Scalar refcount is 4 bits (max 15).

**Lifecycle events (identical for scalar and tile):**

| Event | orphan | refcount | Action |
|-------|--------|----------|--------|
| Allocated as destination at D2 | 0 | 0 | Added to RAT mapping |
| Instruction reads this register (dispatched to RS) | — | +1 | Reader registered |
| Reader completes execution (reads at IS/EX) | — | −1 | Reader done |
| RAT remaps arch-reg to new physical register | 1 | — | Old mapping becomes orphan |
| refcount reaches 0 while orphan=1 | 1 | 0 | **Free**: return to free list |

**Branch misprediction and ref-counts:** When a mispredict occurs, all instructions younger than the branch are flushed. Their RS entries are invalidated, and the ref-counts for their source registers (scalar and tile) are decremented. Physical registers/tiles allocated as destinations by flushed instructions are returned directly to their respective free lists. Both free list head pointers are restored from the checkpoint to reclaim all speculatively allocated registers.

### 10.6 Branch Recovery

```
  ┌────────────────────────────────────────────────────────────┐
  │  Branch Misprediction Recovery (1-cycle dual-RAT restore)  │
  │                                                            │
  │  Cycle 0: Branch resolves as MISPREDICTED at EX1          │
  │    → Identify checkpoint ID from branch's RS entry         │
  │                                                            │
  │  Cycle 1: Recovery actions (all in parallel):              │
  │    a) Flash-restore: checkpoint[id] → active Scalar RAT   │
  │    b) Flash-restore: checkpoint[id] → active Tile RAT     │
  │    c) Restore scalar free-list head pointer                │
  │    d) Restore tile free-list head pointer                  │
  │    e) Restore RAS top pointer from checkpoint              │
  │    f) Invalidate all RS entries younger than branch         │
  │    g) Decrement ref-counts (scalar + tile) for flushed ops │
  │    h) Deallocate all checkpoints younger than this branch  │
  │                                                            │
  │  Cycle 2: Redirect fetch PC to correct branch target       │
  │                                                            │
  │  Cycle 3+: New instructions begin entering F1               │
  │                                                            │
  │  Total penalty: 6 cycles (pipeline refill to EX1)           │
  └────────────────────────────────────────────────────────────┘
```

---

## 11. Memory Subsystem

### 11.1 Cache Hierarchy

```
  ┌────────────┐    ┌────────────┐
  │  L1-I      │    │  L1-D      │
  │  64 KB     │    │  64 KB     │
  │  4-way     │    │  4-way     │
  │  2-cy lat  │    │  4-cy lat  │
  └─────┬──────┘    └─────┬──────┘
        │                 │
        └────────┬────────┘
                 ▼
        ┌────────────────┐
        │  L2 (Unified)  │
        │  512 KB        │
        │  8-way         │
        │  12-cy lat     │
        └───────┬────────┘
                │
                ▼
        External Bus / NoC
```

| Cache | Size | Associativity | Line size | Latency | Ports | MSHRs |
|-------|------|---------------|-----------|---------|-------|-------|
| L1-I | **64 KB** | 4-way | 64 B | **2** cycles | 1 read (fetch) | 4 |
| L1-D | **64 KB** | 4-way | 64 B | **4** cycles | 1 read + 1 write (LSU) | 8 |
| L2 | **512 KB** | 8-way | 64 B | **12** cycles | 1 read + 1 write | 16 |

### 11.2 TLBs

| TLB | Entries | Associativity | Page sizes | Miss penalty |
|-----|---------|---------------|------------|-------------|
| I-TLB | **64** | Fully assoc | 4 KB, 2 MB | L2 TLB lookup |
| D-TLB | **64** | Fully assoc | 4 KB, 2 MB | L2 TLB lookup |
| L2 TLB (unified) | **512** | 8-way | 4 KB, 2 MB, 1 GB | Page table walk |

### 11.3 Store Buffer

| Parameter | Value |
|-----------|-------|
| Entries | **16** |
| Commit policy | **OoO commit** (no precise exception requirement) |
| Forwarding | Full store-to-load forwarding on address+size match |
| Write-combining | Adjacent stores to same cache line are merged |
| Drain policy | Oldest-first to L1-D when not conflicting with loads |

Because the Davinci core does not require precise exceptions, stores are committed to the cache hierarchy as soon as their address and data are both resolved. There is no need to hold stores until in-order retirement.

### 11.4 MTE Memory Path

The MTE unit has a **high-bandwidth path** to the L2 cache (and external memory) for tile data transfers, separate from the scalar LSU path through L1-D.

```
  MTE ──▶ L2 Cache (512 KB) ──▶ External Memory
           64 B/cy sustained bandwidth
           1 cache line per cycle
           1 tile (4 KB) = 64 cache lines = 64 cycles from L2
```

| Parameter | Value |
|-----------|-------|
| MTE → L2 bandwidth | **64 B/cycle** (1 cache line/cycle) |
| Tile load from L2 (hit) | **64 cycles** per tile (4 KB / 64 B) |
| Tile load from external memory | **200–400 cycles** per tile (DRAM dependent) |
| Outstanding MTE requests | **32** (deep buffer for memory-level parallelism) |
| Prefetch support | MTE RS can issue TILE.LD early, buffering data in TRegFile |

The MTE unit exploits the large TRegFile-4K (256 tiles, 1 MB) as a **software-managed scratchpad**. Programmers (or compiler) schedule TILE.LD instructions well ahead of CUBE.OPA to hide memory latency. The 32-entry outstanding request buffer allows many tile loads to be in flight simultaneously, maximizing bandwidth utilization.

### 11.5 Memory Ordering

Because the core executes run-to-completion code without OS interaction, memory ordering is simplified:

- **Scalar loads and stores** within a single thread maintain **program order** through the LSU's address disambiguation (store-to-load forwarding, load queue snooping).
- **TILE.LD/ST** operations are **unordered** with respect to each other by default. Software uses `FENCE` instructions when ordering between tile operations and scalar operations is required.
- **CUBE.OPA** reads from TRegFile-4K are ordered with respect to preceding TILE.LD operations by the **Tile RAT ready bits** (the cube RS will not issue until the source physical tiles are marked "ready" by completed TILE.LD operations).

---

## 12. Mixed-Domain Instruction Scheduling

### 12.1 Unified Front-End, Distributed Back-End

All four instruction domains share the same front-end pipeline (fetch, decode, rename). At dispatch, instructions are routed to domain-specific reservation stations. This allows the core to exploit instruction-level parallelism across domains:

```
  Single instruction stream (architectural tile regs T0–T31):
    ADD   X5, X2, X3        → Scalar RS → ALU
    TILE.LD T10, [X5]       → Tile RAT: T10→PT200;  MTE RS (ptdst=PT200, depends on X5 via CDB)
    TILE.LD T20, [X6]       → Tile RAT: T20→PT201;  MTE RS (ptdst=PT201, independent)
    VADD  T30, T10, T20     → Tile RAT: T30→PT202;  Vector RS (ptsrc=PT200,PT201; depends via TCB)
    CUBE.OPA z0, T10, T20, r1  → Cube RS (ptsrc=PT200,PT201; depends via TCB ready bits)
    TILE.GET X7, T30, X8    → MTE RS (ptsrc=PT202, depends via TCB; pdst=P60 → CDB scalar result)
    TILE.PUT T10, X9, X10   → Tile RAT: T10→PT203; MTE RS (ptsrc=PT200_old, ptdst=PT203; RMW)
    ADD   X11, X7, X9       → Scalar RS → ALU (depends on X7 via CDB from TILE.GET)
```

### 12.2 Cross-Domain Dependencies

Dependencies between domains are tracked through shared mechanisms:

| Dependency | Mechanism |
|------------|-----------|
| **Scalar → MTE** (address operands) | MTE RS entry holds scalar P-reg tag for base address; wakeup via CDB when scalar ALU produces address |
| **Scalar → Vector** (scalar operand in vector reduction) | Vector RS entry holds scalar P-reg tag for scalar inputs; wakeup via CDB |
| **MTE → Vector** (tile data readiness) | Tile RAT: TILE.LD completes → sets ready bit for physical tile; Vector RS wakes via TCB |
| **MTE → Cube** (tile data readiness) | Tile RAT: TILE.LD completes → sets ready bit for physical tile; Cube RS wakes via TCB |
| **Vector → Cube/MTE** (vector result tile) | Tile RAT: vector write completes → sets ready bit; downstream RS entries wake via TCB |
| **Cube → MTE** (drain result tile) | Tile RAT: CUBE.DRAIN completes → sets ready bit for physical tile; MTE RS wakes via TCB |
| **Tile → Scalar** (TILE.GET element extract) | TILE.GET reads physical tile, extracts element, broadcasts scalar result on CDB |
| **Scalar → Tile** (TILE.PUT element insert) | TILE.PUT reads scalar GPR via CDB wakeup, reads old physical tile, writes new physical tile; TCB broadcast |
| **Vector → Vector** (reduction result) | VROWSUM/VCOLMAX etc. produce column/row-vector tile result (TCB completion) |

### 12.3 Tile RAT Wakeup & Tile Completion Bus (TCB)

The Tile RAT maintains a **ready bit** per physical tile register (256 bits total). This replaces a scoreboard: rename ensures every tile destination gets a unique physical tile, so there are no WAW/WAR hazards. The ready bit simply tracks whether the producing operation has finished writing the physical tile.

```
  ┌─────────────────────────────────────────────────────────────────┐
  │  Tile RAT Ready Bits + Tile Completion Bus (TCB)                │
  │                                                                 │
  │  Tile RAT: 32 entries (arch T0–T31) → phys PT0–PT255           │
  │  Ready array: 256 bits (one per physical tile)                  │
  │  TCB: 4 broadcast ports (8-bit tag each, no data payload)      │
  │                                                                 │
  │  TILE.LD T10 renamed:    Tile RAT T10→PT200; ready[PT200] ← 0 │
  │  TILE.LD PT200 completed: ready[PT200] ← 1; TCB broadcast PT200│
  │                                                                 │
  │  VADD T30,T10,T20 renamed: T30→PT202, reads PT200,PT201       │
  │    RS entry: ptsrc1=PT200, ptsrc2=PT201, ptdst=PT202           │
  │    TCB snoop: waits for ready[PT200] && ready[PT201]           │
  │  VADD PT202 completed:   ready[PT202] ← 1; TCB broadcast PT202│
  │                                                                 │
  │  CUBE.OPA reads T10→PT200: checks ready[PT200]                │
  │    if 0 → stall in Cube RS (waits for TCB wakeup)              │
  │    if 1 → issue                                                 │
  │                                                                 │
  │  CUBE.DRAIN writes T12→PT205: ready[PT205] ← 0 at rename      │
  │  CUBE.DRAIN completed:  ready[PT205] ← 1; TCB broadcast PT205 │
  │                                                                 │
  │  TILE.ST reads T12→PT205: checks ready[PT205]                 │
  └─────────────────────────────────────────────────────────────────┘

  TCB wakeup logic (per tile-domain RS entry):
    For each RS entry with N tile sources (up to 3):
      if (ptsrc_k == TCB_tag && !trdy_k):  trdy_k ← 1
    Ready to issue when all trdy bits set (and scalar rdy if applicable)
```

### 12.4 Concurrent Execution Example

A typical transformer inference kernel mixes all four domains:

```
  Cycle  │ Scalar ALU │ LSU        │ Vector            │ MTE             │ Cube MXU
  ───────┼────────────┼────────────┼───────────────────┼─────────────────┼──────────────
  0–7    │ addr calc  │ scalar LD  │ —                 │ TILE.LD T0-T3   │ —
  8–15   │ loop ctrl  │ scalar LD  │ —                 │ TILE.LD T4-T7   │ —
  16–23  │ addr calc  │ —          │ VADD read epoch   │ TILE.LD T8-T11  │ CUBE.OPA z0,...
  24–31  │ addr calc  │ —          │ VADD write epoch  │ TILE.LD T12-T15 │ (OPA continues)
  32–47  │ addr calc  │ scalar ST  │ VMUL (16cy)       │ TILE.LD (next)  │ (OPA continues)
  48–63  │ loop ctrl  │ —          │ VCVT (16cy)       │ TILE.ST T16     │ CUBE.DRAIN z0
  64+    │ next iter  │ —          │ —                 │ TILE.LD (next)  │ CUBE.OPA z1,...
```

Key observations:
- Scalar ALU computes addresses and loop control concurrently with cube execution.
- MTE loads next tiles while cube processes current tiles (double-buffering at software level).
- Vector unit handles element-wise operations (activation functions, normalization) in parallel.
- All domains proceed independently, limited only by true data dependencies.

---

## 13. Performance Targets

### 13.1 Clock & Throughput

| Metric | Target |
|--------|--------|
| Clock frequency | ≥ **1.5 GHz** (5 nm process) |
| Scalar IPC (peak) | **4.0** (4 ALU results/cycle) |
| Scalar IPC (sustained, typical) | **2.5–3.0** (branch + cache miss penalties) |
| Vector throughput (epoch-pipelined) | **1 tile / 8 cycles** (write epoch overlaps next read epoch) = **768 GFLOPS** FP32 @ 1.5 GHz |
| Vector latency | **16** cycles per tile-wide instruction (2 epochs) |
| Cube FP16 | **12.3 TFLOPS** (4096 MACs × 2 × 1.5 GHz) |
| Cube FP8 | **24.6 TOPS** (8192 MACs × 2 × 1.5 GHz) |
| Cube MXFP4 | **98.3 TOPS** (32768 MACs × 2 × 1.5 GHz) |
| MTE tile bandwidth | **4 KB/cy** aggregate read + **4 KB/cy** aggregate write (TRegFile) |
| Memory bandwidth (L2) | **96 GB/s** (64 B/cy × 1.5 GHz) |

### 13.2 Workload Performance Summary

Reference `outerCube.md` §7–§8 for detailed cycle counts. Key results:

| Workload | Format | Dimensions | Cube cycles | MAC utilization |
|----------|--------|-----------|-------------|-----------------|
| Transformer decode (QKV proj) | FP16 | M=8, K=4096, N=4096 | 32,768 | **100%** |
| Transformer decode (FFN) | FP8 | M=8, K=4096, N=4096 | 16,384 | **100%** |
| Transformer decode (FFN) | MXFP4 | M=8, K=4096, N=4096 | 4,096 | **100%** |
| CNN 3×3 early layer | FP16 | M=50176, K=27, N=64 | 21,195 (Mode B) | **100%** |
| CNN 3×3 deep layer | FP16 | M=196, K=2304, N=256 | 28,800 (Mode A) | **98%** |

### 13.3 IPC Breakdown (Transformer Decode Kernel)

```
  Instruction mix (typical transformer layer, M=8, K=4096, N=4096, FP16):
    Scalar:   ~15% (address calc, loop control, pointer updates)
    MTE:      ~25% (TILE.LD / TILE.ST)
    Cube:     ~55% (CUBE.OPA, CUBE.DRAIN, CUBE.ZERO)
    Vector:   ~5%  (activation, normalization, softmax)

  Effective throughput:
    Cube keeps MXU busy ~98% of cycles (K=4096, pipeline overhead < 2%)
    MTE hides memory latency via double-buffered tile loads
    Scalar + vector execute in parallel with cube (no stalls)
```

---

## 14. Area & Power Considerations

### 14.1 Major Area Consumers

| Component | Estimated area (5 nm) | Dominant factor |
|-----------|----------------------|-----------------|
| TRegFile-4K (1 MB SRAM) | ~1.2 mm² | 64 banks × 256×512b SRAM |
| outerCube MXU (4096 MACs + trees) | ~0.8 mm² | 4096 multipliers + adder trees |
| Cube staging SRAM (40–56 KB) | ~0.06 mm² | A/B double-buffer |
| Cube accumulator SRAM (32 KB) | ~0.04 mm² | Ping-pong FP32 accumulators |
| L1-I (64 KB) | ~0.08 mm² | 4-way SRAM |
| L1-D (64 KB) | ~0.08 mm² | 4-way SRAM |
| L2 (512 KB) | ~0.5 mm² | 8-way SRAM |
| Scalar physical RF (1 KB) | ~0.02 mm² | 12R+6W flip-flop array |
| Scalar front-end (Scalar RAT, free list, CDB) | ~0.2 mm² | 32-entry RAT, 96-entry free list, bypass |
| Tile RAT + tile free list + tile ref-counts | ~0.05 mm² | 32-entry RAT, 224-entry free list, 256×4b ref-counts |
| Tile Completion Bus (TCB) + tile RS CAMs | ~0.05 mm² | 4-port TCB, 36-entry tile RS wakeup comparators |
| MTE transpose buffer (4 KB SRAM) | ~0.005 mm² | Single-port SRAM for tile transpose |
| RS + dispatch + checkpoint control | ~0.15 mm² | 8 checkpoint slots (499b each), RS age logic |
| **Total core (estimated)** | **~3.26 mm²** | |

### 14.2 Power Management

| Technique | Scope | Description |
|-----------|-------|-------------|
| Clock gating | Per execution unit | Gate clock to idle ALUs, MUL, vector, cube, MTE |
| Power gating | Cube adder tree upper stages | FP16 mode: gate FP4/FP8 adder stages |
| Power gating | TRegFile-4K banks | Gate unused bank groups when tile count is low |
| Power gating | Cube accumulator | Mode A: gate unused 12 KB of 16 KB ping-pong |
| Operand isolation | MXU bank inputs | Latch-based isolation when bank not computing |
| DVFS | Core-level | Dynamic voltage/frequency scaling for power/perf tradeoff |

---

## 15. External Interfaces

### 15.1 Core-to-NoC Interface

| Parameter | Value |
|-----------|-------|
| Bus width | **256 bits** (32 B) |
| Protocol | AXI4 (or similar point-to-point) |
| Outstanding requests | **32** (read) + **16** (write) |
| Burst length | Up to 4 beats (128 B, 2 cache lines) |
| Clock domain | Core clock (synchronous) or async bridge |

### 15.2 Cache Coherence

The Davinci core is designed primarily for single-core or non-coherent multi-core configurations (AI accelerator context). When coherence is needed:

| Parameter | Value |
|-----------|-------|
| Protocol | MOESI or directory-based |
| Snoop filter | L2 tag duplicate |
| Coherence granularity | 64 B (cache line) |

For tile data (TRegFile-4K), coherence is managed at the software level. Tile data bypasses the coherence protocol, flowing through the MTE's dedicated memory path.

### 15.3 Debug & Trace Interface

| Feature | Description |
|---------|-------------|
| Debug halt | External debug request halts core at next instruction boundary |
| PC trace | Compressed branch trace (taken/not-taken stream) |
| Performance counters | 8 programmable counters: IPC, branch mispredict rate, cache miss rate, cube utilization, MTE stalls, RS occupancy |
| Breakpoint registers | 4 instruction address breakpoints + 2 data address watchpoints |

---

## Appendix A: Glossary

| Term | Definition |
|------|-----------|
| **RAT** | Register Alias Table — maps architectural registers to physical registers (Scalar RAT: 32→128 GPRs; Tile RAT: 32→256 tiles) |
| **TCB** | Tile Completion Bus — lightweight broadcast network (8-bit physical tile tag) for tile wakeup |
| **CDB** | Common Data Bus — broadcast network for scalar execution results |
| **RS** | Reservation Station — holds dispatched instructions waiting for operands |
| **MTE** | Memory Tile Engine — executes tile load/store/gather/scatter instructions |
| **MXU** | Matrix Unit — the outerCube outer-product accumulation engine |
| **TRegFile-4K** | Tile Register File with 4 KB physical tiles (256 × 4 KB = 1 MB), 8R+8W ports |
| **OPA** | Outer Product Accumulate — the fundamental cube computation |
| **ROB** | Reorder Buffer — *not present* in Davinci (no precise exceptions) |
| **MSHR** | Miss Status Holding Register — tracks outstanding cache misses |
| **BTB** | Branch Target Buffer — caches branch target addresses |
| **TAGE** | TAgged GEometric history length predictor |
| **RAS** | Return Address Stack — predicts function return targets |
| **IPC** | Instructions Per Cycle |
| **MLP** | Memory-Level Parallelism |

## Appendix B: Reference Documents

| Document | Content |
|----------|---------|
| `outerCube.md` | outerCube MXU architecture, dual-mode operation, ISA, pipeline, performance analysis |
| `tregfile4k.md` | TRegFile-4K design: 256×4KB tiles, 8R+8W ports, 8-cycle calendar, bypass rules |
| `Simplified_Superscalar Design Concepts-2.md` | OoO execution theory: no ROB, RAT checkpointing, reference-counted register freeing |
| [pto-isa vector docs](https://github.com/hw-native-sys/pto-isa/tree/main/docs/isa) | Vector ISA definition |
