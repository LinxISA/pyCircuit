# Davinci Out-of-Order Processor Core — v2

## 1. Overview & Design Philosophy

The Davinci-v2 core is a **single-threaded, 4-wide, out-of-order, speculatively-executing** processor targeting AI inference, HPC, and dense linear algebra workloads. It executes a unified instruction stream containing four instruction domains — scalar, vector, cube (matrix), and memory-tile-engine (MTE) — on a shared front-end with distributed back-end execution units.

**v2 inherits the v1 baseline ([`Davinci_supersclar.md`](Davinci_supersclar.md)) and adds three architectural enhancements:**

1. **TRegFile-4K with per-port `is_transpose` reads** (see [`tregfile4k.md`](tregfile4k.md) §7). Each read port now carries a 1-bit `is_transpose` flag latched at the epoch boundary alongside `reg_idx`. When asserted, the port delivers the **chunk-grid transpose** of the addressed 4 KB tile at full **512 B/cy** for the entire 8-cycle epoch — bank-conflict-free under the diagonal skew, with no SRAM duplication, no extra latency, and no dedicated transpose buffer. This eliminates the v1 `TILE.TRANSPOSE` predecessor for most use cases and enables the vector unit's per-beat tilelet-transpose mechanism (§8.3).
2. **Vector unit upgraded to VEC-4K-v2** (see [`vector4k_v2.md`](vector4k_v2.md)). Major changes vs. v1's vector unit:
   - Up to **3 tile operands** per instruction: two value tiles (`A`, `B`) plus one **per-element bitmask tile** (`C`).
   - Up to **2 tile results** per instruction (`D0`, `D1`) — value+index, quotient+remainder, etc.
   - **Per-element predication / masking** on every elementwise op, every reduction, and every gather/sort — at zero fetch-phase cost in the common case.
   - **Tile-register metadata** (32 b: `shape.x`, `shape.y`, `format`) carried alongside each 4 KB tile.
   - **SRAM-based staging registers** (`SA`, `SB`, `SC`) decouple TRegFile fetch cadence from compute pipeline; per-beat microcode dispatches `{src, strip, tilelet_xpose}` per ALU operand.
   - **Restored narrow formats**: FP8 (E4M3/E5M2), FP4 (MXFP4/HiFP4) joining FP32, FP16, BF16.
   - **Three new PTO instructions** natively enabled by the v2 datapath: `TINV` (matrix inverse up to 128×128 FP32 / 16-tile range), `TROWRANGE_MUL` (column-wise product over a dynamic row sub-range), `TMRGSORT` (full-tile mergesort over any `N = 2^p` up to 8192 via a reconfigurable 256-lane shuffle + compare-swap primitive).
3. **Speculative out-of-order execution** with a **ROB-less recovery scheme** that nonetheless guarantees architectural state is never corrupted by a misspeculated path (§11). The mechanism extends the v1 RAT-checkpoint + reference-counting infrastructure with a **branch-tagged speculative store buffer** for scalar memory and a **speculative tile-store queue** for MTE memory writes — both of which gate visible side effects until the producing branch tag becomes non-speculative. Section 11 walks through why this is sufficient without a Reorder Buffer, what it costs in area / latency, and which workloads it can and cannot serve correctly.

> **Design discipline:** The v2 core continues to assume **run-to-completion kernel execution** with **no precise architectural exceptions and no OS-level interrupts** — the same envelope as v1. The new speculation-recovery mechanism handles **branch mispredictions** only; it does **not** turn the core into a precise-exception machine. Section 11.7 enumerates the remaining "non-recoverable" classes (asynchronous page faults, signaling NaNs, ECC errors observed mid-kernel) and the kernel-level conventions that bound them.

### 1.1 Key Parameters (v2 deltas in **bold**)

| Parameter | Value |
|-----------|-------|
| Scalar ISA width | **64-bit** RISC (ARM / RISC-V style), unchanged |
| Architectural GPRs | **32** (X0–X31), 64-bit |
| Physical GPRs | **128** (P0–P127), 64-bit |
| Architectural tile regs | **32** (T0–T31), 4 KB each |
| Physical tile regs | **256** (PT0–PT255) in TRegFile-4K |
| **TRegFile-4K read ports** | **8R, each with `is_transpose` bit** (§9.2) |
| **Per-tile metadata** | **32 b** (shape.x, shape.y, format) §9.2.1 |
| Fetch / decode width | **4** instructions / cycle |
| Scalar issue width | **6** (4 ALU + 1 MUL/DIV + 1 branch) |
| **Vector issue width** | **1 VEC-4K-v2 instruction / cycle** (§8.3) |
| Cube issue width | **1** CUBE instruction / cycle |
| MTE issue width | **2** TILE.LD/ST per cycle |
| Pipeline depth (scalar) | **12** stages (fetch-to-writeback) |
| Branch predictor | Hybrid TAGE + BTB + RAS |
| **RAT checkpoints** | **8** (max in-flight branches; same as v1, now repurposed as *speculation tags* §11.3) |
| **Branch tag width** | **3 b** (matches checkpoint count); attached to every in-flight RS / store-buffer / tile-store-queue entry |
| Reservation station entries | Scalar: 32, LSU: 24, **Vector: 24** (was 16; widened for 3-operand v2 entries), Cube: 4, MTE: 16 |
| **Speculative store buffer entries** | **24** (was 16 in v1; widened to absorb branch-tag gating §11.4) |
| **Speculative tile-store queue** | **8** entries (branch-tag-gated, MTE-side §11.5) |
| L1-I cache | 64 KB, 4-way, 64 B line |
| L1-D cache | 64 KB, 4-way, 64 B line, non-blocking (8 MSHRs) |
| L2 cache (core-private) | 512 KB, 8-way, 64 B line |
| Cube MXU | 4096 base MACs, 8 banks, dual-mode A/B |
| Clock target | ≥ **1.5 GHz** (5 nm) |
| **Peak FP32 throughput (vec)** | **0.77 TFLOPS** (1 tile / 8 cy at 1.5 GHz, 128-lane FMA) |
| **Peak FP4 throughput (vec)** | **6.14 TFLOPS** (4× SIMD per group) |
| Peak FP16 throughput (cube) | **12.3 TFLOPS** |
| Peak FP8 throughput (cube) | **24.6 TOPS** |
| Peak MXFP4 throughput (cube) | **98.3 TOPS** |

---

## 2. ISA Summary

The v2 ISA is a strict superset of v1: every v1 opcode encodes identically and behaves identically. v2 adds:

- **Masked variants** of every elementwise vector op, every reduction, and every gather (encoded by a bit in `funct7`).
- **Three new PTO instructions** (§2.2.6).
- **A new tile-metadata setter** `TSETMETA` (§2.2.7).
- **Branch hint bits** in the conditional-branch encoding for static prediction override (§5.2.4).

### 2.1 Scalar ISA

**Unchanged from v1 §2.1.** All branches, jumps, ALU, MUL/DIV, LD/ST, and FENCE instructions are identical. v1 software runs on v2 unmodified.

A new optional 1-bit `H` (hint) field in the conditional-branch funct3 encoding lets the compiler suggest static taken/not-taken when the dynamic predictor has no entry. Predictor still has final say once it has trained — H is consulted only on a TAGE/BTB miss.

### 2.2 Vector ISA — VEC-4K-v2

The vector unit consumes **4 KB tile registers** (`T0–T31`, renamed by the Tile RAT to physical tile slots `PT0–PT255` in TRegFile-4K). All vector instructions are 32-bit fixed-width, with the same R/S/T/U-type encoding skeleton as v1 (§2.2.2 of v1).

**v2 changes:**

#### 2.2.1 Tile metadata (`shape.x`, `shape.y`, `format`)

Every physical tile register carries a **32-bit metadata word** alongside its 4 KB payload (§9.2.1):

```
  ┌────────────┬────────────┬───────────┬─────────────────────┐
  │ shape.x    │ shape.y    │ format    │ flags / reserved    │
  │ [13:0]     │ [27:14]    │ [31:28]   │                     │
  └────────────┴────────────┴───────────┴─────────────────────┘
```

| Field | Width | Range | Meaning |
|-------|-------|-------|---------|
| `shape.x` | 14 b | 1 … 8192 | Number of **columns `C`** (logical row length). Power-of-two only. |
| `shape.y` | 14 b | 1 … 8192 | Number of **rows `R`**. Power-of-two only. |
| `format`  | 4 b  | see below | Logical element format. |
| `flags`   | (overlay) | — | `arg_tile`, `scalar_tile`, `prefetch_hint` (microcode-encoded). |

Legality: `shape.x · shape.y · E = 4096`, where `E` is bytes/element from the format table:

| `format` code | Logical name | `E` (bytes) | Elements/tile |
|----------------|--------------|-------------|---------------|
| `0b0000` | FP32 / INT32 | 4 | 1024 |
| `0b0001` | FP16 / BF16  | 2 | 2048 |
| `0b0010` | FP8 (E4M3 / E5M2) | 1 | 4096 |
| `0b0011` | FP4 (MXFP4 / HiFP4) | 0.5 | 8192 |
| `0b01xx`–`0b11xx` | reserved | — | — |

Metadata is written **only** by the producing instruction (implicit at retire) or the explicit `TSETMETA` op (§2.2.7). It cannot change while the tile is the source of any in-flight fetch.

> **Why metadata?** It lets a single tile op service every shape/format without opcode explosion. The vector unit's stage (A) align/unpack (§8.3) and stage (B) reduction (§8.3) both consult `format` and `shape` from the tile-metadata word read at the first strip of each operand. Microcode programs (§8.3.4) are keyed by `(opcode, format, W-regime, R-regime)`.

#### 2.2.2 Operand model (3 source, 2 destination)

| Operand | Role | Tile-RAT entry | Storage |
|---------|------|----------------|---------|
| **A** | Value tile (primary, mandatory) | source | TRegFile read port → `SA` staging |
| **B** | Value tile (secondary, optional) | source | TRegFile read port → `SB` staging |
| **C** | **Per-element bitmask** (1 b/element) — selects which lanes participate | source (when `has_mask = 1`) | TRegFile read port → `SC` staging |
| **D0** | Result tile (primary) | destination | Write port `W0` |
| **D1** | Result tile (secondary, optional) | destination | Write port `W4` |

The 32-bit instruction word reserves a `has_mask` bit (1 if `C` is fetched), a `retire_mask[1:0]` field (which of `D0`, `D1` are written), and per-operand `is_transpose_{A,B,C}` bits forwarded to the TRegFile read ports (§9.2). Tile register fields stay 5 bits (T0–T31).

#### 2.2.3 Encoding (32-bit)

```
  R-type (3-source, 2-dest):
  31      26 25  21 20  16 15   12 11      6 5     0
 ┌────────┬──────┬──────┬──────┬───────┬────────┐
 │ funct6 │  Tc  │  Tb  │  Ta  │ Td0/d1│ opcode │
 │ + xpA  │ (5b) │ (5b) │ (5b) │ (6b)  │ VEC    │
 │ + xpB  │      │      │      │       │        │
 │ + xpC  │      │      │      │       │        │
 │ +mask  │      │      │      │       │        │
 │ +rmask │      │      │      │       │        │
 └────────┴──────┴──────┴──────┴───────┴────────┘
   funct6 packs 6 bits split between op-extension (3 b),
   has_mask (1 b), is_xpose_A (1 b), is_xpose_B (1 b);
   is_xpose_C and retire_mask travel in the immediate slot
   of S-/T-types or in a fixed funct7 bit pattern.
```

**Backward compatibility:** v1 vector instructions decode as `has_mask = 0`, `retire_mask = 2'b01`, `is_xpose_{A,B,C} = 0` — i.e. unmasked, single-result, no-transpose — and produce bit-exact v1 results.

#### 2.2.4 Masked / predicated variants

Every elementwise op (`VADD`, `VMUL`, `VFMA`, `VSEL`, …), every reduction (`VROWSUM`, `VCOLMAX`, …), and every gather (`VGATHER`, `VGATHERB`) has a masked counterpart. Conventions:

| Encoding | Behaviour |
|----------|-----------|
| `has_mask = 0` | Full-tile op; `C` not fetched; lane gate ties to `IMM_ALL_ONES` (every lane participates). Identical to v1 semantics. |
| `has_mask = 1` | `C` fetched (≤ 2 strips piggybacking on an idle read-port cycle, §8.3.6); per-lane gate `out[lane] = M[lane] ? alu_core_out[lane] : identity[lane]`. The "identity" depends on the op (see [`vector4k_v2.md`](vector4k_v2.md) §5.8): preserves operand A for `TSEL`-style, leaves accumulator unchanged for masked `ACCUM`, etc. |

**Cycle cost.** Masked variants pay **0 extra fetch cycles** in the common case (mask piggybacks on idle port within the value-tile epoch). End-to-end latency is therefore the same as the unmasked variant.

#### 2.2.5 Per-operand transpose (`is_xpose_*`) and per-beat tilelet transpose

