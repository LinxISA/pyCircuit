# Davinci-v2 VTG Vector Micro-Instructions — Hardware-Consistent Design v1.1

> **Document ID**: DSP-002
> **Version**: v1.1
> **Date**: 2026-05-02
> **Status**: **Draft — hardware inconsistencies v1.0 being resolved**
> **Target**: `pyCircuit/designs/outerCube/Davinci_superscalar_v2.md`
> **Change Point**: #2 — Add VTG (Vector Thread Group) vector micro-instructions with SIMD-group execution; v1.1 revises the integration model to be consistent with VEC-4K-v2 and TRegFile-4K-v2 hardware
> **Hardware cross-check**: This document was revised against `vector4k_v2.md` §3–§9, `tregfile4k_v2.md` §3–§7, and `Davinci_superscalar_v2.md` §6–§9.

---

## Change Log v1.0 → v1.1

v1.0 had **3 fatal and 5 high-severity hardware inconsistencies** identified by cross-check:

| # | Issue | Fix in v1.1 |
|---|-------|-------------|
| F1 | TRegFile port conflict: VTG Group Read Adapter claimed independent R0/R4 ports, but VEC-4K-v2 is hard-bound to R0/R4. | VTG **reuses** VEC-4K-v2's port bindings; VTG operands are sub-ranges of the same tiles VEC-4K-v2 stages. No independent ports. |
| F2 | Micro-instruction format mismatch: `MicroOpEntry` {opcode, elem_type} does not match VEC-4K-v2's 64-bit beat-word format {src_*, s_*, xp_*, alu_op, acc_op, ...}. | Micro-instruction buffer stores **pre-decoded VEC beat-word sequences** rather than V*-level instructions. VTG microassembler generates beat words from V* operands. |
| F3 | Beat-level control undefined: VTG microcode is V*-level (38 opcodes) but VEC-4K-v2 ALU is driven beat-by-beat. | VTG micro-instructions are expanded into **per-beat word sequences** by the VTG microassembler. Each VTG op = 1–N beat words. |
| H1 | TRegFile epoch timing: "TRegFile read at I1" implied full tile immediately; ignores 8-cycle epoch. | Revised lifecycle with **prologue model**: VTG submits TRegFile read request; full tile delivered over 8-cycle epoch; sub-range selection happens after prologue. |
| H2 | VTG latency (9 cycles) ignored prologue (8–15 cy) and writeback RMW (16 cy). | Revised to **T_fetch + 1 + T_writeback** = 9–23 cy minimum, plus prologue penalty for alignment. |
| H3 | Group Write Adapter partial-write to TRegFile: TRegFile has no partial-write mechanism. | Group Write Adapter performs **full-tile read-modify-write**: read old tile (8 cy), merge VTG sub-range, write merged tile (8 cy) = **16 cy minimum**. |
| H4 | Port arbitration: "arbitrate via existing issue pipeline" — no mechanism described. | Added **VEC-domain arbitration matrix** covering Vector RS + GVIQ + MTE RS. VEC ALU is 1-wide, single client at a time. |
| H5 | Staging register sizes: VTG described Group Read Adapter outputting 256/512 B to SA/SB; but SA/SB/SC are 4 KB each. | VTG operates **behind** VEC staging: SA/SB/SC filled by VEC-4K-v2 prologue (unchanged); VTG sub-range selector reads **from** SA/SB at the ALU input mux, not from TRegFile. |

---

## 1. Motivation (unchanged from v1.0)

### 1.1 Current Davinci-v2 Vector Execution Model

Davinci-v2 currently executes vector instructions as **full-tile operations** on VEC-4K-v2. For AI kernels with strip-mined inner loops, the current model requires the compiler to generate repeated tile ops with different effective addresses. VTG vector micro-instructions enable the same micro-kernel to run across multiple 256 B or 512 B slices inside a tile using a pre-allocated micro-instruction buffer and a warp-like rotation scheduler.

### 1.2 What This Change Adds (v1.1 corrected model)

