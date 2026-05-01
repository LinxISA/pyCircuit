# Davinci-v2 Block-ROB — Instruction-Block Precise Exception Support (DSP-003)

> **Document ID**: DSP-003
> **Version**: v1.0
> **Date**: 2026-05-02
> **Status**: Draft
> **Target**: `pyCircuit/designs/outerCube/Davinci_superscalar_v2.md`
> **Change Point**: #3 — Add Block-ROB (instruction-block commit) for precise exception support
> **Dependencies**: PR #8 (BCC scalar pipeline), PR #10 (VTG vector micro-instructions)

---

## Change Log

| Version | Date | Changes |
|---------|------|---------|
| v1.0 | 2026-05-02 | Initial draft |

---

## 1. Motivation

### 1.1 What the Current Design Cannot Do

The current Davinci-v2 design (BCC scalar pipeline + VTG vector micro-instructions) implements a **ROB-less out-of-order execution model** with:

- **MapQ** (12-entry ring buffer) for instruction-precise P-reg (scalar GPR) recovery
- **Tile RAT checkpoints** (8 snapshots) for tile register recovery
- **Branch-tag tracker** (8 tags + 8x8 ancestry bitmap) for speculation gating
- **SSB** (24-entry) and **STQ** (8-entry) for speculative memory-side-effect gating
- **Flash-restore** of RAT checkpoints + branch-tag CAM-clear for mispredict recovery

This model is **explicitly limited** to **run-to-completion kernel execution**:

> *"no precise architectural exceptions and no OS-level interrupts"*
> — Davinci_superscalar_v2.md §11.1, §11.7

Specifically, the current design cannot:

1. **Identify the faulting instruction** on an exception (page fault, trap, system call)
2. **Maintain precise architectural state** for interrupts or exceptions mid-execution
3. **Support hardware breakpoints** or single-step debugging with precision
4. **Provide in-order memory commit** for I/O semantics

### 1.2 Why Block-ROB (Not a Flat ROB)

Adding a full flat ROB would cost:
- ~64 entries x ~200 b/entry = ~12.8 Kb
- Complex priority encoder, age-tracking across 64+ entries
- Wide commit width logic (complexity O(n) in entry count)
- Retirement bandwidth bottleneck

A **Block-ROB (BROB)** takes a different approach:

1. **Organize instructions into blocks** — the compiler groups N dynamic instructions into an *instruction block* delimited by a `BSTART` marker at head and a `BSTOP` marker at tail.
2. **Track blocks, not individual instructions** — the BROB manages block lifetimes (allocate → complete → retire), not per-uop commit.
3. **Per-block in-order commit** — all instructions inside a block commit in program order, but the BROB only commits **one block at a time**, at block boundaries.
4. **Precise exceptions at block granularity** — the faulting instruction's block is identified, all younger blocks are squashed.
5. **Reuse existing infrastructure** — the MapQ already provides instruction-precise P-reg recovery; the SSB/STQ already gate memory effects; the branch-tag tracker already manages speculation depth.

The key insight from LinxCore: **a flat ROB is overkill when block structure is already present in the ISA.** By lifting commit from instruction granularity to block granularity, the BROB achieves:

- **Precise exception support** (block-granularity, sufficient for OS kernel entry)
- **Store commit within blocks** (in-order, block-bounded)
- **Reduced complexity** (O(block_count) not O(instruction_count))
- **Natural fit with VTG** (a VTG micro-block is a natural BROB scheduling unit)

### 1.3 Design Goals

| Goal | Target |
|------|--------|
| Precise exception identification | Block-granularity (identify faulting block, squash younger blocks) |
| Register recovery | Instruction-precise within faulting block via MapQ reverse replay |
| Memory recovery | In-order block commit; SSB/STQ gates memory effects within block |
| Store commit | In-order within block; stores retire from SSB only at block commit |
| Branch mispredict | Continue using MapQ + RAT flash-restore + branch-tag CAM-clear |
| Hardware cost | ~128-entry BROB + ~32-entry Block SSB + Block STQ + integration logic |
| Compatibility | Continues to support existing BCC scalar pipeline and VTG micro-instructions |

### 1.4 What Block-ROB Does NOT Provide

Block-granularity precise exception means:
- **Not instruction-precise**: if two instructions in the same block could fault differently, the block is the unit of identification.
- **Not general-purpose OS interrupt support**: this is still an AI-kernel-oriented design; only kernel-entry traps (system calls, fatal page faults) are supported.
- **Not single-step debugging precision**: within a block, instructions may have executed past a breakpoint before it is reported.

This is the same model as modern CPUs with micro-op caches and retirement buffering: the ROB retires in program order but commit is pipelined. The block boundary is the *commit point*, not the exception point.