Two orthogonal transpose mechanisms, both reusing the chunk-grid transpose algorithm at 64 B sub-chunk granularity:

| Mechanism | Granularity | Control | Where |
|-----------|-------------|---------|-------|
| **TRegFile read-port `is_transpose`** | Whole tile, set once at fetch time | `is_xpose_{A,B,C}` bits in the issue packet | TRegFile-4K read port ([`tregfile4k.md`](tregfile4k.md) §7) — costs zero VEC-side hardware |
| **Staging-side `tilelet_xpose`** | Per 512 B tilelet, per-beat | One bit per operand slot in each microcode beat | Staging register (`SA`, `SB`, `SC`) read datapath — costs ~30 K gate per staging register |

The two combine: a tile may be fetched in row-mode and then read in col-mode beat-by-beat from staging, or fetched pre-transposed (col-mode) and re-read in row-mode. Microcode picks the combination per instruction.

**TRegFile-side rule R2** ([`tregfile4k.md`](tregfile4k.md) §6): the two physical read ports active in any 8-cycle epoch must share the same `is_transpose`. For a 2-value-operand instruction with `is_xpose_A ≠ is_xpose_B`, microcode splits the fetch into two epochs (16 cy instead of 8 cy) — this is the only scheduling cost of the new flag (§8.3.6).

#### 2.2.6 New PTO instructions

Three instructions native to v2's unified ALU + Acc feedback + microcode pipeline ([`vector4k_v2.md`](vector4k_v2.md) §7.5):

| Mnemonic | Operands | Semantics | Cycle budget |
|----------|----------|-----------|--------------|
| **TINV** | `Tdst+, Tsrc+, num_tiles` | Square matrix inverse via in-tile Gauss–Jordan with Newton–Raphson reciprocal refinement. Up to **128×128 FP32 (16 tiles)**, 64×64 FP8 (1 tile), 32×32 FP32 (1 tile). | ≈ 2·N²·`S_row` + N·`S_col` + 3N beats. **33 K beats / ~33 µs for 128×128 FP32 @ 1 GHz**. |
| **TROWRANGE_MUL** | `Tdst, Tsrc, Xstart, Xend [, Tmask]` | Column-wise product over dynamic row sub-range `[Xstart, Xend)`. `out[c] = ∏_{r=Xstart}^{Xend−1} Tsrc[r, c]`. Optional mask further filters elements. | `1 + S_active + 1 ≤ 10` beats; ~18 cy end-to-end. |
| **TMRGSORT** | `Td0 (values), Td1 (indices), Tsrc, N` | Full-tile bitonic sort over any `N = 2^p` up to **8192** (FP4 tile). Emits sorted values to `D0` and permutation indices to `D1`. Optional mask = partial-sort. | `p(p+1)/2 × ⌈N/256⌉` beats. **220 beats for N=1024 FP32, 36 beats for N=256.** |

Additional encoding notes:

- **TINV multi-tile** uses a 2-bit `log₂(num_tiles)` field in `funct7` to select `num_tiles ∈ {1, 2, 4, 8, 16}`. Operand register fields then encode the **base** tile of each consecutive range.
- **TROWRANGE_MUL** sources `Xstart`, `Xend` from scalar registers via the staging slots `SX`, `SY` ([`vector4k_v2.md`](vector4k_v2.md) §4.3) — these are read at issue time from the scalar GPR file, costing **0 vector-side cycles**.
- **TMRGSORT** uses `N` from a 4-bit immediate field encoding `log₂(N) ∈ {5..13}` (32..8192).

#### 2.2.7 `TSETMETA` (tile metadata setter)

```
  TSETMETA Td, shape.x_imm, shape.y_imm, format_imm
```

A single-cycle, tile-RAT-only instruction that **rewrites the metadata word** of the destination tile's *current* physical mapping without touching its 4 KB payload. Handled at the D2 rename stage similarly to `TILE.MOVE`: no RS entry, no execute stage. The new metadata becomes visible to subsequent instructions consuming `Td`.

Use cases: reshaping a tile produced by `CUBE.DRAIN` (which writes payload but not shape), changing format after `VCVT`, or installing scalar-broadcast metadata before a `VEXPAND`.

#### 2.2.8 Updated vector instruction list (highlights)

The **95-instruction v1 vector ISA** carries forward, with two changes:

1. Each instruction gets a "masked" variant (no new mnemonic — encoded by `has_mask`).
2. `TSORT32` and `TMRGSORT` from v1 are subsumed by the new `TMRGSORT` (§2.2.6). v1's `TSORT32` mnemonic remains as an alias for `TMRGSORT N=32`.

Categories A–M of v1 §2.2.3 are unchanged in semantics. New Category N is added:

**Category N — Numerical / Reconfigurable Compute (new in v2)**

| Mnemonic | Operands | Semantics | Latency |
|----------|----------|-----------|---------|
| TINV | Tdst+, Tsrc+, num_tiles | Matrix inverse (Gauss–Jordan + NR refine) | ~2 K – 33 K beats |
| TROWRANGE_MUL | Tdst, Tsrc, Xstart, Xend [, Tmask] | Range product per column | ≤ 10 beats |
| TMRGSORT | Td0, Td1, Tsrc, log2N [, Tmask] | Bitonic sort, value+index dual retire | 36 – 2 912 beats |
| TSETMETA | Td, shape.x, shape.y, format | Rewrite tile metadata in-place | 0 (rename-only) |

### 2.3 Cube ISA

**Unchanged from v1.** The outerCube MXU and its `CUBE.{CFG,OPA,DRAIN,ZERO,WAIT}` instructions retain their v1 semantics, encoding, and pipeline. The Tile RAT renames cube operands as before.

### 2.4 MTE ISA

**Unchanged in opcode encoding from v1**, but with two implementation changes that are transparent to software:

1. **`TILE.TRANSPOSE` is now a software-optional accelerator.** With per-port `is_transpose` on the TRegFile read (§9.2) and per-beat `tilelet_xpose` in the vector unit (§8.3), most "pre-transpose then consume" patterns become single-instruction with `is_xpose_*` set on the consuming op. `TILE.TRANSPOSE` is retained for cases that need a *materialized* transposed tile to be reused many times across instructions that themselves don't carry the bit, but the v2 MTE transpose buffer (§8.5) shrinks to a small 512 B staging slice (only used during element-level fixup for non-aligned `W` regimes — see [`tregfile4k.md`](tregfile4k.md) §7.5).
2. **All bulk tile stores (`TILE.ST`, `TILE.SCATTER`) acquire a branch tag at dispatch** and are gated through the **Speculative Tile-Store Queue** (STQ) until their tag becomes non-speculative (§11.5). This is invisible at the ISA level but adds 0–6 cycles of latency to a tile store on the speculative path.

### 2.5 Instruction Domain Identification

**Unchanged from v1.** Opcode[6:5] still selects scalar / vector / cube / MTE.

---

## 3. Top-Level Block Diagram

```
 ┌──────────────────────────────────────────────────────────────────────────────────────┐
 │  DAVINCI-v2 CORE                                                                     │
 │                                                                                      │
 │  ┌─────────────────────────────── FRONT-END ──────────────────────────────────────┐  │
 │  │   ┌──────────┐    ┌───────────┐    ┌──────────────────────────────────┐        │  │
 │  │   │  Branch   │───▶│  Fetch    │───▶│  Instruction Buffer (16 entries) │        │  │
 │  │   │ Predictor │    │  Unit     │    │  4-wide dequeue                  │        │  │
 │  │   │ TAGE+BTB  │    │ (L1-I)    │    └──────────┬───────────────────────┘        │  │
 │  │   │ +RAS      │    └──────────┘               │ 4 instr/cy + branch tag         │  │
 │  │   └──────────┘                                ▼                                │  │
 │  │                              ┌───────────────────────────────────────────┐     │  │
 │  │                              │  Decode + Rename (4-wide)                  │     │  │
 │  │                              │  ┌───────┐  ┌──────────────┐               │     │  │
 │  │                              │  │Scalar │  │  Tile RAT     │               │     │  │
 │  │                              │  │ RAT   │  │  (32→256)     │               │     │  │
 │  │                              │  └───┬───┘  └──────┬───────┘               │     │  │
 │  │                              │  ┌───┴─────────────┴────────┐              │     │  │
 │  │                              │  │ Free Lists + Ref Counters  │              │     │  │
 │  │                              │  └───────────────────────────┘              │     │  │
 │  │                              │  ┌───────────────────────────┐              │     │  │
 │  │                              │  │ Checkpoint Store (8 slots) │              │     │  │
 │  │                              │  │ + Branch-tag allocator    │              │     │  │
 │  │                              │  └───────────────────────────┘              │     │  │
 │  │                              │  ┌───────────────────────────┐              │     │  │
 │  │                              │  │ Tile Metadata RAT (32→256)│              │     │  │
 │  │                              │  │  (32 b per phys tile)     │              │     │  │
 │  │                              │  └───────────────────────────┘              │     │  │
 │  │                              └──────────────┬──────────────────────────────┘     │  │
 │  └─────────────────────────────────────────────┼─────────────────────────────────────┘  │
 │                                                │ renamed µops + branch_tag              │
 │  ┌─────────────────────────── DISPATCH ────────┼───────────────────────────────────┐  │
 │  │                                             ▼                                  │  │
 │  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐         │  │
 │  │  │Scalar RS │  │  LSU RS  │  │Vector RS │  │ Cube RS  │  │  MTE RS  │         │  │
 │  │  │(32 entry)│  │(24 entry)│  │(24 entry)│  │(4 entry) │  │(16 entry)│         │  │
 │  │  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘         │  │
 │  └───────┼──────────────┼─────────────┼─────────────┼──────────────┼───────────────┘  │
 │          │              │             │             │              │                  │
 │  ┌───────┼──────────── EXECUTE ───────┼─────────────┼──────────────┼───────────────┐  │
 │  │       ▼              ▼             ▼             ▼              ▼              │  │
 │  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐         │  │
 │  │  │ 4× ALU   │  │  Load /  │  │  VEC-    │  │outerCube │  │   MTE    │         │  │
 │  │  │ 1× MUL   │  │  Store   │  │  4K-v2   │  │   MXU    │  │  Engine  │         │  │
 │  │  │ 1× BRU   │  │  Unit    │  │ 3R/2W    │  │(4096 MAC)│  │(LD/ST/  │         │  │
 │  │  │          │  │  + SSB   │  │ tiles    │  │          │  │G/S/MOVE)│         │  │
 │  │  │          │  │  (24)    │  │ 1 tile/  │  │          │  │+ STQ(8) │         │  │
 │  │  │          │  │          │  │ 8 cy     │  │          │  │         │         │  │
 │  │  └─────┬────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬────┘         │  │
 │  └────────┼─────────────┼─────────────┼─────────────┼──────────────┼─────────────┘  │
 │           │             │             │             │              │                  │
 │  ┌────────┼──────────── COMPLETE ─────┼─────────────┼──────────────┼───────────────┐  │
 │  │  CDB (6 ports, scalar)  +  TCB (4 ports, tile)                                 │  │
 │  └────────────────────────────────────────────────────────────────────────────────┘  │
 │                                                                                      │
 │  ┌──────────────── REGISTER FILES ───────────────────────────────────────────────┐  │
 │  │  ┌──────────────────┐  ┌─────────────────────────────────────────────────────┐ │  │
 │  │  │ Scalar Physical   │  │ TRegFile-4K (with per-port is_transpose)            │ │  │
 │  │  │ Register File     │  │ 256×4KB = 1MB; 8R+8W @ 512B/cy/port                  │ │  │
 │  │  │ 128×64b           │  │ 8-cycle epoch calendar                                │ │  │
 │  │  │ 12R+6W ports      │  │ 32 b metadata per physical tile                       │ │  │
 │  │  │                   │  │ row-mode AND col-mode reads at full 512 B/cy          │ │  │
 │  │  └──────────────────┘  └─────────────────────────────────────────────────────┘ │  │
 │  └──────────────────────────────────────────────────────────────────────────────────┘  │
 │                                                                                      │
 │  ┌──────────────── MEMORY SUBSYSTEM ─────────────────────────────────────────────┐  │
 │  │  ┌──────────┐  ┌──────────┐  ┌──────────┐                                    │  │
 │  │  │  L1-I    │  │  L1-D    │  │  L2 (512 │───▶ External Bus / NoC              │  │
 │  │  │  64 KB   │  │  64 KB   │  │   KB)    │                                    │  │
 │  │  └──────────┘  └────┬─────┘  └──────────┘                                    │  │
 │  │                     ▲                                                        │  │
 │  │            ┌────────┴────────┐                                                │  │
 │  │            │ Spec Store Buf  │  (24 entries, branch-tag gated, §11.4)        │  │
 │  │            │ (SSB)           │                                                │  │
 │  │            └─────────────────┘                                                │  │
 │  │                     ▲                                                        │  │
 │  │            ┌────────┴────────┐                                                │  │
 │  │            │ Spec Tile-Store │  (8 entries, branch-tag gated, §11.5)         │  │
 │  │            │ Queue (STQ)     │  drains TILE.ST / TILE.SCATTER                 │  │
 │  │            └─────────────────┘                                                │  │
 │  │                     ▲                                                        │  │
 │  │                     │ from MTE                                               │  │
 │  └──────────────────────────────────────────────────────────────────────────────┘  │
 └──────────────────────────────────────────────────────────────────────────────────────┘
```