This change introduces **VTG (Vector Thread Group) vector micro-instructions**:

- One 4 KB tile is partitioned into 16×256 B or 8×512 B VTGs
- Each VTG carries loop/thread counter state in the GVIQ entry prefix
- A **micro-instruction buffer** in the vector ALU holds pre-decoded VEC beat-word sequences, shared by all VTGs via `block_id`
- VTG **operates behind VEC-4K-v2's staging registers** (SA/SB/SC are filled by the VEC prologue); VTG sub-range selection happens at the ALU input mux
- VTG uses the **same TRegFile ports** (R0/R4 for reads, W0 for writeback) as VEC-4K-v2, with VEC-domain arbitration
- The existing VEC-4K-v2 ALU datapath (128-lane SIMD groups, 512 B/cycle throughput) is reused unchanged

---

## 2. Concepts

### 2.1 SIMD Group

The **SIMD group** is VEC-4K-v2's internal 128-lane execution unit. The hardware is identical to the full-tile VEC-4K-v2 path; no changes to the SIMD group are required. In `G512` mode, one VTG fills one SIMD group beat (512 B). In `G256` mode, one VTG fills half a SIMD group beat.

### 2.2 Vector Thread Group (VTG)

A **VTG** is a warp-like scheduling context: 256 B (`G256`) or 512 B (`G512`) inside a 4 KB tile. It has its own `group_id`, loop counters, and active-lane state in the GVIQ entry. VTG operands are tile-relative (`T4.g2` = tile T4, group g2).

**Critical (v1.1):** A VTG is not an independent hardware entity with its own TRegFile ports. VTGs **share** the VEC-4K-v2 datapath and TRegFile ports. Multiple VTGs are in flight simultaneously, but they compete for the same VEC ALU through the GVIQ scheduler. There is no parallel VTG execution — VTG is a **scheduling abstraction**, not a parallel execution unit.

### 2.3 Micro-Instruction Buffer

The **micro-instruction buffer** is a pre-allocated buffer in the vector ALU that holds **pre-decoded VEC beat-word sequences** for VTG blocks. It is shared by all VTGs in the same tile group.

**Critical (v1.1):** The buffer does NOT store V*-level instructions (`VADD.type`, etc.). It stores **VEC beat-word sequences** — each entry is the pre-decoded beat-word sequence that drives VEC-4K-v2's ALU cycle-by-cycle. The VTG microassembler generates these beat-word sequences from V* operands at decode time.

Buffer organization:
```
BufferEntry {
  valid:      1 b
  block_id:   12 b  [tag]
  pc_limit:   8 b   [last beat word index]
  beat_words: array[64] of VECBeatWord  // pre-decoded VEC beat words
}
```

### 2.4 VTG Microassembler

The **VTG microassembler** generates VEC beat-word sequences from V* operands at decode time. It consults the VTG Metadata Table and the VTG's element type, predicate mode, and loop counters to produce the correct beat-word sequence for each VTG micro-op.

---

## 3. Hardware Integration Model

### 3.1 VTG Sits Behind VEC-4K-v2 Staging

The fundamental architectural decision (v1.1) is that **VTG operands are sub-ranges of tiles that VEC-4K-v2 has already staged**:

```
TRegFile-4K
  R0 ──────► VEC prologue (8-cycle epoch)
  R4 ──────► SA (4 KB) ──┐
                            ├── VTG sub-range selector ──► ALU input mux ──► VEC ALU
                       SB (4 KB) ──┘
  W0 ◄────── VTG Group Write Adapter (full-tile RMW)
```

Key consequences:
1. **No new TRegFile ports**: VTG reuses R0/R4 (for tile reads) and W0 (for writeback)
2. **No new staging registers**: VTG reads from SA/SB/SC (4 KB each), which are filled by the VEC prologue
3. **VEC prologue is shared**: Both VTG and full-tile VEC-4K-v2 use the same prologue to fill SA/SB/SC
4. **Sub-range selection at ALU input mux**: The VTG sub-range selector reads 256/512 B sub-ranges from SA/SB/SC and presents them to the ALU