---

## 2. Block Structure

### 2.1 Instruction Block Definition

An **instruction block** is a contiguous sequence of decoded micro-operations bounded by:

- **BSTART** (inclusive start): the first uop in the block
- **BSTOP** (inclusive end): the last uop in the block

The compiler generates block boundaries at natural control-flow join points:

```
BSTART              # block entry
  <all scalar uops in block>
  <all VTG micro-instructions>
BSTOP               # block exit
```

**Block size** (default, configurable):
- Minimum: 4 uops
- Maximum: 64 uops
- Typical AI kernel: 16-32 uops

### 2.2 Block Type

Each block carries a **block type** that determines its execution model:

| Block Type | Scalar-only | Engine-backed | Notes |
|------------|-------------|---------------|-------|
| `STD` | Yes | No | Pure scalar execution |
| `VTG` | Yes | VTG micro-instructions | VTG micro-block (one per VTG macro) |
| `VEC` | No | Full-tile VEC-4K-v2 | T* tile operations |
| `CUBE` | No | outerCube MXU | CUBE.OPA, CUBE.DRAIN |
| `MTE` | Yes | Memory Tile Engine | TILE.LD, TILE.ST |

A block may be **hybrid**: scalar uops + one VTG micro-block. A `VTG` block type indicates that the block contains VTG micro-instructions and the BROB must track the GVIQ sub-schedule within the block.

### 2.3 Block ID (BID)

Each block is assigned a **Block ID (BID)** at allocation:

```
BID[7:0]  — BROB slot index (0..127)
BID[63:8] — Monotonically increasing sequence number (for uniqueness across wraps)
```

The 8-bit slot index directly maps to the BROB entry. The full-width BID is used for flush ordering: all queues and structures flush by `BID_prefix` — keeping entries with `BID <= flush_BID`, killing entries with `BID > flush_BID`.

---

## 3. BROB Structure

### 3.1 BROB Parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| `BROB_ENTRIES` | 128 | Power-of-2; matches branch-tag depth |
| `BROB_ALLOC_PER_CYCLE` | 1 | One block allocated per cycle |
| `BROB_COMPLETE_PER_CYCLE` | 1 | One block completed per cycle |
| `BROB_RETIRE_PER_CYCLE` | 1 | One block retired per cycle |
| `BID_W = log2(BROB_ENTRIES)` | 7 bits | Slot index in low bits of BID |
| `BLOCKTYPE_W` | 4 bits | Block type encoding |

### 3.2 Per-BROB-Entry State

```
BROBEntry {
  valid:          1 b      -- entry is allocated
  state:           2 b      -- ALLOC | ISSUED | COMPLETE | (implicit: retired via head advance)
  block_uid:       64 b     -- unique block identifier
  bid:             64 b     -- full-width BID (slot + sequence)
  block_type:      4 b      -- STD | VTG | VEC | CUBE | MTE
  head_rid:        7 b      -- RID of first uop in block (iROB index)
  tail_rid:        7 b      -- RID of last uop in block
  n_uops:          6 b      -- number of uops in block (1..64)
  checkpoint_id:    4 b      -- which RAT checkpoint is active for this block
  needs_scalar:    1 b      -- block has scalar uops (BSTOP must retire)
  needs_engine:    1 b      -- block has engine ops (engine_done must arrive)
  engine_done:     1 b      -- engine completion signal received
  scalar_done:      1 b      -- BSTOP retired from iROB
  has_exception:    1 b      -- exception detected within this block
  exception_cause: 16 b     -- trap/exception cause code
  fault_rid:       7 b      -- RID of faulting uop (if has_exception)
  redirect_valid:   1 b      -- block requests a redirect on retire
  redirect_pc:      32 b     -- redirect target PC
  n_stores:        5 b      -- number of stores in this block
  n_vtg_ops:       5 b      -- number of VTG micro-instructions
  gviq_block_start: 7 b     -- GVIQ entry index for first VTG op in block
}
```

**State machine:**

```
FREE --[allocate]--> ALLOC --[issued]--> ISSUED --[complete]--> COMPLETE
                                                              |
                                                          [retire: advance head]
                                                              |
                                                          FREE (tail advances)
```

Completion rule:
```
complete = scalar_done && (needs_engine ? engine_done : 1)
```

### 3.3 BROB Pipeline

The BROB sits alongside the existing BCC scalar pipeline:

```
Fetch --> Decode (D1) --> Rename (D2/D3) --> Dispatch (S1/S2)
                                       |
                                       v
                                  BROB Allocate
                                       |
                                       v
                                  iROB Dispatch (D3)  <-- uops enter iROB here
                                       |
                                       v
                                  Issue / Execute
                                       |
                          +-------------+-------------+
                          |                           |
                          v                           v
                   Scalar Done                  Engine Done
                   (BSTOP retires)              (GVIQ/Vector RS/Cube RS
                          |                signals completion)
                          |                           |
                          +-------------+-------------+
                                        |
                                        v
                               BROB Complete Check
                                        |
                                        v
                               BROB Retire (advance head)
                                        |
                                        v
                               Squash / Redirect / Commit
```

---

## 4. Instruction Block Lifecycle

### 4.1 Block Allocation (D2/D3)

At the **BSTART** uop:

```
1. Allocate BROB entry k from free pool (tail pointer)
2. Set bid = {seq_num++, k[7:0]}  -- full-width BID
3. Set block_type from BSTART metadata
4. Set checkpoint_id = current RAT checkpoint (snapshot for this block)
5. Set head_rid = current iROB head (this BSTART's slot)
6. Set n_uops = 0
7. Set n_stores = 0, n_vtg_ops = 0
8. Set needs_scalar = 1, needs_engine = 0
9. Set scalar_done = 0, engine_done = 0, has_exception = 0
10. Push MapQ entry: {checkpoint_id, RID of BSTART, ...}
11. Stamp all uops in block with bid (3 bits in iROB entry)
12. BSTART retires immediately (no execute, no IQ entry)
```

### 4.2 Block Population (D3)

For each subsequent uop in the block (until BSTOP):

```
1. Allocate iROB entry (existing BCC pipeline logic)
2. Stamp iROB entry with bid (3 bits)
3. Set iROB entry's brob_slot = k (index into BROB)
4. Increment n_uops
5. If is_store: increment n_stores; record SSB slot in block_store_slots[n_stores]
6. If is_vtg_op: increment n_vtg_ops; record GVIQ entry in block_gviq_entries[n_vtg_ops]
7. For scalar uops: enter IQ, execute, writeback normally (BCC pipeline)
8. For VTG ops: enter GVIQ, execute, signal engine_done to BROB
```

### 4.3 Block Closure (BSTOP)

At the **BSTOP** uop:

```
1. Set tail_rid = current iROB entry index
2. Set needs_engine = (n_vtg_ops > 0) || (block_type == VEC) || (block_type == CUBE)
3. BSTOP enters iROB but does NOT retire immediately
4. BSTOP's retire is gated by BROB completion (see SS4.4)
```

### 4.4 Scalar Completion (BSTOP Retire Gate)

The iROB's **commit slot step** module is extended with a **BSTOP retire gate**:

```
BSTOP can retire when ALL of:
  1. BROB entry k is in COMPLETE state (scalar_done && engine_done)
  2. No exception is pending in the block

BSTOP retire flow:
  1. Set scalar_done = 1 in BROB[k]
  2. Check: complete = scalar_done && (needs_engine ? engine_done : 1)
  3. If complete && !has_exception: mark BROB[k].state = COMPLETE
  4. If complete && has_exception: trigger exception reporting (see SS6.2)
  5. Advance BROB head to k+1 (mod 128)
```

### 4.5 Engine Completion

Engines signal completion to the BROB via a **completion bus**:

```
Engine completion message: {engine_done_valid, bid, trap_valid, trap_cause}
  -- bid identifies the block
  -- On arrival: BROB[bid_slot].engine_done = 1
  -- If trap_valid: BROB[bid_slot].has_exception = 1, BROB[bid_slot].fault_rid = faulting_rid
```

The completion bus is shared with the existing TCB (Tile Completion Bus) infrastructure. Each engine (VEC-4K-v2, Cube, MTE LSU) already signals completion on the TCB. The BROB listens to the same bus and matches by `bid`.

### 4.6 Block Retire

Only the **oldest block** (head of BROB) can retire per cycle:

```
Retire rule (in order of priority):
  1. If head has has_exception:
       Report exception (see SS6.2)
       Do NOT commit side effects
       Squash all younger blocks
  2. Else if head is COMPLETE:
       Commit side effects:
         a. Advance SSB head past all stores in this block (drain allowed)
         b. Advance STQ head past all tile stores in this block
         c. Advance head pointer
         d. Free BROB entry
  3. Else: stall (wait for completion)
```

---

## 5. Precise Exception Mechanism

### 5.1 Exception Classification

Two classes of exceptions are handled:

| Class | Source | Handling |
|--------|--------|----------|
| **Scalar exception** | ALU trap, divide-by-zero, illegal instruction | Detected at EX1, `trap_valid` set in iROB entry |
| **Engine exception** | TILE.LD page fault, VTG memory fault | Engine signals via completion bus with `trap_valid` |

