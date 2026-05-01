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

> **Design discipline:** The v2 core assumes **run-to-completion kernel execution** with **no OS-level interrupts** — the same envelope as v1. The new v2.3 **Block-ROB (BROB)** adds **block-granularity precise exception support**, enabling the core to identify the faulting instruction block and recover precisely when an exception (trap, page fault, illegal instruction) does occur. The new speculation-recovery mechanism handles **branch mispredictions** and **variable-latency tile ops**; Section 11.7 enumerates the remaining "non-recoverable" classes (asynchronous page faults, signaling NaNs, ECC errors observed mid-kernel) and the kernel-level conventions that bound them.

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
| Scalar issue width | **7** (4 ALU + 1 MUL/DIV + 1 BRU from alu_iq; 2 LSU from lsu_iq) |
| **Vector issue width** | **1 VEC-4K-v2 instruction / cycle** (§8.3) |
| Cube issue width | **1** CUBE instruction / cycle |
| MTE issue width | **2** TILE.LD/ST per cycle |
| Pipeline depth (scalar) | **17+** stages (fetch-to-writeback, with D1/D2/D3 rename and P1/I1/I2 issue separation) |
| Branch predictor | Hybrid TAGE + BTB + RAS |
| **MapQ depth** | **12** entries (speculative rename increment log; instruction-precise recovery via reverse replay; replaces RAT checkpoint snapshots) |
| **Branch tag width** | **3 b** (matches checkpoint count); attached to every in-flight RS / store-buffer / tile-store-queue entry |
| Physical IQ entries | Scalar ALU: **48**, Scalar BRU: **16**, LSU: **32**, Vector: **24**, Cube: **4**, MTE: **16** |
| **GVIQ (Grouped Vector IQ) entries** | **32** entries; 1 VTG micro-op / cycle; entry prefix: block_id + pc_index + group_id + iter0..iter3 |
| **VTG (Vector Thread Group) count** | **16** x 256 B VTGs / tile (`G256`) or **8** x 512 B VTGs / tile (`G512`) |
| **Micro-instruction buffer depth** | **16** entries; shared by all VTGs in a tile group; max **64** micro-ops per block |
| **SIMD lanes per VTG beat** | **128** (FP32) . **256** (FP16/BF16) . **512** (FP8) . **1024** (FP4) |
| **GVIQ (Grouped Vector IQ) entries** | **32** entries; 1 VTG micro-op / cycle; entry prefix: block_id + pc_index + group_id + iter0..iter3 |
| **VTG (Vector Thread Group) count** | **16** × 256 B VTGs / tile (`G256`) or **8** × 512 B VTGs / tile (`G512`) |
| **Micro-instruction buffer depth** | **16** entries; shared by all VTGs in a tile group; max **64** micro-ops per block |
| **SIMD lanes per VTG beat** | **128** (FP32) · **256** (FP16/BF16) · **512** (FP8) · **1024** (FP4) |
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
| **BROB entries** | **128** (Block Reorder Buffer; tracks instruction block lifetimes for precise exceptions; SS11.11) |
| **Block SSB entries** | **32** (in-block scalar store buffer; SS11.11) |
| **Block STQ entries** | **16** (in-block tile-store buffer; SS11.11) |
| **BID width** | **8 b** slot index + 56 b sequence (64 b total); SS11.11 |

### BCC-Style Scalar Pipeline Deltas (v2 BCC overlay)

The following parameters supersede the corresponding v1/v2 entries above when the BCC scalar pipeline is enabled.

| Parameter | Value |
|-----------|-------|
| **Scalar rename pipeline** | D1 (decode + RID/atag allocation) → D2 (SMAP read + ptag allocation + MapQ push) → D3 (rename complete + Ready Table init) |
| **atag** | Architectural register index (0–31 for GPRs), replaces "architectural GPR" terminology |
| **ptag** | Physical register index (P0–P127), replaces "physical GPR" / "P-reg" terminology |
| **Rename tables** | CMAP (committed map, 32×7 b) + SMAP (speculative map, 32×7 b) + MapQ (12-entry ring buffer) |
| **Physical IQ topology** | 3 separate physical IQs: `alu_iq` (48 entries, 4-wide issue), `bru_iq` (16 entries, 1-wide), `lsu_iq` (32 entries, 2-wide) |
| **Wakeup mechanism** | **Ready Table** (128-bit bitmap; O(1) ptag lookup) — replaces CDB comparator arrays |
| **Issue picker** | **Age-matrix cascaded pick** using RID-based sub-head age: `age = (entry.rid − head_rid) mod 64`; purely combinational, no per-entry age field |
| **Issue stages** | P1 (age-matrix pick) → I1 (RF read-port arbitration) → I2 (confirm issue + IQ deallocation) |
| **Wakeup latency** | 2 cycles (Ready Table register → can_issue → pick → RF read) |
| **Flush model** | MapQ reverse replay from `flush_rid` → SMAP restored to exact CMAP state; branch-tag CAM-clear on physical IQs |
| **VTG execution mode** | Full-tile `T*` + VTG `V*` micro-ops share VEC-4K-v2 ALU; GVIQ (32 entries) handles VTG separately from Vector RS (24 entries) |
| **VTG scheduling** | GVIQ rotation scheduler picks oldest-ready VTG; `block_id` → micro-instruction buffer lookup; Group Read/Write Adapters select VTG sub-ranges |
| **SIMD lane count** | 128 lanes/beat (FP32), 256 (FP16/BF16), 512 (FP8), 1024 (FP4) |

---

### BCC-Style Vector Pipeline Deltas (v2.2 VTG overlay)

The following parameters describe the VTG vector micro-instruction overlay on top of the VEC-4K-v2 datapath.

| Parameter | Value |
|-----------|-------|
| **VTG modes** | `G256` (16×256 B VTGs/tile) . `G512` (8×512 B VTGs/tile) |
| **GVIQ depth** | 32 entries, 1-wide issue |
| **Micro-instruction buffer** | 16 entries (2-way set assoc), max 64 micro-ops/block |
| **VTG Metadata Table** | 16 entries / physical tile |
| **VTG Ready Table** | 256-bit bitmap (one bit per PT0..PT255) |
| **Loop counters per GVIQ entry** | 4 × 16-bit (`iter0..iter3`) |
| **Group adapters** | Group Read Adapter (TRegFile → VTG sub-range), Group Write Adapter (VTG → TRegFile) |
| **Paired G256 issue** | Optional: 2 independent 256 B VTGs share one 512 B SIMD group beat |

---

## 2. ISA Summary

The v2 ISA is a strict superset of v1: every v1 opcode encodes identically and behaves identically. v2 adds:

- **Masked variants** of every elementwise vector op, every reduction, and every gather (encoded by a bit in `funct7`).
- **Three new PTO instructions** (§2.2.6).
- **A new tile-metadata setter** `TSETMETA` (§2.2.7).
- **Branch hint bits** in the conditional-branch encoding for static prediction override (§5.2.4).

### 2.1 Scalar ISA

> **(v1 → v2: 内容未变更,以下完整复制自 v1 §2.1。)**

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

**v2 增量 (§5.2.4):** A new optional 1-bit `H` (hint) field in the conditional-branch funct3 encoding lets the compiler suggest static taken/not-taken when the dynamic predictor has no entry. Predictor still has final say once it has trained — H is consulted only on a TAGE/BTB miss. v1 software runs on v2 unmodified — the `H` bit defaults to 0 (no hint) when assembled by a v1-targeted compiler.

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
| **A** | Value tile (primary, mandatory) | source | TRegFile read port R0 → `SA` staging |
| **B** | Value tile (secondary, optional) | source | TRegFile read port R4 → `SB` staging |
| **C** | **Dual role:** `c_role = MASK` → per-element bitmask (1 b/element); `c_role = VALUE` → **third value tile** for native 3-source FMA family (§2.2.6a) | source (when `has_mask = 1` **or** `c_role = VALUE`) | TRegFile read port R1 (v2.1: 3rd VEC-side binding) → `SC` staging |
| **D0** | Result tile (primary) | destination | Write port `W0` |
| **D1** | Result tile (secondary, optional) | destination | Write port `W4` |

The 32-bit instruction word reserves:
- a `c_role` bit (0 = `MASK`, 1 = `VALUE`),
- a `has_mask` bit (1 if `C` is fetched **and** `c_role = MASK`),
- a `retire_mask[1:0]` field (which of `D0`, `D1` are written), and
- per-operand `is_transpose_{A,B,C}` bits forwarded to the TRegFile read ports (§9.2).

Tile register fields stay 5 bits (T0–T31). When `c_role = VALUE` and `N_val = 3` (e.g. `VFMA`, `VFNMA`, `VLERP`), `C` is fetched as a full 4 KB value tile through the dedicated VEC read port R1 — see §2.2.6a and [`vector4k_v2.md`](vector4k_v2.md) §3.1, §7.6 for the rationale and 3-port binding.

> **Why a third VEC-side TRegFile read port?** TRegFile-4K has 8 physical read ports. v1 and v2.0 used only 2 (R0/R4) for VEC, since operand `C` was strictly a small mask. v2.1 binds **R1 = Port C** so that all three value tiles of a 3-source `VFMA` can be fetched **in parallel within one 8 cy epoch** — same cadence as a binary op. The alternative (sequential 2-epoch fetch on R0/R4) would halve `VFMA` throughput. Bandwidth cost: 0 SRAM, 0 bank-conflict pressure (the diagonal skew already supports 8 conflict-free read ports per [`tregfile4k.md`](tregfile4k.md) §4); only a binding allocation. R1 is idle and clock-gated when no `c_role = VALUE` op is in flight.

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
 │ +crole │      │      │      │       │        │  ← v2.1: c_role bit (MASK/VALUE)
 │ +rmask │      │      │      │       │        │
 └────────┴──────┴──────┴──────┴───────┴────────┘
   funct6 packs 6 bits split between op-extension (3 b),
   has_mask (1 b), is_xpose_A (1 b), is_xpose_B (1 b);
   is_xpose_C, c_role, and retire_mask travel in the
   immediate slot of S-/T-types or in a fixed funct7
   bit pattern.
```

**Backward compatibility:** v1 vector instructions decode as `has_mask = 0`, `c_role = MASK`, `retire_mask = 2'b01`, `is_xpose_{A,B,C} = 0` — i.e. unmasked, single-result, no-transpose, no-3rd-tile — and produce bit-exact v1 results. `c_role = VALUE` is only generated by a v2.1-aware compiler emitting `VFMA` / `VFNMA` / `VLERP`; v1 binaries cannot express it.

#### 2.2.6 VTG Vector Micro-Instructions (SIMD-Group Execution)