### 3.2 TRegFile Epoch Sharing

VEC-4K-v2's operand-fetch prologue (`vector4k_v2.md` §6.1–§6.3) occupies the TRegFile ports for the prologue duration:

| N_val | `is_xpose` mix | Prologue T_fetch |
|--------|----------------|-----------------|
| 1 | any | **8–15 cy** |
| 2 | uniform | **8–15 cy** |
| 2 | mixed | **16–23 cy** (R2 penalty) |

VTG, operating behind the prologue, is subject to the same latency:
- VTG submits a tile read request at issue
- TRegFile delivers 512 B/cycle over 8 cycles (1 epoch)
- Sub-range selection from SA/SB begins after the relevant strips have arrived
- VTG compute begins after sub-range selection

### 3.3 VEC-Domain Arbitration Matrix

The VEC-4K-v2 ALU is **1-wide**: only one client can use it per cycle. The three clients are:

| Client | Issue Queue | Throughput | Priority |
|--------|------------|------------|----------|
| Full-tile VEC-4K-v2 | Vector RS (24 entries) | 1 tile op / 8–15 cy | Highest (coarser grain) |
| VTG micro-op | GVIQ (32 entries) | 1 VTG op / 8–16 cy | Medium |
| MTE | MTE RS (16 entries) | Varies | Lowest |

The VEC-domain arbiter grants the VEC ALU to one client per cycle based on readiness and priority. Full-tile VEC ops have higher priority because they hold the prologue for longer and are coarser-grain.

---

## 4. VTG Micro-Instruction Buffer (Hardware-Correct)

### 4.1 Buffer Organization

The buffer stores **pre-decoded VEC beat-word sequences** (not V*-level instructions):

```
BufferEntry {
  valid:      1 b
  block_id:   12 b  [tag]
  pc_limit:   8 b   [last beat_word_index]

  beat_words: array[64] of VECBeatWord
}

VECBeatWord {          // Same as VEC-4K-v2's SOP beat-word format
  src_A:      3 b    // SA / SB / ACC / SX / SY / ZERO
  src_B:      3 b
  src_C:      3 b    // SC (mask/value)
  s_A:        3 b    // strip index 0..7
  s_B:        3 b
  s_C:        3 b
  xp_A:       1 b    // transpose for operand A
  xp_B:       1 b
  xp_C:       1 b
  alu_op:     5 b    // ADD/SUB/MUL/FMA/etc.
  acc_op:     3 b    // NONE/INIT/ACCUM/MERGE/READOUT
  acc_slot:   1 b    // LO/HI
  wr_en_D0:   1 b
  wr_strip_D0: 3 b  // which strip writes to D0
  wr_en_D1:   1 b
  wr_strip_D1: 3 b
}
```

### 4.2 VTG Microassembler

At decode time, the VTG microassembler generates the beat-word sequence for each VTG micro-op:

```
VADD.F32 Td.gN, Ts0.gM, Ts1.gP, Tp:
  // beat 0: SA ← Ts0 sub-range (group M), SB ← Ts1 sub-range (group P)
  beat_words[0] = {
    src_A: SA, s_A: group_strip_of(M), xp_A: 0,
    src_B: SB, s_B: group_strip_of(P), xp_B: 0,
    src_C: SC, s_C: pred_strip_of(Tp), xp_C: 0,
    alu_op: ADD, acc_op: NONE, wr_en_D0: 1, wr_strip_D0: group_strip_of(N)
  }
  // For G256 (256 B = 1 strip): group_strip_of(gN) = 0..7 depending on byte offset
  // For G512 (512 B = full epoch): one strip covers the full VTG
```

The microassembler consults:
- **VTG Metadata Table**: `group_mode`, `elem_type`, `pred_granule`, `active_bytes`
- **Tile Metadata RAT**: `shape.x`, `shape.y`, `format` (from `Davinci_superscalar_v2.md` §6.1)
- **GVIQ entry prefix**: `iter0..iter3`, `active_lanes`

### 4.3 Buffer Allocation and Access

