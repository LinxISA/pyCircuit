# Davinci-v2 Scalar Pipeline: LinxCore BCC-Style Redesign

> **Document ID**: DSP-001  
> **Version**: v1.0  
> **Date**: 2026-05-01  
> **Status**: Proposal  
> **Target**: `pyCircuit/designs/outerCube/Davinci_superscalar_v2.md`  
> **Change Point**: #1 — Adopt LinxCore BCC's scalar frontend/rename/issue/execute pipeline; replace RAT checkpoints with MapQ; use `atag`/`ptag` naming throughout

---

## 1. Motivation

### 1.1 Current Davinci-v2 Issue Model

Davinci-v2 currently uses a **centralized Reservation Station (RS)** model:

| RS | Depth | Issue Width | Functional Units |
|-----|-------|------------|-----------------|
| Scalar RS | 32 entries | 6 (4 ALU + 1 MUL + 1 BRU) | 4× ALU, 1× MUL, 1× BRU |
| LSU RS | 24 entries | 2 (1 load + 1 store) | 1× Load + 1× Store |
| Vector RS | 24 entries | 1 | VEC-4K-v2 |
| Cube RS | 4 entries | 1 | outerCube MXU |
| MTE RS | 16 entries | 2 | MTE Engine |

**Wakeup mechanism**: 6-port CDB broadcast. Each RS entry compares `psrc1`/`psrc2` against CDB tags. Total comparators: `32 × 2 × 6 = 384` (for scalar RS alone).

**Select logic**: Each functional unit selects the oldest-ready entry based on an intra-RS `age[5:0]` field.

**Key limitations**:

1. **Single-cycle rename + dispatch**: The current Scalar RS dispatch is 4-wide, limiting dispatch throughput in tight scalar loops.
2. **No cross-IQ age coordination**: Different RSs have no shared understanding of which instruction is oldest across the machine.
3. **CDB port contention**: 6 CDB ports broadcasting to 100 RS entries creates a wide, high-capacitance comparator bus — this becomes a critical path for wider-issue machines.
4. **No explicit P1/I1/I2 separation**: The wakeup-to-pick cycle-level constraint is undocumented. Ready logic and pick logic are fused into a single combinational block.
5. **RAT checkpoints are insufficient for wide OoO**: The 8-slot RAT checkpoint store snapshots SMAP but not the Ready Table, leaving a gap in the recovery model when speculation goes wide.

### 1.2 Why LinxCore BCC's Model Is Better

LinxCore BCC (Block-Ordered Superscalar Core) establishes the canonical pattern for wide-issue OoO with a clean separation of concerns:

| Dimension | Davinci-v2 (current) | LinxCore BCC (target) |
|-----------|---------------------|----------------------|
| IQ topology | One centralized RS per unit | **Multiple physical IQs**, type-separated |
| Age encoding | Intra-RS `age[5:0]` field | **ROB-slot-index-based sub-head age**, wrap-friendly |
| Pick strategy | Oldest-ready within one RS | **Cross-IQ oldest-ready**, cascaded multi-slot allocation |
| Wakeup | CDB direct broadcast to RS entries | **Ready Table (、集中 ptag bitmap) + per-IQ can_issue** |
| Rename | D2 single cycle | **D1/D2/D3 three-stage pipeline** |
| Recovery | RAT checkpoints (SMAP only) | **MapQ + CMAP + SMAP** (3-table model with instruction-precise cut points) |
| Commit | In-order, no ROB | ROB-based precise retire (LinxCore; Davinci-v2 BCC retains no-ROB) |
| Flush | Global CAM-clear | **BID-based flush propagation** |

**Key insight**: LinxCore BCC's design scales to wider issue machines because:
- The Ready Table reduces the wakeup fanout from `O(iq_depth × issue_w × pregs)` to `O(pregs)` bitmap reads.
- Age comparison uses the ROB slot index (already in program order) rather than a separate age field.
- The issue picker is pure combinational logic with cascaded priority encoding — critical path grows as `O(depth × width)` in logic levels, not wire-bound comparators.

---

## 2. Design Goals

1. **Preserve Davinci-v2's key characteristics**:
   - 32 architectural GPRs (X0–X31), 128 physical GPRs (P0–P127)
   - 12-stage scalar pipeline (Fetch-to-WB)
   - Multi-latency functional units (MUL 4-cycle, DIV 12–20-cycle, LD 4-cycle L1 hit)
   - Mixed-domain scheduling (Scalar/Vector/Cube/MTE share frontend)
   - RAT checkpoints replaced by MapQ with instruction-precise recovery
   - No precise exceptions (AI kernel envelope)

2. **Adopt LinxCore BCC's issue mechanism**:
   - Multi physical IQs (`alu_iq`, `bru_iq`, `lsu_iq`)
   - D1/D2/D3 rename pipeline (decode → rename request → rename complete)
   - Ready Table + age-matrix issue picker
   - P1(pick) / I1(RF read arbitration) / I2(confirm issue) stages
   - BID-based flush propagation

3. **Adopt LinxCore's naming conventions**:
   - GPR architectural register → **atag** (architectural tag)
   - Physical register → **ptag** (physical tag)
   - RAT checkpoints → **MapQ** (speculative rename increment log)
   - Committed rename map → **CMAP**
   - Speculative rename map → **SMAP**

4. **Design constraints**:
   - Fetch/Decode/Dispatch width = 4 (unchanged)
   - Commit width = 4 (unchanged)
   - Issue width extensible to 8-wide (currently 6-wide)
   - No precise exceptions (maintain AI kernel envelope)