**v2 deltas highlighted:**

- Branch-tag allocator at rename / checkpoint store.
- Tile Metadata RAT (32 b per physical tile) co-located with the Tile RAT.
- VEC-4K-v2 unit replaces v1 vector unit; 3R / 2W tile interface.
- Speculative Store Buffer (SSB, 24 entries) gates scalar stores by branch tag.
- Speculative Tile-Store Queue (STQ, 8 entries) gates MTE bulk stores by branch tag.

---

## 4. Pipeline Overview

The v2 scalar pipeline is the same **12 stages** as v1 (no retire/commit stage). Branch-tag administration adds zero cycles — tags are allocated at D2 (alongside the existing checkpoint allocation) and propagated forward as one extra metadata field per RS entry.

```
 F1 → F2 → D1 → D2 → DS → IS → EX1 → EX2 → EX3 → EX4 → WB
 ├── Fetch ──┤├─ Decode/Rename ─┤├DS┤├IS┤├──── Execute ────┤├WB┤
                       │
                       └── allocate branch_tag ∈ {0..7} for each in-flight branch
                       └── flash-copy {Scalar RAT, Tile RAT, Tile-Meta RAT,
                                       free-list heads, RAS top, SSB head, STQ head}
                                       into checkpoint[tag]
```

### 4.1 Per-stage actions (v2 deltas)

| Stage | v2 deltas |
|-------|-----------|
| **D2 — Rename** | Allocate branch_tag if instruction is a branch; tag propagates to every younger instruction's RS entry. Checkpoint (§5/§6) now also snapshots **SSB head pointer** and **STQ head pointer**. |
| **DS — Dispatch** | Dispatched RS entry includes `branch_tag` (3 b). LSU stores additionally allocate an SSB slot tagged with the same branch_tag. MTE bulk-stores allocate an STQ slot. |
| **IS — Issue** | No change (RS wakeup on tag-match for source operands). |
| **EX — Execute** | LSU stores deposit data + address into the SSB slot, but do **not** commit to L1-D. MTE bulk-stores deposit address-list + data into the STQ slot, but do **not** drain to memory. Both wait for `branch_tag → non-speculative` (§11.4, §11.5). |
| **WB — Writeback** | Unchanged for scalar/tile *register* destinations. Memory-side commits gated on tag-clear from the speculation tracker. |

### 4.2 Pipeline timing — unchanged from v1

Scalar ALU, MUL, LD, branch resolve, and vector instruction timing (epoch-pipelined at 1 tile/8 cy) are all preserved. The only timing change is the variable operand-fetch prologue of VEC-4K-v2 (§8.3.6): `T_fetch ∈ {8 cy, 16 cy}` depending on `is_xpose_*` mix. End-to-end vector-instruction latency is unchanged for uniformly-transposed (or uniformly-non-transposed) operands.

### 4.3 Branch misprediction penalty

| Step | v1 | v2 |
|------|----|----|
| Detection (EX1) | 1 cy | 1 cy |
| Recovery (RAT flash-restore + flush + redirect) | 1 cy | 1 cy |
| **+ SSB / STQ flush** | — | concurrent with RAT restore (§11.4.4) |
| Front-end refill | ~6 cy | ~6 cy |
| **Total mispredict penalty** | **6 cy** | **6 cy** (unchanged) |

The SSB / STQ flush runs in parallel with the RAT flash-restore: both are mask-clear operations on small CAMs, single-cycle.

---

## 5. Front-End: Fetch & Branch Prediction

**Largely unchanged from v1 §5.** Fetch unit, BTB (2048 entries), TAGE (~20 KB), and RAS (16 entries) are identical. Two small additions:

### 5.1 Branch-tag allocator