Buffer allocation: at decode, the Vector Micro Block Builder assigns `block_id` and runs the microassembler to generate the beat-word sequence, writing each `VECBeatWord` into `buffer[block_id % depth][way].beat_words[beat_index]`.

Buffer access at issue:
```
at P1/I1:
    winner = gviq.pick_oldest_ready()
    beat_word = buffer.lookup(winner.block_id, winner.pc_index)
    // beat_word drives VEC ALU for this cycle
    winner.pc_index++
    if winner.pc_index > winner.pc_limit:
        winner.valid = 0  // retire
```

---

## 5. Execution Pipeline (Hardware-Correct)

### 5.1 Revised VTG Micro-Op Lifecycle

```
Cycle N+6:  D1/D2   — Decode + Tile RAT rename; microassembler generates beat-word sequence
Cycle N+11: S1/S2   — GVIQ entry write; micro-instruction buffer populated
Cycle N+12: P1       — GVIQ pick: select oldest-ready VTG micro-op
Cycle N+13: I1       — TRegFile read request submitted (pending register)
Cycle N+13..N+20:    — VEC prologue: SA/SB/SC fill over 8-cycle epoch (512 B/cy × 8)
Cycle N+21: I2       — Issue confirm; SA/SB/SC staging populated; prologue done
Cycle N+22: E1       — VTG sub-range selector: read 256/512 B from SA/SB at ALU input mux
Cycle N+22..N+22+K: — VEC ALU: beat_word drives compute for K beats
Cycle N+22+K+1: W1   — Group Write Adapter: read old tile (8 cy), merge VTG sub-range, write merged tile (8 cy)
Cycle N+22+K+17:     — Writeback complete; VTG ready bit set
```

**Total VTG latency for a single-beat VTG op (K=1):**
- Best case (well-aligned epoch): `8 (prologue) + 1 (compute) + 16 (RMW writeback) = 25 cy`
- Worst case (misaligned epoch): `15 (prologue) + 1 + 16 = 32 cy`

This replaces the v1.0 claim of 9 cycles.

### 5.2 TRegFile Writeback: Full-Tile Read-Modify-Write

The Group Write Adapter performs a **full-tile read-modify-write** for every VTG writeback:

```
Group Write Adapter (writeback):
    // Step 1: Read the full current tile
    TRegFile.submit_read_request(dst_ptag)          // occupies W0 for 8 cycles
    wait 8 cycles
    old_tile = TRegFile.read_data                   // 4 KB

    // Step 2: Merge VTG result into the correct sub-range
    if group_mode == G256:
        start = group_id * 256
        end   = start + 256
    else:  # G512
        start = group_id * 512
        end   = start + 512
    new_tile = old_tile
    new_tile[start:end] = vtg_result               // merge sub-range

    // Step 3: Write merged tile back
    TRegFile.submit_write_request(dst_ptag, new_tile)  // occupies W0 for 8 cycles
    wait 8 cycles
    TRegFile.write_complete()

    // Total: 16 cycles for the RMW cycle
```

**Implication:** VTG writeback ties up W0 for **16 cycles** (8 read + 8 write), which is the same write latency as a full-tile VEC op. VTG does not have a separate write port in v1.1 — it shares W0 with VEC-4K-v2.

### 5.3 VTG Sub-Range Selection at ALU Input Mux

After the prologue completes, SA/SB/SC contain the full 4 KB tile. The VTG sub-range selector reads the appropriate 256/512 B slice:

```
V TG Sub-Range Selector:
    input: SA_full[4096 B], group_id, group_mode
    if group_mode == G256:
        vtg_A[256 B] = SA_full[group_id * 256 : (group_id+1) * 256]
    else:  # G512
        vtg_A[512 B] = SA_full[group_id * 512 : (group_id+1) * 512]
    output: vtg_A → ALU operand mux (SA input)
```

This sub-range selection happens **in parallel with** the VEC ALU input muxing — it is a simple byte-range mux, not a separate pipeline stage. It does not add latency to the critical path.