---

## 3. Pipeline Structure: Davinci-v2 BCC

### 3.1 Complete Stage List

```
F0 → F1 → F2 → F3 → IB → F4 → D1 → D2 → D3 → S1 → S2 → P1 → I1 → I2 → E1 → … → EX_n → W1
                                    └── Rename ──┘           └── Issue ──┘        └── Execute ──┘
```

| Stage | Name | Function |
|--------|------|---------|
| F0 | Fetch PC Select | PC mux (redirect/sequential/flush) |
| F1 | I-Cache Lookup + BTB | Tag+BTB lookup, 4-way set-assoc |
| F2 | I-Cache Response + Predict | Cache data return, TAGE/BTB prediction |
| F3 | Stitch + BSTART Annotation | Cross-line stitch, BSTART boundary marking |
| IB | Instruction Buffer | Depth-8 fetch/decode synchronization buffer |
| F4 | Decode Handoff Register | D1 input register |
| **D1** | **Decode + RID/atag Allocation** | Decode 4 instr, allocate RID, atag lookup |
| **D2** | **Rename Request** | Read SMAP, resolve P/T/U source ptag, allocate ptag, build MapQ entry |
| **D3** | **Rename Complete + IQ/ROB Dispatch Prep** | Write SMAP, IQ/ROB tail allocation, dispatch routing |
| **S1** | **Dispatch Preparation** | IQ routing, resource checks (free list, MapQ space) |
| **S2** | **Dispatch Execute** | IQ entry write, free list update, MapQ commit |
| **P1** | **Issue Pick** | Ready Table query, age-matrix pick, select oldest-ready entries |
| **I1** | **Operand Read Planning** | Global RF read-port arbitration |
| **I2** | **Issue Confirm** | IQ entry deallocation, RF read-port occupancy confirm |
| E1–EX_n | Execute | Functional unit execution (variable latency) |
| W1 | Writeback | CDB broadcast, Ready Table update, wakeup |

**Total pipeline depth**: Fetch-to-WB = 17+ cycles (approximately 5 cycles longer than current due to stage separation).

### 3.2 Differences from LinxCore BCC (Davinci-v2-Specific Adaptations)

| LinxCore BCC | Davinci-v2 BCC |
|--------------|----------------|
| 16 checkpoints, MapQ-based | **12 MapQ entries**, instruction-precise recovery |
| Full block structure (BSTART/BSTOP + BID + BROB) | **Deferred** — block model not introduced in this change point |
| ROB (64 entries) for in-order retire | **No ROB** — refcount + MapQ for speculative recovery |
| CMAP updated on ROB retire | **No CMAP update** — ptag freed when orphan + refcount=0 |
| BSTART/BSTOP are architectural markers | **Not introduced** — flat control flow |
| 8 physical IQs (alu_iq0, shared_iq1, bru_iq, agu_iq0, agu_iq1, std_iq0, std_iq1, cmd_iq) | **3 physical IQs**: scalar_alu_iq, scalar_bru_iq, lsu_iq |
| T/U separate FIFO rename tracked via CMAP on BSTOP | **Tile RAT** (32→256) remains independent domain |
| Flush uses `flush_bid` (64-bit) | **Flush uses `flush_tag` (3-bit branch tag)** — no BID yet |

---

## 4. Rename Register Model

### 4.1 The Three-Table Model (CMAP / SMAP / MapQ)

The rename model follows LinxCore BCC exactly, adapted to Davinci-v2's规模和 no-ROB constraint:

#### CMAP (Committed Map)

- **Width**: 32 × `ptag_w` entries. For 128 physical registers, `ptag_w = 7`.
- **Content**: `CMAP[atag] → ptag` for the architecturally committed mapping.
- **Update**: When a ptag becomes **orphan** (no longer the current mapping for any atag) **and** its `refcount` reaches zero, the ptag is returned to the free list. CMAP itself is **not updated on commit** — unlike LinxCore BCC's CMAP (which updates on ROB retire), Davinci-v2 BCC has no retire event. Instead, SMAP is updated at rename time, and CMAP serves as the **flush restoration target**.

```
CMAP[atag] = committed ptag for atag
              updated when: atag's old ptag becomes orphan + refcount=0
              flushed-from on mispredict: SMAP ← CMAP
```

#### SMAP (Speculative Map)

- **Width**: 32 × `ptag_w` entries.
- **Content**: `SMAP[atag] → ptag` for the rename-time (speculative) mapping.
- **Update**: Updated speculatively on each D2 rename group, in program order across the 4 dispatch slots.
- **Flush**: On mispredict, `SMAP ← CMAP` (flash-restore in one cycle). All speculative mappings are discarded.
- **On start marker** (future: BSTART; currently: N/A): SMAP entries 24–31 (T tile regs) are cleared.

```
# D2 rename step (per slot, in program order):
if dst.kind == P:           # GPR destination
    old_ptag = SMAP[dst.atag]   # will become orphan
    SMAP[dst.atag] = new_ptag   # rename: atag now maps to new ptag
    refcount[old_ptag] -= 1       # decremented; freed if orphan + refcount=0
    refcount[new_ptag] += 1
    MapQ.push(MapQEntry { atag=dst.atag, old_ptag, new_ptag, rid=slot.rid })
elif dst.kind == T:          # Tile destination
    # Tile RAT handles independently (separate domain)
```

#### MapQ (Speculative Rename Increment Log)

MapQ is the **replacement for RAT checkpoints**. It records the incremental changes made to SMAP during speculative execution, enabling instruction-precise recovery without a ROB.