### 5.2 Exception Reporting Flow

When an exception is detected inside a block:

```
Step 1: Detection
  Scalar: EX1 stage sets iROB[rid].trap_valid = 1
  Engine: completion bus arrives with trap_valid=1, BROB marks has_exception=1

Step 2: Blocking
  BROB does NOT retire the block
  BSTOP retire is gated: "can retire if !has_exception"
  Block remains at BROB head (blocked)

Step 3: Exception Identification
  BROB[head].has_exception = 1
  BROB[head].fault_rid = faulting_rid
  BROB[head].exception_cause = trap_cause

Step 4: Squash of Younger Blocks
  flush_bid = BROB[head].bid  -- flush all blocks with bid > flush_bid
  All BROB entries with bid > flush_bid are set valid = 0
  All iROB entries with bid > flush_bid are invalidated (branch-tag CAM-clear extended to bid)
  All GVIQ entries with bid > flush_bid are invalidated
  All MapQ entries from flush_rid+1 onward are popped (MapQ reverse replay)
  SSB entries for younger blocks: valid = 0
  STQ entries for younger blocks: valid = 0

Step 5: Register State Recovery
  MapQ reverse replay from faulting RID backward:
    for each MapQ entry from tail down to faulting RID:
      undo SMAP write, restore orphan ptag, pop MapQ
  Tile RAT: restore from checkpoint[BROB[head].checkpoint_id]

Step 6: Exception Delivery
  PC = BSTART_pc (of faulting block)
  Cause = BROB[head].exception_cause
  The faulting instruction's BSTART is the restart point.
  The OS/kernel restores context and re-executes the block.
```

### 5.3 Instruction-Precise Recovery Within a Block

Within a single block, MapQ already provides instruction-precise P-reg recovery:

- MapQ entries are tagged with `RID` (instruction index)
- On exception, MapQ is replayed in reverse from the **faulting RID** (not from the block boundary)
- The `fault_rid` is captured from the iROB entry at exception detection

This means: if two scalar uops in the same block could produce different exceptions, the **faulting RID** is precisely identified and MapQ replay recovers register state up to and including the faulting instruction.

### 5.4 Example: Page Fault in Block

```
Block B: BSTART, uop0, uop1 (TILE.LD), uop2, BSTOP

uop1 executes: TILE.LD triggers page fault on address X.
  -- EX1: TILE.LD sets iROB[rid1].trap_valid = 1
  -- LSU marks block B's BROB entry: has_exception = 1, fault_rid = rid1
  -- BSTOP cannot retire (blocked on has_exception)

Block B is at BROB head, blocked.

Exception delivery:
  flush_bid = B.bid  -- flush all blocks with bid > B.bid (none in this case)
  MapQ replay from faulting RID (rid1) backward
    -- Undo all SMAP writes from uop1, uop0 in reverse order
  Tile RAT: restore from B.checkpoint_id
  Report: PC = BSTART_PC, Cause = page_fault, fault_rid = rid1
```

---

## 6. Store Commit Within Blocks

### 6.1 Block Store Buffer (Block SSB)

Each BROB entry carries an embedded **Block SSB** tracking stores within the block:

```
BROBEntry (extended with Block SSB):
  ...
  block_ssb_base:    5 b   -- index into global Block SSB RAM
  n_stores_in_block: 5 b   -- number of stores in this block
  block_ssb_valid:   1 b   -- stores in this block are in Block SSB
```

The **Block SSB** is a 32-entry structure shared across all BROB entries (using the `block_ssb_base` offset):

```
BlockSSBEntry {
  addr:     40 b   -- cache-line address
  data:     128 b  -- store data (full scalar register)
  size:     3 b    -- 1/2/4/8 B
  valid:     1 b
  bid:       8 b    -- which block this store belongs to
  ssb_idx:   5 b    -- index into scalar SSB (for forwarding)
}
```

### 6.2 Block SSB Allocation

At D3, when a scalar store uop is detected:

```
1. Allocate Block SSB entry j from free pool
2. Set BlockSSB[j] = {addr=0, data=0, size=store_size, valid=1, bid=current_bid}
3. Record j in BROB[current_bid_slot].block_ssb_entries[block_n_stores]
4. Increment BROB[current_bid_slot].n_stores_in_block
5. At EX1: compute address, fill BlockSSB[j].addr
6. At EX2: fill BlockSSB[j].data from GPR writeback
```

### 6.3 Load Forwarding from Block SSB

Loads within the same block forward from the Block SSB:

```
On load execution at EX1:
  for each BlockSSB entry k in same block:
    if BlockSSB[k].valid && addr_match(BlockSSB[k].addr, load_addr):
      forward data from BlockSSB[k]
      -- No need to check bid ordering: Block SSB only contains stores from this block,
         and loads/stores in the same block are already program-ordered
```

### 6.4 Store Commit at Block Retire

At block retire (when head is COMPLETE and !has_exception):

```
1. For each BlockSSB entry j belonging to retiring block:
     -- Transfer BlockSSB[j] to the scalar SSB
     SSB[ssb_idx].valid = 1
     SSB[ssb_idx].addr = BlockSSB[j].addr
     SSB[ssb_idx].data = BlockSSB[j].data
     SSB[ssb_idx].btag = 0xFF  -- immediately non-speculative
     SSB[ssb_idx].drain_rdy = 1   -- can drain immediately
     BlockSSB[j].valid = 0  -- free Block SSB entry
2. Advance BROB head
3. Retire BSTOP: iROB[BSTOP_rid].done = 1
```

**Key property**: stores are committed in program order (block order = program order), and they become non-speculative at block retire. The scalar SSB drain pump then handles memory-write timing independently.

### 6.5 MTE Tile Stores Within Blocks

Tile stores (TILE.ST, TILE.SCATTER) are handled via the **Block STQ**:

```
BlockSTQEntry (per block):
  base_addr:    40 b   -- base memory address
  tile_phys:     8 b   -- source physical tile
  size_log2:     3 b   -- tile size
  valid:         1 b
  bid:           8 b
```

At block retire:
```
For each BlockSTQ entry k in retiring block:
  -- Transfer to STQ
  STQ[stq_idx].valid = 1
  STQ[stq_idx].btag = 0xFF  -- immediately non-speculative
  STQ[stq_idx].drain_rdy = 1  -- can drain immediately
```

---

## 7. Integration with Existing Infrastructure

### 7.1 MapQ (Instruction-Precise P-Reg Recovery)

MapQ is **fully reused** with no modification:

- Each renamed destination still pushes a MapQ entry with `{arch_reg, old_ptag, new_ptag, RID, checkpoint_id}`
- On exception: MapQ is replayed in reverse from `fault_rid` (captured from iROB entry)
- On branch mispredict: MapQ is replayed from `flush_rid` (unchanged from BCC design)

The BROB does not interfere with MapQ. MapQ provides instruction-precise recovery within a block; the BROB provides block-granularity exception identification.

### 7.2 Branch-Tag Tracker

The existing 8-entry branch-tag tracker is **fully reused**:

- Each block is assigned a branch tag at BSTART (same as any branch)
- The block's `bid` is stamped in all iROB entries within the block
- On branch mispredict: the branch-tag tracker flushes all entries with `btag` matching the mispredicted branch or its descendants
- BROB entries are flushed using the same rule: `bid > flush_bid`

### 7.3 RAT Checkpoints

The 8-entry RAT checkpoint snapshot is **extended**:

- Each block's `checkpoint_id` references a RAT checkpoint
- The checkpoint captures the full SMAP state at block entry
- On exception: Tile RAT is restored from `BROB[head].checkpoint_id`
- On branch mispredict: both Scalar RAT and Tile RAT are restored (unchanged)

### 7.4 SSB and STQ (Scalar Memory and Tile Memory)

The existing SSB and STQ are **extended with bid tagging**:

```
SSBEntry (extended):
  ...
  bid:     8 b   -- which block this store belongs to

STQEntry (extended):
  ...
  bid:     8 b   -- which block this tile store belongs to
```

At D2 (store allocation):
```
SSB[idx].bid = current_bid  -- stamp with block ID
STQ[idx].bid = current_bid
```

At block retire:
```
SSB entries with bid == retiring_bid:
  SSB[idx].btag = 0xFF  -- become non-speculative
  SSB[idx].drain_rdy = 1
STQ entries with bid == retiring_bid:
  STQ[idx].btag = 0xFF
  STQ[idx].drain_rdy = 1
```

### 7.5 VTG Integration (GVIQ)

The VTG micro-instruction path integrates with the BROB as follows:

```
Block allocation at BSTART (if block_type == VTG):
  BROB[k].needs_engine = 1
  BROB[k].gviq_block_start = current GVIQ head index

During block execution:
  GVIQ entries are stamped with bid
  VTG micro-instructions execute via GVIQ rotation scheduler
  Each completed VTG micro-op signals completion to GVIQ
  GVIQ tracks: engine_done for the block = (all VTG ops in block completed)

Engine completion at block boundary:
  GVIQ checks: all VTG ops in block have completed
  If yes: signal engine_done to BROB with bid
  If no: engine_done remains 0, BSTOP cannot retire
```