---

## 6. VTG Micro-Instruction Families

### 6.1 Instruction Syntax (unchanged from v1.0)

```
VINST.type  Td.gN, Ts0.gM, Ts1.gP, Tp.gQ
```

### 6.2 ALU Instructions

| Instruction | Operation | VEC beat words |
|------------|-----------|---------------|
| `VADD` | `Td[i] = Tp[i] ? (Ts0[i] + Ts1[i]) : merge(Td[i])` | 1 beat: `alu_op=ADD, src_A=SA, src_B=SB, mask=SC` |
| `VSUB` | `Td[i] = Tp[i] ? (Ts0[i] - Ts1[i]) : merge(Td[i])` | 1 beat: `alu_op=SUB` |
| `VMUL` | Multiplication | 1 beat: `alu_op=MUL` |
| `VMIN` | `min(Ts0, Ts1)` | 1 beat: `alu_op=MIN` |
| `VMAX` | `max(Ts0, Ts1)` | 1 beat: `alu_op=MAX` |
| `VABS` | `abs(Ts0)` | 1 beat: `alu_op=PASS_A` + post-processing |
| `VNEG` | `-Ts0[i]` | 1 beat: `alu_op=PASS_A` + negate |

### 6.3 Scalar-Broadcast ALU

| Instruction | Operation | Notes |
|------------|-----------|-------|
| `VADDS` | `Td[i] = Ts[i] + scalar` | Scalar from SX/SY broadcast |
| `VMULS` | `Td[i] = Ts[i] × scalar` | Scalar broadcast via SX/SY |

The scalar operand comes from the scalar register file (via SX/SY staging) and is broadcast to all lanes by VEC's existing broadcast mechanism.

### 6.4 Compare and Select

| Instruction | Operation | VEC beat words |
|------------|-----------|---------------|
| `VCMP.{LT/...}` | Predicate result | 1 beat: `alu_op=CMP`, `wr_en_D0=0` |
| `VSEL` | `Td = Tp ? Ts0 : Ts1` | 1 beat: `alu_op=SELECT, src_A=Ts0, src_B=Ts1` |
| `VMERGE` | Merging-mode fill | 1 beat: `alu_op=PASS_A` (old dest + pred gate) |

### 6.5 Memory Instructions

| Instruction | Operation | VEC beat words |
|------------|-----------|---------------|
| `VLD` | Load 256/512 B into VTG | VTG microassembler expands to VEC-style strip-fill sequence |
| `VST` | Store 256/512 B from VTG | VTG microassembler expands to VEC-style strip-drain sequence |
| `VLDSTRIDE` | Strided load | Multiple beats with stride address calculation |
| `VSTSTRIDE` | Strided store | Multiple beats |
| `PGATHER` | Predicate gather | Multiple beats with gather address |

**Inactive-lane fault suppression** is handled by the LSU checking the active-lane mask before address generation.

### 6.6 Predicate Instructions

| Instruction | Operation |
|-------------|-----------|
| `PLT` | `Tpd[i] = (i < iter0)` — loop counter predicate via SX/SY broadcast |
| `PAND` | Predicate AND |
| `POR` | Predicate OR |
| `PXOR` | Predicate XOR |
| `PNOT` | Predicate NOT |

---

## 7. GVIQ — Grouped Vector Issue Queue

### 7.1 GVIQ Entry (unchanged from v1.0)

```
GVIQEntry {
  valid:           1 b
  block_id:       12 b
  pc_index:        8 b
  tile_group:      5 b   // architectural tile T0..T31
  phys_tile:       8 b   // physical tile PT0..PT255
  group_id:        4 b   // 0..15 (G256) or 0..7 (G512)
  group_mode:      1 b
  thread_id:       8 b
  iter0..iter3:   4×16 b
  active_lanes:   16 b
  active_group_mask: 16 b
  src0_ptag:       8 b
  src1_ptag:       8 b
  src2_ptag:       8 b
  pred_ptag:       8 b
  dst_ptag:        8 b
  has_dst:         1 b
  src_ready:       4 b
  vtg_ready:       1 b
  branch_tag:      3 b
}
```