**Key insight**: Unlike LinxCore BCC's MapQ (which uses CMAP for commit-side updates), Davinci-v2 BCC uses MapQ purely for **speculative tracking**. Commit-side freeing is handled by the existing refcount mechanism.

```
MapQ Entry:
{
    valid: 1,
    atag: 6,        # which architectural register
    old_ptag: 7,    # what it mapped to before this rename
    new_ptag: 7,    # what it maps to now (speculative)
    rid: 6,         # rename ID (program order, for cut-point)
    is_push_t: 1,   # T-stack push (clears downstream T entries)
    is_push_u: 1,   # U-stack push (clears downstream U entries)
}

# MapQ behavior:
# - D2: push MapQ entry for each P-dst rename
# - Flush: replay MapQ in reverse order to restore SMAP to CMAP state
#   (iterate MapQ from tail to head, undo each rename)
# - Commit (future ROB): evict MapQ entries as they become non-speculative
```

**MapQ depth**: 12 entries (matching the checkpoint depth in LinxCore BCC). New entries push from head; the oldest 12 rename operations can be recovered on flush.

**Instruction-precise recovery**: On branch mispredict at `flush_rid`, MapQ is replayed backwards from the youngest entry until `entry.rid > flush_rid`. For each entry reversed: `SMAP[atag] ← old_ptag`, `refcount[new_ptag]--`, `refcount[old_ptag]++`. This restores SMAP to the exact state it had at the mispredicted branch's rename time.

### 4.2 Rename Register State Machine

```
┌─────────────────────────────────────────────────────────────┐
│  CMAP [32 × ptag_w]                                          │
│  atag → ptag (committed view)                                │
│  Updated when: ptag becomes orphan + refcount=0               │
│  Flush target: SMAP ← CMAP                                   │
└─────────────────────────────────────────────────────────────┘
         ↑ restore on flush (full CMAP restore)
         │
┌─────────────────────────────────────────────────────────────┐
│  SMAP [32 × ptag_w]                                          │
│  atag → ptag (speculative view, active for rename)          │
│  Updated on each D2 rename group (in program order)          │
│  Flush: SMAP ← CMAP (full restore via MapQ replay)         │
└─────────────────────────────────────────────────────────────┘
         ↑ writeback (ptag becomes ready)
         │
┌─────────────────────────────────────────────────────────────┐
│  Ready Table [128-bit bitmap]                                │
│  bit[i] = 1: ptag i has a valid value                       │
│  bit[i] = 0: ptag i is waiting for writeback                 │
│  Set on: CDB writeback (ptag becomes ready)                  │
│  Clear on: D2 dispatch (ptag allocated as dst, not yet ready) │
│  Reset on: flush (all ptags become temporarily untrusted)      │
└─────────────────────────────────────────────────────────────┘
         ↑ can_issue check
         │
┌─────────────────────────────────────────────────────────────┐
│  MapQ [12-entry ring buffer]                                 │
│  Records speculative SMAP increments                          │
│  Push on: each P-dst rename in D2                           │
│  Flush replay: reverse from tail until rid > flush_rid       │
│  Fields: {atag, old_ptag, new_ptag, rid, is_push_t/u}    │
└─────────────────────────────────────────────────────────────┘
         ↑ tracked by
         │
┌─────────────────────────────────────────────────────────────┐
│  Refcount [128 × 3 bits]                                    │
│  refcount[ptag] = number of active mappings to ptag         │
│  Incremented on: rename allocates ptag as new mapping        │
│  Decremented on: rename remaps atag away (old ptag orphans) │
│  Freed when: orphan + refcount == 0                         │
└─────────────────────────────────────────────────────────────┘
```

---

## 5. D1 Stage: Decode + RID / atag Allocation

### 5.1 Function

D1 reads the 4-wide fetch bundle from F4 and produces the canonical decoded uop fields. D1 is responsible for:

1. **Decode**: `f4_window` (64-bit raw bits) → opcode, uop_type, operand registers, immediate
2. **RID allocation**: Allocate a unique **Rename ID** (6-bit) per decoded uop, tracking program order
3. **atag lookup**: Resolve source architectural register indices to their **atag** form

### 5.2 Source Operand Classification

Each source operand carries its **architectural register index** (atag, 0–31 for GPRs) and its **operand class**:

| Class | Register Set | Davinci-v2 Domain |
|-------|-------------|-------------------|
| **P** | X0–X31 (GPR) | Scalar integer, FP |
| **T** | T0–T31 (Tile) | Vector, Cube, MTE operands |
| **U** | U0–U3 (Uncore) | Future extension |

**Operand encoding** in D1 output:
```
src[i].atag  : 6-bit   # architectural register index (0–31)
src[i].class : 2-bit   # P=0, T=1, U=2, CARG=3
```

### 5.3 D1 Output Uop Format

```python
{
    valid: 1,                    # Fetch bundle slot valid
    pc: 64,                     # This instruction's PC
    opcode: 12,                  # Operation code
    uop_type: 8,                # Micro-operation type
    src: [                     # 3 source operands
        { atag: 6, pclass: 2 },  # P/T/U/CARG
        { atag: 6, pclass: 2 },
        { atag: 6, pclass: 2 }
    ],
    dst: { atag: 6, pclass: 2 },  # Destination operand
    imm: 64,                   # Immediate value
    imm_type: 4,               # Immediate type
    rid: 6,                    # Rename ID (program order)
    lsid: 32,                  # Load-Store ID (LSU ops only)
    checkpoint_id: 4,           # MapQ entry ID (for flush recovery)
    pred_taken: 1,            # Branch prediction direction
    boundary_type: 3,          # Block boundary type (future)
    insn_len: 3,             # Instruction length (bytes)
    insn_raw: 64,              # Raw instruction bits
}
```