A small hardware counter at D2 allocates a 3-bit branch_tag for each newly-decoded branch, drawn from the same 8-slot pool used by the v1 RAT-checkpoint store. Allocation policy is round-robin among free slots; when the pool is empty, the rename stage stalls (same condition as v1's checkpoint-pool exhaustion).

The branch_tag is then attached to:

- The branch's own RS entry.
- All RS entries dispatched **after** the branch and **before** any older branch resolves.
- All SSB entries created in the same window.
- All STQ entries created in the same window.
- Free-list pointers in the checkpoint snapshot.

When the branch resolves correctly, the tag is freed and propagated as a "tag-clear" event to all consumers (RS / SSB / STQ). When it mispredicts, the tag becomes the "flush key" — every entry tagged with this branch (or any *younger* branch tag) is invalidated atomically (§11.4.4).

### 5.2 Static hint bit

The compiler may set the conditional-branch funct3's `H` bit (1 = predict taken on TAGE/BTB miss). The hint is consulted only on a predictor cold-miss; once TAGE has trained on the branch, dynamic prediction wins.

---

## 6. Decode & Rename

**Largely unchanged from v1 §6.** All four register renaming structures (Scalar RAT, Tile RAT, Scalar free list, Tile free list) operate identically. Three small additions:

### 6.1 Tile Metadata RAT

A new **32 b × 256 entry** SRAM stores the metadata word for each physical tile. Access pattern:

| Event | Action |
|-------|--------|
| Physical tile allocated (D2 of a producer instruction) | Reset metadata to "uninitialized" sentinel |
| Producer retires | Producer writes metadata word (alongside its 4 KB payload) |
| Consumer reads tile | Consumer reads metadata word from the **first strip**'s arrival at staging (§8.3.3) |

The Tile Metadata RAT is read at D2 alongside the regular Tile RAT lookup; the metadata word is forwarded to the consumer's VEC RS entry (`SOP` staging at issue, [`vector4k_v2.md`](vector4k_v2.md) §4.4).

`TSETMETA` writes the metadata RAT directly at D2, with no execute stage.

**Storage:** 256 × 32 b = 1024 B = ~10 K gate. Read ports: 4 (one per decode slot) + 1 (TCB completion). Write ports: 2 (1 at retire, 1 for `TSETMETA`).

### 6.2 Branch-tag stamping

Every µop entering the rename stage receives the **current** branch_tag (the tag of the youngest unresolved branch ahead of it; or `0xFF` if none). Storing this tag on the µop's RS entry (3 b) lets the misprediction recovery logic identify which entries to flush in one cycle.

### 6.3 Checkpoint extensions

The 8 RAT-checkpoint slots are extended to also snapshot:

- SSB head pointer (5 b).
- STQ head pointer (4 b).
- Tile Metadata RAT delta-vector (4 b "tags last touched" — the metadata RAT itself doesn't need full snapshot because metadata writes are tied to retire, not rename; only the *delta-tag* needs replay).

Updated checkpoint size:

| Field | v1 | v2 |
|-------|----|----|
| Scalar RAT | 224 b | 224 b |
| Tile RAT | 256 b | 256 b |
| Scalar free-list head | 7 b | 7 b |
| Tile free-list head | 8 b | 8 b |
| RAS top pointer | 4 b | 4 b |
| **SSB head pointer** | — | 5 b |
| **STQ head pointer** | — | 4 b |
| **Metadata-delta tag** | — | 4 b |
| **Per-checkpoint total** | ~499 b | **~512 b** |

8 slots × 512 b = **4 KB checkpoint store** (negligible vs. the 1 MB TRegFile). Flash-restore latency stays at **1 cycle**.

---

## 7. Dispatch & Issue

### 7.1 Dispatch (DS)

After rename, each µop dispatches into the appropriate RS along with its branch_tag.

| RS | v1 entries | v2 entries | Sizing rationale |
|----|------------|------------|-------------------|
| Scalar | 32 | 32 | unchanged |
| LSU | 24 | 24 | unchanged |
| **Vector** | 16 | **24** | VEC-4K-v2 entries are wider (3 source tile tags + 2 dest + mask flag + xpose flags + retire mask + scalar staging tags) and multi-cycle ops occupy slots longer (`TINV` up to 33 K beats; `TMRGSORT` up to 2 912 beats — these are fewer in number than elementwise ops, but their long execute time means the RS must absorb more dispatched ops without stalling the front-end) |
| Cube | 4 | 4 | unchanged |
| MTE | 16 | 16 | unchanged |
| **Total** | **92** | **100** | +8 vector RS entries |

### 7.2 Reservation Station Entry Format

**Scalar / LSU RS entry (v2):**

```
 ┌─────────┬──────┬─────┬──────┬──────┬──────┬──────┬──────┬──────┬──────┬───────┬─────┬─────┐
 │ valid   │ age  │ btag│ op   │ psrc1│ rdy1 │data1 │ psrc2│ rdy2 │data2 │ pdst  │ ckpt│ ssb │
 │ (1b)    │ (6b) │(3b) │(8b)  │(7b)  │(1b)  │(64b) │(7b)  │(1b)  │(64b) │ (7b)  │(3b) │(5b) │
 └─────────┴──────┴─────┴──────┴──────┴──────┴──────┴──────┴──────┴──────┴───────┴─────┴─────┘
   ~178 bits per scalar/LSU entry (v1: ~170; +8 b for branch_tag and SSB index)
```

**Tile-domain RS entry (v2 — Vector / Cube / MTE):**

```
 ┌──────┬──────┬─────┬──────┬───────┬──────┬───────┬──────┬───────┬──────┬───────┬──────┬───────┬─────┬─────┬──────┐
 │valid │ age  │ btag│ op   │ptsrc1 │trdy1 │ptsrc2 │trdy2 │ptsrc3 │trdy3 │ptdst1 │ptdst2│pscalar│ ckpt│ stq │meta_v│
 │(1b)  │(6b)  │(3b) │(8b)  │ (8b)  │(1b)  │ (8b)  │(1b)  │ (8b)  │(1b)  │ (8b)  │ (8b) │(8b)*  │(3b) │(4b) │(1b)  │
 │      │      │     │      │ +xp   │      │ +xp   │      │ +xp   │      │       │      │       │     │     │      │
 │      │      │     │      │ (1b)  │      │ (1b)  │      │ (1b)  │      │       │      │       │     │     │      │
 └──────┴──────┴─────┴──────┴───────┴──────┴───────┴──────┴───────┴──────┴───────┴──────┴───────┴─────┴─────┴──────┘
   ~92 bits per tile-domain entry (v1: ~80)
   (* pscalar field is double-wide: 7 b GPR tag + 1 b GPR-vs-IMM selector;
    sized to carry up to 2 scalar GPR tags for VEC-4K-v2 SX/SY operands
    encoded compactly in `pscalar` + immediate field of `op`)
```

New fields vs. v1:

| Field | Width | Purpose |
|-------|-------|---------|
| `btag` | 3 b | Branch tag of the youngest unresolved branch ahead of this µop. Used for one-cycle CAM-clear on mispredict. |
| `ptsrc3 + xp` | 8+1 b | Third source tile (the `C` mask in VEC, or 3rd cube operand). xp = is_xpose forwarded to TRegFile. |
| `xp_A`, `xp_B`, `xp_C` | 1 b each | Per-operand TRegFile-side `is_transpose` bits (forwarded to TRegFile read port at issue) |
| `ptdst2` | 8 b | Second destination tile (for dual-retire ops like `TMRGSORT`, `TROWARGMAX`) |
| `ssb` | 5 b | Speculative-Store-Buffer index (LSU stores only) |
| `stq` | 4 b | Speculative Tile-Store Queue index (MTE bulk stores only) |
| `meta_v` | 1 b | Metadata-valid flag (set when the producer's metadata write has propagated) |

### 7.3 Wakeup Logic

CDB wakeup behaves as in v1: 6 CDB ports broadcast 7 b scalar tags + 64 b data; every RS entry compares against psrc1/psrc2.

TCB wakeup (v2 widened): 4 TCB ports broadcast 8 b tile tags. Tile-domain RS entries compare against ptsrc1/ptsrc2/ptsrc3 (3 sources × 4 ports = 12 comparators per entry × 24 vector entries = 288 comparators on the vector RS, vs. v1's 192).

### 7.4 Select Logic

Unchanged from v1 §7.4: oldest ready instruction issues per functional unit per cycle.

### 7.5 Dispatch Stall Conditions (v2 additions)

| Condition | Recovery |
|-----------|----------|
| Target RS is full | Wait for RS entry to be freed |
| Scalar / Tile free list empty | Wait for refcount-driven free |
| All checkpoint / branch-tag slots occupied | Wait for an in-flight branch to resolve |
| **SSB full** (LSU stores blocked) | Wait for SSB entry to drain (oldest non-speculative store commits) |
| **STQ full** (MTE bulk stores blocked) | Wait for STQ entry to drain |

---

## 8. Execution Units

### 8.1 Scalar Unit

**Unchanged from v1 §8.1.** 4× ALU, 1× MUL/DIV, 1× BRU; same latencies and operations.

### 8.2 Load/Store Unit (LSU)

The LSU pipeline is identical to v1 in terms of address calculation, TLB access, cache lookup, and L1-D MSHRs. The **store path** is the only structural change: stores no longer commit directly to L1-D; instead, they pass through a **Speculative Store Buffer** (SSB) that gates them by branch tag.

#### 8.2.1 Speculative Store Buffer (SSB) — overview

```
  ┌────────────────────────────────────────────────────────────┐
  │  Speculative Store Buffer  (SSB, 24 entries)              │
  │                                                            │
  │  Each entry:                                               │
  │   ┌──────┬──────┬──────┬──────┬──────┬──────┬──────┐       │
  │   │valid │ btag │ addr │ data │ size │ alloc│ drain│       │
  │   │ (1b) │ (3b) │ (40b)│(128b)│ (3b) │_age  │_rdy  │       │
  │   │      │      │      │      │      │ (6b) │ (1b) │       │
  │   └──────┴──────┴──────┴──────┴──────┴──────┴──────┘       │
  │                                                            │
  │   Total: 24 × ~182 b ≈ ~550 B                              │
  └────────────────────────────────────────────────────────────┘
```

| Field | Width | Purpose |
|-------|-------|---------|
| `valid` | 1 b | Slot is occupied |
| `btag` | 3 b | Branch tag inherited from the producing store µop |
| `addr` | 40 b | Physical address (post-TLB) |
| `data` | 128 b | Up to 16 B of store data (covers byte/half/word/dword) |
| `size` | 3 b | 1/2/4/8/16 B store width |
| `alloc_age` | 6 b | Sequence number for in-order drain |
| `drain_rdy` | 1 b | Set when `btag = 0xFF` (non-speculative); the entry can drain to L1-D |

**Capacity:** 24 entries — a 50% increase over v1's 16-entry "store buffer". The increase is driven by the speculation window: at maximum, all 8 branch tags can have their stores in flight, and each branch may generate several stores. With branch-prediction accuracy ~95% and a typical kernel mix of 20–30% memory ops, 24 entries provide ~10 cycles of buffering at peak issue (2 stores/cycle into a smaller pool would risk dispatch stall).

#### 8.2.2 SSB drain policy

```
  Tag-clear from speculation tracker (when branch resolves correctly):
    For each SSB entry e:
      if e.btag == cleared_tag:
         e.btag ← (next-older unresolved branch tag, or 0xFF if none)
         if e.btag == 0xFF: e.drain_rdy ← 1

  Drain to L1-D (1 store/cycle, oldest-first among drain_rdy entries):
    Pick oldest e with valid && drain_rdy
    Issue write to L1-D pipeline (with 4-cy occupancy, like v1 store)
    On completion: e.valid ← 0; SSB entry returned to free pool

  Mispredict (tag invalidation):
    For each SSB entry e:
      if e.btag is younger than (or equal to) mispredicted_tag:
         e.valid ← 0   (entry invalidated; never reaches L1-D)
```

The SSB head pointer is the **next allocation slot** (FIFO discipline for in-flight ordering); the head is snapshotted into each branch checkpoint at D2.

#### 8.2.3 Store-to-load forwarding from SSB

Loads still forward from the SSB on address match (same semantic as v1's store-to-load forwarding). Only loads with the **same or younger** branch_tag are eligible to forward — a load on a different speculation path must NOT forward from a store on its own path; instead, the load goes to L1-D.

```
  Load (addr, btag_load) forwards from SSB entry e iff:
    e.valid && e.addr == addr && e.size matches &&
    e.btag is "ancestor" of btag_load
            ──────────────────────
            i.e. e.btag is in the chain of unresolved branches
            that branch_tag_load also depends on.
```

Implementation: the speculation tracker (§11.3) maintains a 8 × 8 ancestry bitmap; load-forwarding eligibility is one bitmap-lookup + AND.

#### 8.2.4 SSB area

| Block | Area |
|-------|------|
| 24 × 182 b flip-flop array | ~24 × 1.8 K gate ≈ ~45 K gate |
| Address CAM (24 × 40 b for forwarding) | ~30 K gate |
| Branch-tag ancestry bitmap (8 × 8 b) | ~1 K gate |
| Drain FSM | ~2 K gate |
| **Total** | **~80 K gate** (~0.02 mm² @ 5 nm) |

### 8.3 Vector Unit — VEC-4K-v2

The vector unit is a full re-architecting of v1's vector unit, specified in detail in [`vector4k_v2.md`](vector4k_v2.md). This section summarizes the integration into the Davinci-v2 core; refer to the standalone document for full datapath, microcode, and worked microcode examples.

#### 8.3.1 High-level structure

```
  ┌──────────────────────────────────────────────────────────────────────┐
  │  VEC-4K-v2 Unit                                                       │
  │                                                                      │
  │  TRegFile-4K read ports R0, R4 (with is_transpose)                   │
  │       │  512 B/cy each                                               │
  │       ▼                                                              │
  │  ┌──────────┐  ┌──────────┐  ┌──────────┐                            │
  │  │ SA       │  │ SB       │  │ SC       │  ← staging registers       │
  │  │ 4 KB     │  │ 4 KB     │  │ 4 KB     │     (24 × 1R1W SRAM        │
  │  │ SRAM     │  │ SRAM     │  │ SRAM     │      macros total, §9.1    │
  │  │ +meta    │  │ +meta    │  │ +meta    │      of vector4k_v2.md)    │
  │  └─────┬────┘  └─────┬────┘  └────┬─────┘                            │
  │        │ 512 B/cy    │ 512 B/cy   │ 1-bit-per-lane mask              │
  │        ▼             ▼            ▼                                  │
  │  ┌───────────────────────────────────────────────────────────┐       │
  │  │  Stage (A): align / unpack / permute (per operand slot)    │       │
  │  └─────────────────────────────┬───────────────────────────────┘       │
  │                                ▼                                      │
  │  ┌───────────────────────────────────────────────────────────┐       │
  │  │  Stage (B): 128 compute groups (FP32-equivalent FMA core   │       │
  │  │             with intra-group format widener), Acc feedback,│       │
  │  │             per-lane mask gate, 256-lane shuffle+CAS       │       │
  │  │             primitive (for TMRGSORT)                        │       │
  │  └───────────────────────────────────────────────────────────┘       │
  │                                                                       │
  │  ┌───────────────────────────────────────────────────────────┐       │
  │  │  Acc (256 × 32 b × 2 ping-pong, parity-banked LO/HI)       │       │
  │  └───────────────────────────────────────────────────────────┘       │
  │                                                                       │
  │  ┌──────────────┐    ┌──────────────┐                                 │
  │  │ Pack → D0    │    │ Pack → D1    │                                 │
  │  │ → W0 (512B/cy)│    │ → W4 (512B/cy)│                               │
  │  └──────────────┘    └──────────────┘                                 │
  │                                                                       │
  │  Microcode ROM: ~64 b × 64 beats × 256 programs ≈ 128 Kb              │
  └──────────────────────────────────────────────────────────────────────┘
```

#### 8.3.2 Key parameters

| Parameter | Value | Note |
|-----------|-------|------|
| Compute datapath width | 512 B / 128-lane FP32-equivalent | unchanged from v1 |
| TRegFile read ports used | R0 (Port A), R4 (Port B) | per epoch; 8 cy/epoch |
| TRegFile write ports used | W0 (D0), W4 (D1) | per epoch |
| Staging: SA, SB, SC payload | 4 KB each; 1R1W SRAM (recommended) | 24 × `512 b × 16 × 1R1W` macros |
| Compute groups | 128 (format-independent) | Per-format SIMD width: FP32×1, FP16×2, FP8×4, FP4×4(×2 sub-beats) |
| Accumulator | 256 × 32 b × 2 ping-pong, LO/HI parity-banked | `N_run = 512` |
| Microcode ROM | ~128 Kb (64 b × ~64 beats × ~256 programs) | regenerable per ISA version |

#### 8.3.3 Tile metadata flow

Each VEC-4K-v2 instruction at D2 rename consults the **Tile Metadata RAT** (§6.1) for each source tile and forwards `(shape.x, shape.y, format)` to the VEC RS entry. At issue, the metadata is propagated into the `SOP` staging register ([`vector4k_v2.md`](vector4k_v2.md) §4.4) where it stays stable for the entire compute phase. The destination tile's metadata is **derived** from the operation's semantics and `retire_format_*` fields (e.g. `VCVT` produces metadata with `format = retire_format_0`, same shape as source).

For backward-compatibility with v1 binaries, a tile that has never been written by a v2 instruction (and therefore has no explicit metadata) defaults to `(shape.x, shape.y, format) = (16, 64, FP32)` — the canonical v1 4 KB tile interpretation.

#### 8.3.4 Microcode beat machine

Compute is driven beat-by-beat from a microcode ROM (`SOP.ucode_base`, `SOP.ucode_len`). Each beat word (~64 b) names per ALU operand slot:

- `src` ∈ {SA, SB, ACC_READ_LO, ACC_READ_HI, SX_broadcast, SY_broadcast, IMM_ZERO}
- `s` (strip index 0..7)
- `tilelet_xpose` (per-beat, primary transpose mechanism)
- `mask_src` ∈ {SC_mask, IMM_ALL_ONES, IMM_FROM_SOP}
- `alu_op` ∈ {ADD, SUB, MUL, FMA, FNMA, MAX, MIN, CMP, AND, XOR, PASS_A, PASS_B, SELECT, RECIP, RSQRT, SHUFFLE_CAS_UP, SHUFFLE_CAS_DOWN, …}
- `acc_op` ∈ {NONE, INIT, ACCUM, MERGE_STAGE, READOUT}
- `wr_en_D0`, `wr_en_D1`

Microcode is keyed by `(opcode, format, W-regime, R-regime)`; the ROM is regenerable in software per ISA version (no RTL change).

#### 8.3.5 Operand-fetch prologue

Operand fetch is **variable-length** ([`vector4k_v2.md`](vector4k_v2.md) §6):

| `N_val` | `is_xpose` mix | `has_mask` | `T_fetch` (best/worst) |
|--------:|----------------|-----------:|------------------------:|
| 1 | any | 0 or 1 | **8 / 15 cy** |
| 2 | uniform | 0 or 1 | **8 / 15 cy** |
| 2 | mixed (R2 penalty) | 0 or 1 | **16 / 23 cy** |

The mask fetch (1–2 strips of `SC`) piggybacks on an idle port cycle within a value-tile epoch and never extends the prologue in the common case.

#### 8.3.6 Per-operand `is_transpose` and per-beat `tilelet_xpose`

VEC-4K-v2 forwards each operand's `is_xpose` bit (latched at D2 rename, present in the RS entry §7.2) to the assigned TRegFile read port at issue time. The TRegFile delivers either row-mode or col-mode strips; the staged content reflects the requested mode.

Per-beat `tilelet_xpose` (microcode bit per operand slot per beat) re-transposes the staged tile inside the staging register's diagonal-skew read datapath at no scheduling cost. Most reduction kernels use `tilelet_xpose` exclusively (TRegFile-side bit defaults to 0); the TRegFile-side bit is reserved for cases where the same transposed view is reused many times.

#### 8.3.7 Latency table (selected ops)

| Op | Format | Shape | Latency |
|----|--------|-------|---------|
| VADD / VMUL / VFMA (full tile) | any | 1024..8192 elements | 16 cy (8 fetch + 8 retire); throughput 1/8 cy |
| VADD masked | any | any | same as unmasked (mask piggybacks) |
| VFMA with `is_xpose_A ≠ is_xpose_B` | any | any | **24 cy** (16 fetch + 8 retire); throughput 1/16 cy |
| VROWSUM (wide, R=8 C=128 FP32) | FP32 | 8×128 | 16 cy fetch + 13 compute + 8 retire ≈ **37 cy** (recommended baseline, no cross-group tree) |
| VROWSUM (alt config with cross-group tree) | FP32 | 8×128 | 16 + 9 + 8 = **33 cy** |
| VCOLSUM (wide) | FP32 | 8×128 | 8 + 9 + 8 = **25 cy** |
| TMRGSORT N=256 FP32 | FP32 | 256 elements | 8 + 36 + 8 = **52 cy** |
| TMRGSORT N=1024 FP32 (1 tile) | FP32 | 1024 elements | 8 + 220 + 8 = **236 cy** |
| TINV 32×32 FP32 (1 tile) | FP32 | 32×32 | 8 + ~2 200 + 8 = **~2.2 K cy** |
| TINV 128×128 FP32 (16 tiles) | FP32 | 128×128 | ~120 cy fetch + ~33 K + 8 retire = **~33 K cy** |
| TROWRANGE_MUL 8 strips | FP32 | 8×128 | 8 + 10 + 8 = **26 cy** |

#### 8.3.8 TRegFile-4K port usage

A single VEC-4K-v2 instruction occupies:

- 1–2 TRegFile read ports for `T_fetch` cycles (R0 + optional R4)
- 1–2 TRegFile write ports for the 8-cycle retire epoch (W0 + optional W4 if `retire_mask = 2'b11`)

**Epoch-pipelined throughput** is preserved: the retire epoch of instruction N overlaps with the fetch epoch of instruction N+1 on independent ports. The per-port allocation table (§9.2.4) is unchanged from v1 in topology — VEC-4K-v2 happens to use the same R0/R4 + W0/W4 binding as v1.

#### 8.3.9 Speculation handling

VEC-4K-v2 instructions are speculation-safe **inherently**: their only "external" side effect is a tile write to TRegFile-4K, which is renamed and reference-counted (§10.5). On branch mispredict:

- VEC RS entries with `btag` in the misprediction's flush set are invalidated.
- In-flight compute beats are flushed within 1 cycle (the staging-register state is overwritten by the next instruction's operand fetch).
- Destination physical tiles (allocated at D2) are returned to the tile free list via free-list-head-pointer restore from the checkpoint.

**No special speculation hardware is needed inside the vector unit.** The flush converges to a quiescent state in `T_fetch + max_beat_count` cycles in the worst case (a long-running `TINV` or `TMRGSORT` taking the full hit), but this is bounded by the number of in-flight vector ops (≤ 24 RS entries) and does not stall the front-end's recovery.

### 8.4 Cube Unit (outerCube MXU)

**Unchanged from v1 §8.4.** The cube unit benefits indirectly from the TRegFile-4K `is_transpose` enhancement: software can now feed the cube either row-major or col-major B-operand tiles by setting `is_xpose` on the cube's B-operand tile-RAT entries (the cube pipeline controller propagates the bit to TRegFile read ports R1–R4), eliminating the need for `TILE.TRANSPOSE` predecessors in many GEMM kernels. The cube ALU and accumulator are unchanged.

### 8.5 MTE Unit

The MTE unit retains the v1 architecture (§8.5 of v1) for `TILE.LD`, `TILE.GATHER`, `TILE.ZERO`, `TILE.COPY`, `TILE.GET`, `TILE.PUT`, and `TILE.MOVE`. Three v2 changes:

#### 8.5.1 `TILE.TRANSPOSE` — reduced footprint

Because TRegFile-4K can deliver col-mode reads directly (§9.2), most "transpose then consume" patterns are subsumed by the consumer's `is_xpose` bit. The dedicated 4 KB MTE transpose buffer of v1 shrinks to a small **512 B element-level fixup buffer** used only for the non-aligned `W ∈ {128, 256, 1024, 2048, 4096}` regimes that [`tregfile4k.md`](tregfile4k.md) §7.5 leaves to downstream consumers. (For these regimes, the chunk-grid transpose at 64 B granularity is not element-level valid, and `TILE.TRANSPOSE` materializes an element-correct transpose in a new physical tile.)

| MTE TILE.TRANSPOSE behaviour | v1 | v2 |
|------------------------------|-----|-----|
| 4 KB transpose buffer | required | replaced by 512 B fixup SRAM |
| Latency | 16 cy | 16 cy (unchanged) |
| Use case | universal | rare — only when materializing a transposed tile for reuse across many instructions that don't carry `is_xpose` |

#### 8.5.2 Speculative Tile-Store Queue (STQ)

`TILE.ST` and `TILE.SCATTER` allocate an STQ entry at dispatch (alongside the regular MTE RS entry):

```
  STQ entry (8 entries total):
    valid (1b) | btag (3b) | base_addr (40b) | tile_phys_idx (8b) |
    stride (40b) | scatter_idx_phys (8b) | size_log2 (3b) | drain_rdy (1b)
    + meta_v (1b)
```

Total STQ size: 8 × ~110 b ≈ ~110 B. Drain logic mirrors the SSB:

- **Tag clear** (branch resolves correctly): `btag` is updated; if it reaches `0xFF`, `drain_rdy` is set, and the STQ controller can issue the actual memory write.
- **Mispredict**: STQ entries with `btag` younger or equal to the mispredicted tag are invalidated. The corresponding tile data — still resident in TRegFile-4K — is freed via the normal Tile RAT refcount path; no memory write was issued.
- **Drain**: oldest `drain_rdy` entry begins streaming the tile from TRegFile through the MTE memory pipeline. Drain is overlapped with subsequent MTE operations.

**Why only 8 entries?** Bulk tile stores are infrequent compared to scalar stores: a typical kernel issues 1 `TILE.ST` per ~10–50 scalar stores. The 8 entries provide ~64 cycles of buffering at peak issue (1 TILE.ST/4 cy), enough to absorb a burst at the end of a kernel without causing dispatch stall.

**Why STQ is separate from SSB:** the TILE.ST data payload (4 KB) cannot reasonably be captured in the SSB's flip-flop register — it must remain resident in TRegFile-4K. The STQ stores only the *intent* (address + tile-pointer + branch_tag) and triggers the memory write on commit.

#### 8.5.3 STQ area

| Block | Area |
|-------|------|
| 8 × 110 b flip-flop array | ~10 K gate |
| Branch-tag ancestry check (shared with SSB) | ~0 (reuses SSB's bitmap) |
| Drain controller FSM | ~2 K gate |
| **Total** | **~12 K gate** (~0.003 mm² @ 5 nm) |

#### 8.5.4 MTE RS entry per instruction (v2 update)

The `TILE.ST` and `TILE.SCATTER` entries gain a 4 b STQ index. Other MTE instructions are unaffected.

| Instruction | STQ allocation | Notes |
|-------------|----------------|-------|
| TILE.LD, TILE.GATHER, TILE.ZERO | — | tile-write only, refcount-managed; no memory side effect, fully recoverable via Tile RAT |
| TILE.ST, TILE.SCATTER | **STQ slot** | held in STQ until non-speculative |
| TILE.COPY, TILE.MOVE, TILE.TRANSPOSE | — | tile-internal, fully recoverable |
| TILE.GET | — | scalar GPR result; recoverable via Scalar RAT + ref-count |
| TILE.PUT | — | tile-write (RMW); recoverable via Tile RAT |

---

## 9. Register Files

### 9.1 Scalar GPR Physical Register File

**Unchanged from v1 §9.1.** 128 × 64 b, 12R + 6W, flip-flop array.

### 9.2 TRegFile-4K (with per-port `is_transpose`)

The TRegFile-4K is the physical tile register file for vector, cube, and MTE. Full specification: [`tregfile4k.md`](tregfile4k.md). v2 highlights below.

#### 9.2.1 Tile metadata storage

A new **256 × 32 b SRAM** sits alongside the 1 MB tile data array, holding `(shape.x, shape.y, format)` per physical tile. Read ports: 4 (decode) + 1 (TCB completion). Write ports: 2 (retire + `TSETMETA`). Storage: 1024 B = ~10 K gate.

The metadata is **physically distinct** from the 4 KB tile payload but is read together at the **first strip** of an operand fetch (§4.4 of [`vector4k_v2.md`](vector4k_v2.md)) so that the consumer's microcode program can be selected based on `(format, R, C)` before the second strip arrives.

#### 9.2.2 Per-port `is_transpose` flag

Each of the 8 read ports (R0–R7) accepts a 1-bit `is_transpose` flag double-registered alongside the 8-bit `reg_idx`. The flag is latched at the epoch boundary and held constant for the entire 8-cycle epoch.

| `is_transpose` | Strip delivery | Bank pattern per cycle |
|----------------|----------------|------------------------|
| 0 (row-mode) | strip `s` = chunk-grid row `s` (contiguous bytes `s·512 .. s·512+511`) | 8 banks of one group |
| 1 (col-mode) | strip `s` = chunk-grid column `s` (8 × 64 B chunks across all 8 groups along the wrapped diagonal) | 1 bank per group |

Both modes deliver **512 B/cy** through the 8-cycle epoch — same throughput, no extra latency. The diagonal skew layout `bank_id = 8·g + ((l + g) mod 8)` is what makes col-mode bank-conflict-free.

**Scheduling rule R2** ([`tregfile4k.md`](tregfile4k.md) §6): the 8 active read ports of any 8-cycle epoch must share the same `is_transpose` value. Mixed-mode reads in the same epoch collide on the 1R-port SRAM banks. This rule is enforced at the dispatch / port-allocation stage; if a vector instruction's operand `is_xpose_A ≠ is_xpose_B`, the operand-fetch prologue automatically splits into two epochs (16 cy instead of 8 cy, §8.3.6).

#### 9.2.3 Hardware delta vs. v1 TRegFile-4K

| Component | v1 | v2 |
|-----------|----|----|
| 64 SRAM banks (256×512b each) | yes | yes (unchanged) |
| Diagonal skew bank map | (introduced together) | yes |
| Per-port pending+active address registers | 1 reg_idx × 2 | (1 reg_idx + 1 is_transpose) × 2 — adds **1 b × 2 × 8 ports = 16 FF** |
| Read-port datapath: bank-select mux | 1 option (row-mode) | **2 options (row OR col)** — small 2-to-1 mux per port + col-mode address generator (`bank_i = 8·i + (p+cy+i) mod 8`) |
| Read-port output rotator | 8-way 64 B (always active in row-mode) | **8-way 64 B, active only in row-mode**; bypassed in col-mode |
| Metadata SRAM (256 × 32 b, 4R/2W) | — | **+10 K gate** |
| Write-side | unchanged | unchanged |
| Latency / throughput per port | 1 reg_idx / 8 cy | 1 reg_idx + 1 is_transpose / 8 cy (same epoch) |

**Total v2 delta**: ~12 K gate (mostly metadata SRAM + col-mode address generator). The transpose capability adds **zero SRAM duplication** and **zero latency** to the basic read path.

#### 9.2.4 Port allocation

Port assignment across vector, cube, and MTE units is **identical to v1 §9.2** (table reproduced below for reference). The introduction of `is_transpose` does not change which port serves which client; it only changes the data delivery order on each read port.

| Port | Cube active — MXFP4/HiFP4 | Cube active — FP16/BF16/FP8 | Cube idle |
|------|----------------------------|------------------------------|-----------|
| R0 | Cube A (1 tile/epoch) | Cube A (1 tile/epoch) | VEC-4K-v2 / MTE — free |
| R1–R4 | Cube B operands | Cube B (R1–R2) | VEC-4K-v2 / MTE — free |
| R5–R7 | Vector / MTE | Vector / MTE | Vector / MTE — free |
| W0 | Cube C drain | Cube C drain | VEC-4K-v2 / MTE — free |
| W1–W7 | Vector / MTE | Vector / MTE | Vector / MTE — free |

VEC-4K-v2 binding: **R0 (Port A, with `is_xpose_A`)**, **R4 (Port B, with `is_xpose_B`)**, **W0 (D0)**, **W4 (D1)**. Mask `C` rides on whichever value-tile read port is idle (1–2 strips per fetch).

---

## 10. Out-of-Order Execution Model — Foundations

(This section recapitulates the ROB-less OoO model from v1 §10. The new speculation-recovery mechanism is in **§11**.)

### 10.1 Core principles (unchanged)

1. **OoO dispatch, OoO execution, OoO completion.** Results commit to the physical register file as soon as execution completes; no in-order retirement stage.
2. **False dependencies (WAW, WAR) eliminated by register renaming.** Both Scalar RAT (32→128) and Tile RAT (32→256) — plus the new Tile Metadata RAT — assign each destination to a unique physical register/tile.
3. **True dependencies (RAW) resolved by tag-based wakeup.** Scalar via CDB, tile via TCB.
4. **Branch recovery via RAT checkpoints.** On mispredict, three RATs (Scalar, Tile, Tile-Metadata) are flash-restored in 1 cycle; all younger instructions are flushed. v2 extends this with branch-tag-driven SSB / STQ flush (§11).
5. **Physical registers freed by reference counting.** Scalar (4 b refcount), tile (3 b refcount). Same as v1.

### 10.2 Instruction Lifecycle (unchanged from v1 §10.2)

### 10.3 Register Alias Table (RAT) Operation

Three RATs in v2 (vs. two in v1):

- Scalar RAT (32 → 128) — unchanged from v1
- Tile RAT (32 → 256) — unchanged in geometry; receives one additional companion structure:
- **Tile Metadata RAT** (256 × 32 b, §6.1, §9.2.1) — a sibling structure that holds `(shape.x, shape.y, format)` per physical tile. It is *not* renamed in the same sense as the Tile RAT; it is updated by retire-time writes from producing instructions.

### 10.4 Common Data Bus / Tile Completion Bus

- CDB: 6 ports (4 ALU + 1 MUL/LSU + 1 TILE.GET) — **unchanged from v1**.
- TCB: 4 ports (Vector, Cube, MTE, MTE) — **unchanged in port count**, but with one additional payload bit per port indicating "metadata committed" (tells consumers that the tile metadata has been written and is safe to read).

### 10.5 Reference counting

**Unchanged from v1 §10.5.** 4 b refcount per scalar physical register, 3 b refcount per physical tile. Lifecycle: MAPPED → ORPHAN → FREE.

### 10.6 Branch recovery — pointer to §11

The basic v1 branch-recovery sequence (RAT flash-restore, free-list-head restore, RS flush) is preserved. **Section 11 specifies the additional mechanisms required for safe speculative execution beyond branch prediction**: SSB / STQ flush, branch-tag ancestry tracking, and the proof that this set of mechanisms is **sufficient to keep architectural state correct without a Reorder Buffer**.

---

## 11. Speculative Execution Recovery Without a Reorder Buffer

> **Question:** The v1 design eliminates the Reorder Buffer (ROB) by leveraging the no-precise-exception envelope, using the Reservation Station + reference-counting + RAT-checkpoint trio for OoO execution. v2 adds branch-prediction-driven **speculative execution** to extend the OoO window past unresolved branches. Can we do this safely — i.e., guarantee that a misspeculated path **never** corrupts architectural state — **without** introducing a ROB?
>
> **Answer: Yes, with two additional structures (Speculative Store Buffer and Speculative Tile-Store Queue) plus a small Branch-Tag Ancestry Tracker. This section proves it and details the mechanisms.**

### 11.1 What the ROB traditionally does

Textbook OoO processors use a Reorder Buffer to provide three services bundled together:

1. **Precise architectural state** — every instruction completes (writes to architecturally-visible state) **in program order**. On exception, the ROB lets the processor identify the exact instruction that faulted and discards everything younger.
2. **Speculative recovery for memory-side effects** — stores remain in the ROB (or a coupled store queue) until they retire in order; mispredicted-path stores are simply not retired.
3. **Resource freeing in program order** — physical registers / tiles are returned to the free list when the consuming arch-reg's old mapping retires.

The Davinci-v1 design noted that **service (1) is unneeded** in the run-to-completion AI-kernel envelope, that **service (3) is replaced by reference counting**, and that **service (2) does not arise** because the processor doesn't speculate past an unresolved branch (it instead flushes the pipeline at every mispredict — only flushing **non-stored** state, since v1 stores commit to L1-D OoO once the producing branch has resolved).

The v1 caveat — *"don't speculate past an unresolved branch"* — is restrictive: it limits the OoO window to the time between branches, which is typically only 5–10 instructions in tight scalar loops. v2 lifts this restriction by allowing instructions younger than an unresolved branch to dispatch, issue, and execute speculatively. **Doing this correctly requires service (2) to be re-introduced — but only service (2), not (1) or (3).**

### 11.2 Categorizing speculative state

When an instruction executes speculatively (i.e. depends on an unresolved branch), its effects fall into one of three classes:

| Class | What it touches | Who recovers it on flush | Currently handled by |
|-------|-----------------|---------------------------|----------------------|
| **A. Renamed register / tile state** | Writes to a physical scalar register (P0–P127), a physical tile (PT0–PT255), or a metadata RAT entry | Returns to free list once orphan + refcount=0 (no ROB needed) | RAT checkpoint flash-restore + refcount + free-list-head restore |
| **B. Pipeline state** | Occupies an RS slot; in flight in EX stages; consumes CDB/TCB cycles | Branch-tag CAM-clear invalidates the RS entry; in-flight EX is flushed at WB | Branch-tag stamping at D2 (§5.1, §6.2) |
| **C. Externally-visible state** | Writes to L1-D / L2 cache, scatters to memory, MMIO accesses, fences/barriers, cross-core observable ordering | **Cannot be recovered** once it leaves the core | **NEW: SSB (§11.4) and STQ (§11.5) gate these to never *reach* memory until non-speculative** |

Class A is fully handled by the v1 mechanisms — the RAT flash-restore + refcount path is **logically equivalent** to a ROB's "retire on commit" for register/tile state, because:

- A misspeculated destination is simply *never* the architectural mapping (RAT restore overwrites it).
- A misspeculated source-register read either consumed a valid value (which produces no side effect, just a wasted compute) or stalled until the branch resolved (in which case the RS entry is flushed before the read).

Class B is fully handled by the branch-tag CAM-clear: every RS / reservation-station entry tagged with the mispredicted branch (or any *younger* branch in its dependency chain) is invalidated in one cycle, and the corresponding physical-register/tile destinations roll back to the free list.

**Class C is the only class that needs a new mechanism**, and the new mechanism only needs to ensure that any class-C effect *of a speculative instruction* is **delayed** until the instruction is known to be on the correct path. This is the core insight: **we don't need a ROB to track instruction order globally; we only need to gate the externally-visible side effects by branch tag**.

### 11.3 The Branch-Tag Speculation Tracker

The processor maintains a **branch-tag tracker** — a small structure with three components:

```
  ┌───────────────────────────────────────────────────────────────┐
  │  Speculation Tracker  (always 8 active branch tags max)       │
  │                                                               │
  │  (a) Tag-state vector: 8 × 2 b state                          │
  │       0 = free                                                │
  │       1 = speculative (branch not yet resolved)               │
  │       2 = correct (branch resolved correctly; tag draining)   │
  │       3 = wrong (branch mispredicted; tag flushing)           │
  │                                                               │
  │  (b) Ancestry bitmap: 8 × 8 b symmetric matrix                │
  │       anc[i][j] = 1 iff branch i is an ancestor of branch j   │
  │       (i.e. j was fetched while i was unresolved)             │
  │       Allocated at D2 when each new branch is renamed.        │
  │                                                               │
  │  (c) Instruction-tag map: maintained in RS entries' btag      │
  │       field (3 b each). Already part of every RS entry §7.2.  │
  └───────────────────────────────────────────────────────────────┘
```

#### 11.3.1 Tracker operation

- **Branch enters D2:** allocate a free tag `t`. Set `state[t] = speculative`. Snapshot ancestry: `anc[t][:] = anc[parent_t][:] | (1 << parent_t)` where `parent_t` is the youngest still-speculative branch's tag (or all zeros if none). The branch's RS entry stamps `btag = t`. All instructions following until the next branch also stamp `btag = t`.
- **Branch resolves correctly at EX1:** set `state[t] = correct`. The drain logic propagates this to SSB / STQ entries, which then clear their `btag` (replacing it with the next-older speculative tag, if any). When `btag = 0xFF` (no older speculative branches remain), the entry becomes `drain_rdy`.
- **Branch mispredicts at EX1:** set `state[t] = wrong`. **Atomically** clear all RS / SSB / STQ entries with `btag` matching `t` *or* matching any descendant of `t` (computed from `anc[t][:]`). The RAT flash-restore from checkpoint completes in the same cycle.
- **Tag freed:** when an entry transitions from speculative to non-speculative *and* fully drains (or is invalidated), the corresponding tag's allocation in the tracker is released. A tag is freed when no in-flight entry references it.

#### 11.3.2 Tracker area

| Component | Size | Gate count |
|-----------|------|------------|
| Tag-state vector | 8 × 2 b = 16 FF | ~200 gate |
| Ancestry bitmap | 64 b register | ~700 gate |
| State-update FSM (allocate, resolve, mispredict) | ~3 K gate |
| Drain-broadcast wiring (to SSB, STQ, vector RS, etc.) | ~1 K gate |
| **Total** | | **~5 K gate** (negligible — ~0.001 mm² @ 5 nm) |

### 11.4 Speculative Store Buffer (SSB) — Scalar Memory Path

The SSB is the **gate** between the LSU's store pipeline and the L1-D cache. Already introduced in §8.2.1; this subsection details the speculation-recovery mechanism it enables.

#### 11.4.1 Allocation and population

```
  D2 (rename) of a scalar store instruction:
    Allocate SSB slot k from the free pool (FIFO order)
    Set SSB[k] = {valid=1, btag=current_btag, addr=⊥, data=⊥, drain_rdy=0}
    Stamp the LSU-RS entry with ssb_idx = k

  IS / EX (issue + execute):
    LSU computes addr and reads data from physical RF (or captures from CDB)
    SSB[k].addr ← addr
    SSB[k].data ← data
    SSB[k].size ← size
    LSU-RS entry retires (released; no CDB broadcast for stores)
```

At this point the store's address and data are **fully resolved**, but the store has **not** committed to L1-D. The store's effect on memory is **isolated within the SSB**.

#### 11.4.2 Drain to L1-D (when non-speculative)

```
  When tag t becomes correct:
    For each SSB[k] with valid=1 && btag=t:
      btag_new = next-older speculative tag in this entry's history (from §11.3 ancestry)
      if btag_new == 0xFF (no older speculation): SSB[k].drain_rdy ← 1
      else: SSB[k].btag ← btag_new (still speculative)

  Drain pump (1 store per cycle):
    Pick oldest SSB[k] with valid && drain_rdy
    Issue write to L1-D pipeline (same path as v1 store commit)
    On L1-D ack: SSB[k].valid ← 0, slot returned to free pool
```

#### 11.4.3 Forwarding to loads

Loads can forward from the SSB on address match, with the **branch-ancestry constraint**:

```
  For a load (addr, btag_load):
    For each SSB entry e with e.valid && addr_match(e.addr, addr) && size_compatible(e):
      if e.btag == 0xFF:                      # store is non-speculative — always OK
         forward
      elif anc[e.btag][btag_load]:           # store is speculative on load's ancestry chain
         forward
      else:                                   # store is on a different speculation path
         skip (don't forward; load goes to L1-D, where it sees the pre-mispredict view)
```

Address-ambiguous stores (older stores still computing addresses) cause the load to wait, as in v1.

#### 11.4.4 Mispredict invalidation

```
  When tag t becomes wrong (mispredict at EX1):
    descendants = {j : anc[t][j] = 1} ∪ {t}
    
    For each SSB[k] with valid && btag ∈ descendants:
      SSB[k].valid ← 0   (entry invalidated; never reaches L1-D)
    SSB free-list head pointer ← restored from checkpoint[t]
```

Critically, **invalidated stores are silently discarded** — no memory write was issued. Memory state is unaffected by the misspeculated path.

#### 11.4.5 Capacity sizing

- **Min capacity:** at peak, all 8 active speculation tags can have stores in flight. Each tag's "store density" is bounded by the speculation window between branches (typically 5–10 instructions, of which 20–30% are stores → ~2 stores per tag per branch).
- **Sized for:** 8 tags × ~3 stores/tag = 24 entries, matching the v2 SSB capacity.
- **Stall behavior:** if the SSB fills, dispatch stalls at the next store. This is rare in practice (97th-percentile occupancy is ~12 entries on typical kernels) but the mechanism is correct under any occupancy — the front-end simply waits for an SSB slot to drain.

### 11.5 Speculative Tile-Store Queue (STQ) — MTE Memory Path

The STQ is the analogue of the SSB for **bulk tile stores** issued by the MTE unit (`TILE.ST`, `TILE.SCATTER`). Already introduced in §8.5.2; this subsection emphasizes the speculation-recovery semantics.

Key differences from SSB:

- **Data does not reside in the STQ.** The 4 KB tile payload stays in TRegFile-4K, referenced by `tile_phys_idx`. The STQ holds only the *intent* (address, source phys-tile, branch tag).
- **Smaller capacity (8 entries).** Bulk tile stores are infrequent compared to scalar stores.
- **Drain triggers a memory-bound transfer (8-cy TRegFile read epoch + ~64 cy memory write).** Unlike a single-cycle SSB drain, STQ drains take dozens of cycles and overlap with subsequent MTE operations.

#### 11.5.1 Allocation, drain, invalidation

The flow mirrors §11.4.1–§11.4.4 with these adaptations:

```
  Allocation: D2 of TILE.ST or TILE.SCATTER → STQ slot
  Population: at MTE issue, fields {base_addr, tile_phys_idx, stride or scatter_idx_phys} fill in
  Drain: when btag becomes 0xFF, drain_rdy ← 1; oldest drain_rdy entry begins
         streaming the tile from TRegFile (8-cy read epoch) to memory (64-cy write)
  Invalidation: on tag becoming wrong, matching STQ entries set valid ← 0
                The source physical tile is freed via the normal Tile RAT refcount path
                (the entry's allocation incremented refcount; invalidation decrements it)
```

#### 11.5.2 Why 8 entries

- **Min capacity:** 1 STQ entry per active speculation tag = 8.
- **Worst case:** A kernel with 1 TILE.ST per ~10 instructions issues ~1 STQ entry per ~50 ns at 1.5 GHz; the drain rate is ~1 entry per ~72 cy = ~50 ns. The STQ stays at ~1–2 entries average.
- **Stall behavior:** STQ-full is rare; when it occurs, dispatch stalls at the next bulk tile store.

### 11.6 What about other externally-visible operations?

The full taxonomy of externally-visible side effects in the Davinci-v2 ISA is:

| Operation | External effect | Speculation-safe via |
|-----------|-----------------|----------------------|
| Scalar store (SB/SH/SW/SD) | Write to L1-D / L2 / DRAM | **SSB** (§11.4) |
| TILE.ST | 4 KB write to memory | **STQ** (§11.5) |
| TILE.SCATTER | Indexed memory write | **STQ** (§11.5) |
| Scalar load | Reads L1-D, fills physical register; *no external state change* | Recovered via RAT/refcount; load has no externally-visible effect on commit (other than caching, which is not architectural) |
| TILE.LD, TILE.GATHER | Reads memory into TRegFile-4K; *no external state change* | Recovered via Tile RAT/refcount |
| FENCE | Memory ordering barrier | Allocated at D2; **does not execute until btag = 0xFF**. On mispredict, flushed like any other RS entry. |
| AMO atomics (future) | Read-modify-write to memory | Would need to allocate a SSB-like slot held until non-speculative; fits naturally in the SSB. |
| Branch resolve | Updates predictor tables | Predictor update is **conditional on branch correctness**: predictor write fires only when the branch tag becomes "correct" (i.e., on the same drain trigger as SSB/STQ). |
| TCB / CDB broadcast | Wakes dependent RS entries | Internal to core; not externally visible. Mispredicted broadcasts are absorbed by the RS branch-tag CAM-clear. |
| Performance-counter increment | Updates a CSR | Counters are explicitly architectural; v2 reuses the FENCE-style "execute-only-when-non-speculative" gate (§11.6.1). |
| MMIO load/store (future) | I/O side effect | Would need explicit speculation barrier; recommended to gate at the SSB level with a "MMIO" qualifier that forces in-order completion. |

#### 11.6.1 FENCE and CSR semantics under speculation

```
  D2 (rename) of FENCE / CSRRW:
    Allocate RS entry as usual; stamp btag
  
  IS (issue):
    The instruction is held in RS until btag == 0xFF (i.e. no older
    speculation). This is a small extension to the issue-ready
    predicate: in addition to "all source operands ready", we add
    "btag is non-speculative".
  
  EX:
    Execute as normal. By construction, the instruction is on the
    correct path.
```

This adds latency to FENCE / CSR ops (they wait for older branches to resolve) but is correct. In practice, FENCE is rare in tight kernels.

#### 11.6.2 Cache-line state changes (loads)

Speculative loads can pull cache lines into L1-D / L2 that wouldn't have been pulled on the correct path. This is a **microarchitectural** effect (cache-state pollution), not an architectural one — it doesn't violate program semantics. Modern OoO processors all have this property; the well-known Spectre-class side-channel concerns apply equally to v1 (speculative loads were already permitted because they only fill physical registers, not memory). Recovery against side-channel leakage is **out of scope** for this document; standard mitigations (cache partitioning, speculation barriers) are orthogonal to the no-ROB recovery scheme.

### 11.7 What this scheme does NOT provide (and why that's OK)

The branch-tag + SSB + STQ scheme guarantees: **no misspeculated instruction's externally-visible effect ever reaches memory or any architecturally-visible state, regardless of how deep the speculation goes (up to 8 levels) or how many instructions execute speculatively.**

It does **not** provide:

1. **Precise exceptions.** If a load page-faults (in a hypothetical paged Davinci variant), the processor cannot identify the program-order point of the fault — only the renamed-register view. This is the same v1 limitation. The kernel-execution envelope assumes faults don't happen mid-kernel; any fault is treated as a fatal error.
2. **Precise hardware breakpoints / single-step.** Without ordered retirement, hitting a breakpoint at instruction *i* may have already executed a few instructions past *i*. Debug support is degraded but not broken: breakpoints fire at the granularity of the ROB-less retirement cluster (~10-instruction window).
3. **Strict in-order memory ordering for I/O semantics.** Loads and stores still drain to L1-D in store-buffer-FIFO order, so total-store-order (TSO) within a single thread is preserved; but if the core were extended to support SC (sequential consistency) across threads, additional ordering machinery would be needed.
4. **Recovery from non-branch misspeculation.** Memory-disambiguation misspeculations (where a younger load reads from L1-D before an older store's address resolves, then the older store's address turns out to alias) are not handled — the LSU's address-disambiguation logic still requires older stores' addresses to be known before the load issues, as in v1. **Memory dependence prediction is not implemented.** Removing this restriction is an orthogonal extension that would require an additional tag for "load misspeculation" similar to the branch tag.

These limitations are consistent with the v1 design envelope (run-to-completion AI kernel).

### 11.8 Comparison with a ROB-based design

| Aspect | ROB-based design | Davinci-v2 (no ROB) |
|--------|------------------|---------------------|
| Recovery for register / tile state | ROB walks back, undoes mappings in order | RAT flash-restore + refcount free; **same outcome, simpler hardware** |
| Recovery for memory stores | Stores in ROB / coupled SQ; release on retire | **SSB / STQ** with branch-tag gating |
| Recovery for fences / CSR | ROB serializes; instruction retires in order | Issue gated on `btag = 0xFF`; correct but adds 0–6 cy latency |
| Speculative depth | bounded by ROB capacity (typically 64–256 entries) | bounded by **8 active branch tags** (≈ 8-deep nested branches) |
| Storage overhead | ROB ≈ 256 entries × ~150 b = ~5 KB + retirement logic | Branch-tag tracker (5 K gate) + SSB extension (24 vs 16, +8 entries × 182 b ≈ ~1.5 KB) + STQ (8 × 110 b ≈ ~1 KB) ≈ **~2.5 KB total** |
| Wakeup logic | RS does dependency tracking; ROB independent | **RS does dependency tracking AND speculation tracking** via branch-tag CAM (∝ RS size, already paid for) |
| Mispredict penalty | ROB walk-back + flush ≈ 5–10 cy | RAT flash-restore + SSB/STQ tag-CAM-clear ≈ **1 cy parallel restore + 6-cy refill = 7 cy** |
| Precise exceptions | yes (free) | no (out of envelope) |
| Single-thread TSO memory ordering | yes | yes (FIFO drain through SSB) |

**The key insight:** in environments where precise exceptions are not required (the AI-kernel envelope), the ROB's three bundled services unbundle naturally. Service (1) is free if you don't need it. Service (3) is replaced by reference counting. Service (2) — **the only remaining service** — is implemented by the SSB + STQ + branch-tag tracker at a fraction of a ROB's cost.

### 11.9 Cycle-by-cycle example: speculative store followed by mispredict

```
  Cycle  Action
  ─────  ──────────────────────────────────────────────────────────────────
   0     Branch B1 enters D2; allocated tag t=3, state[3] = speculative
   1     SD X5, [X8]+0    — younger than B1 — D2 allocates SSB[7] tagged 3
   2     SD X6, [X8]+8    — younger than B1 — D2 allocates SSB[8] tagged 3
   3     SD X7, [X8]+16   — younger than B1 — D2 allocates SSB[9] tagged 3
   4     ... 5 more in-flight instructions ...
   9     B1 reaches EX1: mispredicted!
  10     state[3] = wrong; all RS entries with btag = 3 invalidated;
         SSB[7], SSB[8], SSB[9] set valid ← 0; tile/scalar free-list heads
         restored from checkpoint[3]; RAT restored from checkpoint[3]
  11     Fetch redirected to correct branch target
  12-16  Front-end refill (5 cy)
  17     First correct-path instruction enters EX1 (total mispredict
         penalty: 17 - 10 = 7 cy front-end refill + 1 cy restore = 8 cy)

  Architectural state at cycle 17:
    Memory: NEVER wrote SSB[7..9]. Cache lines unaffected. Correct.
    X5, X6, X7: physical reg mappings rolled back via RAT restore.
    Free lists: include the orphan physical regs allocated in cycles 1–8.
```

The mispredict is recovered in 8 total cycles — one cycle longer than v1's 6-cy penalty for two reasons:

1. **One additional cycle of redirect** because the branch tag's CAM-clear must propagate to all RS entries before the next instruction enters DS. (v1's 6-cy penalty assumed instantaneous RS flush; v2 makes this explicit.)
2. **One additional cycle of front-end refill** in the worst case where the first correctly-fetched instruction's source register depends on a value being restored to the RAT.

In practice, both effects can be hidden through pipelining (the CAM-clear is parallel; the front-end refill is identical to v1). The realistic mispredict penalty for v2 is **6–7 cycles**, well within the v1 envelope.

### 11.10 Hardware cost summary

| Block | v2 addition | Gate count |
|-------|-------------|------------|
| Branch-tag tracker (state vector + ancestry bitmap + FSM) | new | ~5 K |
| Speculative Store Buffer (24 entries, 182 b each) | extends v1's 16-entry store buffer | ~80 K (incl. CAM) |
| Speculative Tile-Store Queue (8 entries, 110 b each) | new | ~12 K |
| Branch-tag stamping in RS entries (24 + 16 + 24 + 4 + 16 = 84 entries × 3 b) | extends RS entry width | ~2 K |
| Checkpoint extension (+13 b/slot × 8 slots) | extends checkpoint store | ~1 K |
| Tile Metadata RAT (256 × 32 b SRAM, 4R/2W) | new (also serves §6.1) | ~10 K |
| **Total v2 speculation hardware** | | **~110 K gate (~0.025 mm² @ 5 nm)** |

This is **~3.5%** of the ~3.26 mm² total core area (v1) — far less than a comparable-capacity ROB (typically 256 entries × ~10 b/entry with full retirement bypass = ~50 K gate just for storage, plus equivalent forwarding logic that would push a ROB-based v2 to ~300–500 K gate of new structure).

---

## 12. Memory Subsystem

The memory subsystem is structurally identical to v1 §11. The two changes are integration points for the SSB and STQ:

### 12.1 Cache hierarchy

**Unchanged from v1.** L1-I 64 KB / L1-D 64 KB / L2 512 KB.

### 12.2 Store path

```
  Scalar store:      LSU-RS → SSB → L1-D (only on tag-clear)
  Bulk tile store:   MTE-RS → STQ → MTE memory pipeline → L2 (only on tag-clear)
```

The L1-D's existing 8 MSHRs and 4-cy store pipeline are unchanged. The SSB inserts in front of L1-D as a CAM-addressable forwarding buffer; it already played that role in v1's 16-entry store buffer, so the L1-D interface is unchanged.

### 12.3 TLB and address translation

**Unchanged from v1 §11.2.**

### 12.4 Memory ordering

Within a single thread:
- Scalar loads / stores maintain program order via SSB FIFO drain + load-queue snooping (same as v1).
- TILE.LD / TILE.ST are unordered with respect to each other unless serialized by FENCE.
- Cross-domain ordering (e.g., scalar ST → TILE.LD reading the same location) is software-managed via FENCE.

The SSB's branch-tag gating is **orthogonal** to memory ordering: stores still drain in alloc-age order, just only when their branch tag is non-speculative.

---

## 13. Mixed-Domain Instruction Scheduling

**Largely unchanged from v1 §12.** All four domains share the same front-end, dispatch to domain-specific RSs, and synchronize through Tile RAT ready bits / TCB / CDB.

### 13.1 New scheduling considerations under speculation

| Scenario | v1 behaviour | v2 behaviour |
|----------|--------------|--------------|
| Speculative TILE.LD | Couldn't be issued past branch | Can issue speculatively; on mispredict, allocated physical tile rolls back via Tile RAT/refcount |
| Speculative VEC op | Couldn't be issued past branch | Can issue speculatively; staging registers, Acc, etc. are renamed-tile-only and rollback via Tile RAT |
| Speculative TILE.ST | Couldn't be issued past branch | Issues speculatively to STQ; STQ-full → dispatch stall |
| Speculative CUBE.OPA | Couldn't be issued past branch | Can issue speculatively; cube unit's accumulator is in physical-tile rename space |
| FENCE under speculation | Held until branch resolves | Held in RS until `btag = 0xFF` (issue-gated) |

### 13.2 Speculative tile-domain ops

A subtle point: vector / cube / MTE-load instructions younger than an unresolved branch can execute speculatively because:

- Their inputs (source physical tiles) are versioned via the Tile RAT — even if the wrong physical tile is read, the instruction simply commits to its (also-renamed) destination physical tile.
- Their destination is a fresh physical tile that gets freed via refcount + free-list-restore on misspeculation.
- They consume TRegFile-4K port cycles (epoch slots) but don't change architectural memory state.

The only "wasted" resource on misspeculation is **TRegFile-4K port bandwidth** and **microcode beats** spent on the wrong path. In a typical kernel where vector ops are 20–30% of dispatch volume, ~95% branch prediction accuracy means ~1–1.5% of vector compute is wasted on misspeculated paths — well within budget.

### 13.3 Cross-domain dependency table (v2 adds two rows)

| Dependency | Mechanism |
|------------|-----------|
| Scalar → MTE (address operands) | MTE RS holds scalar P-reg tag; CDB wakeup |
| Scalar → Vector (scalar operand SX/SY) | Vector RS holds scalar P-reg tag; CDB wakeup; OR captured at issue-time GPR read |
| MTE → Vector / Cube (tile data readiness) | Tile RAT ready bit + TCB wakeup |
| Vector → Cube/MTE | Tile RAT ready bit + TCB |
| Cube → MTE | Tile RAT ready bit + TCB |
| Tile → Scalar (TILE.GET) | CDB scalar broadcast |
| Scalar → Tile (TILE.PUT) | TCB tile broadcast |
| **Branch → Speculative Memory** | **SSB / STQ branch-tag gating (§11.4, §11.5)** |
| **Branch → Speculative Register / Tile** | **RAT checkpoint flash-restore + refcount free-list-head restore (§11.3)** |

---

## 14. Performance Targets

### 14.1 Clock & throughput

| Metric | Target |
|--------|--------|
| Clock frequency | ≥ **1.5 GHz** (5 nm) |
| Scalar IPC peak / sustained | 4.0 / 2.5–3.0 |
| **Vector throughput (FP32 elementwise, 1 tile/8 cy)** | **0.77 TFLOPS** |
| **Vector throughput (FP4 elementwise)** | **6.14 TFLOPS** |
| **Vector throughput (FP32 wide row-reduce, recommended baseline)** | **~8.4 GFLOPS effective** (13 beats/8 lanes/iteration) |
| **TINV throughput (128×128 FP32)** | **~1 inverse / 33 µs ≈ 30 K inverses / s @ 1.5 GHz, single-tile-resident** |
| **TMRGSORT throughput (1024 FP32 sort)** | **~6.8 M sorts / s @ 1.5 GHz, single-instruction** |
| Cube FP16 / FP8 / MXFP4 | 12.3 / 24.6 / 98.3 TFLOPS / TOPS |
| MTE tile bandwidth | 4 KB/cy aggregate read + 4 KB/cy aggregate write |
| Memory bandwidth (L2) | 96 GB/s |
| **Mispredict penalty** | **6–7 cy** (vs. v1's 6 cy; the +1 cy is the SSB/STQ tag-CAM propagation) |
| **Speculative depth** | **up to 8 unresolved branches** (matches RAT checkpoint count) |

### 14.2 Workload performance summary

For pure-cube kernels (transformer GEMM, CNN), v2 performance equals v1 (cube unit unchanged). Improvements vs. v1 appear in:

| Workload | v1 | v2 | Speedup |
|----------|-----|----|---------|
| Softmax (batch 8, dim 4096) | 24K cy (vector + scalar) | 18K cy (masked reductions, no `TTRANS` predecessor) | **1.33×** |
| Layer norm (batch 8, dim 4096) | 22K cy | 16K cy | **1.38×** |
| Attention with mask (batch 8, seq 1024) | 80K cy | 56K cy (per-element mask native) | **1.43×** |
| GEMM 128×128 inverse (Kalman update) | software emulation (~4M cy CPU-equivalent on vector) | **TINV 33 K cy** | **~120×** |
| 1024-element top-k | software ~50K cy | TMRGSORT 220 cy + scalar cleanup ~250 cy | **~100×** |
| Speculative scalar-heavy code (e.g. graph algorithm) | limited to in-branch parallelism | full speculation past 8 unresolved branches | **~2–3× sustained IPC improvement** |

### 14.3 IPC breakdown (transformer decode)

```
  Instruction mix (typical transformer layer, M=8, K=4096, N=4096, FP16):
    Scalar:   ~15%
    MTE:      ~25%
    Cube:     ~55%
    Vector:   ~5%
  
  v2 advantage:
    Speculation lets 30–40% of scalar ops past unresolved branches issue early
    → effective scalar IPC rises from ~2.5 (v1) to ~3.2 (v2)
    → end-to-end kernel time drops ~3–5% (cube remains the bottleneck)
```

---

## 15. Area & Power

### 15.1 Area summary (v2 deltas vs. v1 in **bold**)

| Component | v1 area | v2 area | Δ |
|-----------|---------|---------|---|
| TRegFile-4K (1 MB SRAM + 32 b metadata SRAM) | ~1.20 mm² | ~1.20 mm² + ~0.005 mm² metadata | +0.005 mm² |
| outerCube MXU | ~0.80 mm² | ~0.80 mm² | 0 |
| **Vector unit** | ~0.20 mm² (v1) | **~0.30 mm² (VEC-4K-v2 SRAM-staging baseline)** | +0.10 mm² (with new ISA: TINV/TROWRANGE_MUL/TMRGSORT) |
| L1-I / L1-D / L2 | ~0.66 mm² | ~0.66 mm² | 0 |
| Scalar physical RF + Scalar RAT + free list | ~0.22 mm² | ~0.22 mm² | 0 |
| Tile RAT + Tile free list + tile refcount | ~0.05 mm² | ~0.05 mm² + 0.005 mm² metadata RAT | +0.005 mm² |
| Tile Completion Bus (TCB) + tile RS CAMs | ~0.05 mm² | ~0.06 mm² (24-entry vector RS) | +0.01 mm² |
| MTE transpose buffer | 0.005 mm² (4 KB) | 0.001 mm² (512 B) | -0.004 mm² (smaller buffer) |
| RS + dispatch + checkpoint control | ~0.15 mm² | ~0.15 mm² | 0 |
| **Speculative Store Buffer (SSB, 24 entries)** | — | **+0.02 mm²** | +0.02 mm² |
| **Speculative Tile-Store Queue (STQ, 8 entries)** | — | **+0.003 mm²** | +0.003 mm² |
| **Branch-tag tracker** | — | **+0.001 mm²** | +0.001 mm² |
| **Total core (estimated)** | **~3.26 mm²** | **~3.41 mm²** | **+0.15 mm² (+4.6%)** |

### 15.2 Net impact

The v2 core is approximately **5% larger** than v1 in exchange for:

- A re-architected vector unit with per-element masking, 3-source/2-dest, restored FP4/FP8, and three new high-impact instructions (TINV / TROWRANGE_MUL / TMRGSORT).
- Per-port `is_transpose` on TRegFile-4K (eliminating most `TILE.TRANSPOSE` predecessors).
- Branch-prediction-driven speculative execution with full architectural-state recovery (no ROB).

Vector unit area actually shrinks vs. v1's vector unit when accounting for [`vector4k_v2.md`](vector4k_v2.md) §10's analysis (~27% smaller for VEC-v2 SRAM-staging baseline vs. v1) — but the v2 unit also adds the new instructions (TINV / TROWRANGE_MUL / TMRGSORT) that bring net area roughly to parity with v1's vector unit, while delivering ~100× higher performance on those kernels.

### 15.3 Power management

**Same techniques as v1 §14.2.** Adds:

- **Branch-tag tracker clock-gates** when no branches are in flight (very common in straight-line code regions).
- **SSB / STQ entries clock-gate** their flip-flop fields when invalid.

---

## 16. External Interfaces

**Unchanged from v1 §15.** Core-to-NoC AXI4 interface, optional MOESI coherence, debug/trace remain identical.

---

## Appendix A: Glossary (v2 additions in **bold**)

| Term | Definition |
|------|-----------|
| RAT | Register Alias Table |
| **Tile Metadata RAT** | 256 × 32 b SRAM holding per-physical-tile (shape.x, shape.y, format) |
| TCB | Tile Completion Bus — 4-port broadcast for tile wakeup |
| CDB | Common Data Bus — broadcast network for scalar results |
| RS | Reservation Station |
| MTE | Memory Tile Engine |
| MXU | Matrix Unit (outerCube) |
| TRegFile-4K | Tile Register File with 4 KB physical tiles, 8R+8W ports, **per-port `is_transpose` (v2)** |
| OPA | Outer Product Accumulate |
| **VEC-4K-v2** | Re-architected vector unit ([`vector4k_v2.md`](vector4k_v2.md)) with staging registers, microcode beat machine, and 3-operand support |
| **SA, SB, SC** | Vector unit's value-tile and mask staging registers (4 KB each, 1R1W SRAM in production baseline) |
| **SX, SY** | Scalar staging slots in VEC-4K-v2 (64 b each, GPR/IMM/TILE/ACC sourced) |
| **TINV** | Tile matrix inverse instruction (up to 128×128 FP32) |
| **TROWRANGE_MUL** | Column-wise product over a dynamic row sub-range |
| **TMRGSORT** | Reconfigurable bitonic sort over any `N = 2^p` up to 8192 |
| **TSETMETA** | Rename-only instruction that updates a tile's metadata word |
| **is_transpose** | Per-read-port flag on TRegFile-4K that selects row-mode vs. col-mode chunk-grid delivery |
| **tilelet_xpose** | Per-beat microcode bit in VEC-4K-v2 selecting per-tilelet chunk-grid transpose at staging-side |
| **branch_tag** | 3-bit tag attached to every µop younger than an unresolved branch; 8 active tags max |
| **SSB** | Speculative Store Buffer — 24-entry buffer that gates scalar stores by branch tag |
| **STQ** | Speculative Tile-Store Queue — 8-entry buffer that gates MTE bulk stores by branch tag |
| **Speculation Tracker** | 5-K-gate structure tracking 8 active branch tags + 8×8 ancestry bitmap |
| ROB | Reorder Buffer — *not present* in Davinci-v2; functionally replaced by RAT checkpoint + refcount + SSB + STQ |
| MSHR | Miss Status Holding Register |
| BTB | Branch Target Buffer |
| TAGE | TAgged GEometric history length predictor |
| RAS | Return Address Stack |
| IPC | Instructions Per Cycle |
| MLP | Memory-Level Parallelism |

## Appendix B: Reference Documents

| Document | Content |
|----------|---------|
| [`Davinci_supersclar.md`](Davinci_supersclar.md) | Davinci v1 — direct predecessor; v2 inherits all unchanged subsystems |
| [`outerCube.md`](outerCube.md) | outerCube MXU architecture, dual-mode operation, ISA, pipeline, performance analysis |
| [`tregfile4k.md`](tregfile4k.md) | TRegFile-4K design (256×4KB tiles, 8R+8W ports, **§7 per-port `is_transpose` enhancement**) |
| [`vector4k_v2.md`](vector4k_v2.md) | **VEC-4K-v2 vector unit specification** — staging registers, per-beat microcode, masked / 3-source / 2-dest, TINV/TROWRANGE_MUL/TMRGSORT |
| [`vector4k.md`](vector4k.md) | VEC-4K v1 vector unit (referenced by v2 for unchanged subsystems) |
| [`Simplified_Superscalar Design Concepts-2.md`](Simplified_Superscalar%20Design%20Concepts-2.md) | OoO execution theory background: no-ROB design, RAT checkpointing, refcount freeing |
| [pto-isa vector docs](https://github.com/hw-native-sys/pto-isa/tree/main/docs/isa) | Authoritative PTO ISA |

## Appendix C: Document History

| Version | Date | Notes |
|---------|------|-------|
| **v2.0** | 2026-04-30 | **Initial Davinci-v2 specification.** Three major changes vs. v1: **(1) TRegFile-4K with per-port `is_transpose` flag** ([`tregfile4k.md`](tregfile4k.md) §7), enabling row-mode or col-mode delivery at full 512 B/cy; consumed by v2 vector unit and (optionally) by cube and MTE. **(2) Vector unit re-architected to VEC-4K-v2** ([`vector4k_v2.md`](vector4k_v2.md)): explicit SRAM-based staging registers (`SA`, `SB`, `SC`) decoupling TRegFile fetch from compute, per-beat microcode dispatch, 3-source / 2-dest tile operands with per-element bitmask predication, restored FP4 and FP8 formats, three new PTO instructions (`TINV` matrix inverse up to 128×128 FP32, `TROWRANGE_MUL` row-range product, `TMRGSORT` bitonic sort over any `N = 2^p` up to 8192), and **tile-register metadata** (32 b: `shape.x`, `shape.y`, `format`) carried via a new Tile Metadata RAT. **(3) Branch-prediction-driven speculative execution** with a ROB-less recovery scheme (§11): a 5-K-gate Branch-Tag Speculation Tracker (8 tags + 8×8 ancestry bitmap), a 24-entry **Speculative Store Buffer** (SSB) that gates scalar stores by branch tag, and an 8-entry **Speculative Tile-Store Queue** (STQ) that gates MTE bulk stores by branch tag. The scheme proves that all three classes of speculative state (renamed registers/tiles, in-flight pipeline state, externally-visible memory effects) can be safely recovered without a Reorder Buffer: classes A and B reuse the v1 RAT-checkpoint + refcount + branch-tag-CAM machinery; class C is gated by SSB / STQ until the producing branch tag becomes non-speculative. Mispredict penalty: 6–7 cy (vs. v1's 6 cy). Total v2 speculation hardware: ~110 K gate (~0.025 mm²), about 3.5% of the v1 core area. v2 core area: ~3.41 mm², a ~5% increase over v1's ~3.26 mm². Performance gains: 1.3–1.4× on masked-vector kernels (softmax, layer norm, masked attention), ~100× on `TINV`-bound (Kalman, NeRF pose) and `TMRGSORT`-bound (top-k, beam-search) kernels, and ~2–3× sustained scalar IPC improvement on speculative-heavy code paths. Cube unit, scalar unit, memory subsystem (caches), and external interfaces remain unchanged from v1. |