### 7.2 VTG Wakeup

VTG readiness is tracked by the **VTG Ready Table** (256-bit bitmap, one bit per physical tile). When a VTG micro-op writes back:
1. Group Write Adapter performs full-tile RMW (16 cy)
2. On writeback completion, `VTG_Ready_Table[dst_ptag] = 1`
3. GVIQ entries waiting on this tile set `src_ready`

### 7.3 Issue Rules

| Rule | Description |
|------|-------------|
| GVIQ-1 | `pc_index <= pc_limit` for the given `block_id` |
| GVIQ-2 | All source VTG `src_ready` bits set |
| GVIQ-3 | Active loop counter (`iter*`) non-zero |
| GVIQ-4 | GVIQ is 1-wide: one VTG micro-op per cycle |
| GVIQ-5 | VEC-4K-v2 ALU is single-ported: VTG competes with Vector RS for ALU access |
| GVIQ-6 | VTG competes with Vector RS for TRegFile ports (R0, R4, W0) |
| GVIQ-7 | Paired `G256` issue (optional): two VTGs with matching beat_word share one VEC beat cycle |

---

## 8. VTG Metadata Table

### 8.1 Metadata Structure

The VTG Metadata Table overlays the existing **Tile Metadata RAT** (`Davinci_superscalar_v2.md` §6.1). Each physical tile's metadata entry is extended with VTG-specific fields:

```
TileMetadataEntry (extended, 32+14 = 46 b per physical tile):
  // From Tile Metadata RAT:
  shape.x:    14 b   // columns C
  shape.y:    14 b   // rows R
  format:      4 b   // FP32/FP16/FP8/FP4
  flags:       4 b   // arg_tile, scalar_tile, prefetch_hint

  // VTG additions (overlaid or extending):
  group_mode:  1 b   // G256=0, G512=1
  pred_granule: 2 b  // 8/16/32-bit lane grouping
  // The following are per-VTG (16 entries per tile):
  vtg_meta[16]: {
    valid:       1 b
    defined:     1 b
    dirty:       1 b
    kind:        3 b   // VEC | PRED | WIDE_LO | WIDE_HI | SCRATCH | UNDEF
  }
```

**Note (v1.1):** `elem_type` is NOT duplicated — it is the same 4-bit `format` field from the Tile Metadata RAT. `active_bytes` is computed from `shape.x × shape.y × E` and the VTG's position in the tile.

### 8.2 VTG Byte Mapping (unchanged)

`G256` (16 VTGs, 256 B each):

| VTG | Byte range |
|-----|-----------|
| `g0` | `[0, 255]` |
| `g1` | `[256, 511]` |
| ... | ... |
| `g15` | `[3840, 4095]` |

`G512` (8 VTGs, 512 B each):

| VTG | Byte range |
|-----|-----------|
| `g0` | `[0, 511]` |
| `g1` | `[512, 1023]` |
| ... | ... |
| `g7` | `[3584, 4095]` |

---

## 9. Integration with Davinci_superscalar_v2.md

### 9.1 What Sections Need Updates

| Section | Update Required |
|---------|----------------|
| §1 Key Parameters | Add VTG parameters; note VTG reuses VEC-4K-v2's R0/R4 ports |
| §2.2.6 VTG Micro-Instr | Update with hardware-correct lifecycle and beat-word format |
| §3 Block Diagram | VTG sub-range selector shown between SA/SB and ALU input mux |
| §7.4 GVIQ | Update with prologue model and VEC-domain arbitration |
| §8.3.10 VTG | Update with staging model, prologue timing, RMW writeback |
| §9.2.5 VTG | Update metadata to overlay Tile Metadata RAT (not separate table) |
| §10.5.1 VTG dependency | Update with VTG Ready Table and RMW writeback latency |
| §12.5.1 VTG memory | Update with prologue timing and RMW writeback |

### 9.2 Key Corrections to Apply