---

## 6. D2 Stage: Rename Request

### 6.1 Function

D2 receives the D1 decoded uop bundle and performs **register renaming** in a single cycle. This is the core rename logic.

#### P (GPR) Rename

```python
# Per D2 slot (slots 0..3, processed in program order):
# "smap_live" accumulates SMAP updates from earlier slots in the same group

for slot in range(4):
    u = d1_uop[slot]
    if not u.valid:
        continue
    
    # Step 1: Resolve source ptag from SMAP (live state)
    if u.src[0].pclass == P:
        src0_ptag = smap_live[u.src[0].atag]   # atag → ptag lookup
    elif u.src[0].pclass == T:
        src0_ptag = tile_rat[u.src[0].atag]   # Tile RAT lookup (independent)
    elif u.src[0].pclass == CARG:
        src0_ptag = 0  # CARG resolved by BID, no ptag
    
    # (Same for src[1], src[2])
    
    # Step 2: Allocate new ptag for destination (if P-dst)
    if u.dst.pclass == P and u.dst.atag != 0:   # atag=0 is r0 (hardwired zero)
        old_ptag = smap_live[u.dst.atag]        # will become orphan
        new_ptag = allocate_from_free_list(free_list)  # lowest-numbered free ptag
        
        # Update SMAP (live, for later slots in same group)
        smap_live[u.dst.atag] = new_ptag
        
        # Update refcount
        refcount[old_ptag] -= 1    # old ptag: one fewer mapping
        refcount[new_ptag] += 1     # new ptag: one more mapping
        
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
        
        # If old_ptag becomes orphan (refcount=0) → free old_ptag to free_list
    
    # Step 3: T-stack push (Tile RAT independent; increment tile refcount)
    if u.dst.pclass == T:
        tile_rat.push(u.dst.atag, new_tile_ptag)
    
    # Step 4: U-stack push (U registers; future)
    if u.dst.pclass == U:
        ucore_rat.push(u.dst.atag, new_utag)
```

#### T/U Rename (Tile RAT, Independent Domain)

Tile rename is **independent** of the scalar P rename. Davinci-v2's existing Tile RAT (32 architectural tile regs → 256 physical tile regs) is preserved unchanged. The Tile RAT operates with its own free list and refcount mechanism, entirely separate from the P-reg rename pipeline.

| Domain | Rename Mechanism | Map | Recovery |
|--------|----------------|-----|---------|
| **P** (GPR) | Map-based (SMAP) | SMAP / CMAP / MapQ | MapQ replay |
| **T** (Tile) | Map-based (Tile RAT) | Tile RAT | Tile RAT refcount |
| **U** (Uncore) | FIFO (future) | — | — |

### 6.2 Intra-Group Bypass

Instructions in the same D2 group that depend on a destination produced by an earlier slot in the same group use **bypass** (not a SMAP lookup):

```
Slot 0:  ADD  X5, X2, X3     → dst.atag=X5 → ptag=P40
Slot 1:  MUL  X6, X5, X7   → src.atag=X5 → bypass P40 (not SMAP)
Slot 2:  SUB  X5, X8, X9   → dst.atag=X5 → ptag=P41
Slot 3:  ADD  X10, X5, X6  → src.atag=X5 → bypass P41 (newest)
```

Implementation: SMAP is updated sequentially within the group. Each slot reads the SMAP state produced by all previous slots (not the SMAP from the previous cycle). Bypass is resolved by comparing `src.atag == earlier_dst.atag` — `36 × 7-bit comparators` for 3 sources × 3 earlier slots.

---

## 7. D3 Stage: Rename Complete + Dispatch Prep

### 7.1 Function

D3 is the **rename-complete register boundary**. It holds:

1. **Resolved source ptags**: `src0_ptag`, `src1_ptag`, `src2_ptag` (resolved from SMAP or bypass)
2. **Resolved destination ptag**: `pdst` (newly allocated)
3. **Ready query**: From the Ready Table, query `ready_mask[src0_ptag]`, `ready_mask[src1_ptag]` — initializes each IQ entry's `src_ready` state

D3 also performs the MapQ head advancement and initial free-list management.

### 7.2 D3 Output Format

```python
{
    valid: 1,
    pc: 64,
    opcode: 12,
    src0_ptag: 7,       # Resolved source ptag 0
    src1_ptag: 7,       # Resolved source ptag 1
    src2_ptag: 7,       # Resolved source ptag 2 (immediate/pc-rel/3rd operand)
    src_ready: (3,),     # Ready state from Ready Table: {src0_ready, src1_ready, src2_ready}
    pdst: 7,            # Newly allocated destination ptag
    dst_atag: 6,        # Destination atag (for refcount on orphan)
    dst_class: 2,       # P/T/U
    has_dst: 1,         # Whether this uop writes a register
    lsid: 32,           # Load-Store ID (LSU ops only)
    rid: 6,             # Program order ID
    checkpoint_id: 4,    # MapQ entry ID
    imm: 64,
}
```

---

## 8. S1 / S2 Stages: Dispatch Preparation + Execute

### 8.1 S1 — Dispatch Preparation

S1 receives D3 renamed uops and performs **resource availability checks**:

| Check | Condition | Recovery |
|-------|-----------|---------|
| **Free list** | `free_mask` has ≥ N free ptags for dispatched P-dst ops | Stall |
| **MapQ space** | `mapq.count < mapq_depth - 1` (keep 1 slot for safety) | Stall |
| **IQ space** | Each IQ type has enough free entries for dispatched slots | Stall |