> **(v2.2 BCC vector overlay — Change Point #2)**

In addition to full-tile `T*` vector instructions, Davinci-v2 supports **VTG (Vector Thread Group) vector micro-instructions** — a warp-grouped execution model where one 4 KB tile is partitioned into multiple SIMD-group-sized scheduling units.

**Key concepts:**

| Concept | Definition |
|---------|------------|
| **SIMD group** | The 128-lane execution unit inside VEC-4K-v2; one VTG maps to 1 or 2 SIMD group beats (512 B or 256 B). Not software-visible. |
| **VTG (Vector Thread Group)** | A warp-like scheduling context: 256 B (`G256` mode, 16 VTGs/tile) or 512 B (`G512` mode, 8 VTGs/tile). Each VTG has its own `group_id`, `thread_id`, `iter0..iter3` loop counters, and `active_lanes` in the GVIQ entry prefix. |
| **Micro-instruction buffer** | A pre-allocated buffer in the vector ALU, shared by all VTGs in the same tile group. Stores the decoded micro-op list keyed by `block_id`. No re-decode at issue time. |

**Architectural naming:**

```text
VADD.F32     T4.g2, T4.g0, T4.g1, T4.p0    ; tile T4, groups g0+g1 -> g2, pred p0
VMULS.F16    T7.g5, T7.g5, X12, T7.p1       ; scalar broadcast mul: X12 to all lanes
VLD.F32      T8.g0, [Xbase + Xoff], T8.p0   ; vector load into VTG g0
VST.F32      T8.g2, [Xbase + Xoff], T8.p0    ; vector store from VTG g2
```

**Instruction families (38 total):**

| Category | Instructions | Notes |
|----------|-------------|-------|
| Elementwise ALU | `VADD`, `VSUB`, `VMUL`, `VDIV`, `VMIN`, `VMAX`, `VABS`, `VNEG` | Standard lane-wise ops under predicate |
| Scalar-broadcast ALU | `VADDS`, `VMULS`, `VMAXS` | Scalar from GPR broadcast via SX/SY staging |
| Compare & Select | `VCMP.{LT/LE/GT/GE/EQ/NE}`, `VSEL`, `VMERGE` | Compare writes predicate VTG; `VSEL` selects between sources |
| Conversion | `VCVT.dtype.stype`, `VROUND`, `VTRUNC` | Type conversion with saturation/rounding |
| Math | `VSQRT`, `VEXP`, `VLOG`, `VRELU` | Non-linear elementwise ops |
| Predicate | `PLT`, `PAND`, `POR`, `PXOR`, `PNOT` | Predicate generation and logic |
| Memory | `VLD`, `VST`, `VLDSTRIDE`, `VSTSTRIDE`, `PGATHER` | VTG load/store; inactive lanes do not fault |
| Reduction / Wide | `VREDUCE_ADD`, `VREDUCE_MAX`, `WADD` | Scalar reduction output; wide 2-VTG result |

**GVIQ entry prefix** (before operand fields):

```text
{ block_id[11:0], pc_index[7:0], group_id[3:0], thread_id[7:0],
  iter0[15:0], iter1[15:0], iter2[15:0], iter3[15:0],
  active_lanes[15:0], active_group_mask[15:0] }
```

**Full-tile vs. VTG coexistence:**

| Execution mode | ISA prefix | Operand unit | Scheduling unit | Typical use |
|---------------|-----------|-------------|----------------|-------------|
| Full-tile `T*` | `T` | 4 KB tile | One tile per VEC op | Large matrix GEMM, full-tile reductions |
| VTG `V*` micro-op | `V` | 256 B or 512 B VTG | One VTG per GVIQ entry; VTG operates behind VEC-4K-v2 staging (SA/SB/SC) | Strip-mined elementwise, inner loops. VTG latency: **25-32 cy minimum** (8-15 cy prologue + 1 cy compute + 16 cy RMW writeback). VTG reuses VEC-4K-v2 ALU and TRegFile ports (R0/R4/W0) via VEC-domain arbitration. |

The two execution paths share the same VEC-4K-v2 ALU and TRegFile ports (R0/R4 for reads, W0 for writeback). They arbitrate for VEC staging and TRegFile ports via the VEC-domain arbiter. Full-tile VEC ops have higher priority than VTG micro-ops. A VTG micro-op uses the **micro-instruction buffer** (pre-decoded VEC beat-word sequences) rather than the Vector RS opcode field.

**TSPLIT / TJOIN metadata transitions:** The compiler may emit `TSPLIT` (declare tile as NxVTG sub-units) or `TJOIN` (declare VTGs form a coherent tile view) as metadata-only transitions. No physical instruction encoding required — the metadata is set via tile metadata fields at rename time., `c_role = MASK`, `retire_mask = 2'b01`, `is_xpose_{A,B,C} = 0` — i.e. unmasked, single-result, no-transpose, no-3rd-tile — and produce bit-exact v1 results. `c_role = VALUE` is only generated by a v2.1-aware compiler emitting `VFMA` / `VFNMA` / `VLERP`; v1 binaries cannot express it.

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

#### 2.2.6a Native 3-source ternary FMA family (`VFMA`, `VFNMA`, `VLERP`) — v2.1 增量

A new family of vector instructions that consume **three independent value tiles** and produce one (or two) result tiles, enabled by the operand-`C` dual-role mechanism (§2.2.2) and the 3rd VEC-side TRegFile read port (R1, [`vector4k_v2.md`](vector4k_v2.md) §3.1, §7.6). v0.16 of [`vector4k_v2.md`](vector4k_v2.md) only supported ternary FMA via the **accumulator feedback path** (`VFMA_ACC D = A·B + Acc`), which is suitable for GEMM-epilogue / FMA-accumulate kernels but **fails** for the canonical FMA pattern `D = A·B + C` where the third operand is **not** the previous accumulator.

| Mnemonic | Operands | Semantics | Encoding | Cycle budget (typical, uniform `is_transpose`) |
|----------|----------|-----------|----------|-------------------------------------------------|
| **VFMA** | `Td0, Ta, Tb, Tc` | `Td0 = Ta · Tb + Tc` (single-rounding IEEE-754 FMA, all formats) | R-type, `c_role = VALUE`, `has_mask = 0` | **8 cy fetch (3-port parallel) + 8 compute beats + 1 cy fall-through ≈ 10–12 cy end-to-end** — same as `VADD`/`VMUL` |
| **VFNMA** | `Td0, Ta, Tb, Tc` | `Td0 = -(Ta · Tb) + Tc` | R-type, `c_role = VALUE`, `funct6.fnma = 1` | same as `VFMA` |
| **VLERP** | `Td0 [, Td1], Ta, Tb, Tc` | `Td0 = Ta · (1 − Tc) + Tb · Tc` (linear interpolation; optional `Td1 = Tb − Ta` retired in same op) | R-type, `c_role = VALUE`, `funct6.lerp = 1` | 2 fused beats per strip (8 strip × 2 beat = 16 compute beats), `~18 cy` end-to-end |

**Why this family is needed.** From [`FMA指令场景说明.md`](FMA指令场景说明.md):

| Real-world kernel | FMA form | Notes |
|-------------------|----------|-------|
| **LayerNorm / RMSNorm final affine** | `y = γ·x̂ + β` | The dominant FMA in transformer normalisation. `γ`, `x̂`, `β` are three independent tile registers — none is the accumulator. Without `VFMA`, every LayerNorm pays 2× cost (`VMUL` + `VADD`). |
| **Welford incremental update — mean** | `μ_new = δ·inv_n + μ_old` | Streaming variance estimator at the heart of LayerNorm reductions. |
| **Welford incremental update — M2** | `M2_new = δ·δ_2 + M2_old` | Single-rounding FMA preserves precision against catastrophic cancellation on small variance terms (matters for FP16 / BF16 / FP8). |
| **Welford state merge** | `μ = δ·factor + μ_A`; `M2 = M2_A + δ·(δ·factor_m2) + M2_B` | Distributed-norm cross-thread merges. |
| **Activation polynomials** | `gelu`, `swiglu` polynomial / Padé approximations | Multiple FMAs over independent tile inputs. |
| **Trigonometric polynomials** | `sin(x) ≈ x·(c₁ + x²·(c₃ + x²·c₅))` | Horner-form FMAs. |

**Justification (decisive advantages of FMA over emulated `MUL` + `ADD`):**

1. **Throughput doubling** — one fused instruction instead of two halves the FMA-bound pipeline depth and the issue/RS occupancy.
2. **Precision preservation** — IEEE-754 FMA performs a *single* rounding after the infinite-precision `A·B` intermediate, eliminating the second-rounding error of `(A·B) + C`. This matters for FP16 / BF16 / FP8 normalisation kernels that re-feed the result into subsequent reductions.

**Hardware delta (vs. v0.16).** The stage (B) per-lane FMA core, microcode beat machinery, and 8-port TRegFile already supported `A·B + Z`. The only structural changes are:

| Block | Δ |
|-------|---|
| `MUX_Z` per-lane input MUX (already 6:1) — one source retargeted to `SC` value-mode read | ~0 (same gate count) |
| `SC` staging — add 512 B/cy value-mode read path alongside the existing 1-bit-mask read path (sub-bank tree reused from [`vector4k_v2.md`](vector4k_v2.md) §4.2.1) | **~5 K gate** |
| TRegFile read port R1 binding to VEC | **0** (allocation only — TRegFile-4K already has 8R) |
| Issue-time `c_role` bit through Tile RAT / RS / dispatch | **~1 K gate** (control-path widening) |
| **Total v2.1 hardware add** | **~6 K gate (~0.2 % of VEC-4K-v2 area)** |

**Pipeline timing** (Davinci-v2.1 vector pipeline, [`vector4k_v2.md`](vector4k_v2.md) §6.2):

| Op | `N_val` | `c_role` | `is_transpose` mix | Fetch | Compute | End-to-end | Throughput |
|----|--------:|----------|---------------------|------:|--------:|-----------:|------------|
| **VFMA** (typical) | 3 | VALUE | uniform | **8 cy** | 8 beats | **~10–12 cy** | **1 tile / 8 cy** |
| VFMA (one xp odd-out) | 3 | VALUE | one-mismatched | 16 cy | 8 beats | ~18 cy | 1 tile / 16 cy |
| VFMA (all xp different — degenerate) | 3 | VALUE | all distinct | 24 cy | 8 beats | ~26 cy | 1 tile / 24 cy |

**Backward compatibility.** A v1 / v2.0 binary emits `c_role = MASK` exclusively; the new instructions are decoded only when the v2.1-aware compiler sets `c_role = VALUE`. Old binaries see no behaviour change and the R1 read port stays idle and clock-gated.

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

The **95-instruction v1 vector ISA** carries forward, with three changes:

1. Each instruction gets a "masked" variant (no new mnemonic — encoded by `has_mask`).
2. `TSORT32` and `TMRGSORT` from v1 are subsumed by the new `TMRGSORT` (§2.2.6). v1's `TSORT32` mnemonic remains as an alias for `TMRGSORT N=32`.
3. **v2.1 增量:** A new family of native 3-source ternary FMA instructions (`VFMA`, `VFNMA`, `VLERP`) is added under Category O (§2.2.6a), motivated by LayerNorm / Welford / activation / trig kernels (see [`FMA指令场景说明.md`](FMA指令场景说明.md)).

Categories A–M of v1 §2.2.3 are unchanged in semantics. Two new categories are added:

**Category N — Numerical / Reconfigurable Compute (new in v2)**

| Mnemonic | Operands | Semantics | Latency |
|----------|----------|-----------|---------|
| TINV | Tdst+, Tsrc+, num_tiles | Matrix inverse (Gauss–Jordan + NR refine) | ~2 K – 33 K beats |
| TROWRANGE_MUL | Tdst, Tsrc, Xstart, Xend [, Tmask] | Range product per column | ≤ 10 beats |
| TMRGSORT | Td0, Td1, Tsrc, log2N [, Tmask] | Bitonic sort, value+index dual retire | 36 – 2 912 beats |
| TSETMETA | Td, shape.x, shape.y, format | Rewrite tile metadata in-place | 0 (rename-only) |

**Category O — Native 3-source Ternary FMA family (new in v2.1; §2.2.6a)**

| Mnemonic | Operands | Semantics | Latency (typical) |
|----------|----------|-----------|--------------------|
| **VFMA** | `Td0, Ta, Tb, Tc` | `Td0 = Ta · Tb + Tc` (single-rounding IEEE-754 FMA) | **~10–12 cy** end-to-end (8 cy fetch + 8 compute beats); throughput **1 tile / 8 cy** |
| **VFNMA** | `Td0, Ta, Tb, Tc` | `Td0 = -(Ta · Tb) + Tc` | same as `VFMA` |
| **VLERP** | `Td0 [, Td1], Ta, Tb, Tc` | `Td0 = Ta·(1−Tc) + Tb·Tc`; optional `Td1 = Tb − Ta` retired in same instruction | ~18 cy end-to-end (8 cy fetch + 16 compute beats) |

All three issue with `c_role = VALUE`. Mixed `is_transpose_{A,B,C}` adds 8 cy per odd-out (one mismatch → 16 cy fetch; all three different → 24 cy fetch — degenerate). Common kernels (LayerNorm `γ·x̂ + β`, Welford updates) all use uniform `is_transpose` and hit the **8 cy** path.

### 2.3 Cube ISA

> **(v1 → v2: 内容未变更,以下完整复制自 v1 §2.3。)**

Tile-level instructions that drive the outerCube MXU. Each `CUBE.OPA` consumes tile registers and executes all K-loop OPA steps internally.

| Instruction | Operands | Function |
|-------------|----------|----------|
| CUBE.CFG | mode, fmt [, Mactive] | Set operating mode (A/B) and data format |
| CUBE.OPA | zd, Ta, Tb, Rn | Outer product accumulate: iterate over Nb B-tiles |
| CUBE.DRAIN | zd, Tc | Drain accumulator buffer to tile register(s) |
| CUBE.ZERO | zd | Zero accumulator buffer (1 cycle) |
| CUBE.WAIT | zd | Stall until pending drain completes |

Supported formats: FP16, BF16, FP8 (E4M3/E5M2), MXFP4, HiFP4. All accumulate into FP32.

Full cube ISA specification: see [`outerCube.md`](outerCube.md) §6. The Tile RAT renames cube operands exactly as in v1; the outerCube MXU itself is unmodified between v1 and v2.

### 2.4 MTE ISA (Memory Tile Engine)

> **(v1 → v2: ISA 编码与语义 100% 兼容。以下 §2.4.1 / §2.4.2 / §2.4.3 完整复制自 v1 §2.4。v2 实现层增量列在本节末。)**

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

The transpose operates on **square sub-blocks** whose dimension equals the number of elements per 512-bit row. For FP8/INT8 the entire tile is one 64×64 block and transposes in-place. For FP16/BF16/INT16, the 64 rows are split into two 32-row halves, each transposed as a 32×32 block. In v1, the MTE unit contained a dedicated **transpose buffer** (4 KB SRAM) that accumulated rows during the read epoch and emitted transposed rows during the write epoch. In v2 this buffer shrinks to 512 B (see §8.5.1).

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

#### 2.4.4 v2 实现层增量(对软件不可见)

1. **`TILE.TRANSPOSE` becomes a software-optional accelerator.** With per-port `is_transpose` on the TRegFile read (§9.2) and per-beat `tilelet_xpose` in the vector unit (§8.3), most "pre-transpose then consume" patterns become single-instruction with `is_xpose_*` set on the consuming op. `TILE.TRANSPOSE` is retained for cases that need a *materialized* transposed tile reused many times across instructions that themselves don't carry the bit; its physical staging buffer shrinks from 4 KB → 512 B (§8.5.1).
2. **All bulk tile stores (`TILE.ST`, `TILE.SCATTER`) acquire a branch tag at dispatch** and are gated through the **Speculative Tile-Store Queue** (STQ, §11.5) until their tag becomes non-speculative. Invisible at the ISA level; adds 0–6 cycles of latency to a tile store on the speculative path.

### 2.5 Instruction Domain Identification

> **(v1 → v2: 内容未变更,以下完整复制自 v1 §2.5。)**

The 7-bit opcode field encodes the instruction domain:

| Opcode[6:5] | Domain | Decode path |
|-------------|--------|-------------|
| 00, 01 | Scalar | Scalar rename → Scalar RS |
| 10 | Vector / MTE | Tile RAT rename → Vector RS or MTE RS |
| 11 | Cube | Tile RAT rename → Cube RS |

---

## 3. Top-Level Block Diagram

```
 ┌──────────────────────────────────────────────────────────────────────────────────────────────┐
 │  DAVINCI-v2 CORE (BCC Scalar Pipeline)                                                        │
 │                                                                                              │
 │  ┌─────────────────────────────── FRONT-END ──────────────────────────────────────────────┐  │
 │  │   ┌──────────┐    ┌───────────┐    ┌──────────────┐   ┌──────────────┐              │  │
 │  │   │  Branch   │───▶│  Fetch    │───▶│  IB (8 entries)│───▶│  F4 Register  │              │  │
 │  │   │ Predictor │    │  (F0-F3)  │    │  4-wide sync  │   │  D1 handoff  │              │  │
 │  │   │ TAGE+BTB  │    └──────────┘    └──────────────┘   └──────┬───────┘              │  │
 │  │   │ +RAS      │                                                 │                        │  │
 │  │   └──────────┘                                                 ▼                        │  │
 │  │                              ┌─────────────────────────────────────────────┐             │  │
 │  │                              │  D1: Decode + RID / atag Allocation        │             │  │
 │  │                              │  - 4-wide decode (domain, opcode, operands)│             │  │
 │  │                              │  - RID allocation (6-bit program order)      │             │  │
 │  │                              └─────────────────┬───────────────────────┘             │  │
 │  │                                                ▼                                       │  │
 │  │                              ┌─────────────────────────────────────────────┐             │  │
 │  │                              │  D2: Rename Request                         │             │  │
 │  │                              │  - SMAP read for source ptags               │             │  │
 │  │                              │  - ptag allocation from free list            │             │  │
 │  │                              │  - MapQ push (speculative increment log)    │             │  │
 │  │                              │  - SMAP live update (intra-group bypass)    │             │  │
 │  │                              │  - Tile RAT / Tile-Meta RAT unchanged        │             │  │
 │  │                              └─────────────────┬───────────────────────┘             │  │
 │  │                                                ▼                                       │  │
 │  │                              ┌─────────────────────────────────────────────┐             │  │
 │  │                              │  D3: Rename Complete + Dispatch Prep        │             │  │
 │  │                              │  - SMAP write (committed state)             │             │  │
 │  │                              │  - Ready Table init (source ready bits)     │             │  │
 │  │                              │  - IQ routing (alu_iq / bru_iq / lsu_iq)   │             │  │
 │  │                              └─────────────────┬───────────────────────┘             │  │
 │  └────────────────────────────────────────────────┼────────────────────────────────────────┘  │
 │                                                   │ renamed muops + RID + ptags + MapQ entry      │
 │  ┌────────────────────────────────────────────────┼────────────────────────────────────────┐  │
 │  │                                                ▼                                        │  │
 │  │  ┌──────────────────────────────────┐  ┌──────────────────────────────────────────┐  │  │
 │  │  │  S1: Dispatch Preparation         │  │  S2: Dispatch Execute                    │  │  │
 │  │  │  - Free list vacancy check        │  │  - IQ entry write (alu_iq / bru_iq /   │  │  │
 │  │  │  - MapQ space check             │  │    lsu_iq)                             │  │  │
 │  │  │  - IQ vacancy per type            │  │  - Free list update                    │  │  │
 │  │  └──────────────────────────────────┘  │  - MapQ head advance                   │  │  │
 │  │                                           └───────────────────┬──────────────────┘  │  │
 │  │                                                               ▼                      │  │
 │  │  ┌──────────────────────────────────┐  ┌──────────────────────────────────────────┐  │  │
 │  │  │  P1: Issue Pick                  │  │  I1: RF Read Planning   │ I2: Confirm  │  │  │
 │  │  │  - Ready Table bitmap query       │  │  - RF port arbitration  │  - IQ entry  │  │  │
 │  │  │    (O(1) bit-test per ptag)     │  │  - 7-wide across IQs   │    dealloc   │  │  │
 │  │  │  - Age-matrix cascaded pick       │  │                        │  - Port conf  │  │  │
 │  │  │    (RID-based sub-head age)       │  │                        │              │  │  │
 │  │  └──────────────────────────────────┘  └──────────────────────────────────────────┘  │  │
 │  └───────────────────────────────────────────────────────────────────────────────────┘  │
 │                                                                                              │
 │  ┌─────────────────────────── EXECUTE ────────────────────────────────────────────────────┐  │
 │  │       ▼              ▼             ▼             ▼              ▼                      │  │
 │  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐              │  │
 │  │  │ 4x ALU   │  │  Load /  │  │  VEC-    │  │outerCube │  │   MTE    │              │  │
 │  │  │ 1x MUL   │  │  Store   │  │  4K-v2   │  │   MXU    │  │  Engine  │              │  │
 │  │  │ 1x BRU   │  │  Unit    │  │ 3R/2W    │  │(4096 MAC)│  │(LD/ST/  │              │  │
 │  │  │ (alu_iq) │  │  + SSB   │  │ tiles    │  │          │  │G/S/MOVE)│              │  │
 │  │  │          │  │ (lsu_iq) │  │(vec_iq)  │  │          │  │+ STQ(8) │              │  │
 │  │  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘              │  │
 │  └────────┼─────────────┼─────────────┼─────────────┼──────────────┼────────────────────┘  │
 │           │             │             │             │              │                          │
 │  ┌────────┼──────────── COMPLETE ─────┼─────────────┼──────────────┼────────────────────┐    │
 │  │  CDB (6 ports, scalar)  +  TCB (4 ports, tile) + Ready Table (128-bit bitmap)      │    │
 │  │  Ready Table: set bit[i] = ptag i ready on CDB writeback; clear on ptag alloc      │    │
 │  └────────────────────────────────────────────────────────────────────────────────────────┘    │
 │                                                                                              │
 │  ┌──────────────── REGISTER FILES ───────────────────────────────────────────────────────┐  │
 │  │  ┌──────────────────┐  ┌─────────────────────────────────────────────────────────────┐│  │
 │  │  │ Scalar Physical   │  │ TRegFile-4K (with per-port is_transpose)                  ││  │
 │  │  │ Register File     │  │ 256x4KB = 1MB; 8R+8W @ 512B/cy/port                      ││  │
 │  │  │ 128x64b          │  │ 8-cycle epoch calendar                                    ││  │
 │  │  │ 12R+6W ports     │  │ 32 b metadata per physical tile                          ││  │
 │  │  └──────────────────┘  └─────────────────────────────────────────────────────────────┘│  │
 │  └──────────────────────────────────────────────────────────────────────────────────────────┘  │
 │                                                                                              │
 │  ┌──────────────── RENAME STATE ─────────────────────────────────────────────────────────┐  │
 │  │  ┌────────────────┐  ┌────────────────┐  ┌────────────────────────────────┐           │  │
 │  │  │ CMAP [32x7b]   │  │ SMAP [32x7b]   │  │ MapQ [12-entry ring buffer]   │           │  │
 │  │  │ Committed map    │  │ Speculative map │  │ {atag, old_ptag, new_ptag,    │           │  │
 │  │  │ Flush target     │  │ Active rename   │  │  rid, is_push_t/u}            │           │  │
 │  │  └────────────────┘  └────────────────┘  └────────────────────────────────┘           │  │
 │  └──────────────────────────────────────────────────────────────────────────────────────────┘  │
 └──────────────────────────────────────────────────────────────────────────────────────────────┘
```

**BCC scalar pipeline deltas (highlighted):**

- **Three-stage rename pipeline**: D1 (decode + RID/atag allocation) -> D2 (SMAP read + ptag allocation + MapQ push) -> D3 (SMAP write + Ready Table init + IQ routing)
- **Three physical IQs**: `alu_iq` (48 entries, 4-wide), `bru_iq` (16 entries, 1-wide), `lsu_iq` (32 entries, 2-wide) — replace centralized Scalar RS
- **Ready Table** (128-bit bitmap): replaces CDB comparator arrays for scalar wakeup; O(1) ptag lookup per source
- **Age-matrix issue picker**: purely combinational cascaded pick using RID-based sub-head age (no per-entry age field)
- **P1/I1/I2 issue stages**: explicit pick (P1) / RF-read arbitration (I1) / confirm (I2) separation
- **MapQ** (12-entry ring buffer): replaces RAT checkpoint snapshots; instruction-precise recovery via reverse replay
- **atag / ptag naming**: architectural registers = atag (0-31), physical registers = ptag (P0-P127)
- **CMAP + SMAP + MapQ**: three-table rename model (committed / speculative / increment log)
- Branch-tag allocator at rename (unchanged from v2).
- Tile Metadata RAT (32 b per physical tile) co-located with the Tile RAT (unchanged).
- VEC-4K-v2 unit; 3R / 2W tile interface (unchanged).
- Speculative Store Buffer (SSB, 24 entries) gates scalar stores by branch tag (unchanged).
- Speculative Tile-Store Queue (STQ, 8 entries) gates MTE bulk stores by branch tag (unchanged).

---


---

## 4. Pipeline Overview

The Davinci-v2 scalar pipeline is extended to **17+ stages** with the BCC-style scalar frontend. Branch-tag administration adds zero cycles — tags are allocated at D2 (alongside MapQ entry push) and propagated forward as one extra metadata field per IQ entry. The Tile RAT, Vector RS, Cube RS, MTE RS, and memory subsystems are unchanged by the BCC scalar pipeline change.

```
F0 → F1 → F2 → F3 → IB → F4 → D1 → D2 → D3 → S1 → S2 → P1 → I1 → I2 → E1 → … → EX_n → W1
                                     ├── Rename ──┤           ├── Issue ──┤           ├── Execute ──┤
```

**v2.3 Block-ROB addition:**

```
  +--------------------------------------------------------------------------------------+
  |  BROB -- Block Reorder Buffer (v2.3 新增)                                               |
  |  128-entry; tracks block lifetimes; scalar_done && engine_done gates retire              |
  |  Block SSB (32) + Block STQ (16) for in-block store commit                            |
  |  Provides block-granularity precise exception identification                            |
  +--------------------------------------------------------------------------------------+
```

### 4.1 Complete Stage List (BCC Scalar Pipeline)

| Stage | Name | Function |
|-------|------|---------|
| F0 | Fetch PC Select | PC mux (redirect/sequential/flush) |
| F1 | I-Cache Lookup + BTB | Tag+BTB lookup, 4-way set-assoc |
| F2 | I-Cache Response + Predict | Cache data return, TAGE/BTB prediction |
| F3 | Stitch + BSTART Annotation | Cross-line stitch, BSTART boundary marking (deferred; N/A in this change) |
| IB | Instruction Buffer | Depth-8 fetch/decode synchronization buffer |
| F4 | Decode Handoff Register | D1 input register |
| **D1** | **Decode + RID / atag Allocation** | Decode 4 instr, allocate RID (6-bit program-order ID), resolve atag for sources |
| **D2** | **Rename Request** | Read SMAP, resolve source ptag, allocate ptag, push MapQ entry, T/U stack push |
| **D3** | **Rename Complete + Dispatch Prep** | Write SMAP, resolve source readiness from Ready Table, IQ routing assignment |
| **S1** | **Dispatch Preparation** | IQ resource checks (free list, MapQ space, IQ vacancy per type) |
| **S2** | **Dispatch Execute** | IQ entry write, free list update, MapQ head advance |
| **P1** | **Issue Pick** | Ready Table combinational query, age-matrix cascaded pick (RID-based sub-head age) |
| **I1** | **Operand Read Planning** | Global physical RF read-port arbitration |
| **I2** | **Issue Confirm** | IQ entry deallocation, RF read-port occupancy confirm |
| E1–EX_n | Execute | Functional unit execution (variable latency) |
| W1 | Writeback | CDB/TCB broadcast, Ready Table update, wakeup |
| **16. BROB Retire** | *(v2.3 Block-ROB only)* Block completion check. If : advance BROB head (commit Block SSB/STQ to SSB/STQ). If exception: deliver and squash. If incomplete: stall. Off the critical execution path. |

**Total pipeline depth**: Fetch-to-WB = **17+ cycles** (~5 cycles longer than the 12-stage v1/v2 baseline, due to D1/D2/D3 rename split and P1/I1/I2 issue separation).

### 4.B Pipeline Timing — Scalar ALU Instruction (BCC Scalar Pipeline)

```
  Cycle:  0    1    2    3    4    5    6    7    8    9    10   11   12
  ─────  ────  ────  ────  ────  ────  ────  ────  ────  ────  ────  ────  ────  ────
  i0:    F0   F1   F2   F3   IB   F4   D1   D2   D3   S1   S2   P1   I1   I2  E1  WB
  i1:           F0   F1   F2   F3   IB   F4   D1   D2   D3   S1   S2   P1   I1   I2  E1  WB
  i2:                  F0   F1   F2   F3   IB   F4   D1   D2   D3   S1   S2   P1   I1   I2  E1  WB
  i3:                         F0   F1   F2   F3   IB   F4   D1   D2   D3   S1   S2   P1   I1   I2  E1  WB
         └──────────────── 6-cycle fetch-to-D1 ──────────────────┘
                                        └──── Rename ────┘
                                                └──── S ──┘  └─ P/I ─┘
```

**Note:** Pipeline timing is shown for a single-cycle ALU operation. The 17+ stage pipeline means an instruction takes ~11 cycles from D1 to WB. The additional latency does not reduce throughput — all 4 slots of each pipeline stage are occupied every cycle, maintaining 4-wide dispatch rate. Variable-latency ops (MUL, DIV, LD) follow the same stage layout but occupy EX for additional cycles.

### 4.C Execution Latencies by Domain (unchanged from v2)

| Domain | Operation | Stages | Latency (cycles) | Pipelined |
|--------|-----------|--------|-------------------|-----------|
| Scalar | ALU (add, logic, shift) | EX1 | **1** | yes |
| Scalar | MUL | EX1–EX4 | **4** | yes |
| Scalar | DIV | EX1–EX(12–20) | **12–20** | no |
| Scalar | Branch resolve | EX1 | **1** | yes |
| LSU | Load (L1 hit) | EX1–EX4 | **4** | yes |
| LSU | Load (L2 hit) | EX1–EX(12) | **12** | yes |
| LSU | Store | EX1–EX4 | **4** (addr+data) | yes |
| Vector | VADD/VMUL/VFMA (full tile, elementwise) | 2 epochs (16 cy) | **16** (8 read + 8 write, compute hidden) | epoch-pipelined |
| Vector | Reduce (VROWSUM/VCOLSUM/...) | 1 epoch + reduce | **16** (8 read + reduce + 8 write) | no |
| Vector (v2) | TINV (128×128 FP32) | multi-epoch | **~33 K beats** (~22 µs @ 1.5 GHz) | no |
| Vector (v2) | TMRGSORT (1024 FP32) | multi-epoch | **~220 cy** | no |
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

---

### 4.3 Per-stage Actions (BCC Scalar Pipeline)

| Stage | Function |
|-------|---------|
| **D1** | Decode 4 instr; allocate RID (6-bit program-order ID); resolve atag for each source operand; classify each operand as P (GPR, 32 regs), T (tile, 32 regs), or U (uncore) |
| **D2** | Read SMAP for source ptags; allocate new ptag for P-dst; push MapQ entry; update SMAP (live for intra-group bypass); T/U stack push for tile/uncore operands |
| **D3** | Write SMAP to committed state; initialize Ready Table source-ready bits from Ready Table query; assign IQ routing (alu_iq / bru_iq / lsu_iq) |
| **S1** | Check free list vacancy, MapQ space, IQ vacancy per routing target |
| **S2** | Write IQ entries; advance free list head; advance MapQ head |
| **P1** | Ready Table combinational query; age-matrix cascaded pick selects oldest-ready entries per IQ |
| **I1** | Physical RF read-port arbitration across 7 issue slots (alu_iq x 4 + bru_iq x 1 + lsu_iq x 2) |
| **I2** | Confirm IQ entry deallocation; confirm RF port occupancy |
| **E1-EX_n** | Functional unit execution; CDB/TCB broadcast at W1 |
| **W1** | Ready Table bitmap update: clear allocated ptag bits (D2 to W1 = 8 cycles after rename); set ptag bits on CDB writeback |

### 4.4 Pipeline Timing Notes

- All execution latencies (Section 4.C) are measured from the E1 stage, not from fetch. The extended pipeline does not change the functional unit latencies.
- Variable-latency ops (MUL, DIV, LD) follow the same D1/D2/D3/S1/S2/P1/I1/I2/E1 stage layout but occupy E1-EX_n for additional cycles.
- The Ready Table is updated on the clock edge at W1 (D2 dispatch to W1 = 8 cycles). A renamed ptag is cleared from Ready Table at W1, and set again on the CDB writeback cycle.

### 4.5 Branch Misprediction Penalty (BCC Scalar Pipeline)

| Step | v1 | v2 BCC |
|------|----|--------|
| Detection (EX1) | 1 cy | 1 cy |
| MapQ reverse replay + SMAP <- CMAP | -- | 1 cy (parallel with flush) |
| RAT flash-restore (CMAP snapshot) | 1 cy | 1 cy |
| SSB / STQ flush | -- | concurrent with RAT restore |
| Ready Table reset | -- | concurrent (mask=ALL_ONES) |
| Physical IQ CAM-clear | -- | concurrent (branch_tag match) |
| Front-end refill | ~6 cy | ~6 cy |
| **Total mispredict penalty** | **6 cy** | **6-7 cy** |

The MapQ replay and Ready Table reset run in parallel with the RAT restore and branch-tag CAM-clear -- all within the single recovery cycle.

---

## 5. Front-End: Fetch & Branch Prediction

> **(v1 → v2: 子节 5.A / 5.B / 5.C 完整复制自 v1 §5.1 / §5.2 / §5.3,内容未变更。v2 增量为 §5.1 Branch-tag allocator 与 §5.2 Static hint bit。)**

### 5.A Fetch Unit (v1 §5.1, 未变更)

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

### 5.B Branch Predictor (v1 §5.2, 未变更)

The branch predictor uses a **hybrid scheme** combining three components.

#### 5.B.1 TAGE Predictor (Conditional Branches)

| Parameter | Value |
|-----------|-------|
| Base predictor | 4K-entry bimodal (2-bit saturating counters) |
| Tagged tables | 5 tables: T1(512), T2(512), T3(1K), T4(1K), T5(1K) |
| History lengths | 4, 8, 16, 32, 64 (geometric series) |
| Tag width | 8–12 bits per entry |
| Total storage | ~20 KB |
| Prediction accuracy | ~95% (typical workloads) |

#### 5.B.2 Branch Target Buffer (BTB)

| Parameter | Value |
|-----------|-------|
| Entries | **2048** |
| Associativity | 4-way set-associative |
| Tag | partial PC (upper bits) |
| Target | full 64-bit target address |
| Hit latency | 1 cycle (available end of F1) |

#### 5.B.3 Return Address Stack (RAS)

| Parameter | Value |
|-----------|-------|
| Depth | **16** entries |
| Push | on JAL/JALR to link register |
| Pop | on JALR from link register (return pattern) |
| Speculative management | checkpoint RAS top-of-stack pointer with RAT checkpoints |

### 5.C Fetch Redirect Priorities (v1 §5.3, 未变更)

```
  Priority (highest to lowest):
    1. Branch mispredict redirect (from EX1)  — flush + restart
    2. BTB/TAGE taken-branch redirect (from F2) — next-cycle redirect
    3. Sequential PC+16 (default)
```

---

### 5.1 Branch-tag allocator (v2 增量)

A small hardware counter at D2 allocates a 3-bit branch_tag for each newly-decoded branch, drawn from the same 8-slot pool used by the v1 RAT-checkpoint store. Allocation policy is round-robin among free slots; when the pool is empty, the rename stage stalls (same condition as v1's checkpoint-pool exhaustion).

The branch_tag is then attached to:

- The branch's own RS entry.
- All RS entries dispatched **after** the branch and **before** any older branch resolves.
- All SSB entries created in the same window.
- All STQ entries created in the same window.
- Free-list pointers in the checkpoint snapshot.

When the branch resolves correctly, the tag is freed and propagated as a "tag-clear" event to all consumers (RS / SSB / STQ). When it mispredicts, the tag becomes the "flush key" — every entry tagged with this branch (or any *younger* branch tag) is invalidated atomically (§11.4.4).

### 5.2 Static hint bit (v2 增量)

The compiler may set the conditional-branch funct3's `H` bit (1 = predict taken on TAGE/BTB miss). The hint is consulted only on a predictor cold-miss; once TAGE has trained on the branch, dynamic prediction wins.

---

## 6. Decode & Rename

> **BCC scalar pipeline change: §6.A–§6.F replace the v1/v2 scalar RAT with the three-table model. §6.E (Tile RAT) and §6.F (Tile Metadata RAT) are unchanged from v2.**

### 6.A Decode Stage (D1)

D1 processes **4 instructions per cycle**, allocating a **Rename ID (RID, 6-bit)** per instruction and resolving the architectural register indices (atag) for each source operand.

| Function | Detail |
|----------|--------|
| Decode width | **4** instructions / cycle |
| RID allocation | Unique 6-bit program-order ID per decoded instruction |
| Domain classification | Opcode[6:5] -> scalar, vector, cube, MTE |
| atag resolution | Source atag: architectural register index (0-31 for GPRs) |
| Operand classification | `pclass`: P=GPR (32 regs), T=tile (32 regs), U=uncore, CARG=compile-time arg |
| Immediate extraction | Sign-extend and format-dependent extraction |
| Branch detection | Identify branch instructions for branch_tag allocation |

**D1 output uop format:**

| Field | Width | Description |
|-------|-------|-------------|
| `valid` | 1 b | Fetch bundle slot valid |
| `pc` | 64 b | Instruction PC |
| `opcode` | 12 b | Operation code |
| `src[i].atag` | 6 b | Source i architectural register index |
| `src[i].pclass` | 2 b | P=0, T=1, U=2, CARG=3 |
| `dst.atag` | 6 b | Destination architectural register index |
| `dst.pclass` | 2 b | Destination operand class |
| `rid` | 6 b | Rename ID (program order; used for age-based issue pick) |
| `checkpoint_id` | 4 b | MapQ entry ID (for flush recovery) |
| `imm` | 64 b | Immediate value |

### 6.B Rename Pipeline: D1 -> D2 -> D3

#### 6.B.1 D2: Rename Request

D2 performs register renaming in a single cycle, operating on the D1 output bundle in program order (slot 0 first, slot 3 last). Within a bundle, later slots can bypass the newly allocated ptag of earlier slots.

**P-register (GPR) rename:**

```
# Per D2 slot (processed in program order; smap_live accumulates updates):
smap_live = SMAP.copy()   # initial state from SMAP

for slot in range(4):
    u = d1_uop[slot]
    if not u.valid: continue

    # Step 1: Resolve source ptag from SMAP (live state)
    if u.src[0].pclass == P:
        src0_ptag = smap_live[u.src[0].atag]   # SMAP lookup or bypass from earlier slot
    # ... same for src[1], src[2]

    # Step 2: Allocate new ptag for P-destination
    if u.dst.pclass == P and u.dst.atag != 0:   # atag=0 is r0 (hardwired zero)
        old_ptag = smap_live[u.dst.atag]        # will become orphan
        new_ptag = allocate_from_free_list(free_list)  # lowest-numbered free ptag

        # Update SMAP (live for later slots in same group)
        smap_live[u.dst.atag] = new_ptag

        # Update refcount
        refcount[old_ptag] -= 1
        refcount[new_ptag] += 1
        if refcount[old_ptag] == 0:
            free_list.push(old_ptag)   # orphan freed immediately

        # Push MapQ entry (for flush recovery)
        mapq.push(MapQEntry {
            valid: 1,
            atag: u.dst.atag,
            old_ptag: old_ptag,
            new_ptag: new_ptag,
            rid: u.rid,
            is_push_t: 0,
            is_push_u: 0
        })
```

**Intra-group bypass**: If a source atag matches a destination atag allocated in an earlier slot of the same group, the ptag is forwarded directly from `smap_live` without an SMAP lookup. Comparator cost: 3 sources x 3 earlier slots x 6-bit = 54 comparators.

**T/U operands**: Handled by the Tile RAT independently (unchanged from v2).

**D2 uop output** (carried to D3 in a pipeline register):

| Field | Width | Description |
|-------|-------|-------------|
| `src0_ptag` | 7 b | Resolved source ptag 0 |
| `src1_ptag` | 7 b | Resolved source ptag 1 |
| `src2_ptag` | 7 b | Resolved source ptag 2 (immediate/pc-rel) |
| `pdst` | 7 b | Newly allocated destination ptag |
| `dst_atag` | 6 b | Destination atag (for MapQ entry) |
| `dst_class` | 2 b | P / T / U |
| `has_dst` | 1 b | Whether this uop writes a register |
| `src_ready` | 3 b | Ready Table init: {src0_rdy, src1_rdy, src2_rdy} from Ready Table query |

#### 6.B.2 D3: Rename Complete + Dispatch Prep

D3 is the **rename-complete boundary**. It finalizes the SMAP write and initializes the Ready Table state for each dispatched instruction:

```
# D3: finalize SMAP write
SMAP = smap_live   # atomic write of live state to SMAP

# Initialize Ready Table for each dispatched entry
for slot in range(4):
    u = d3_uop[slot]
    if u.dst_class == P and u.has_dst:
        # Clear the newly allocated ptag from Ready Table
        # (not ready until CDB writeback)
        ready_table.clear(u.pdst)

    # Source ready bits were pre-computed from Ready Table query in D2
    # (combinational; registered into IQ entry at S2)
```

D3 also assigns IQ routing:

| Op type | Target IQ | Issue width |
|---------|---------|-------------|
| ALU (ADD/SUB/AND/OR/XOR/SLT/SLL/SRL/SRA/MUL) | `alu_iq` | 4-wide |
| FSU (floating-point scalar ops) | `alu_iq` | 4-wide |
| BRU (branch/jump) | `bru_iq` | 1-wide |
| LSU (load/store) | `lsu_iq` | 2-wide |

#### 6.B.3 Rename Register State Machine

Three parallel structures manage P-reg rename:

```
CMAP [32 x 7b]:  atag -> committed ptag
  - Updated when: ptag becomes orphan + refcount=0 (freed)
  - Flush target: SMAP <- CMAP (full restore)

SMAP [32 x 7b]:  atag -> speculative ptag (active rename view)
  - Updated on each D2 group (in program order)
  - Flush: SMAP <- CMAP via MapQ reverse replay

MapQ [12-entry ring buffer]:
  Fields: {atag, old_ptag, new_ptag, rid, is_push_t, is_push_u}
  - D2: push entry for each P-dst rename
  - Flush: reverse replay from tail to head until rid > flush_rid
  - Undo per entry: SMAP[atag] <- old_ptag; refcount[new_ptag]--; refcount[old_ptag]++
  - After replay: SMAP == CMAP (exact committed state)
```

#### 6.B.4 Checkpoint Extensions (BCC Scalar Pipeline)

RAT checkpoints are replaced by MapQ for P-reg rename recovery. The branch-tag allocator and SSB/STQ head-pointer snapshot remain unchanged from v2.

| Component | v2 (RAT checkpoints) | BCC (MapQ) |
|-----------|---------------------|------------|
| P-reg recovery | 8 x 224-bit RAT snapshots | 12-entry incremental MapQ |
| T-reg recovery | 8 x 256-bit Tile RAT snapshots | Unchanged (Tile RAT independent) |
| Flush precision | Checkpoint at branch time | Instruction-precise via rid cut |
| SSB/STQ recovery | Head pointer snapshot | Unchanged |

### 6.C Free Lists

| Parameter | Value |
|-----------|-------|
| **Scalar free list** | FIFO, 96 entries (128 ptags minus 32 atags) |
| Scalar dequeue rate | up to 4 per cycle (P-destinations at D2) |
| Scalar enqueue rate | up to 4 per cycle (orphan + refcount=0) |
| **Tile free list** | FIFO, 224 entries (256 ptags minus 32 atags) — unchanged |

**Stall condition**: If the free list cannot supply enough ptags for the current D2 group, the rename pipeline stalls at D1.

### 6.D Intra-Group Bypass Logic

When 4 instructions are renamed simultaneously, later instructions in the group may depend on earlier ones. Hardware **priority-encoded comparators** detect these intra-group dependencies:

```
  Scalar example:
    D2 slot 0:  X5 -> P40  (destination)
    D2 slot 1:  reads X5  -> comparator detects match -> bypass P40
    D2 slot 2:  X5 -> P41  (re-definition)
    D2 slot 3:  reads X5  -> comparator detects slot 2 match -> bypass P41

    4 slots x 2 sources x 3 older slots = 24 comparators (7-bit each)
    + 8 bypass MUXes (select forwarded ptag vs SMAP read)

  Tile example (unchanged from v2):
    D2 slot 0:  TILE.LD T10  -> PT200  (destination)
    D2 slot 1:  VADD dst=T10 -> PT201  (re-definition)
    D2 slot 2:  reads T10    -> comparator detects slot 1 match -> bypass PT201

    4 slots x 3 tile sources x 3 older slots = 36 comparators (8-bit each)
    + 12 bypass MUXes
```

### 6.E Tile RAT — unchanged from v2 (SS6.2)

All tile-consuming domains (vector, cube, MTE) share a single **Tile RAT** that renames 32 architectural tile registers (T0-T31) to 256 physical tile slots (PT0-PT255) in TRegFile-4K. The Tile RAT is **independent** of the P-reg rename pipeline (SMAP/CMAP/MapQ) and is **unchanged** from v2.

| Parameter | Value |
|-----------|-------|
| Architectural tile registers | 32 (T0-T31) |
| Physical tile registers | **256** (PT0-PT255), 4 KB each in TRegFile-4K |
| Tile RAT storage | 32 entries x 8 bits = **256 bits** |
| Read ports | **8** (up to 3 source tiles x 4 decode slots, shared/muxed) |
| Write ports | **4** (1 destination tile x 4 decode slots) |

### 6.F Tile Metadata RAT — unchanged from v2 (SS6.1)

A **256 x 32 b SRAM** stores the metadata word `(shape.x, shape.y, format)` per physical tile. Access pattern unchanged from v2 SS6.1.

---

## 7. Dispatch & Issue

### 7.1 Dispatch (S1 / S2)

After rename (D1->D2->D3), each uop is dispatched through the **S1 / S2** two-stage dispatch.

**S1 -- Dispatch Preparation**: Checks resource availability before writing IQ entries.

**S2 -- Dispatch Execute**: Writes IQ entries and updates free lists.

**S1 resource checks:**

| Check | Condition | Recovery |
|-------|-----------|----------|
| Scalar free list | `free_mask` has >= N free ptags for dispatched P-dst ops | Stall at D1 |
| MapQ space | `mapq.count < 11` (keep 1 slot safety margin) | Stall at D1 |
| `alu_iq` space | >= N alu-class slots in current dispatch group | Stall at S1 |
| `bru_iq` space | >= N bru-class slots in current dispatch group | Stall at S1 |
| `lsu_iq` space | >= N lsu-class slots in current dispatch group | Stall at S1 |
| Tile free list | >= N free tile ptags | Stall at D1 |

**IQ routing (from D3 assignment):**

| Op type | Target IQ | Issue width |
|---------|---------|-------------|
| ALU (ADD/SUB/AND/OR/XOR/SLT/SLL/SRL/SRA/MUL) | `alu_iq` | 4-wide |
| FSU (floating-point scalar ops) | `alu_iq` | 4-wide |
| BRU (branch/jump) | `bru_iq` | 1-wide |
| LSU (load/store) | `lsu_iq` | 2-wide |
| Vector ops | Vector RS | 1-wide |
| Cube ops | Cube RS | 1-wide |
| MTE ops | MTE RS | 2-wide |

**S2 dispatch execute:**

```
for slot in dispatched_slots:
    iq_type = s1_iq_route[slot]
    entry_idx = iq_alloc.allocate(iq_type)

    iq[entry_idx].valid = 1
    iq[entry_idx].src0_ptag = d3_uop[slot].src0_ptag
    iq[entry_idx].src1_ptag = d3_uop[slot].src1_ptag
    iq[entry_idx].src2_ptag = d3_uop[slot].src2_ptag
    iq[entry_idx].pdst = d3_uop[slot].pdst
    iq[entry_idx].src_ready = d3_uop[slot].src_ready
    iq[entry_idx].rid = d3_uop[slot].rid
    iq[entry_idx].lsid = d3_uop[slot].lsid
    iq[entry_idx].checkpoint_id = d3_uop[slot].checkpoint_id
    iq[entry_idx].branch_tag = current_branch_tag

free_mask &= ~allocated_ptags
```

### 7.2 Physical IQ Entry Formats

#### ALU IQ Entry (48 entries, ~95 bits each)

| Field | Width | Description |
|-------|-------|-------------|
| `valid` | 1 b | Entry occupied |
| `rid` | 6 b | Rename ID (program order, used for age) |
| `op` | 12 b | Operation code |
| `imm` | 64 b | Immediate value |
| `src0_ptag` | 7 b | Source 0 ptag |
| `src1_ptag` | 7 b | Source 1 ptag |
| `src2_ptag` | 7 b | Source 2 ptag |
| `pdst` | 7 b | Destination ptag |
| `has_dst` | 1 b | Whether this uop writes a register |
| `src_ready` | 3 b | Ready bits: {src0_rdy, src1_rdy, src2_rdy} |
| `checkpoint_id` | 4 b | MapQ entry ID |
| `branch_tag` | 3 b | Speculation branch tag |

**Total: 48 x ~95 b approx 570 B**

#### BRU IQ Entry (16 entries, ~120 bits each)

| Field | Width | Description |
|-------|-------|-------------|
| `valid` | 1 b | Entry occupied |
| `rid` | 6 b | Rename ID (program order) |
| `op` | 12 b | Operation code |
| `pc` | 64 b | Instruction PC |
| `src0_ptag` | 7 b | Source 0 ptag |
| `src1_ptag` | 7 b | Source 1 ptag |
| `pdst` | 7 b | Destination ptag (branch target register) |
| `has_dst` | 1 b | Whether this uop writes a register |
| `src_ready` | 2 b | Ready bits: {src0_rdy, src1_rdy} |
| `checkpoint_id` | 4 b | MapQ entry ID |
| `branch_tag` | 3 b | Speculation branch tag |
| `pred_taken` | 1 b | Branch prediction direction |

**Total: 16 x ~120 b approx 240 B**

#### LSU IQ Entry (32 entries, ~104 bits each)

| Field | Width | Description |
|-------|-------|-------------|
| `valid` | 1 b | Entry occupied |
| `rid` | 6 b | Rename ID (program order) |
| `op` | 12 b | Operation code |
| `lsid` | 32 b | Load-Store ID |
| `src0_ptag` | 7 b | Base register ptag |
| `src1_ptag` | 7 b | Offset register ptag |
| `pdst` | 7 b | Destination ptag |
| `has_dst` | 1 b | Whether this uop writes a register |
| `src_ready` | 2 b | Ready bits: {src0_rdy, src1_rdy} |
| `checkpoint_id` | 4 b | MapQ entry ID |
| `branch_tag` | 3 b | Speculation branch tag |
| `addr_ready` | 1 b | AGU address computation complete |

**Total: 32 x ~104 b approx 416 B**

**Key difference from v1 RS entries**: No per-entry `age` field -- age is encoded in the `rid` (6-bit Rename ID, program order). No per-entry `rdy1`/`rdy2` ready bits -- ready state is maintained in the **Ready Table** and checked at issue time.

### 7.3 Ready Table

The Ready Table is a **128-bit bitmap** that tracks which ptags have valid values. It replaces the `O(iq_depth x issue_w x pregs)` CDB comparator array with `O(1)` bit-tests.

```
Ready Table: bit[i] = 1 means ptag i has a valid value

set(ptag):    mask |= (1 << ptag)     # Called on CDB writeback
clear(ptag):  mask &= ~(1 << ptag)    # Called on ptag allocation at D2
is_ready(i):  return (mask >> i) & 1
read(i):       return is_ready(i)       # Combinational read for can_issue
```

**Update rules per cycle:**

| Event | Action |
|-------|--------|
| D2 dispatch: ptag allocated | `ready.clear(pdst)` -- set bit=0 |
| CDB writeback | `ready.set(wb.ptag)` -- set bit=1 |
| Flush | `ready.mask <= ALL_ONES` -- conservative reset |

**Can_issue computation (P1, combinational):**

```
for each IQ entry e:
    src0_rdy = ready_table.read(e.src0_ptag)   # O(1) bit-test
    src1_rdy = ready_table.read(e.src1_ptag)
    src2_rdy = ready_table.read(e.src2_ptag)
    e.can_issue = e.valid & src0_rdy & src1_rdy & src2_rdy
```

**Wakeup timing:**

| Cycle | Event |
|-------|-------|
| W1 | CDB broadcasts ptag P40 is ready |
| W1 (end) | `ready_next = ready | {P40}` registered |
| Clock edge | Ready Table Register <= ready_next |
| N+1 P1 | `can_issue` recomputed with new Ready Table |
| N+1 P1 | Age-matrix pick selects winners |
| N+1 I1 | RF read-port arbitration |
| N+2 I2 | Issue confirm; IQ entry deallocated |

**Total wakeup latency: 2 cycles** (Ready Table register -> can_issue -> pick -> RF read).

### 7.4 Age-Matrix Issue Picker

The issue picker is **purely combinational logic** (no state). For each physical IQ, a cascaded priority encoder selects the oldest-ready entries.

**Age encoding**: RID is a 6-bit program-order counter. Sub-head age:
```
age = (entry.rid - head_rid) mod 64
```
Smaller age = older instruction = higher priority. Mod-64 arithmetic handles wrap correctly.

**Cascaded pick (alu_iq, 4-wide):**

```
selected = []
excluded = set()
for lane in range(4):   # alu_w = 4
    winner = None
    best_age = 0x3F     # Max RID value = youngest
    for entry in alu_iq.entries:
        if entry not in excluded and entry.can_issue:
            age = (entry.rid - head_rid) & 0x3F   # mod-64 wrap-friendly
            if age < best_age:
                best_age = age
                winner = entry
    if winner:
        selected.append(winner)
        excluded.add(winner)
    else:
        selected.append(None)   # Lane empty
```

**Per-IQ issue widths:**

| IQ | Issue width | Description |
|----|-------------|-------------|
| `alu_iq` | 4 | 4x ALU + FSU |
| `bru_iq` | 1 | 1x BRU |
| `lsu_iq` | 2 | 1x Load + 1x Store |
| Vector RS | 1 | 1x VEC-4K-v2 op |
| Cube RS | 1 | 1x outerCube op |
| MTE RS | 2 | 2x MTE ops |

### 7.5 Issue Stages: P1 / I1 / I2

#### P1 -- Issue Pick

The P1 stage performs the **Ready Table query and age-matrix cascaded pick** (described in SS7.3 and SS7.4). Results are registered at the end of P1.

#### I1 -- Operand Read Planning

The I1 stage arbitrates **physical RF read-port access** across all 7 issue slots (alu_iq x 4 + bru_iq x 1 + lsu_iq x 2). The scalar RF has 12 read ports (8 from rename + 4 from issue). Port conflicts are resolved by priority.

#### I2 -- Issue Confirm

The I2 stage **deallocates IQ entries** for the selected instructions and confirms RF read-port occupancy. The physical RF read operation itself begins in I2 (registered input to RF) with data available at the start of E1.

#### 7.4 GVIQ — Grouped Vector Issue Queue (VTG Micro-Instructions)

> **(v2.2 BCC vector overlay — Change Point #2)**

The GVIQ holds VTG vector micro-instructions waiting for their source VTGs to become ready. It is **separate from** the existing Vector RS (24 entries, full-tile `T*` ops). The two paths share the VEC-4K-v2 ALU, TRegFile ports (R0/R4 for reads, W0 for writeback), and SA/SB/SC staging registers.

**VEC-domain arbitration:** The VEC-4K-v2 ALU is 1-wide. The VEC-domain arbiter grants ALU access based on readiness and priority. Full-tile VEC ops (from Vector RS) have **higher priority** than VTG micro-ops (from GVIQ) because they are coarser-grain and hold the prologue longer.

**GVIQ entry** (before operand fields):

| Field | Width | Description |
|-------|-------|-------------|
| `valid` | 1 b | Entry is live |
| `block_id` | 12 b | Index into micro-instruction buffer (shared by all VTGs in this block) |
| `pc_index` | 8 b | Current micro-instruction within block (0..63) |
| `tile_group` | 5 b | Architectural tile T0..T31 |
| `phys_tile` | 8 b | Physical tile PT0..PT255 (after Tile RAT rename) |
| `group_id` | 4 b | VTG index: 0..15 (`G256`) or 0..7 (`G512`) |
| `group_mode` | 1 b | `G256` (0) or `G512` (1) |
| `thread_id` | 8 b | Scheduler context (usually = `group_id`) |
| `iter0..iter3` | 4x16 b | Loop counters for VTG's current iteration |
| `active_lanes` | 16 b | Active lane count or mask |
| `active_group_mask` | 16 b | Which VTG groups are active in this block |

**GVIQ operand fields** (after prefix):

| Field | Width | Description |
|-------|-------|-------------|
| `src0_ptag` | 8 b | Physical tile tag for source VTG 0 |
| `src1_ptag` | 8 b | Physical tile tag for source VTG 1 |
| `src2_ptag` | 8 b | Physical tile tag for source VTG 2 |
| `pred_ptag` | 8 b | Physical tile tag for predicate VTG |
| `dst_ptag` | 8 b | Physical tile tag for destination VTG |
| `has_dst` | 1 b | Whether this micro-op writes a VTG |
| `src_ready` | 4 b | VTG-ready bits: src0/1/2/pred ready |
| `vtg_ready` | 1 b | All source VTGs ready + loop counters ready |
| `branch_tag` | 3 b | Branch tag for speculation gating |

**Micro-instruction buffer** (in vector ALU):

The micro-instruction buffer is a **set-associative buffer** (16 entries, 2-way) keyed by `block_id`. Each entry stores the pre-decoded micro-op list for a tile group:

```text
BufferEntry {
  valid:     1 b
  block_id:  12 b  [tag]
  pc_limit:  8 b   [max pc_index]
  micro_ops: array[64] of MicroOpEntry
}

MicroOpEntry {
  opcode:     12 b   // VADD / VMUL / VCMP / VLD / VST / ...
  elem_type:  4 b   // FP32 / FP16 / FP8 / FP4 / ...
  pred_mode:  1 b   // 0=zeroing, 1=merging
  src0_ref:   VTGRef | Scalar | Imm
  src1_ref:   VTGRef | Scalar | Imm | None
  src2_ref:   VTGRef | Scalar | Imm | None
  dst_ref:    VTGRef
  pred_ref:   VTGRef | implicit_all_true
  imm:        32 b  // immediate or address offset
}
```

At P1/I1 issue time, the GVIQ winner's `{block_id, pc_index}` drives a single-cycle buffer lookup. The `MicroOpEntry` drives VEC staging control, ALU opcode, and writeback routing — no re-decode needed.

**VTG wakeup:** VTG readiness is tracked by a **256-bit VTG Ready Table** (one bit per physical tile PT0..PT255). When a VTG micro-op writes back, its `dst_ptag` sets the corresponding bit. When a VTG is dispatched, its source `ptag` bits are cleared.

**VTG rotation scheduling:**

```python
while any VTG active in block:
    winner = gviq.pick_oldest_ready()          # age = (rid - head_rid) mod 64
    micro_op = buffer.lookup(winner.block_id, winner.pc_index)
    SA = TRegFile.read(winner.src0_ptag)       # full 4 KB tile
    SB = TRegFile.read(winner.src1_ptag)
    SA_vtg = select_vtg(SA, winner.group_id, winner.group_mode)   # 256/512 B sub-range
    result = vec_alu.execute(micro_op, SA_vtg, SB_vtg)
    TRegFile.write_vtg(winner.dst_ptag, winner.group_id, result)
    winner.pc_index++
    if loop_end(winner): winner.iterN--, winner.pc_index = loop_start
    if all_iters_done(winner): winner.valid = 0
```

**Issue rules:**

| Rule | Description |
|------|-------------|
| GVIQ-1 | `pc_index <= pc_limit` for the given `block_id` |
| GVIQ-2 | All source VTG `src_ready` bits set |
| GVIQ-3 | Active loop counter (`iter*`) non-zero |
| GVIQ-4 | GVIQ is 1-wide: one VTG micro-op per cycle |
| GVIQ-5 | VEC-4K-v2 ALU is single-ported per VTG: one VTG per VEC beat |
| GVIQ-6 | Paired `G256` issue (optional): two independent 256 B VTGs share one 512 B SIMD group beat if `{opcode, elem_type, pred_mode}` match |

### 7.6 Dispatch Stall Conditions

| Condition | Recovery |
|-----------|----------|
| Target IQ is full | Wait for entry to be freed |
| Scalar free list empty | Wait for refcount-driven free |
| MapQ space exhausted | Wait for MapQ eviction on branch resolve |
| All branch-tag slots occupied | Wait for an in-flight branch to resolve |
| SSB full | Wait for SSB entry to drain |
| STQ full | Wait for STQ entry to drain |

---

## 8. Execution Units

### 8.1 Scalar Unit

> **(v1 → v2: 内容未变更,以下完整复制自 v1 §8.1。)**

The scalar unit contains **6 functional units** sharing the Scalar RS.

#### 8.1.1 ALU (×4) — (v1 §8.1.1, 未变更)

Four identical single-cycle ALUs handle integer arithmetic, logic, shift, and compare operations.

| Parameter | Value |
|-----------|-------|
| Count | **4** symmetric ALUs |
| Operations | ADD, SUB, AND, OR, XOR, SLL, SRL, SRA, SLT, SLTU, LUI, AUIPC |
| Latency | **1** cycle |
| Throughput | **4** ops / cycle |
| Input width | 64-bit |

#### 8.1.2 MUL/DIV Unit (×1) — (v1 §8.1.2, 未变更)

| Parameter | Value |
|-----------|-------|
| MUL latency | **4** cycles (pipelined, 1 MUL issued/cycle) |
| MUL operations | MUL, MULH, MULHU, MULHSU, MULW |
| DIV latency | **12–20** cycles (non-pipelined, blocks MUL during execution) |
| DIV operations | DIV, DIVU, REM, REMU, DIVW, DIVUW |

#### 8.1.3 Branch Unit (×1) — (v1 §8.1.3, 未变更)

| Parameter | Value |
|-----------|-------|
| Latency | **1** cycle (compare + resolve) |
| Operations | BEQ, BNE, BLT, BGE, BLTU, BGEU, JAL, JALR |
| On correct prediction | Deallocate checkpoint; no pipeline impact |
| On mispredict | Flash-restore RAT; flush pipeline stages F1–IS; redirect fetch |
| Mispredict penalty | **6** cycles (front-end refill) |

### 8.2 Load/Store Unit (LSU)

> **(v1 → v2: §8.2.1 架构与 §8.2.2 参数完整复制自 v1。v2 用 SSB §8.2.3 替换 v1 §8.2.3 简化提交。)**

The LSU handles all scalar memory operations with a **simplified** design enabled by the no-exception guarantee. The LSU pipeline is identical to v1 in terms of address calculation, TLB access, cache lookup, and L1-D MSHRs. The **store path** is the only structural change: stores no longer commit directly to L1-D; instead, they pass through a **Speculative Store Buffer** (SSB) that gates them by branch tag.

#### 8.2.A Architecture (v1 §8.2.1, 未变更)

```
  ┌──────────────────────────────────────────────────────────┐
  │  Load/Store Unit                                          │
  │                                                          │
  │  LSU RS (24 entries) ──┬──▶ Load Pipeline  (EX1–EX4)    │
  │                        └──▶ Store Pipeline (EX1–EX4)    │
  │                                                          │
  │  ┌─────────────────┐    ┌─────────────────┐             │
  │  │ Load Queue       │    │ Store Buffer →  │             │
  │  │ (16 entries)     │    │  SSB (24 ent.)  │  ← v2       │
  │  │ addr + tag       │    │ addr + data +   │             │
  │  │                  │    │ branch_tag      │             │
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

#### 8.2.B Key Parameters (v1 §8.2.2, 未变更 except store buffer entries)

| Parameter | v1 | v2 |
|-----------|----|----|
| Load pipeline latency | **4** cycles (address calc + TLB + cache access + align) | **4** cycles (unchanged) |
| Store pipeline latency | **4** cycles (address calc + TLB + write to store buffer) | **4** cycles (unchanged) |
| Load queue entries | **16** | **16** (unchanged) |
| Store buffer entries | **16** | **24** (now SSB §11.4) |
| Store-to-load forwarding | Full forwarding when address and size match | Full forwarding (now from SSB §11.4.3) |
| L1-D MSHRs | **8** (non-blocking, 8 outstanding misses) | **8** (unchanged) |
| D-TLB | 64 entries, fully associative | unchanged |

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
| VADD / VMUL / VFMA_ACC (binary or Acc-feedback ternary) | any | 1024..8192 elements | 16 cy (8 fetch + 8 retire); throughput 1/8 cy |
| **VFMA / VFNMA** (native 3-source, `c_role=VALUE`, §2.2.6a) | any | 1024..8192 elements | **16 cy (8 fetch on R0/R4/R1 + 8 retire); throughput 1/8 cy** — same as binary thanks to 3-port parallel fetch |
| **VLERP** (native 3-source, dual retire D0/D1, §2.2.6a) | any | 1024..8192 elements | **24 cy (8 fetch + 16 retire); throughput 1/16 cy** |
| VADD masked | any | any | same as unmasked (mask piggybacks) |
| VFMA_ACC with `is_xpose_A ≠ is_xpose_B` | any | any | **24 cy** (16 fetch + 8 retire); throughput 1/16 cy |
| **VFMA with one mismatched `is_xpose_*`** | any | any | **24 cy** (16 fetch + 8 retire); throughput 1/16 cy |
| **VFMA with all three `is_xpose_*` distinct** (degenerate) | any | any | **32 cy** (24 fetch + 8 retire); throughput 1/24 cy |
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

#### 8.3.10 VTG Vector Micro-Instruction Execution Mode (v2.2)

> **(Change Point #2 — VTG / SIMD-group overlay)**

The VEC-4K-v2 datapath supports two execution modes. **VTG operates behind VEC-4K-v2's staging registers** -- VTGs share SA/SB/SC with the full-tile path and do not introduce new staging structures or new TRegFile ports. VTG operands are sub-ranges of tiles that VEC-4K-v2 has already staged in SA/SB/SC via its 8-cycle operand-fetch prologue. VTG reads from SA/SB at the **ALU input mux** after the prologue completes.

| Mode | Execution unit | VTG size | SIMD groups | TRegFile access |
|------|---------------|----------|-------------|----------------|
| **Full-tile `T*`** | One VEC tile op | 4 KB | Full epoch (8 cycles) | Full 4 KB read/write |
| **VTG `V*` micro-op** | One VTG micro-op | 256 B or 512 B | 1 or 2 SIMD group beats | VTG sub-range via Group Read/Write Adapters. **VTG latency: 25-32 cy minimum** (8-15 cy prologue + 1 cy compute + 16 cy RMW writeback). VTG operates behind VEC-4K-v2 staging (SA/SB/SC) and reuses TRegFile ports (R0/R4/W0). |

**VEC staging reuse for VTG micro-ops:**

| Staging Register | VTG Micro-Op Use |
|-----------------|-----------------|
| `SA` | VEC-4K-v2 operand A staging (4 KB, filled by R0). VTG reads 256/512 B sub-range from SA at ALU input mux via Group Read Adapter. |
| `SB` | VEC-4K-v2 operand B staging (4 KB, filled by R4). VTG reads 256/512 B sub-range from SB at ALU input mux. Scalar broadcast via SX/SY unchanged. |
| `SC` | Predicate VTG (or third source VTG for wide ops) |
| `SX / SY` | Scalar operand broadcast / loop counter broadcast |
| `SOP` | VEC beat word from micro-instruction buffer (pre-decoded VEC beat-word sequence generated by VTG microassembler at decode time). One `VECBeatWord` drives VEC ALU for one cycle. |

**SIMD lane mapping:**

| Element type | Lanes per 512 B VTG | Lanes per 256 B VTG |
|-------------|---------------------|---------------------|
| FP32 / INT32 | **128** | 64 |
| FP16 / BF16 | **256** | 128 |
| FP8 | **512** | 256 |
| FP4 | **1024** | 512 |

**Group Read Adapter** (ALU input mux -- after prologue):

```text
input:  SA_full[4096 B] (from VEC prologue), SB_full[4096 B], group_id, group_mode
G256:   vtg_A[256 B] = SA_full[group_id * 256 : (group_id+1) * 256]
        vtg_B[256 B] = SB_full[group_id * 256 : (group_id+1) * 256]
G512:   vtg_A[512 B] = SA_full[group_id * 512 : (group_id+1) * 512]
        vtg_B[512 B] = SB_full[group_id * 512 : (group_id+1) * 512]
output: vtg_A, vtg_B -> VEC ALU operand mux
Note:  Group Read Adapter reads from SA/SB (4 KB), NOT directly from TRegFile.
      VEC prologue fills SA/SB over 8-cycle epoch before this mux activates.
```

**Group Write Adapter** (VEC result -> TRegFile, full-tile RMW):

```text
input:  vtg_result[256/512 B], dst_ptag, group_id, group_mode

// Step 1: Read full current tile (occupies W0 for 8 cycles)
TRegFile.submit_read(dst_ptag)       -- occupies W0 for 8-cycle epoch --
wait 8 cycles
old_tile = TRegFile.read_data        -- 4 KB --

// Step 2: Merge VTG result into correct sub-range
if group_mode == G256:
    start = group_id * 256; end = start + 256
else:  -- G512 --
    start = group_id * 512; end = start + 512
new_tile = old_tile
new_tile[start:end] = vtg_result     -- merge sub-range --

// Step 3: Write merged tile back (occupies W0 for 8 cycles)
TRegFile.submit_write(dst_ptag, new_tile)
wait 8 cycles
TRegFile.write_complete()

-- Total RMW latency: 16 cycles minimum (8 read + 8 write) --
// update VTG_metadata[dst_ptag][group_id] = {valid=1, defined=1, dirty=1}
```

> **Note:** TRegFile has no partial-write mechanism. All writes are full-tile, 512 B/cy x 8 cycles. The Group Write Adapter must read the current tile, merge the VTG sub-range, and write the full tile back. W0 is occupied for the full 16-cycle RMW cycle, blocking other tile writes.

**Micro-instruction buffer integration:** At decode time, the VTG microassembler generates a pre-decoded `VECBeatWord` sequence for each VTG micro-op and writes it into the buffer. At P1/I1, `beat_word = buffer.lookup(block_id, pc_index)` drives VEC ALU for one cycle. The buffer lookup happens in parallel with the GVIQ pick -- both are 1-cycle combinational operations. The beat word is the same format as VEC-4K-v2's SOP beat word, enabling seamless integration with the existing VEC datapath without new microcode structures.

**Paired `G256` issue (optional):** When two 256 B VTGs have matching `{opcode, elem_type, pred_mode}`, the VTG rotation scheduler may issue them together, filling the full 512 B SIMD group beat. GVIQ-6 and the VEC-domain arbiter must still resolve port conflicts (both VTGs need W0 for writeback).

**No special speculation hardware is needed inside the vector unit.** The flush converges to a quiescent state in `T_fetch + max_beat_count` cycles in the worst case (a long-running `TINV` or `TMRGSORT` taking the full hit), but this is bounded by the number of in-flight vector ops (≤ 24 RS entries) and does not stall the front-end's recovery.

### 8.4 Cube Unit (outerCube MXU)

> **(v1 → v2: §8.4.1 / §8.4.2 完整复制自 v1。v2 增量见本节末:cube 现可消费 TRegFile-4K 的 col-mode 读出。)**

The cube unit is the outerCube Matrix Unit, a large-scale outer-product accumulation engine. Full specification is in [`outerCube.md`](outerCube.md).

#### 8.4.1 Summary (v1 §8.4.1, 未变更)

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

#### 8.4.2 Cube Instruction Dispatch (v1 §8.4.2, 未变更)

Cube instructions (CUBE.OPA, CUBE.DRAIN, etc.) are dispatched to the **Cube RS** (4 entries) after Tile RAT rename. Each CUBE.OPA is a long-running instruction that occupies the MXU for many cycles (N + 18, where N = Nb × S OPA steps). While the MXU is busy, the Cube RS holds subsequent cube instructions until the current one completes.

A CUBE.OPA may reference a range of architectural tile registers (e.g., T[Tb]..T[Tb+Na−1]). At dispatch, the Tile RAT translates each architectural tile index to a physical tile index. For multi-tile operands, the cube RS stores a base physical tile index plus a **tile address table** (up to 16 entries) holding the physical indices of all tiles in the range; the cube pipeline controller uses these physical indices to program TRegFile-4K port addresses.

The cube unit reads tile data from TRegFile-4K ports R0 (A operand) and R1–R4 (B operand), and drains results via W0 (C output). Port interactions are managed by the cube pipeline controller, which issues epoch-aligned physical tile addresses to TRegFile-4K's pending registers (see [`tregfile4k.md`](tregfile4k.md) §3).

#### 8.4.3 v2 增量(对硬件不可见)

The cube unit benefits indirectly from the TRegFile-4K `is_transpose` enhancement: software can now feed the cube either row-major or col-major B-operand tiles by setting `is_xpose` on the cube's B-operand tile-RAT entries (the cube pipeline controller propagates the bit to TRegFile read ports R1–R4), eliminating the need for `TILE.TRANSPOSE` predecessors in many GEMM kernels. The cube ALU, accumulator, and pipeline are otherwise unchanged.

### 8.5 MTE Unit

> **(v1 → v2: §8.5.A / §8.5.B / §8.5.C / §8.5.D 完整复制自 v1 §8.5.1 / §8.5.2 / §8.5.3 / §8.5.4 / §8.5.5。v2 增量集中在 §8.5.1 (TRANSPOSE 缩减) 与 §8.5.2 (STQ)。)**

The MTE unit is the **bridge between three domains**: memory ↔ TRegFile-4K (bulk tile transfers) and scalar GPR ↔ TRegFile-4K (single-element access via TILE.GET/TILE.PUT). All MTE instructions go through full **dual-RAT rename** at D2: scalar operands are renamed via the Scalar RAT, and tile operands are renamed via the Tile RAT. Instructions that produce a new tile (TILE.LD, TILE.ZERO, TILE.COPY, TILE.GATHER, TILE.PUT) allocate a fresh physical tile from the tile free list. TILE.GET produces a scalar GPR result and broadcasts on the CDB.

#### 8.5.A Architecture (v1 §8.5.1, 未变更)

```
  ┌──────────────────────────────────────────────────────────────────┐
  │  Memory Tile Engine (MTE)                                        │
  │                                                                  │
  │  MTE RS (16 entries) ──┬──▶ Load Tile Pipeline                  │
  │                        ├──▶ Store Tile Pipeline ──▶ STQ (v2)    │
  │                        ├──▶ Gather Pipeline                     │
  │                        ├──▶ Scatter Pipeline ──▶ STQ (v2)       │
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

#### 8.5.B Key Parameters (v1 §8.5.2, 未变更)

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

#### 8.5.C MTE Rename → Issue → Execute Flow (Bulk Transfer) — (v1 §8.5.3, 未变更)

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

#### 8.5.D TILE.GET / TILE.PUT Execution Flow (Element Access) — (v1 §8.5.4, 未变更)

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

#### 8.5.E TILE.MOVE (Move Elimination) — (v1 §8.5.5, 未变更)

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

---

**v2 增量(下面 §8.5.1 / §8.5.2 / §8.5.3 / §8.5.4):**

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

> **(v1 → v2: 内容未变更,以下完整复制自 v1 §9.1。)**

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

#### 8.3.11 Worked Examples: TSOFTMAX (Full-Tile ROM) and TSOFTMAX_VTG (VTG Variant)

This section walks through two concrete instantiations of the same algorithm: (A) as a **full-tile VEC-4K-v2 instruction** driven by the microcode ROM, and (B) as a **VTG micro-instruction** driven by the micro-instruction buffer. Both execute the same five-pass TSOFTMAX algorithm; the difference is the scheduling context and where the beat-word sequence comes from.

##### 8.3.11.1 Algorithm: TSOFTMAX Along the Row Axis

For a tile shaped **8 x 128 FP32** (W = 512 B, R = 8), softmax along each row is:

```
Pass 1 -- row_max[i] = max(input[i][*])                              [col-reduce]
Pass 2 -- diff[i][j]  = exp(input[i][j] - row_max[i])              [elementwise SUB + EXP]
Pass 3 -- row_sum[i]  = SIGMA_j diff[i][j]                              [col-reduce]
Pass 4 -- inv_sum[i]   = 1.0 / row_sum[i]                           [scalar RECIP]
Pass 5 -- output[i][j] = diff[i][j] * inv_sum[i]                   [elementwise MUL]
```

##### 8.3.11.2 ROM Entry for Full-Tile TSOFTMAX

The VEC-4K-v2 microcode ROM is keyed by `(opcode, format, W-regime, R-regime)`. The TSOFTMAX ROM entry for FP32, W = 512 B (one strip), R = 8 is:

```
ROM[TSOFTMAX, FP32, W=512B, R=8] --> {
  ucode_base:  <addr>
  ucode_len:   42    <-- 9+8+9+1+8+7 = 42 beats
  shape:       (R=8, C=128, E=4, format=FP32)
  N_strips:    8
  elem_per_strip: 128  (512 B / 4 B)
}
```

**VECBeatWord format** (from `vector4k_v2.md` SS5.4):

```
VECBeatWord {
  src_A, src_B, src_Z : 3x3 b   <-- {SA, SB, SX, SY, ACC_LO, ACC_HI, IMM_ZERO, --}
  s_A, s_B              : 2x3 b   <-- strip index 0..7
  xp_A, xp_B            : 2x1 b   <-- 0 (row-mode; TSOFTMAX is purely row-axis)
  mask_src               : 2 b     <-- {SC_mask, IMM_ALL_ONES, IMM_FROM_SOP}
  mask_strip             : 3 b
  alu_op                 : 5 b     <-- {ADD, SUB, MUL, MAX, PASS_A, RECIP, ...}
  acc_op                 : 3 b     <-- {NONE, INIT, ACCUM, MERGE_STAGE, READOUT}
  acc_slot               : 4 b     <-- 0..15
  wr_en_D0, wr_en_D1    : 1 b each
  wr_strip_D0, wr_strip_D1 : 3 b each
}
```

##### 8.3.11.3 Beat-Word Sequence (Full-Tile TSOFTMAX, 42 Beats)

```
-- === PASS 1: row_max = max(input[i][*]) ===
-- Beats 0-8: col-reduce via MAX on accumulator slot 0

beat  0: INIT   src_A=SA, s_A=strip0,
              src_B=ACC_LO, alu_op=PASS_A,
              acc_op=INIT, acc_slot=0
              -- acc[0] <-- SA[strip0] (128 FP32 elements)

beats 1-7: ACCUM src_A=SA, s_A=strip[1..7],
              src_B=ACC_LO, alu_op=MAX,
              acc_op=ACCUM, acc_slot=0
              -- acc[0] <-- MAX(acc[0], SA[strip_j]) pairwise

beat  8: READOUT src_A=ACC_LO, alu_op=PASS_A,
              acc_op=READOUT, acc_slot=0
              -- broadcast row_max to all 128 lanes via SX

-- === PASS 2: diff = exp(input - row_max) ===
-- Beats 9-16: elementwise SUB, write to scratch tile T_scratch

beats 9-16: src_A=SA, s_A=strip[0..7],
              src_B=SX, alu_op=SUB,
              mask_src=IMM_ALL_ONES,
              acc_op=NONE,
              wr_en_D0=1, wr_strip_D0=strip[0..7]
              -- diff_strip[j] = SA[strip_j] - SX(row_max)
              -- written to T_scratch via D0, one strip per beat
              -- actual ROM folds SUB+EXP into a single EXP beat
              -- with a preceding subtract (two beats per strip)

-- === PASS 3: row_sum = SIGMA_j diff[i][j] ===
-- Beats 17-25: col-reduce via ADD on accumulator slot 1

beat 17: INIT   src_A=T_scratch, s_A=strip0,
              src_B=ACC_LO, alu_op=PASS_A,
              acc_op=INIT, acc_slot=1
              -- acc[1] <-- T_scratch[strip0]

beats 18-24: ACCUM src_A=T_scratch, s_A=strip[1..7],
              src_B=ACC_LO, alu_op=ADD,
              acc_op=ACCUM, acc_slot=1
              -- acc[1] <-- ADD(acc[1], T_scratch[strip_j]) pairwise

beat 25: READOUT src_A=ACC_LO, alu_op=PASS_A,
              acc_op=READOUT, acc_slot=1
              -- broadcast row_sum to all 128 lanes via SX

-- === PASS 4: inv_sum = 1.0 / row_sum ===
-- Beat 26: RECIP

beat 26: src_A=SX, alu_op=RECIP,
          acc_op=READOUT, acc_slot=1
          -- inv_sum = RECIP(row_sum), broadcast via SY

-- === PASS 5: output = diff * inv_sum ===
-- Beats 27-34: elementwise MUL, retire to D0

beats 27-34: src_A=T_scratch, s_A=strip[0..7],
              src_B=SY, alu_op=MUL,
              mask_src=IMM_ALL_ONES,
              acc_op=NONE,
              wr_en_D0=1, wr_strip_D0=strip[0..7]
              -- out[strip_j] = T_scratch[strip_j] x SY(inv_sum)
              -- retired to D0, one strip per beat

-- === Finalize ===
-- Beats 35-41: flush pending retire
beats 35-41: wr_en_D0=1, wr_strip_D0=strip[0..7]

Pipeline timing (full-tile TSOFTMAX):
  Fetch prologue:     8 cycles  (TRegFile R0 + R4, 1 epoch)
  Compute:           42 cycles  (5 passes)
  Retire:             8 cycles  (W0, 1 epoch)
  End-to-end:        ~58 cycles
```

##### 8.3.11.4 VTG Variant: TSOFTMAX_VTG

The VTG variant operates on **one VTG at a time** (one 256 B sub-range of the tile in G256 mode). It **reuses the same ROM entry** -- the microassembler parameterizes the beat-word template for the VTG's group context at decode time and caches it in the micro-instruction buffer.

**VTG GVIQ entry at dispatch (D1/D2):**

```
TSOFTMAX_F32 Td.gN, Ts.gM
  gviq.push({
    block_id:    allocate_micro_block(),   -- 12 b
    pc_index:   0,
    tile_group:  Td,           -- architectural tile
    phys_tile:   PTd,           -- renamed via Tile RAT
    group_id:    N,            -- VTG index gN
    group_mode:   G256,         -- 256 B per VTG, 16 VTGs per tile
    thread_id:   0,
    iter0..iter3: loop counters,
    active_lanes: 2048,        -- 256 B / 4 B per FP32
    src0_ptag:   PTs,          -- source tile renamed via Tile RAT
    src1_ptag:   PTs_scratch,  -- scratch tile renamed
    dst_ptag:    PTd,          -- destination tile renamed
    vtg_ready:   0,
    branch_tag:  current_btag
  })
```

**Microassembler at decode (D1/D2):**

```
-- Consult Tile Metadata RAT for source tile Ts --
shape   = TileMetadataRAT[PTs].shape      -- (R=8, C=128)
format  = TileMetadataRAT[PTs].format     -- = FP32
W       = shape.C x E(format)              -- = 512 B
R       = shape.R                          -- = 8

-- Parameterize ROM entry for this VTG --
-- G256 mode: tile split into 16 VTGs x 256 B each
-- W=256B means: only 4 strips active (256 B / 512 B per strip)
-- R=4 means:    4 rows, 4 strips
-- Result: 26 beats instead of 42
rom_key = (TSOFTMAX, format=FP32, W-regime=256B, R-regime=4)
rom_entry = ROM.lookup(rom_key)

for i in 0..25:
    bw = rom_entry.beat_words[i]
    bw.group_id       = N
    bw.group_mode     = G256
    bw.dst_ptag       = PTd
    bw.src0_ptag      = PTs
    bw.src1_ptag      = PTs_scratch
    bw.output_range   = (N x 256, (N+1) x 256)
    buffer.write(block_id=allocate_micro_block(), pc_index=i, beat_word=bw)
```

**GVIQ issue and execution (P1/I1):**

```
-- VTG rotation scheduler (one VTG at a time) --
winner = gviq.pick_oldest_ready()     -- age = (rid - head_rid) mod 64
bw     = buffer.lookup(winner.block_id, winner.pc_index)

-- Wait for VEC prologue to fill SA/SB with full 4 KB tile --
-- (VEC-4K-v2 operand-fetch prologue: 8 cycles)
-- VTG sub-range selector: byte-mux from SA/SB (4 KB) to 256 B --
SA_full  = TRegFile.read(winner.src0_ptag)    -- 8 cy epoch
SB_full  = TRegFile.read(winner.src1_ptag)  -- 8 cy epoch (scratch tile)

-- At I2 (after prologue): select VTG sub-range at ALU input mux --
vtg_A = SA_full[winner.group_id x 256 : (winner.group_id+1) x 256]
vtg_B = SB_full[winner.group_id x 256 : (winner.group_id+1) x 256]

-- Drive VEC ALU with this beat word (1 cycle) --
result = VEC_alu.execute(bw, vtg_A, vtg_B)

-- Group Write Adapter: full-tile RMW --
--   Step 1: read old tile (8 cy)
--   Step 2: merge VTG sub-range (combinational)
--   Step 3: write merged tile (8 cy)
--   Total: 16 cy minimum
submit_group_write(winner.dst_ptag, winner.group_id, result, 256)

winner.pc_index++
if winner.pc_index > 25:
    winner.valid = 0       -- retire after beat 25
```

**Comparison: Full-Tile vs. VTG TSOFTMAX:**

| Aspect | Full-Tile TSOFTMAX | VTG TSOFTMAX_VTG |
|--------|---------------------|-------------------|
| Input size | 4 KB (1024 FP32) | 256 B (64 FP32) per VTG |
| ROM key | `(TSOFTMAX, FP32, W=512B, R=8)` | `(TSOFTMAX, FP32, W=256B, R=4)` |
| Beat count | 42 | 26 |
| VTG count | 1 (single tile) | 16 (loop over all VTGs via GVIQ) |
| Prologue | 8 cy (full epoch) | 8 cy (shared with VEC) |
| Writeback | 8 cy (full tile write) | 16 cy (full-tile RMW) |
| Total per op | ~58 cy | ~50 cy + 16 cy RMW = ~66 cy per VTG |
| Throughput | 1 tile / 58 cy | 1 VTG / 66 cy; 16 VTGs sequentially via GVIQ |

**Key architectural points illustrated by this example:**

1. **ROM is the source of truth.** Both full-tile and VTG TSOFTMAX execute beat-word sequences that originate from the same ROM entry. VTG microassembler parameterizes the template at decode time and caches it in the micro-instruction buffer; full-tile does a ROM lookup at issue time. No separate VTG microcode path is needed.

2. **Prologue is shared.** VTG does not introduce a new operand-fetch path. It submits a tile read request through the same R0/R4 ports as VEC-4K-v2, and the 8-cycle prologue fills SA/SB. VTG then reads from the already-staged data at the ALU input mux.

3. **Group Write Adapter RMW is the writeback tax.** Every VTG write must perform a full-tile read-modify-write: 8 cy to read the old tile, merge the VTG sub-range, 8 cy to write the merged tile. This 16-cycle overhead is amortized across all 16 VTGs.

4. **GVIQ rotation schedules one VTG at a time.** After TSOFTMAX_VTG finishes one VTG (beat 25), the GVIQ scheduler picks the next ready VTG (or the same VTG's next iteration if iterN > 1). Loop counters in the GVIQ entry prefix drive strip-mined iterations without re-entering the GVIQ.

5. **`format` from ROM = `format` from Tile Metadata RAT.** The microassembler reads `format` from the Tile Metadata RAT at decode time to select the correct ROM entry. No separate VTG `elem_type` field is needed -- confirming the metadata overlay design (SS9.2.5).

---

#### 9.2.5 VTG Sub-Unit and VTG Metadata Table (v2.2) VTG Sub-Unit and VTG Metadata Table (v2.2)

> **(Change Point #2 -- hardware-revised)**

Each 4 KB tile register is partitionable into **Vector Thread Groups (VTGs)** for SIMD-group execution. The VTG metadata **overlays the Tile Metadata RAT entry** (from §6.1) rather than being a separate table. The Tile Metadata RAT provides `shape.x`, `shape.y`, `format`; VTG-specific fields are added as extensions.

**Unified metadata structure** (overlays Tile Metadata RAT):

```text
TileMetadataEntry (extended, 46+ b per physical tile):
  -- From Tile Metadata RAT (§6.1):
  shape.x:   14 b   -- columns C
  shape.y:   14 b   -- rows R
  format:     4 b   -- FP32/FP16/FP8/FP4 (same encoding as VTG elem_type -- NOT duplicated)
  flags:      4 b   -- arg_tile, scalar_tile, prefetch_hint

  -- VTG additions (overlay on Tile Metadata RAT):
  group_mode:  1 b   -- G256=0, G512=1
  pred_granule: 2 b  -- 8/16/32-bit lane grouping
  -- Per-VTG validity (16 entries per tile, G256 mode):
  vtg_meta[16]: {
    valid:    1 b
    defined:  1 b
    dirty:    1 b
    kind:     3 b   -- VEC | PRED | WIDE_LO | WIDE_HI | ALIGN_LD | SCRATCH | UNDEF
  }
```

> **Note (v1.1 fix):** `elem_type` is NOT a separate VTG field -- it is the **same `format` field** from the Tile Metadata RAT. VTG uses `format` directly. `active_bytes` is computed from `shape.x x shape.y x E` and the VTG's position in the tile.

**VTG byte mapping** (`G256` mode, 16 VTGs / tile):

| VTG | Byte range | VTG | Byte range |
|-----|-----------|-----|-----------|
| `g0` | `[0, 255]` | `g8` | `[2048, 2303]` |
| `g1` | `[256, 511]` | `g9` | `[2304, 2559]` |
| `g2` | `[512, 767]` | `g10` | `[2560, 2815]` |
| `g3` | `[768, 1023]` | `g11` | `[2816, 3071]` |
| `g4` | `[1024, 1279]` | `g12` | `[3072, 3327]` |
| `g5` | `[1280, 1535]` | `g13` | `[3328, 3583]` |
| `g6` | `[1536, 1791]` | `g14` | `[3584, 3839]` |
| `g7` | `[1792, 2047]` | `g15` | `[3840, 4095]` |

In `G512` mode: `g0`=`[0,511]`, `g1`=`[512,1023]`, ..., `g7`=`[3584,4095]`.

**VTG Metadata Table** (16 entries per physical tile):

```text
VTGMeta {
  valid:        1 b,   // VTG contains defined data
  kind:         3 b,   // VEC | PRED | WIDE_LO | WIDE_HI | ALIGN_LD | SCRATCH | UNDEF
  group_mode:   1 b,   // G256=0, G512=1
  elem_type:    4 b,   // FP32/FP16/FP8/FP4/INT32/...
  active_bytes: 10 b,  // 0..256 (G256) or 0..512 (G512)
  pred_granule: 2 b,  // 8/16/32-bit lane grouping
  pred_mode:    1 b,   // 0=zeroing, 1=merging (default)
  defined:      1 b,
  dirty:        1 b,
}
```

The VTG Metadata Table is read by the Group Read/Write Adapters to determine VTG validity and predicate granularity. It is updated on every VTG write.

**Rename:** The Tile RAT maps architectural tile `Tg` -> physical tile `PT`. VTG `group_id` is a sub-location index into the renamed `PT`. A VTG micro-instruction writing `Tg.gN` may either update `PT.gN` in place (if uniquely owned and no older readers) or allocate a fresh physical tile and merge unchanged VTGs (copy-on-write policy, v1 conservative policy).

**Writeback rename:** VTG writeback performs a full-tile read-modify-write (16 cy minimum, see §8.3.10). The destination `ptag` remains unchanged -- only the VTG sub-range content is modified. No Tile RAT update is needed at writeback; the `dirty` bit in `vtg_meta[dst_ptag][group_id]` is set.

---

## 10. Out-of-Order Execution Model — Foundations

> **(v1 → v2: 本章基础部分完整复制自 v1 §10。v2 增量为 Tile Metadata RAT(§10.3 引用)与 §10.6 指向 §11 的扩展投机恢复机制。)**

The Davinci-v2 core implements a **ROB-less out-of-order** execution model. Because the core does not need to maintain precise architectural state (no interrupts, no exceptions in run-to-completion kernels), it dispenses with the Reorder Buffer entirely. This section describes how instructions flow through the core and how correctness is maintained.

### 10.1 Core Principles (BCC Scalar Pipeline)

1. **OoO dispatch, OoO execution, OoO completion.** An instruction's result is committed to the physical register file as soon as execution completes. There is no in-order retirement stage.
2. **False dependencies (WAW, WAR) eliminated by register renaming.** The SMAP (speculative map) maps each atag to a unique ptag, so no instruction ever overwrites another's live data. The Tile RAT (32->256) and Tile Metadata RAT are unchanged from v2.
3. **True dependencies (RAW) resolved by Ready Table tag-based wakeup.** Instructions wait for source ptag readiness via the Ready Table bitmap (O(1) lookup per ptag). Tile-domain instructions wait for Tile RAT ready bits signaling physical tile completion.
4. **Branch recovery via MapQ reverse replay.** On mispredict, the SMAP is restored to CMAP state by replaying MapQ entries in reverse order from the flush_rid. All younger instructions are flushed from physical IQs via branch_tag CAM-clear.
5. **Physical registers freed by reference counting.** No ROB means no retirement-based freeing; instead, a ptag is freed when it is both *orphaned* (no longer mapped by SMAP) and its refcount reaches zero.
6. **Ready Table provides O(1) ptag readiness.** The 128-bit Ready Table bitmap replaces the 384-entry CDB comparator array for scalar wakeup. Each ptag's readiness is a single bit-test.
7. **Speculative memory side effects gated by branch tag.** Speculative scalar stores live in the SSB and speculative bulk tile stores live in the STQ until their `branch_tag` resolves to non-speculative. See Section 11.
8. **(v2.3 Block-ROB 增量) Block-granularity precise exceptions via BROB.** The Block Reorder Buffer (BROB) tracks instruction blocks (BSTART to BSTOP) and provides precise exception identification: when a fault is detected, the faulting block is identified, younger blocks are squashed, and register state is recovered via MapQ reverse replay from the faulting RID. See SS11.11.
8. **(v2.3 Block-ROB 增量) Block-granularity precise exceptions via BROB.** The Block Reorder Buffer (BROB) tracks instruction blocks (BSTART to BSTOP) and provides precise exception identification: when a fault is detected, the faulting block is identified, younger blocks are squashed, and register state is recovered via MapQ reverse replay from the faulting RID. See SS11.11.### 10.2 Instruction Lifecycle (BCC Scalar Pipeline)

```
  ┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐
  │Fetch│──>│Decode│──>│Rename│──>│ Disp│──>│Issue│──>│ Exec│──>│  WB │
  │F0-F4│   │  D1  │   │D2  D3│   │ S1 S2│   │P1I1I2│  │E1-n│   │
  └─────┘   └─────┘   └─────┘   └─────┘   └─────┘   └─────┘   └─────┘
                         │                                         │
                    D1: RID/atag + BROB entry alloc (BSTART)      W1: Ready Table
                    D2: SMAP read + ptag alloc + MapQ push      update + CDB / TCB
                    D3: SMAP write + RT init + BID stamp          broadcast
                                                              + Block SSB/STQ transfer
                                                              BROB Retire (off critical path)
```

Detailed per-stage actions:

| Stage | Actions |
|-------|---------|
| **Fetch (F0-F4)** | PC -> I-cache + branch predictor; receive 4 instructions; stitch + IB buffer; F4 handoff |
| **Decode (D1)** | Decode opcode, identify domain, allocate RID (6-bit program order), resolve atag for sources, classify operands (P/T/U) |
| **Rename (D2)** | Read SMAP for source ptags; allocate new ptag from free list; update SMAP (live); push MapQ entry; T/U stack push for tile operands. If branch: allocate branch_tag from 8-slot pool. |
| **Rename (D3)** | Write SMAP (committed state); initialize Ready Table source-ready bits; assign IQ routing |
| **Dispatch (S1)** | Check free list vacancy, MapQ space, IQ vacancy per routing target |
| **Dispatch (S2)** | Write IQ entries (alu_iq / bru_iq / lsu_iq); update free list; advance MapQ head |
| **Issue (P1)** | Ready Table bitmap query (O(1) per ptag); age-matrix cascaded pick selects oldest-ready per IQ |
| **Issue (I1)** | Physical RF read-port arbitration across 7 issue slots |
| **Issue (I2)** | Confirm IQ entry deallocation; confirm RF port occupancy |
| **Execute (E1-EX_n)** | Compute result (variable latency). Scalar stores deposit into SSB; MTE bulk stores deposit into STQ. |
| **Writeback (W1)** | CDB/TCB broadcast; write to physical RF; Ready Table update (set ready bit); wakeup dependents; free orphans |### 10.3 Rename Register State (atag / ptag / SMAP / CMAP / MapQ)

The BCC scalar pipeline replaces the Scalar RAT with a **three-table model**: CMAP (committed map), SMAP (speculative map), and MapQ (speculative rename increment log). The Tile RAT and Tile Metadata RAT are unchanged from v2.

**Rename example (4-wide, 3-stage D1/D2/D3):**

```
  Instruction stream:
    i0:  ADD  X5, X2, X3
    i1:  MUL  X6, X5, X7
    i2:  SUB  X5, X8, X9
    i3:  ADD  X10, X5, X6

  Before D1:
    CMAP: X2->P2, X3->P3, X5->P5, X6->P41, X7->P7, X8->P8, X9->P9
    SMAP: (matches CMAP initially)
    Ready Table: all allocated ptags ready

  D1 (Decode + RID/atag allocation):
    i0: RID=0, atag=X5,X2,X3;  i1: RID=1, atag=X6,X5,X7; ...

  D2 (Rename Request):
    i0: src0=P2, src1=P3, dst=P80 (new);  SMAP[X5]<=P80;  P5 orphan;  MapQ<= {X5, P5, P80, RID=0}
    i1: src0=P80 (bypass), src1=P7, dst=P81 (new);  SMAP[X6]<=P81;  MapQ<= {X6, P41, P81, RID=1}
    i2: src0=P8, src1=P9, dst=P82 (new);  SMAP[X5]<=P82;  P80 orphan;  MapQ<= {X5, P80, P82, RID=2}
    i3: src0=P82 (bypass), src1=P81 (bypass), dst=P83 (new);  SMAP[X10]<=P83

  D3 (Rename Complete):
    SMAP committed: X2->P2, X3->P3, X5->P82, X6->P81, X7->P7, X8->P8, X9->P9, X10->P83

  Ready Table at S2 dispatch:
    Clear bits: P80, P81, P82, P83 (not yet ready)

  On CDB writeback (e.g. i0 completes):
    Ready Table.set(P80)  -> P80 ready
    -> P1 can_issue recomputed -> P1 picks i1 (src0=P80 now ready)

  On orphan detection (i2 renamed X5->P82; i0's result P80 now orphan):
    refcount[P80]-- -> if refcount[P80]==0: free_list.push(P80)
```

**Tile RAT rename example** (unchanged from v2, shown for completeness):

```
  Instruction stream:
    i0:  TILE.LD  T10, [X5]
    i1:  TILE.LD  T20, [X6]
    i2:  VADD     T10, T10, T20
    i3:  TILE.ST  [X7], T10

  Before rename:
    Tile RAT: T10->PT10, T20->PT20

  Rename (D2, unchanged from v2):
    i0: scalar src=<from SMAP>, tile dst=PT100 (new);  Tile RAT: T10->PT100;  PT10 orphaned
    i1: scalar src=<from SMAP>, tile dst=PT101 (new);  Tile RAT: T20->PT101;  PT20 orphaned
    i2: tile src1=PT100 (bypass), tile src2=PT101 (bypass), tile dst=PT102 (new);  Tile RAT: T10->PT102
    i3: tile src=PT102 (bypass);  no tile dst (store)

  After rename:
    Tile RAT: T10->PT102, T20->PT101
```

**Tile Metadata RAT** (unchanged from v2): A 256 x 32 b SRAM holding (shape.x, shape.y, format) per physical tile. Updated by retire-time writes; read together with the tile's first strip during operand fetch.### 10.4 Ready Table + CDB / TCB

The BCC scalar pipeline introduces the **Ready Table** (128-bit bitmap) as the primary scalar wakeup mechanism. The CDB and TCB remain for result broadcast and tile completion.

**Ready Table** (described in full in Section 7.3):

```
Ready Table: 128-bit bitmap
  bit[i] = 1: ptag i has a valid value (ready)
  bit[i] = 0: ptag i is waiting for writeback

set(ptag):    mask |= (1 << ptag)     # On CDB writeback
clear(ptag):  mask &= ~(1 << ptag)    # On ptag allocation at D2
read(i):       return (mask >> i) & 1  # Combinational, O(1)
```

**CDB / TCB** (unchanged from v2 except for Ready Table integration):

| Parameter | Value |
|-----------|-------|
| CDB ports | **6** (4 ALU + 1 MUL/LSU + 1 TILE.GET) |
| Broadcast width | 7-bit ptag + 64-bit data per port |
| Snoop points | Ready Table update (not per-RS-entry comparison) |

When the CDB broadcasts a result (ptag, data):
1. **Ready Table update**: `ready.set(ptag)` -- set bit in bitmap
2. **Physical RF write**: capture data at destination ptag
3. **Tile Completion Bus (TCB)**: unchanged from v2, 4 ports, 8-bit tile tag

TCB port allocation (unchanged from v2):### 10.5.1 VTG Dependency and VTG-Ready Bits (v2.2)

> **(Change Point #2 -- hardware-revised)**

VTG vector micro-instructions have a two-level dependency model. VTG writeback is a **full-tile read-modify-write** (16 cy minimum), during which the destination tile's VTG ready bit is set only after writeback completion.

| Level | Token | Purpose |
|-------|-------|---------|
| **Physical tile tag** | `ptag` (8 b) | Coarse readiness: entire tile is ready |
| **VTG ready bit** | per-VTG sub-location bit in VTG Metadata Table | Fine readiness: specific VTG inside the tile is ready |

The **VTG Ready Table** is a **256-bit bitmap** (one bit per physical tile PT0..PT255), similar in structure to the scalar Ready Table. It tracks tile-level writeback readiness. At VTG dispatch, the source `ptag` bits are cleared. At VTG writeback, the destination `ptag` bit is set.

Inside the GVIQ, each entry's `src_ready` field tracks per-VTG readiness independently from tile-level readiness:

```text
vtg_ready = src_ready[0] & src_ready[1] & src_ready[2] & src_ready[3]
           & loop_counters_ready
```

This two-level model allows a tile to contain some VTGs that are ready and others that are not — essential for warp-rotated scheduling where different VTGs are at different loop iterations.

**VTG write policies:**

| Policy | Trigger | Action |
|--------|---------|--------|
| In-place VTG write | Unique tile ownership, no older readers | Group Write Adapter reads old tile, merges VTG sub-range, writes merged tile. W0 occupied for 16 cy (RMW). |
| Copy-on-write tile group | Shared tile or speculative update | Allocate fresh PT; Group Write Adapter reads old tile, merges all unchanged VTGs, writes to fresh PT. W0 occupied for 16 cy. |
| Read-modify-write VTG | Merging predication | Fetch old destination VTG via Group Read Adapter; merge inactive lanes |
| Fresh group define | Load or producer overwrites enough VTGs | Allocate fresh tile; mark all VTGs defined |

**Scalar ordering without `VWAIT`:** No software-visible `VWAIT` instruction exists in v2.2. Ordering between VTG micro-ops and scalar ops is enforced by normal `ptag` / `src_ready` dependency tracking through the GVIQ.

### 10.5 Physical Register Freeing (Reference Counting)

> **(v1 -> v2 BCC: SMAP replaces Scalar RAT for the P-reg freeing path; Tile RAT unchanged. The refcount mechanism itself is identical to v1.)**

```
  Per physical scalar register (128 entries):
    orphan (1 bit) | refcount (4 bits)

  Per physical tile register (256 entries):
    orphan (1 bit) | refcount (3 bits)

  State machine (same for both):
    MAPPED:   SMAP/Tile RAT points to this register; refcount tracks in-flight readers
    ORPHAN:   SMAP/Tile RAT no longer points here (remapped); refcount may be > 0
    FREE:     orphan AND refcount == 0 -> returned to free list
```

**Lifecycle events:**

| Event | orphan | refcount | Action |
|-------|--------|----------|--------|
| Allocated as destination at D2 | 0 | 0 | Added to SMAP mapping |
| Instruction reads this register (dispatched to IQ) | -- | +1 | Reader registered |
| Reader completes execution (at I1/I2) | -- | -1 | Reader done |
| SMAP remaps atag to new ptag | 1 | -- | Old mapping becomes orphan |
| refcount reaches 0 while orphan=1 | 1 | 0 | **Free**: return to free list |

**Branch misprediction and ref-counts:** When a mispredict occurs, all instructions younger than the branch are flushed. MapQ reverse replay restores SMAP to CMAP state, reclaiming all speculatively allocated ptags. Physical registers allocated as destinations by flushed instructions are returned directly to their respective free lists. Ready Table is reset to ALL_ONES (conservative).### 10.6 Branch Recovery (BCC Scalar Pipeline)

On a branch mispredict, the BCC scalar pipeline recovers via **MapQ reverse replay** + **branch-tag CAM-clear** on all physical IQs.

```
  ┌────────────────────────────────────────────────────────────┐
  │  Branch Misprediction Recovery (BCC Scalar Pipeline)           │
  │                                                            │
  │  Cycle 0: Branch resolves as MISPREDICTED at EX1          │
  │    -> flush_rid = branch.rid (from IQ entry)              │
  │    -> flush_btag = branch.branch_tag (3-bit)              │
  │                                                            │
  │  Cycle 1: Recovery actions (all in parallel):              │
  │                                                            │
  │    (a) MapQ reverse replay:                                │
  │        for each MapQ entry (youngest to oldest):           │
  │          if entry.rid > flush_rid:                        │
  │            SMAP[entry.atag] = entry.old_ptag              │
  │            refcount[entry.new_ptag]--                    │
  │            refcount[entry.old_ptag]++                     │
  │            entry.valid = 0                                 │
  │          else: break                                      │
  │        -> SMAP == CMAP (exact committed state)              │
  │                                                            │
  │    (b) Physical IQ CAM-clear:                             │
  │        for each alu_iq / bru_iq / lsu_iq entry:           │
  │          if entry.checkpoint_id > flush_checkpoint:        │
  │            entry.valid = 0                                 │
  │                                                            │
  │    (c) Ready Table: mask <= ALL_ONES                     │
  │       (conservative; all ptags become temporarily untrusted)│
  │                                                            │
  │    (d) SSB flush: entries with btag >= flush_btag invalid │
  │                                                            │
  │    (e) STQ flush: entries with btag >= flush_btag invalid  │
  │                                                            │
  │    (f) Tile RAT: unchanged from v2 (independent domain)   │
  │                                                            │
  │    (g) Free list: restore head from CMAP state           │
  │                                                            │
  │  Cycle 2: Redirect fetch PC to correct branch target       │
  │                                                            │
  │  Cycle 3+: New instructions begin entering F0             │
  │                                                            │
  │  Total penalty: 6-7 cy (MapQ replay parallel with others)  │
  └────────────────────────────────────────────────────────────┘
```

The MapQ replay is O(depth) = 12 iterations maximum. All recovery actions run in parallel within the single recovery cycle.

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
| **A. Renamed register / tile state** | Writes to a ptag (P0–P127), a physical tile (PT0–PT255), or a metadata RAT entry | Returns to free list once orphan + refcount=0 (no ROB needed) | **MapQ reverse replay + SMAP restore + refcount + free-list restore** (BCC scalar pipeline) |
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
| Recovery for register / tile state | ROB walks back, undoes mappings in order | **MapQ reverse replay + SMAP <- CMAP + refcount free**; instruction-precise, more efficient |
| Recovery for memory stores | Stores in ROB / coupled SQ; release on retire | **SSB / STQ** with branch-tag gating |
| Recovery for fences / CSR | ROB serializes; instruction retires in order | Issue gated on `btag = 0xFF`; correct but adds 0–6 cy latency |
| Speculative depth | bounded by ROB capacity (typically 64–256 entries) | bounded by **8 active branch tags** (≈ 8-deep nested branches) |
| Storage overhead | ROB ≈ 256 entries × ~150 b = ~5 KB + retirement logic | Branch-tag tracker (5 K gate) + SSB/STQ (~2.5 KB) + MapQ (144 B) + Ready Table (16 B) ≈ **~3 KB total** |
| Wakeup logic | RS does dependency tracking; ROB independent | **Ready Table (128-bit bitmap) replaces CDB comparators**: 0 comparators vs. 384 for scalar RS; O(1) ptag lookup |
| Mispredict penalty | ROB walk-back + flush ≈ 5–10 cy | **MapQ replay + Ready Table reset + IQ CAM-clear + SSB/STQ flush**: all parallel in 1 cy + 6-cy refill = 7 cy |
| Precise exceptions | yes (free) | block-granularity via BROB (SS11.11) |
| Single-thread TSO memory ordering | yes | yes (FIFO drain through SSB) |

**The key insight:** in environments where precise exceptions are not required (the AI-kernel envelope), the ROB's three bundled services unbundle naturally. Service (1) is free if you don't need it. Service (3) is replaced by reference counting. Service (2) — **the only remaining service** — is implemented by the SSB + STQ + branch-tag tracker at a fraction of a ROB's cost.

**v2.3 extends this:** by lifting exception handling to block granularity via BROB, the design achieves the ROB's service (1) for kernel-entry traps without a full flat ROB. The block boundary is the commit point; the faulting block is identified; younger blocks are squashed; MapQ reverse replay recovers register state to the faulting instruction.

**v2.3 extends this:** by lifting exception handling to block granularity via BROB, the design achieves the ROB's service (1) for kernel-entry traps without a full flat ROB. The block boundary is the commit point; the faulting block is identified; younger blocks are squashed; MapQ reverse replay recovers register state to the faulting instruction.

### 11.9 Cycle-by-cycle example: speculative store followed by mispredict (BCC Scalar Pipeline)

> **Block-ROB note:** This example uses the pre-BROB model. For cycle-by-cycle examples with BROB and block-level commit, see SS11.11.

```
  Cycle  Action
  ───  ────────────────────────────────────────────────────────────────────────────
   0     Branch B1 enters D1; allocated tag t=3, state[3] = speculative
         B1 pushes MapQ entry {atag=B1_dst, old_ptag, new_ptag, rid=B1.rid}
   1     SD X5, [X8]+0    — younger than B1 — D2 allocates SSB[7] tagged 3
         MapQ push: {atag=X5, old=P5, new=P80, rid=next}
   2     SD X6, [X8]+8    — younger than B1 — D2 allocates SSB[8] tagged 3
   3     SD X7, [X8]+16   — younger than B1 — D2 allocates SSB[9] tagged 3
   4-8   ... more instructions dispatched to physical IQs ...
   9     B1 reaches EX1: mispredicted!  flush_rid = B1.rid; flush_btag = 3
  10     MapQ reverse replay (all in parallel):
           (1) SMAP restored to CMAP (entries with rid > flush_rid undone)
           (2) Physical IQ CAM-clear: entries with checkpoint_id > flush_btag invalidated
           (3) Ready Table: mask <= ALL_ONES (conservative reset)
           (4) SSB[7..9] valid ← 0
           (5) Free list head restored from CMAP state
  11     Fetch redirected to correct branch target
  12-16  Front-end refill (5 cy)
  17     First correct-path instruction enters EX1 (total mispredict penalty = 7 cy)

  Architectural state at cycle 17:
    Memory: NEVER wrote SSB[7..9]. Cache lines unaffected. Correct.
    SMAP == CMAP: MapQ replay restored all speculative renames.
    P80, P81, P82, P83: freed (orphan + refcount=0 after replay).
    Ready Table: all bits reset; instructions re-query on next cycle.
```

**Key BCC scalar pipeline differences:**

- MapQ entries are pushed at D2 for each P-dst rename, carrying `{atag, old_ptag, new_ptag, rid}`.
- On flush, MapQ reverse replay (max 12 iterations) restores SMAP to exact CMAP state.
- Physical IQ entries are CAM-cleared by `checkpoint_id` (MapQ entry ID), not just `btag`.
- Ready Table reset ensures no stale ready bits survive the flush.

The mispredict is recovered in 7 total cycles. The MapQ replay runs in parallel with the IQ CAM-clear and Ready Table reset — all within the single recovery cycle.

### 11.10 Hardware cost summary

| Block | BCC scalar pipeline change | Gate count |
|-------|--------------------------|------------|
| Branch-tag tracker (state vector + ancestry bitmap + FSM) | unchanged | ~5 K |
| Speculative Store Buffer (24 entries, 182 b each) | unchanged | ~80 K |
| Speculative Tile-Store Queue (8 entries, 110 b each) | unchanged | ~12 K |
| Branch-tag stamping in IQ entries | unchanged (now across 3 IQs) | ~2 K |
| MapQ (12-entry ring buffer, ~96 b/entry) | **replaces RAT checkpoint store** | ~1.5 K |
| Ready Table (128-bit bitmap + control) | **new** | ~1 K |
| CDB comparator reduction | **384 -> 0** (Ready Table replaces all) | ~-50 K (saves area) |
| Tile Metadata RAT (256 × 32 b SRAM) | unchanged | ~10 K |
| **Total v2 BCC speculation hardware** | | **~113 K gate** |

**v2.3 Block-ROB new hardware (added on top of v2 BCC):**

| Block | Change | Gate count |
|-------|--------|------------|
| BROB (128 entries x ~120 b) | **new** | ~150 K |
| Block SSB (32 entries x ~200 b) | **new** | ~60 K |
| Block STQ (16 entries x ~100 b) | **new** | ~20 K |
| BID tagging in iROB / GVIQ / IQ / SSB / STQ | **new** | ~8 K |
| BROB allocate FSM + complete check | **new** | ~20 K |
| Exception delivery logic | **new** | ~10 K |
| **Total v2.3 Block-ROB hardware** | | **~268 K gate** |
| **Total v2.3 with Block-ROB** | | **~381 K gate** |

**v2.3 Block-ROB new hardware (added on top of v2 BCC):**

| Block | Change | Gate count |
|-------|--------|------------|
| BROB (128 entries x ~120 b) | **new** | ~150 K |
| Block SSB (32 entries x ~200 b) | **new** | ~60 K |
| Block STQ (16 entries x ~100 b) | **new** | ~20 K |
| BID tagging in iROB / GVIQ / IQ / SSB / STQ | **new** | ~8 K |
| BROB allocate FSM + complete check | **new** | ~20 K |
| Exception delivery logic | **new** | ~10 K |
| **Total v2.3 Block-ROB hardware** | | **~268 K gate** |
| **Total v2.3 with Block-ROB** | | **~381 K gate** |

The v2 BCC speculation hardware is **~3.5%** of the ~3.26 mm² total core area — the same as v2 with RAT checkpoints. The Ready Table (~1 K gate) and MapQ (~1.5 K gate) add negligible area. The key win is the **CDB comparator elimination** (~50 K gate saved) and the **IQ split** (simpler, more scalable). The net gate count for the scalar wakeup/issue path is approximately equal or slightly lower than v1.

---

## 11.11 Block-ROB -- Block-Granularity Precise Exception Support (v2.3 新增)

### 11.11.1 Motivation

The pre-BROB Davinci-v2 model explicitly excludes precise exception support, treating all faults as fatal. Block-ROB relaxes this to **block-granularity precise exceptions**:

- The faulting instruction block is identified.
- All younger blocks are squashed (BID-order flush).
- Register state is recovered via MapQ reverse replay from the faulting RID.
- Memory side effects within squashed blocks are discarded (SSB/STQ invalidation).
- After OS/kernel handler restores context, the faulting block is re-executed.

The block boundary (BSTART to BSTOP) is the commit point. This matches the design principle from LinxCore: block structure enables natural ROB-bounded commit without a flat instruction-level ROB.

### 11.11.2 Instruction Block Definition

An **instruction block** is a contiguous sequence of decoded micro-operations bounded by:

- **BSTART** (inclusive start): first uop in the block; triggers BROB entry allocation.
- **BSTOP** (inclusive end): last uop in the block; gates retirement.

Block boundaries are compiler-generated at natural control-flow join points. Block size: 4-64 uops (typical AI kernel: 16-32 uops).

**Block types:**

| Block Type | Scalar-only | Engine-backed | Notes |
|------------|-------------|---------------|-------|
| `STD` | Yes | No | Pure scalar execution |
| `VTG` | Yes | VTG micro-instructions | GVIQ sub-schedule within block |
| `VEC` | No | Full-tile VEC-4K-v2 | `T*` tile operations |
| `CUBE` | No | outerCube MXU | CUBE.OPA, CUBE.DRAIN |
| `MTE` | Yes | Memory Tile Engine | TILE.LD, TILE.ST |

### 11.11.3 Block ID (BID)

Each block receives a **64-bit BID** at BSTART:

```
BID[7:0]  -- BROB slot index (0..127)
BID[63:8] -- Monotonically increasing sequence number
```

The 8-bit slot index directly maps to the BROB entry. Full-width BID enables flush by ordering: **keep `bid <= flush_bid`, kill `bid > flush_bid`**.

### 11.11.4 BROB Structure

| Parameter | Value |
|-----------|-------|
| `BROB_ENTRIES` | 128 |
| `BROB_ALLOC_PER_CYCLE` | 1 |
| `BROB_COMPLETE_PER_CYCLE` | 1 |
| `BROB_RETIRE_PER_CYCLE` | 1 |
| `BID_W` | 8 b (slot) + 56 b (sequence) |

**Per-BROB-entry state:**

```
BROBEntry {
  valid:          1 b     -- entry is allocated
  state:          2 b     -- ALLOC | ISSUED | COMPLETE
  bid:            64 b    -- full-width Block ID
  block_type:     4 b     -- STD | VTG | VEC | CUBE | MTE
  head_rid:       7 b     -- RID of first uop (BSTART's iROB slot)
  tail_rid:       7 b     -- RID of last uop (BSTOP's iROB slot)
  n_uops:         6 b     -- number of uops in block (1..64)
  checkpoint_id:   4 b     -- RAT checkpoint active for this block
  needs_scalar:   1 b     -- block has scalar uops (BSTOP must retire)
  needs_engine:   1 b     -- block has engine ops (GVIQ/Vector/Cube RS)
  engine_done:     1 b     -- engine completion signal received
  scalar_done:    1 b     -- BSTOP retired from iROB
  has_exception:   1 b     -- exception detected within this block
  exception_cause: 16 b    -- trap / exception cause code
  fault_rid:       7 b     -- RID of faulting uop (if has_exception)
  n_stores:        5 b     -- number of scalar stores in this block
  n_vtg_ops:       5 b     -- number of VTG micro-instructions
  block_ssb_base:  5 b     -- index into Block SSB RAM for first store
  block_stq_base:  4 b     -- index into Block STQ RAM for first tile store
}
```

**State machine:**

```
FREE --[allocate]--> ALLOC --[dispatched]--> ISSUED --[complete]--> COMPLETE
                                                                   |
                                                            [retire: advance head]
                                                                   |
                                                                  FREE
```

**Completion rule:**

```
complete = scalar_done && (needs_engine ? engine_done : 1)
```

### 11.11.5 Instruction Block Lifecycle

**BSTART at D2:**
1. Allocate BROB entry `k` from free pool (tail pointer).
2. Set `bid = {seq_num++, k[7:0]}`.
3. Set `block_type` from BSTART metadata.
4. Set `checkpoint_id` = current RAT checkpoint snapshot.
5. Set `head_rid` = current iROB head.
6. Set `needs_scalar = 1`, `needs_engine = 0`, `scalar_done = 0`, `engine_done = 0`, `has_exception = 0`.
7. Stamp all uops in block with `bid` (stored alongside `branch_tag` in IQ/GVIQ/iROB entries).
8. BSTART retires immediately (bypasses IQ, EX, WB).

**Subsequent uops (D3):**
1. Allocate iROB entry; stamp `bid`.
2. Set `iROB[rid].brob_slot = k`.
3. Increment `n_uops`.
4. If `is_store`: allocate Block SSB slot, increment `n_stores`.
5. If `is_vtg_op`: increment `n_vtg_ops`; set `needs_engine = 1`.
6. Execute normally through BCC pipeline.

**BSTOP at D2:**
1. Set `tail_rid` = current iROB entry index.
2. Set `needs_engine = (n_vtg_ops > 0) || (block_type == VEC) || (block_type == CUBE)`.
3. BSTOP enters iROB but **retirement is gated** (see below).

### 11.11.6 BSTOP Retire Gate and Block Completion

The iROB commit logic is extended with a **BSTOP retire gate**:

```
BSTOP can retire when ALL of:
  1. BROB[bid_slot].state == COMPLETE
  2. !BROB[bid_slot].has_exception

On BSTOP retire:
  1. Set scalar_done = 1 in BROB[bid_slot]
  2. If complete && !has_exception: advance BROB head to k+1
  3. If complete && has_exception: trigger exception delivery
```

**Engine completion:** Engines (VEC-4K-v2, Cube, MTE LSU, GVIQ) signal `engine_done` to the BROB via the existing TCB (Tile Completion Bus) with `bid` in the response. On match: `BROB[bid_slot].engine_done = 1`.

### 11.11.7 Block Retire

Only the **oldest block** (BROB head) retires per cycle:

```
1. If head.has_exception:
     Report exception (see SS11.11.9)
     Do NOT commit side effects
     Squash all younger blocks (bid > head.bid)
2. Else if head.state == COMPLETE:
     Commit side effects:
       a. Transfer Block SSB entries to SSB (drain_rdy = 1, btag = 0xFF)
       b. Transfer Block STQ entries to STQ (drain_rdy = 1, btag = 0xFF)
       c. Advance head pointer
       d. Free BROB entry
3. Else: stall (wait for completion)
```

### 11.11.8 Precise Exception Mechanism

**Exception detection:**
- **Scalar exception**: EX1 stage sets `iROB[rid].trap_valid = 1`.
- **Engine exception**: TCB response arrives with `trap_valid=1`; BROB marks `has_exception=1`, `fault_rid=faulting_rid`.

**Exception reporting flow:**

```
Step 1: Detection
  Scalar: EX1 sets iROB[rid].trap_valid = 1
  Engine: TCB arrives with trap_valid=1

Step 2: Blocking
  BROB does NOT retire the block
  BSTOP retire is blocked (has_exception == TRUE)

Step 3: Squash of Younger Blocks
  flush_bid = BROB[head].bid
  In parallel (1 cycle):
    a. iROB: invalidate entries with bid > flush_bid
    b. BROB: set valid = 0 for entries with bid > flush_bid
    c. GVIQ: invalidate entries with bid > flush_bid
    d. IQ: invalidate entries with bid > flush_bid
    e. SSB: valid = 0 for entries with bid > flush_bid
    f. STQ: valid = 0 for entries with bid > flush_bid
    g. Block SSB: invalidate entries with bid > flush_bid
    h. Block STQ: invalidate entries with bid > flush_bid

Step 4: Register State Recovery
  MapQ reverse replay from faulting RID backward:
    for each MapQ entry from tail down to faulting RID:
      undo SMAP write, restore orphan ptag, pop MapQ
  Tile RAT: restore from BROB[head].checkpoint_id

Step 5: Exception Delivery
  EPC   = BSTART_PC (of faulting block)
  Cause = BROB[head].exception_cause
  Fault RID = BROB[head].fault_rid
  OS/kernel handler restores context and re-executes the block.
```

**Within-block instruction precision:** MapQ already provides instruction-precise P-reg recovery. On exception, MapQ is replayed in reverse from `fault_rid` (captured at detection), not from the block boundary. The faulting uop is precisely identified and all younger uops in the same block are undone.

### 11.11.9 Worked Example: Page Fault in Block

```
Block B: BSTART, u0 (ADD r1, r2, r3), u1 (TILE.LD), u2 (MUL r6, r4, r7), BSTOP

u1 executes: TILE.LD triggers page fault.
  LSU sets iROB[rid1].trap_valid = 1
  LSU sets BROB[5].has_exception = 1
  LSU sets BROB[5].fault_rid = rid1
  LSU sets BROB[5].exception_cause = PAGE_FAULT

BSTOP cannot retire (blocked on has_exception).
Block B is at BROB head, blocked.

Next cycle:
  flush_bid = B.bid  (no younger blocks)
  MapQ replay from rid1 backward:
    undo SMAP writes from u1, u0
    restore ptags for r4, r1
  Tile RAT restore from checkpoint_id = 3
  Deliver: EPC = BSTART_PC, Cause = PAGE_FAULT
  OS handler restores context.
  Block B is re-fetched and re-executed after handler returns.
```

### 11.11.10 Store Commit Within Blocks

**Block SSB:** 32-entry structure shared across BROB entries. Each entry tracks a scalar store within a block.

**Block SSB entry:**

```
valid:    1 b   -- entry is valid
addr:    40 b   -- cache-line address (filled at EX1)
data:    128 b -- store data (filled at EX2)
size:     3 b   -- 1/2/4/8 B
bid:      8 b   -- which block this store belongs to
ssb_idx:  5 b   -- mapped SSB slot index
```

**Load forwarding within block:** Loads forward from Block SSB entries in the same block without BID ordering checks (Block SSB only contains stores from this block, which are already program-ordered).

**Store commit at block retire:** All Block SSB entries for the retiring block are transferred to the SSB with `btag=0xFF` and `drain_rdy=1`. They drain to L1-D in program order via the existing SSB drain pump.

**Block STQ:** Analogous to Block SSB for tile stores (TILE.ST, TILE.SCATTER). Tile data stays in TRegFile-4K; Block STQ holds the intent (address, source phys-tile, bid).

### 11.11.11 Integration with Existing Infrastructure

**MapQ:** Fully reused. Each renamed destination pushes a MapQ entry with `{arch_reg, old_ptag, new_ptag, RID, checkpoint_id}`. On exception, MapQ reverse replay from `fault_rid` recovers P-reg state. Unchanged.

**Branch-tag tracker:** Fully reused. Each block receives a branch tag at BSTART. Branch-tag CAM-clear is extended to flush by `bid > flush_bid`. Unchanged.

**RAT checkpoints:** Extended with `checkpoint_id` per BROB entry. On exception: Tile RAT restored from `BROB[head].checkpoint_id`. Scalar RAT recovered via MapQ reverse replay (already instruction-precise).

**SSB/STQ:** Extended with `bid` field (8 b per entry). At D2: `SSB[idx].bid = current_bid`. At block retire: `SSB[idx].btag = 0xFF`, `drain_rdy = 1`. At flush: entries with `bid > flush_bid` are invalidated.

**VTG / GVIQ:** GVIQ entries are stamped with `bid`. GVIQ issue is gated by `block_complete = (BROB[bid_slot].engine_done || !BROB[bid_slot].needs_engine)`. Unchanged GVIQ rotation scheduler.

### 11.11.12 Flush Protocol Summary

```
flush_bid = BROB[head].bid

In parallel (1 cycle):
  a) iROB: invalidate entries with bid > flush_bid
  b) BROB: valid = 0 for entries with bid > flush_bid; tail advances
  c) GVIQ: invalidate entries with bid > flush_bid
  d) IQ: invalidate entries with bid > flush_bid
  e) SSB: valid = 0 for entries with bid > flush_bid
  f) STQ: valid = 0 for entries with bid > flush_bid
  g) Block SSB: invalidate entries with bid > flush_bid
  h) Block STQ: invalidate entries with bid > flush_bid
  i) MapQ: pop entries from flush_rid+1 backward (undo SMAP writes)
  j) Tile RAT: restore from BROB[flush_bid_slot].checkpoint_id
  k) Scalar RAT: flash-restore from checkpoint (unchanged)
  l) Branch-tag tracker: free tags for flushed blocks
```

## 12. Memory Subsystem

> **(v1 → v2: §12.1 / §12.3 / §12.4 / §12.5 完整复制自 v1 §11。v2 增量集中在 §12.2 Store path,把 v1 的 16-entry store buffer 升级为 24-entry SSB,并加入 STQ。)**

The memory subsystem is structurally identical to v1 §11. The two changes are integration points for the SSB and STQ.

### 12.1 Cache Hierarchy (v1 §11.1, 未变更)

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

### 12.2 Store Path (v2 增量)

```
  Scalar store:      LSU-RS → SSB (24 entries) → L1-D (only on tag-clear)
  Bulk tile store:   MTE-RS → STQ (8 entries)  → MTE memory pipeline → L2 (only on tag-clear)
```

The L1-D's existing 8 MSHRs and 4-cy store pipeline are unchanged. The SSB inserts in front of L1-D as a CAM-addressable forwarding buffer; it already played that role in v1's 16-entry store buffer, so the L1-D interface is unchanged. v2 widens the buffer to 24 entries and adds branch-tag gating (full design in §11.4).

| Property | v1 store buffer | v2 SSB |
|----------|-----------------|--------|
| Entries | 16 | **24** |
| Forwarding | yes | yes (now btag-aware §11.4.3) |
| Branch-tag gating | — | **yes (§11.4)** |
| Drain to L1-D | OoO upon resolve | only when btag = `0xFF` |

### 12.3 TLBs (v1 §11.2, 未变更)

| TLB | Entries | Associativity | Page sizes | Miss penalty |
|-----|---------|---------------|------------|-------------|
| I-TLB | **64** | Fully assoc | 4 KB, 2 MB | L2 TLB lookup |
| D-TLB | **64** | Fully assoc | 4 KB, 2 MB | L2 TLB lookup |
| L2 TLB (unified) | **512** | 8-way | 4 KB, 2 MB, 1 GB | Page table walk |

### 12.4 MTE Memory Path (v1 §11.4, 未变更)

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

**v2 增量:** `TILE.ST` and `TILE.SCATTER` traffic on this path is gated by the 8-entry STQ (§11.5).

### 12.5.1 VTG Vector Load/Store (v2.2)

> **(Change Point #2 -- hardware-revised)**

VTG vector memory operations load or store 256 B or 512 B VTG payloads under predicate control. VTG memory ops share the LSU pipeline with MTE. VTG loads perform a full-tile RMW on writeback (16 cy minimum, same as ALU ops).

**Vector Load:**

```text
VLD.F32  T8.g0, [Xbase + Xoff], T8.p0
```

Flow:
1. Read loop/thread counters from GVIQ entry prefix (`iter0..iter3`)
2. Compute effective address from scalar operands, immediate, and loop counters
3. LSU fetches active lanes from memory (inactive lanes skipped)
4. LSU builds 256 B or 512 B VTG payload
5. LSU submits VTG payload to Group Write Adapter
6. Group Write Adapter: full-tile RMW (16 cy minimum): read old tile, merge VTG payload into sub-range, write merged tile back
7. Update VTG metadata for `T8.g0` (`valid=1`, `defined=1`, `dirty=1`, set after writeback complete)

**Inactive-lane fault suppression:** VTG loads/stores MUST NOT fault for predicate-inactive lanes. The LSU checks `active_lanes` and the predicate VTG before performing address calculation for each lane. Faulting addresses in inactive lanes are suppressed and do not generate exceptions.

**Vector Store:**

```text
VST.F32  T8.g2, [Xbase + Xoff], T8.p0
```

Inactive lanes MUST NOT write memory and MUST NOT fault for invalid inactive-lane addresses.

**Strided and Gather:**

| Instruction | Syntax | Operation |
|-------------|--------|-----------|
| `VLDSTRIDE` | `VLDSTRIDE.type Td, Xbase, Xstride, Xcount, Tp` | `Td[i] = mem[Xbase + i*Xstride]` |
| `VSTSTRIDE` | `VSTSTRIDE.type Ts, Xbase, Xstride, Xcount, Tp` | `mem[Xbase + i*Xstride] = Ts[i]` |
| `PGATHER` | `PGATHER.type Tpd, [Xbase + Ts*esize], Tp` | Predicate gather: `Tpd[i] = mem[Xbase + Ts[i]*esize]` |

**Ordering within a VTG micro block:** Vector loads/stores inside the same micro block use conservative ordering (load after store with unknown alias requires a block boundary or fence). No `VWAIT` — ordering is handled by the existing scalar memory ordering model (§12.5).

### 12.5 Memory Ordering (v1 §11.5, 未变更; v2 增加 SSB 备注)

Within a single thread:

- **Scalar loads and stores** maintain **program order** through the LSU's address disambiguation (store-to-load forwarding, load queue snooping). In v2 the forwarding source is the SSB (24 entries) instead of v1's 16-entry store buffer; semantics identical.
- **TILE.LD/ST** operations are **unordered** with respect to each other by default. Software uses `FENCE` instructions when ordering between tile operations and scalar operations is required.
- **CUBE.OPA** reads from TRegFile-4K are ordered with respect to preceding TILE.LD operations by the **Tile RAT ready bits** (the cube RS will not issue until the source physical tiles are marked "ready" by completed TILE.LD operations).

The SSB's branch-tag gating is **orthogonal** to memory ordering: stores still drain in alloc-age order, just only when their branch tag is non-speculative (§11.4.2).

---

## 13. Mixed-Domain Instruction Scheduling

> **(v1 → v2: 子节 13.A / 13.B / 13.C / 13.D 完整复制自 v1 §12.1 / §12.2 / §12.3 / §12.4。v2 增量为 §13.1 / §13.2 / §13.3。)**

All four domains share the same front-end, dispatch to domain-specific RSs, and synchronize through Tile RAT ready bits / TCB / CDB.

### 13.A Unified Front-End, Distributed Back-End (v1 §12.1, 未变更)

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

### 13.B Cross-Domain Dependencies (v1 §12.2, 未变更; v2 增加投机条目见 §13.3)

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
| **Vector → Vector** (reduction result) | VEC reduction ops produce column/row-vector tile result (TCB completion) |

### 13.C Tile RAT Wakeup & Tile Completion Bus (TCB) — (v1 §12.3, 未变更)

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
    For each RS entry with N tile sources (up to 3 in v2):
      if (ptsrc_k == TCB_tag && !trdy_k):  trdy_k ← 1
    Ready to issue when all trdy bits set (and scalar rdy if applicable)
```

### 13.D Concurrent Execution Example (v1 §12.4, 未变更)

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

### 13.1 New scheduling considerations under speculation (v2 增量)

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

> **(v1 → v2: 内容未变更,以下完整复制自 v1 §15。)**

### 16.1 Core-to-NoC Interface (v1 §15.1)

| Parameter | Value |
|-----------|-------|
| Bus width | **256 bits** (32 B) |
| Protocol | AXI4 (or similar point-to-point) |
| Outstanding requests | **32** (read) + **16** (write) |
| Burst length | Up to 4 beats (128 B, 2 cache lines) |
| Clock domain | Core clock (synchronous) or async bridge |

### 16.2 Cache Coherence (v1 §15.2)

The Davinci core is designed primarily for single-core or non-coherent multi-core configurations (AI accelerator context). When coherence is needed:

| Parameter | Value |
|-----------|-------|
| Protocol | MOESI or directory-based |
| Snoop filter | L2 tag duplicate |
| Coherence granularity | 64 B (cache line) |

For tile data (TRegFile-4K), coherence is managed at the software level. Tile data bypasses the coherence protocol, flowing through the MTE's dedicated memory path.

### 16.3 Debug & Trace Interface (v1 §15.3)

| Feature | Description |
|---------|-------------|
| Debug halt | External debug request halts core at next instruction boundary |
| PC trace | Compressed branch trace (taken/not-taken stream) |
| Performance counters | 8 programmable counters: IPC, branch mispredict rate, cache miss rate, cube utilization, MTE stalls, RS occupancy |
| Breakpoint registers | 4 instruction address breakpoints + 2 data address watchpoints |

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
| **v2.1** | 2026-04-30 | **Native 3-source ternary FMA family (`VFMA`, `VFNMA`, `VLERP`) added — see §2.2.6a.** Operand `C` is promoted to a **dual role** (mask **or** value tile) selected by a new 1-bit issue-time `c_role ∈ {MASK, VALUE}` flag in the instruction word's `funct6` field (§2.2.2, §2.2.3). With `c_role = VALUE`, `C` is fetched as a full 4 KB value tile through a **3rd VEC-side TRegFile read port (R1)** — TRegFile-4K has 8 read ports, so this is purely a binding allocation, no new SRAM or bank-conflict pressure. With three value tiles fetched in parallel within one 8 cy epoch, `VFMA` runs at the **same throughput as a binary `VADD` / `VMUL` (1 tile / 8 cy)** — a 2× speed-up over the emulated `VMUL` + `VADD` two-instruction sequence — and produces the IEEE-754 single-rounding FMA result, halving the rounding error vs. emulation (critical for FP16 / BF16 / FP8 narrow-format normalisation kernels). **Justification (from [`FMA指令场景说明.md`](FMA指令场景说明.md))**: the canonical `y = γ·x̂ + β` LayerNorm / RMSNorm affine, Welford incremental update (`μ_new = δ·inv_n + μ_old`, `M2_new = δ·δ_2 + M2_old`), Welford state merge, activation polynomials (`gelu`, `swiglu`), and trigonometric polynomials (`sin`, `cos`) all need a third operand that is **not** the previous accumulator — v2.0's `VFMA_ACC D = A·B + Acc` does not apply. **Hardware delta vs. v2.0**: ~6 K gate (~0.2 % of VEC-4K-v2 area) — ~5 K for adding a 512 B/cy value-mode read path on `SC` alongside the existing 1-bit-mask path, ~1 K for control-path widening (Tile RAT / RS / dispatch carry the `c_role` bit). The stage-(B) per-lane FMA core, microcode beat machinery, and 8-port TRegFile already supported `A·B + Z` and the 3rd binding allocation. RS entry width unchanged in concept (the `c_role` bit slots into the existing flags). **Pipeline timing**: §8.3.7 latency table updated with `VFMA / VFNMA` rows (16 cy total = 8 fetch + 8 retire, 1/8 throughput) and `VLERP` (24 cy total, 1/16 throughput due to dual retire); mixed-`is_transpose` rows added (16 cy fetch for one-mismatched, 24 cy for all-distinct degenerate). **Documentation updates**: §2.2.2 operand model gains the `c_role` row + 3rd-port rationale callout; §2.2.3 encoding diagram shows the new `c_role` bit; **new §2.2.6a with full ISA semantics, kernel motivation, hardware-cost breakdown, and pipeline-timing table**; §2.2.8 instruction list gains **Category O — Native 3-source Ternary FMA family**; §8.3.7 latency table updated. **Backward compatibility preserved**: v1 / v2.0 binaries emit `c_role = MASK` exclusively, `R1` stays idle and clock-gated, no behaviour change. Companion update in [`vector4k_v2.md`](vector4k_v2.md) v0.18 (§3.1, §3.3c, §6.2, §7.6, §10). |
| **v2.0** | 2026-04-30 | **Initial Davinci-v2 specification.** Three major changes vs. v1: **(1) TRegFile-4K with per-port `is_transpose` flag** ([`tregfile4k.md`](tregfile4k.md) §7), enabling row-mode or col-mode delivery at full 512 B/cy; consumed by v2 vector unit and (optionally) by cube and MTE. **(2) Vector unit re-architected to VEC-4K-v2** ([`vector4k_v2.md`](vector4k_v2.md)): explicit SRAM-based staging registers (`SA`, `SB`, `SC`) decoupling TRegFile fetch from compute, per-beat microcode dispatch, 3-source / 2-dest tile operands with per-element bitmask predication, restored FP4 and FP8 formats, three new PTO instructions (`TINV` matrix inverse up to 128×128 FP32, `TROWRANGE_MUL` row-range product, `TMRGSORT` bitonic sort over any `N = 2^p` up to 8192), and **tile-register metadata** (32 b: `shape.x`, `shape.y`, `format`) carried via a new Tile Metadata RAT. **(3) Branch-prediction-driven speculative execution** with a ROB-less recovery scheme (§11): a 5-K-gate Branch-Tag Speculation Tracker (8 tags + 8×8 ancestry bitmap), a 24-entry **Speculative Store Buffer** (SSB) that gates scalar stores by branch tag, and an 8-entry **Speculative Tile-Store Queue** (STQ) that gates MTE bulk stores by branch tag. The scheme proves that all three classes of speculative state (renamed registers/tiles, in-flight pipeline state, externally-visible memory effects) can be safely recovered without a Reorder Buffer: classes A and B reuse the v1 RAT-checkpoint + refcount + branch-tag-CAM machinery; class C is gated by SSB / STQ until the producing branch tag becomes non-speculative. Mispredict penalty: 6–7 cy (vs. v1's 6 cy). Total v2 speculation hardware: ~110 K gate (~0.025 mm²), about 3.5% of the v1 core area. v2 core area: ~3.41 mm², a ~5% increase over v1's ~3.26 mm². Performance gains: 1.3–1.4× on masked-vector kernels (softmax, layer norm, masked attention), ~100× on `TINV`-bound (Kalman, NeRF pose) and `TMRGSORT`-bound (top-k, beam-search) kernels, and ~2–3× sustained scalar IPC improvement on speculative-heavy code paths. Cube unit, scalar unit, memory subsystem (caches), and external interfaces remain unchanged from v1. |