1. **VEC staging reuse**: VTG operates **behind** SA/SB/SC, not as a separate path. Show VTG sub-range selector between staging and ALU.
2. **Prologue model**: VTG latency starts with `T_fetch` (8–15 cy), not cycle I1.
3. **RMW writeback**: Group Write Adapter = full-tile read (8 cy) + merge + full-tile write (8 cy) = **16 cy minimum**.
4. **Metadata overlay**: VTG metadata fields (`group_mode`, `pred_granule`, VTG validity) extend the Tile Metadata RAT entry, not a separate table.
5. **elem_type = format**: Remove duplicate `elem_type` field; use `format` from Tile Metadata RAT.

---

## 10. Key Parameters (v1.1)

| Parameter | Value | Notes |
|-----------|-------|-------|
| VTG modes | `G256` (16×256 B VTGs/tile) · `G512` (8×512 B VTGs/tile) | |
| GVIQ depth | 32 entries, 1-wide issue | |
| Micro-instruction buffer | 16 entries, 2-way; stores **VEC beat-word sequences** | Each entry = up to 64 × ~50 b = ~3.2 Kb |
| VTG beat words per micro-op | 1–N (1 for elementwise, N for strip-strided) | |
| VTG latency (best case) | **25 cy** (8 prologue + 1 compute + 16 RMW) | |
| VTG latency (worst case) | **32 cy** (15 prologue + 1 + 16) | |
| TRegFile ports used | R0 + R4 (reads, shared with VEC-4K-v2 prologue) + W0 (writeback, shared) | |
| VTG sub-range selection | At ALU input mux, after prologue completes | No extra pipeline stage |
| VTG Ready Table | 256-bit bitmap | Same as scalar Ready Table |
| Metadata | Tile Metadata RAT extended with VTG fields | No separate VTG Metadata Table |

---

## 11. Open Questions (remaining after v1.1)

| ID | Question | Priority |
|----|----------|----------|
| OQ-A | Does VTG need a dedicated write port (W6/W7) to avoid blocking VEC-4K-v2's W0 during VTG RMW? | High |
| OQ-B | What is the arbitration priority between Vector RS and GVIQ for VEC ALU access? | High |
| OQ-C | How does VTG interact with VEC's accumulator (256×32 b ping-pong)? Can VTG produce accumulator results? | Medium |
| OQ-D | For VTG memory ops (VLD/VST), does VTG share the LSU pipeline with MTE, or does it have its own LSU path? | Medium |
| OQ-E | Should paired G256 issue be v1 or deferred? | Medium |
| OQ-F | What is the exact beat-word encoding for each VTG opcode? Requires enumerating all 38 V* × all format × all predicate_mode combinations. | High |
| OQ-G | Does VTG support the full VEC beat-word set (including `acc_op`, `shuffle`, `CAS` for TMRGSORT)? | Medium |

---

## Appendix A: VTG Lifecycle Comparison (v1.0 vs v1.1)

| Dimension | v1.0 (incorrect) | v1.1 (hardware-correct) |
|-----------|---------------------|--------------------------|
| TRegFile ports | Independent R0/R4 | Shared with VEC-4K-v2 |
| TRegFile reads | "Immediate" at I1 | 8-cycle epoch prologue |
| TRegFile writes | Direct `write_vtg(dst_ptag, group_id, result)` | Full-tile RMW (16 cy minimum) |
| Staging | VTG output to SA/SB (new 256/512 B) | VTG reads from SA/SB (4 KB) at ALU mux |
| Latency claimed | 9 cycles | 25–32 cycles minimum |
| Micro-instruction format | `MicroOpEntry {opcode, elem_type, pred_mode}` | Pre-decoded `VECBeatWord` sequence |
| Beat-level control | Not specified | Each VTG micro-op = 1–N beat words from microassembler |
| Metadata | Separate VTG Metadata Table (16 entries/tile) | Overlaid on Tile Metadata RAT |
| Port arbitration | "Arbitrate via existing pipeline" | VEC-domain arbiter: Vector RS > GVIQ > MTE RS |