**IQ Routing** (identical to LinxCore BCC):

```python
def classify(uop):
    if uop.opcode in LSU_OPS:
        return lsu_iq       # LSU: 32 entries, 2-wide issue
    elif uop.opcode in BRU_OPS:
        return bru_iq       # BRU: 16 entries, 1-wide issue
    else:
        return alu_iq       # ALU/FSU: 48 entries, 4-wide issue
```

### 8.2 S2 — Dispatch Execute

S2 performs the actual IQ entry write and free list update:

```python
# Per physical IQ (S2 executes per IQ type in parallel):
for slot in range(dispatch_w):
    iq_type = s1_iq_route[slot]
    entry_idx = iq_alloc.allocate(iq_type)  # Lowest-numbered free entry
    
    # IQ entry write:
    iq[entry_idx].valid = 1
    iq[entry_idx].src0_ptag = d3_uop[slot].src0_ptag
    iq[entry_idx].src1_ptag = d3_uop[slot].src1_ptag
    iq[entry_idx].pdst = d3_uop[slot].pdst
    iq[entry_idx].src_ready = d3_uop[slot].src_ready  # From Ready Table at D3
    iq[entry_idx].rid = d3_uop[slot].rid               # Program order ID (for age)
    iq[entry_idx].lsid = d3_uop[slot].lsid
    iq[entry_idx].checkpoint_id = d3_uop[slot].checkpoint_id

# Free list update:
free_mask &= ~allocated_ptags   # Clear bits for newly allocated ptags
```

---

## 9. Physical IQ Layout

### 9.1 IQ Topology

Three physical IQs, parameterized from LinxCore BCC:

| Physical IQ | Type | Depth | Issue Width | Functional Units |
|-------------|------|-------|------------|-----------------|
| `alu_iq` | ALU / FSU | 48 entries | **4** | 4× ALU + FSU |
| `bru_iq` | BRU | 16 entries | **1** | 1× BRU (branch resolve) |
| `lsu_iq` | LSU | 32 entries | **2** | 1× Load + 1× Store |

> **Note**: Issue width increases from current 6-wide (4 ALU + 1 MUL + 1 BRU) to 7-wide (alu_iq × 4 + bru_iq × 1 + lsu_iq × 2). MUL reuses ALU IQ but has 4-cycle pipelined execution. DIV reuses ALU IQ with non-pipelined blocking.

### 9.2 IQ Entry Format

All IQ entries carry the **program order RID** (`rid`, 6-bit) for age-based pick. No separate age field is maintained — the RID is the age encoding.

```python
# ALU IQ Entry (48 entries × 93 bits)
{
    valid: 1,
    rid: 6,              # Program order ID — used for age comparison in pick
    op: 12,              # Operation code
    pc: 64,              # PC (for trace)
    imm: 64,              # Immediate value
    src0_ptag: 7,         # Source 0 ptag
    src1_ptag: 7,         # Source 1 ptag
    src2_ptag: 7,         # Source 2 ptag (immediate/pc-rel)
    pdst: 7,              # Destination ptag
    src_ready: (3,),      # Source ready bits (from Ready Table at dispatch)
    has_dst: 1,
    checkpoint_id: 4,       # MapQ entry ID (for flush)
}
# ALU IQ total: 48 × 93 ≈ 4,464 bits ≈ 558 bytes

# BRU IQ Entry (16 entries × 95 bits)
{
    valid: 1,
    rid: 6,
    op: 12,
    pc: 64,
    pdst: 7,
    src0_ptag: 7,
    src1_ptag: 7,
    src_ready: (2,),
    has_dst: 1,
    checkpoint_id: 4,
    pred_taken: 1,         # Branch prediction direction
}
# BRU IQ total: 16 × 95 ≈ 1,520 bits ≈ 190 bytes

# LSU IQ Entry (32 entries × 128 bits)
{
    valid: 1,
    rid: 6,
    op: 12,
    pc: 64,
    lsid: 32,             # Load-Store ID (memory ordering)
    src0_ptag: 7,         # Base register ptag
    src1_ptag: 7,         # Offset register ptag
    src_ready: (2,),
    has_dst: 1,
    checkpoint_id: 4,
    addr_ready: 1,        # AGU address computation complete
}
# LSU IQ total: 32 × 128 ≈ 4,096 bits ≈ 512 bytes

# Total IQ storage: ~558 + 190 + 512 ≈ 1,260 bytes
```

### 9.3 Comparison with Current Davinci-v2

| Dimension | Current Davinci-v2 RS | Davinci-v2 BCC IQ |
|-----------|----------------------|-------------------|
| IQ topology | 1 centralized Scalar RS (32 entries) | 3 physical IQs (alu_iq 48 + bru_iq 16 + lsu_iq 32 = 96 total) |
| Wakeup comparators | 384 (32×2×6 CDB ports) | **0** (Ready Table bitmap lookup replaces all) |
| Issue width | 6 | 7 (4+1+2) |
| IQ storage | ~1.7 KB (RS entries only) | ~1.26 KB |
| Age field | `age[5:0]` per entry | **RID-based** (no extra field; uses existing rid) |
| Ready state | Per-entry `rdy1`/`rdy2` bits set by CDB | **Per-entry `src_ready` bitmap** from Ready Table at dispatch |

---

## 10. Ready Table + Age-Matrix Issue Picker

### 10.1 Ready Table