The VTG path is **compatible** with the BROB because:
- VTG micro-instructions already carry `block_id` (the VTG block identifier)
- The GVIQ already tracks completion state per block
- The BROB extends this to gate block retirement

### 7.6 GVIQ Block Scheduling

Within a VTG block, the GVIQ rotation scheduler operates as described in PR #10. The BROB integration adds one rule:

```
GVIQ issue gate (extended):
  old rules: src_ready, iter_nonzero, VEC ALU available
  NEW: block_complete = (BROB[bid_slot].engine_done || !BROB[bid_slot].needs_engine)
  If !block_complete: VTG micro-op cannot retire from GVIQ
```

---

## 8. Pipeline Integration

### 8.1 Extended Pipeline Stages

The Davinci-v2 BCC pipeline is extended with BROB stages:

```
F0 --> F1 --> F2 --> F3 --> IB --> F4 --> D1 --> D2 --> D3
                                                          |
                                                          v
                                                     BROB Alloc
                                                          |
                                                          v
S1 --> S2 --> P1 --> I1 --> I2 --> E1 --> EX_n --> W1
                                                          |
                                                   +------+------+
                                                   |             |
                                                   v             v
                                             Scalar Done   Engine Done
                                                   |             |
                                                   +------+------+
                                                          |
                                                          v
                                                    BROB Complete
                                                          |
                                                          v
                                                    BROB Retire
```

### 8.2 BSTART / BSTOP in the Pipeline

```
BSTART uop:
  D2/D3: Allocate BROB entry
  S1:    Allocate iROB entry, stamp bid
  EX:    Bypasses all execute (no IQ entry)
  WB:    Bypasses writeback
  Retire: Immediate (no gate) -- block allocation is the "commit" of BSTART

BSTOP uop:
  D2/D3: Close block (set tail_rid, needs_engine flags)
  S1:    Allocate iROB entry, stamp bid
  EX:    Bypasses execute
  WB:    Bypasses writeback
  Retire: GATED on BROB[bid_slot].COMPLETE
```

---

## 9. Flush and Recovery

### 9.1 Flush Trigger

Flush is triggered by:
1. **Branch mispredict** (existing branch-tag mechanism)
2. **Exception in block** (new BROB mechanism)

### 9.2 Flush Protocol

```
flush_bid = BROB[head].bid   -- flush all blocks with bid > flush_bid

In parallel (1 cycle):
  a) iROB: invalidate all entries with bid > flush_bid
  b) BROB: set valid = 0 for entries with bid > flush_bid
  c) GVIQ: invalidate entries with bid > flush_bid
  d) IQ entries: invalidate entries with bid > flush_bid (branch-tag CAM-clear extended)
  e) SSB: set valid = 0 for entries with bid > flush_bid
  f) STQ: set valid = 0 for entries with bid > flush_bid
  g) Block SSB: invalidate entries with bid > flush_bid
  h) Block STQ: invalidate entries with bid > flush_bid
  i) MapQ: pop entries from flush_rid+1 backward (undo SMAP writes)
  j) Tile RAT: restore from checkpoint[BROB[flush_bid_slot].checkpoint_id]
  k) Scalar RAT: flash-restore from checkpoint (unchanged)
  l) Branch-tag tracker: free tags for flushed blocks
  m) BROB tail: advance to flush_bid+1 (reclaim flushed entries)
```

### 9.3 Exception Delivery Protocol

```
On exception in block B at BROB head:
  1. Block B is NOT retired (remains at head, blocked)
  2. Flush all blocks with bid > B.bid
  3. MapQ reverse replay from fault_rid backward
  4. Tile RAT restore from B.checkpoint_id
  5. Deliver exception:
       EPC = BSTART_PC
       Cause = B.exception_cause
       TVAL = fault_rid_value (if applicable)
  6. OS/kernel handler restores context and re-executes block B
```

---

## 10. Hardware Cost

### 10.1 New Structures

| Block | Size | Gate Count Estimate |
|-------|------|-------------------|
| BROB (128 entries) | 128 x ~120 b = ~15 Kb | ~150 K (state + control FSM) |
| Block SSB (32 entries) | 32 x ~200 b = ~6.4 Kb | ~60 K |
| Block STQ (16 entries) | 16 x ~100 b = ~1.6 Kb | ~20 K |
| BID tagging in iROB entries (3 b x 64) | ~192 b | ~2 K |
| BID tagging in GVIQ entries (3 b x 32) | ~96 b | ~1 K |
| BID tagging in IQ entries (3 b x 96) | ~288 b | ~3 K |
| BID tagging in SSB/STQ (8 b x 24/8) | ~192 b | ~2 K |
| BROB allocate FSM + complete check | | ~20 K |
| Exception delivery logic | | ~10 K |
| **Total new structures** | ~24 Kb | **~268 K gate** |