The Ready Table is the core innovation that eliminates the `O(iq_depth × issue_w × pregs)` CDB comparator array.

```python
class ReadyTable:
    """128-bit bitmap: bit[i] = 1 means ptag i has a valid value."""
    
    def __init__(self):
        self.mask = 0xFFFFFFFFFFFFFFFFFFFFFFFF  # 128 bits = 2 × uint64
    
    def set(self, ptag: int):
        """Set ptag to ready (called on CDB writeback)."""
        self.mask |= (1 << ptag)
    
    def clear(self, ptag: int):
        """Set ptag to not-ready (called on dispatch allocation)."""
        self.mask &= ~(1 << ptag)
    
    def is_ready(self, ptag: int) -> bool:
        return (self.mask >> ptag) & 1 == 1
    
    def read(self, ptag: int) -> bool:
        """Combinational read for can_issue computation."""
        return self.is_ready(ptag)
```

**Update rules per cycle** (combinational → registered):

```
ready_next = ready_current

# 1. D2 dispatch: allocated ptag is not yet ready
for each dispatched dst with pclass == P:
    ready_next.clear(pdst)      # clear bit in bitmap

# 2. CDB writeback: ptag becomes ready
for each CDB writeback port (6 ports):
    if wb.valid:
        ready_next.set(wb.ptag)  # set bit in bitmap

# 3. Flush: all ptags become temporarily untrusted
if do_flush:
    ready_next = ALL_ONES   # reset all bits

Ready Table Register ← ready_next   # Sampled on clock edge
```

**Ready Table is read combinatorially by the issue picker** (not registered at read time). This is the key efficiency: each `can_issue` computation is a single bit-test, not a bank of comparators.

### 10.2 Issue Picker (Age-Matrix Pick)

The issue picker is **purely combinational logic** (no state). For each physical IQ:

**Step 1: can_issue computation** (per IQ entry):

```python
for entry in iq.entries:
    src0_rdy = ready_table.read(entry.src0_ptag)  # O(1) bit-test
    src1_rdy = ready_table.read(entry.src1_ptag)  # O(1) bit-test
    src2_rdy = ready_table.read(entry.src2_ptag)  # O(1) bit-test
    
    can_issue[entry] = (
        entry.valid
        & src0_rdy
        & src1_rdy
        & src2_rdy
    )
```

**Step 2: Cascaded age-matrix pick** (one pick per issue lane):

```python
# ALU IQ: 4-wide issue (alu_w = 4)
# Slot 0: select oldest can_issue entry
# Slot 1: select next-oldest (excluding Slot 0's choice)
# ...

selected = []
excluded = set()

for lane in range(4):   # alu_w = 4
    winner = None
    best_age = 0x3F     # Max RID value = youngest
    
    for entry in alu_iq.entries:
        if entry not in excluded and can_issue[entry]:
            # Sub-head age: smaller = older = higher priority
            age = (entry.rid - head_rid) & 0x3F  # Wrap-friendly: mod 64
            if age < best_age:
                best_age = age
                winner = entry
    
    if winner:
        selected.append(winner)
        excluded.add(winner)
    else:
        selected.append(None)   # Lane empty
```

**Age encoding** (wrap-friendly, from LinxCore BCC):

The RID is a 6-bit program-order counter. Sub-head age:
```
age = (entry.rid - head_rid) mod 64
```

This gives:
- **Older than head** (entry.rid < head_rid): age ≈ 64 + entry.rid - head_rid → large positive value
- **Younger than head** (entry.rid > head_rid): age ≈ entry.rid - head_rid → small positive value

Therefore, the entry with the **smallest age** is the **oldest instruction**. The mod-64 arithmetic handles the 6-bit wrap correctly without special casing.

### 10.3 Wakeup Path Timing

```
Cycle N:
  W1:  CDB broadcast → ptag P40 is ready
  W1:  ready_next ← ready | {P40}
  
Cycle N+1 (clock edge):
  Ready Table Register ← ready_next
  
Cycle N+1:
  P1:  can_issue[i] recomputed (Ready Table combinational read → O(1) bit-test)
  P1:  Age-matrix pick selects winners
  I1:  RF read-port arbitration
  
Cycle N+2:
  I2:  Issue confirm: IQ entry.valid ← 0 (deallocated)
  I2:  RF read-port occupancy confirmed
```

**Total wakeup latency: 2 cycles** (Ready Table register → can_issue visible → pick → RF read).

This is 1 cycle slower than Davinci-v2's current single-cycle CDB→RS comparator wakeup, but:
- Ready Table lookup is O(1) bit-mask, not O(pregs) comparators
- The latency does not grow with issue width
- For AI kernels (compute-bound, not branch-bound), the impact is negligible

---

## 11. Multi-Latency Functional Unit Handling

### 11.1 Variable-Latency FU Wakeup

Davinci-v2 has multiple variable-latency functional units:

| Unit | Latency | Treatment |
|------|---------|-----------|
| ALU | 1 cycle | Single-cycle, normal wakeup |
| MUL | 4 cycles | Pipelined; multi-cycle in-flight entries tracked by Ready Table |
| DIV | 12–20 cycles | Non-pipelined, blocks FU until complete |
| LD (L1 hit) | 4 cycles | LSU pipeline |
| LD (L2 hit) | 12 cycles | LSU MSHR |
| LD (DRAM) | 200–400 cycles | LSU MSHR + external |

**Issue picker does not distinguish latency** — each selected entry enters the execution unit, and the FU manages its own latency pipeline. CDB writeback broadcasts the ptag to the Ready Table regardless of which FU produced it.

**Wakeup for variable-latency ops**: Multiple results from different latency units can be in flight simultaneously. Each IQ entry's `can_issue` is independently determined by the Ready Table. This is correct because each ptag has exactly one producer; the Ready Table tracks ptag readiness, not instruction age.

### 11.2 Load/Store Special Handling

LSU IQ entries have an additional `addr_ready` bit, managed independently from the Ready Table:

```python
lsu_entry.can_issue = (
    entry.valid
    & entry.src_ready[0]      # base register ready (Ready Table)
    & entry.src_ready[1]      # offset register ready (Ready Table)
    & entry.addr_ready        # AGU address computation complete
)
```

`addr_ready` is set by the AGU execution stage when address calculation finishes. This is separate from the Ready Table update (which tracks register file data readiness, not address readiness).

---

## 12. MapQ: Flush and Recovery

### 12.1 Flush Trigger Sources

| Source | Condition | Recovery |
|--------|-----------|---------|
| Branch mispredict | Branch direction wrong at EX1 | MapQ replay + SMAP ← CMAP |
| FP exception | FP exception detected | Same |
| TLB miss | Page fault | Same (AI kernels assume no page faults) |

### 12.2 MapQ Replay Recovery

On a branch mispredict at `flush_rid` (the RID of the mispredicted branch):

```
# MapQ is a 12-entry ring buffer (oldest at head, newest at tail)
# Replay in reverse order (youngest → oldest)

for entry in mapq.entries.reversed():
    if entry.rid > flush_rid:    # Entry is younger than the mispredicted branch
        # Undo this rename:
        smap[entry.atag] = entry.old_ptag
        refcount[entry.new_ptag] -= 1
        refcount[entry.old_ptag] += 1
        if refcount[entry.new_ptag] == 0 and entry.old_ptag is orphan:
            free_list.push(entry.new_ptag)
        entry.valid = 0        # Invalidate MapQ entry
    else:
        break                   # Older entries are on the correct path

# After replay: SMAP == CMAP (committed state)
# All MapQ entries from the mispredicted branch and younger are undone
```

**Flush propagation**:

| Component | Flush Action |
|-----------|-------------|
| Fetch (F4) | `valid ← 0` (clear fetch bundle) |
| SMAP | Restored from CMAP via MapQ replay |
| Ready Table | `mask ← ALL_ONES` (all ptags temporarily untrusted) |
| Free List | Recomputed from SMAP + refcount |
| MapQ | Entries younger than `flush_rid` invalidated |
| Physical IQs | All entries with `checkpoint_id ≥ flush_checkpoint` → `valid ← 0` |

### 12.3 MapQ vs. RAT Checkpoints (Davinci-v2 v1)

| Dimension | RAT Checkpoints (v1) | MapQ (v2 BCC) |
|-----------|---------------------|----------------|
| Snapshots | 8 full SMAP copies (224 bits each = 1.75 Kb) | 12 incremental entries (96 bits each = 1.15 Kb) |
| Recovery precision | Checkpoint captures SMAP at branch time; restore is exact | MapQ replays increments; same precision |
| Flush cost | Full SMAP copy: 32 × 7-bit = 224 bits | MapQ replay: iterate up to 12 entries |
| Storage efficiency | O(8 × SMAP_size) | O(12 × entry_size) |
| Old ptag recovery | Via refcount | Via MapQ replay (undoes the rename that orphaned the ptag) |
| T/U support | Separate Tile RAT | Separate Tile RAT (unchanged) |

---

## 13. Branch-Tag Flush Model (No BID Yet)