### 10.2 Comparison

| Design | Exception Support | Cost |
|--------|-----------------|------|
| Davinci-v2 original (no ROB) | None (kernel-only) | ~110 K gate (SSB/STQ/branch-tag) |
| Davinci-v2 + Block-ROB | Block-granularity precise | ~378 K gate total |
| Flat 64-entry ROB (hypothetical) | Instruction-precise | ~500+ K gate (estimation) |

Block-ROB achieves ~75% of the precision of a flat ROB at ~75% of the gate cost.

---

## 11. Integration with Davinci_superscalar_v2.md

### 11.1 Sections to Update

| Section | Update |
|---------|--------|
| §1 Key Parameters | Add BROB, Block SSB, Block STQ parameters |
| §3 Block Diagram | Add BROB, Block SSB, Block STQ blocks |
| §4 Pipeline | Add BROB Alloc stage; BSTOP retire gate |
| §6 Decode & Rename | BSTART/BSTOP handling, BID allocation |
| §7 Dispatch & Issue | Block SSB allocation, GVIQ block scheduling |
| §8.1 Scalar Unit | BSTOP retire gate in commit logic |
| §10 OoO Model | Add BROB to core principles |
| §10.3 Rename | Block checkpoint_id integration |
| §10.6 Branch Recovery | BROB flush protocol |
| §11 Speculation Recovery | Replace exception "out of scope" with Block-ROB model |
| §11.7 | Remove "precise exceptions out of scope" statement |
| §11.8 Comparison | Add Block-ROB to comparison table |

### 11.2 New Sections to Add

| New Section | Content |
|-------------|---------|
| §11.X BROB — Block Reorder Buffer | Full BROB specification |
| §11.X Block SSB — In-Block Store Commit | Block SSB allocation, commit protocol |
| §11.X Precise Exception Delivery | Exception flow, MapQ replay, OS entry |

---

## 12. Open Questions

| ID | Question | Priority |
|----|----------|----------|
| OQ-1 | Should VTG micro-blocks be treated as a separate block or sub-block? A VTG macro contains multiple VTG micro-ops; should the BROB track at the VTG macro level or at the individual VTG micro-op level? | High |
| OQ-2 | How does BROB interact with the existing VTG GVIQ rotation scheduler? Should GVIQ issue be gated by `BROB[bid_slot].engine_done`? | High |
| OQ-3 | For mixed blocks (scalar + VTG + VEC), does `engine_done` require ALL engines to complete, or can scalar and engine complete in parallel before retire? | High |
| OQ-4 | How many checkpoint entries are needed? Each block uses one checkpoint. With 128 BROB entries and 8 checkpoints, the maximum nesting depth is 8. Is this sufficient? | Medium |
| OQ-5 | Should block retirement commit SSB entries immediately (drain_rdy=1) or queue them for the existing SSB drain pump? | Medium |
| OQ-6 | How does the BROB handle interrupts (external interrupts, not exceptions)? Interrupts are asynchronous and may arrive mid-block. | Low |
| OQ-7 | Should there be a "block fence" instruction that forces a block boundary for interrupt latency reasons? | Low |

---

## Appendix A: Comparison with LinxCore Block-ROB

| Aspect | LinxCore Block-ROB | Davinci-v2 Block-ROB |
|--------|--------------------|----------------------|
| ISA mandate | LinxISA requires BSTART/BSTOP at every control-flow join | Optional; compiler generates blocks at natural join points |
| Block types | 10 types (STD, FP, SYS, MPAR, MSEQ, VPAR, VSEQ, TMA, CUBE, TEPL) | 5 types (STD, VTG, VEC, CUBE, MTE) |
| Engine completion binding | `scalar_done && engine_done` via signal-level wire | Completion bus + bid matching |
| iROB width | Banked (8-entry banks, 64 total) | Unbanked (64 entries, shared) |
| Block size | Variable (macro-op boundary) | 4-64 uops (configurable) |
| MapQ analog | None (instruction-precise via per-uop checkpoint) | MapQ provides instruction-precise P-reg recovery within block |
| Store commit | In-order within block; drain through committed-store path | Block SSB transfers to SSB at block retire |
| Branch correction | Epoch-gated deferred correction | Unchanged from BCC (MapQ reverse replay) |
| Exception granularity | Block-granularity | Block-granularity with fault_rid for instruction precision |
| BID encoding | 64-bit BID (slot + sequence) | 64-bit BID (slot + sequence) |

---

## Appendix B: Worked Example

### B.1 Scalar Block with Exception