Since this change point does not introduce block structure (BSTART/BSTOP/BID/BROB), flush uses **branch_tag** (3-bit, matching Davinci-v2's existing model):

- Each in-flight branch is assigned a unique `branch_tag` from a pool of 8.
- All IQ entries, MapQ entries, SSB entries, and STQ entries carry the `branch_tag` of the youngest unresolved branch ahead of them.
- On branch resolve (correct): the tag is freed and propagated as a tag-clear event.
- On branch mispredict: the tag becomes the **flush key** — every entry tagged with this branch (or any younger branch) is invalidated atomically.

```
# Branch-tag flush on mispredict:
flush_tag = mispredicted_branch.branch_tag

for each IQ entry e:
    if e.branch_tag == flush_tag:
        e.valid = 0

for each MapQ entry e:
    if e.rid > flush_rid:    # Younger than mispredicted branch
        e.valid = 0

for each SSB entry e:
    if e.branch_tag == flush_tag:
        e.valid = 0

for each STQ entry e:
    if e.branch_tag == flush_tag:
        e.valid = 0
```

The SSB (24 entries) and STQ (8 entries) are preserved from Davinci-v2 v1 and handle speculative memory side effects. Their flush behavior is unchanged — only the tag-based invalidation is consistent with the MapQ flush model.

---

## 14. Key Parameters

| Parameter | Davinci-v2 v1 | Davinci-v2 BCC |
|-----------|---------------|----------------|
| Fetch width | 4 | 4 |
| Decode width | 4 | 4 |
| Dispatch width | 4 | 4 |
| **Scalar issue width** | **6** | **7** (alu_iq×4 + bru_iq×1 + lsu_iq×2) |
| Commit width | 4 | 4 |
| Physical GPRs | 128 (P0–P127) | 128 (P0–P127) |
| `ptag_w` | 7 | 7 |
| RAT checkpoints / MapQ depth | 8 | **12** |
| **IQ topology** | 1 centralized Scalar RS (32 entries) | **3 physical IQs** |
| Scalar RS/IQ depth | 32 | alu_iq: **48**, bru_iq: **16**, lsu_iq: **32** |
| LSU RS/IQ depth | 24 | **32** |
| Wakeup mechanism | CDB broadcast → 384 comparators | **Ready Table (128-bit bitmap) + 0 comparators** |
| CDB comparators | 384 (32×2×6) | **0** |
| Rename stages | D1–D2 (1 cycle) | **D1/D2/D3 (3 cycles)** |
| Age encoding | `age[5:0]` per entry | **RID-based sub-head age (mod 64)** |
| Fetch-to-WB latency | 12 cycles | **17+ cycles** |
| IQ storage | ~1.7 KB | **~1.26 KB** |

---

## 15. Difference Summary: Davinci-v2 BCC vs. LinxCore BCC

| Design Decision | LinxCore BCC | Davinci-v2 BCC | Reason |
|----------------|-------------|----------------|--------|
| Commit model | ROB-based in-order retire | **No ROB** — writeback → immediate commit | AI kernel envelope, no precise exceptions needed |
| CMAP update | On ROB retire | **Not updated** — refcount handles freeing | Same reason |
| T/U rename | FIFO push + CMAP commit on BSTOP | **Tile RAT** (separate domain, unchanged) | T regs are a distinct execution domain |
| Flush target | `flush_bid` (64-bit BID) | **`flush_tag` (3-bit branch tag)** | No block structure yet |
| BRU correction | Deferred to BSTOP commit | **Immediate** (EX1 detect → flush) | No BSTOP boundary model |
| Block model | BSTART/BSTOP + BID + BROB | **Not introduced** (future change point) | Flat control flow for AI kernels |
| Ready Table size | `pregs = 64` | **`pregs = 128`** (P0–P127) | Davinci-v2 has 128 physical GPRs |
| Age bits | `rob_w = 6` (ROB slot) | **`rid_w = 6` (program order)** | No ROB; use RID instead |
| Age wrap | `rob_w`-bit wrap | **6-bit wrap** | Same mechanism |
| Checkpoint depth | 16 | **12** | 8→12 is the incremental change |

---

## 16. Implementation Phases

### Phase 1: Core infrastructure (low risk)
1. Split D2 rename into D1/D2/D3 three-stage pipeline
2. Replace centralized Scalar RS with three physical IQs (alu_iq / bru_iq / lsu_iq)
3. Implement Ready Table (128-bit bitmap) replacing CDB comparators
4. Implement age-matrix issue picker using RID-based sub-head age
5. Replace RAT checkpoints with MapQ (12-entry ring buffer)

### Phase 2: Scheduling enhancements (depends on Phase 1)
6. Extend issue width to 7-wide (alu_iq × 4 + bru_iq × 1 + lsu_iq × 2)
7. Expand LSU IQ to 32 entries, ALU IQ to 48 entries
8. Tune MapQ depth to 12 entries

### Phase 3: Advanced features (future change points)
9. Introduce BID/BROB block tracking (change point #2)
10. Precise exception support (change point #3)

---

## 17. Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| 2-cycle wakeup latency vs. current 1-cycle | Ready Table lookup is O(1) bit-test; latency does not grow with issue width. AI kernels are compute-bound, not branch-bound. |
| Age wrap handling at RID boundary | Mod-64 sub-head age formula handles wrap automatically; formal verification needed |
| MapQ replay correctness | Formal proof that reverse replay restores exact SMAP state; test suite with mispredict injection |
| IQ split increases dispatch complexity | S1/S2 stages encapsulate routing and allocation; parameterizable per IQ type |
| Ready Table multi-port reads | 3 IQs read Ready Table combinatorially (3 bit-tests per entry, not full-port reads); no multi-port SRAM needed |

---

## Appendix A: Pipeline Stage Mapping

| Current Davinci-v2 Stage | New BCC Stage | Change |
|--------------------------|---------------|--------|
| F1–F2 (Fetch) | F0→F1→F2→F3→IB→F4 | Added F3 (BSTART annotation) and IB (sync buffer) |
| D1–D2 (Rename) | **D1** (decode + RID/atag allocation) → **D2** (read SMAP, rename) → **D3** (write SMAP, IQ allocation) | Split into 3 stages |
| DS (Dispatch) | **S1** (resource check) → **S2** (IQ entry write) | Split into 2 stages |
| IS (Issue) | **P1** (age-matrix pick) → **I1** (RF read arbitration) → **I2** (confirm issue) | Split into 3 stages |
| EX (Execute) | E1–EX_n | Unchanged |
| WB (Writeback) | W1 (+ Ready Table update) | Unchanged (Ready Table update added) |

**Net pipeline depth increase**: 5 cycles (D1/D2/D3 replaces D1/D2; S1/S2 replaces DS; P1/I1/I2 replaces IS).

---

## Appendix B: Key Design Equations

```
Ready Table lookup latency:       O(1)    (single bit-test per ptag)
Age-matrix pick latency:          O(depth) per lane (cascaded compare, depth = iq_depth)
Wakeup-to-pick cycle gap:        2 cycles (Ready Table reg → can_issue → pick)
Age encoding:                   age = (entry.rid - head_rid) mod 64
IQ storage:                     ~1.26 KB (alu_iq 558 + bru_iq 190 + lsu_iq 512)
CDB comparator reduction:        384 → 0 (Ready Table replaces all)
MapQ storage:                   12 × 96 b = 1,152 b ≈ 144 bytes
Total recovery storage:           ~1.4 KB (vs. RAT checkpoints ~2.2 KB)
```