```
Block B: BSTART, u0 (ADD r1, r2, r3), u1 (LD r4, [r5]), u2 (MUL r6, r4, r7), BSTOP

Assume u1 triggers a TLB miss / page fault.

Step 1: BSTART at D2
  BROB tail = 5, allocate entry BROB[5]
  bid = {seq=17, slot=5} = 0x11_05
  checkpoint_id = 3
  head_rid = iROB head

Step 2: u0, u1, u2 dispatch through iROB
  Each stamped with bid[7:0] = 5 (3 bits)
  u0 executes normally, writes back
  u1 (LD r4, [r5]) executes: TLB miss detected
    LSU sets iROB[rid1].trap_valid = 1
    LSU sets BROB[5].has_exception = 1
    LSU sets BROB[5].fault_rid = rid1
    LSU sets BROB[5].exception_cause = PAGE_FAULT (0x0F)

Step 3: BSTOP reaches iROB head
  BSTOP retire is gated: !BROB[5].has_exception = FALSE
  BSTOP cannot retire. Block B is blocked at head.

Step 4: Exception delivery (next cycle)
  flush_bid = BROB[5].bid  -- no younger blocks in this case
  MapQ replay from fault_rid (rid1) backward:
    undo SMAP writes from u1 (LD r4), u0 (ADD r1)
    Restore ptags: r4_old --> SMAP[r4], r1_old --> SMAP[r1]
    Pop MapQ entries down to BSTART
  Tile RAT restore from checkpoint_id = 3
  Deliver exception:
    EPC = BSTART_PC
    Cause = PAGE_FAULT
    tval = faulting address from r5
  OS handler restores context, re-executes from BSTART

Step 5: After handler returns
  Block B is re-fetched and re-executed
  BROB[5] is re-allocated (seq number incremented)
```

### B.2 Branch Mispredict Within Block

```
Block B: BSTART, u0 (ADD r1, r2, r3), u1 (setc.eq r4, r1, r0), u2 (BEQ target), BSTOP

Assume u2 (BEQ) is predicted taken but actually not taken (mispredict at EX1).

Step 1: u2 mispredicts at EX1
  Branch-tag tracker: mispredict signal for tag t
  MapQ reverse replay from flush_rid = u2's RID
    undo SMAP writes from u2, u1, u0
  RAT flash-restore from checkpoint_id = 3
  Branch-tag CAM-clear: invalidate iROB entries with btag = t or descendant
  SSB invalidation: entries with btag = t or descendant
  Fetch redirect to correct PC (fall-through)

Step 2: BROB[5] is NOT affected
  The branch mispredict squashes all entries with btag = t
  BROB entries are stamped with bid, not btag
  BROB[5] is not invalidated by the branch-tag mechanism
  Block B is squashed because its iROB entries were invalidated

Step 3: Block B squashed
  BROB[5].valid = 0  (flushed as part of the mispredict)
  MapQ already replayed
  BSTOP never retired; BROB tail advances past BROB[5]

Step 4: New block B' starts at redirect target
  BSTART' allocated at BROB[6]
  BSTOP' closes the correct block
```

**Key point**: the BROB is **not** invalidated by the branch-tag mechanism. Blocks are invalidated because their constituent iROB entries are invalidated (the uops are gone). The BROB entry is invalidated as a side effect of the fetch redirect.

### B.3 VTG Block with Multiple VTG Micro-Ops

```
Block B: BSTART, u0 (VTG_VADD T0.g0, T1.g0, T2.g0), u1 (VTG_VMUL T3.g1, T0.g0, T2.g1), BSTOP

Block type: VTG
needs_engine = 1 (GVIQ engine)
n_vtg_ops = 2

Step 1: BSTART at D2
  BROB[5] allocated, needs_engine = 1, engine_done = 0
  GVIQ entries allocated for u0, u1

Step 2: Scalar uops (BSTART) complete immediately
  Scalar side done (no scalar uops in this block)
  scalar_done = 1 (BSTOP retired trivially for scalar-only uops)

Step 3: VTG micro-ops execute via GVIQ rotation
  u0 (VTG_VADD): GVIQ picks, executes, signals completion to BROB[5]
    engine_done_partial |= (u0 completed)
  u1 (VTG_VMUL): GVIQ picks, executes, signals completion to BROB[5]
    engine_done_partial |= (u1 completed)

Step 4: BROB completion check
  After both VTG ops complete:
    BROB[5].engine_done = 1
    complete = scalar_done && engine_done = 1 && 1 = 1
    BROB[5].state = COMPLETE

Step 5: Block retire
  If no exception: commit VTG writes (Group Write Adapter RMW for each VTG op)
  Advance BROB head
  Retire BSTOP
```
