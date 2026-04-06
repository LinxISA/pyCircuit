### Tile Register File (TRegFile)

The TRegFile is a **4-read / 4-write tile register file** built from **32 physical 1R1W SRAM banks** at **1× core clock**. Storage is organized into **2 KB tiles**, each interleaved across all 32 banks (64 B per bank). A **4-cycle synchronized calendar** rotates port-to-group assignments so that every bank sees exactly **1R + 1W per cycle**.

#### 1. Core Parameters

| Parameter | Value |
|-----------|-------|
| SRAM instance | **512 × 512 bits** (64 B wide, depth 512, 1R1W) |
| Banks | **32** (1 SRAM per bank), 4 groups × 8 banks |
| Total size | 32 × 32 KB = **1 MB** |
| Tile size / count | **2 KB** (2048 B) / **512** tiles (tile\_idx 0..511) |
| Read ports | **4** (RA, RB, RC, RD) — 512 B/cy each |
| Write ports | **4** (WA, WB, WC, WD) — 512 B/cy each |
| Calendar | **4 cycles**, synchronized; new tile\_idx every 4 cycles per port |

#### 2. Tile Layout & Physical Organization

Each 2 KB tile is striped across all 32 banks. Bank select is pure wiring (zero decode logic):

```
  bank[4:0]    = chunk_offset[4:0]       ← pure wiring
  SRAM addr    = tile_idx[8:0]           ← 9 bits → 512 rows

  Bank groups (8 banks each):
    G0 = banks  0– 7    (c[4:3] = 00)
    G1 = banks  8–15    (c[4:3] = 01)
    G2 = banks 16–23    (c[4:3] = 10)
    G3 = banks 24–31    (c[4:3] = 11)

  1 bank  → 64 B   (one chunk)
  1 group → 512 B  (8 banks, one cycle per port)
  4 groups → 2 KB  (full tile, 4 cycles)
```

```
 ┌──────────────────────────────────────────────────────────────────────────────────┐
 │  TRegFile:  512 tiles × 2 KB = 1 MB                                            │
 │                                                                                 │
 │  ┌──────────────────────────┐  ┌──────────────────────────┐                     │
 │  │  Group G0 (banks 0–7)    │  │  Group G1 (banks 8–15)   │                     │
 │  │  ┌────┐┌────┐ ... ┌────┐ │  │  ┌────┐┌────┐ ... ┌────┐ │                     │
 │  │  │Bk0 ││Bk1 │     │Bk7 │ │  │  │Bk8 ││Bk9 │     │Bk15│ │                     │
 │  │  │64B ││64B │     │64B │ │  │  │64B ││64B │     │64B │ │                     │
 │  │  │×512││×512│     │×512│ │  │  │×512││×512│     │×512│ │                     │
 │  │  └────┘└────┘     └────┘ │  │  └────┘└────┘     └────┘ │                     │
 │  └──────────────────────────┘  └──────────────────────────┘                     │
 │  ┌──────────────────────────┐  ┌──────────────────────────┐                     │
 │  │  Group G2 (banks 16–23)  │  │  Group G3 (banks 24–31)  │                     │
 │  │  ┌────┐┌────┐ ... ┌────┐ │  │  ┌────┐┌────┐ ... ┌────┐ │                     │
 │  │  │Bk16││Bk17│     │Bk23│ │  │  │Bk24││Bk25│     │Bk31│ │                     │
 │  │  │64B ││64B │     │64B │ │  │  │64B ││64B │     │64B │ │                     │
 │  │  │×512││×512│     │×512│ │  │  │×512││×512│     │×512│ │                     │
 │  │  └────┘└────┘     └────┘ │  │  └────┘└────┘     └────┘ │                     │
 │  └──────────────────────────┘  └──────────────────────────┘                     │
 │                                                                                 │
 │  ══════════════════════════════════════════════════════════════                  │
 │    Rotating group mux: each port gets 1 group per cycle                         │
 │  ══════════════════════════════════════════════════════════════                  │
 │  ▼(8bk)    ▼(8bk)    ▼(8bk)    ▼(8bk)    ▲(8bk)    ▲(8bk)    ▲(8bk)    ▲(8bk)│
 │  RA        RB        RC        RD        WA        WB        WC        WD      │
 │  512B/cy   512B/cy   512B/cy   512B/cy   512B/cy   512B/cy   512B/cy   512B/cy │
 └──────────────────────────────────────────────────────────────────────────────────┘
```

#### 3. Port Interface

Each port presents **512 B per cycle** (one group of 8 banks × 64 B). A port latches its `tile_idx[8:0]` on cycle 0 of the epoch and accesses one group per cycle for 4 consecutive cycles, completing the full 2 KB tile.

| Ports | Direction | Data Width | Address |
|-------|-----------|------------|---------|
| **RA, RB, RC, RD** | Read | 512 B (4096 bits) / cy | tile\_idx[8:0] |
| **WA, WB, WC, WD** | Write | 512 B (4096 bits) / cy | tile\_idx[8:0] + w\_en |

#### 4. 4-Cycle Synchronized Calendar

All 8 ports share a global 2-bit **epoch counter** (`cy[1:0]`). Read and write ports follow the **same** rotation pattern — port *p* (phase offset *p*) accesses group `(p + cy) % 4`:

| Cycle | Port phase 0 (RA / WA) | Phase 1 (RB / WB) | Phase 2 (RC / WC) | Phase 3 (RD / WD) |
|-------|------------------------|--------------------|--------------------|---------------------|
| 0 | **G0** | **G1** | **G2** | **G3** |
| 1 | **G1** | **G2** | **G3** | **G0** |
| 2 | **G2** | **G3** | **G0** | **G1** |
| 3 | **G3** | **G0** | **G1** | **G2** |

Over 4 cycles each port visits all 4 groups exactly once → reads/writes one complete 2 KB tile.

**Conflict-free proof:** At every cycle, the 4 read ports cover {G0, G1, G2, G3} and the 4 write ports independently cover {G0, G1, G2, G3}. Each group sees exactly 1R + 1W. The reader and writer assigned to the same group are always the **same-phase** pair (RA/WA, RB/WB, RC/WC, RD/WD).

```
  Cy 0: R = G0(RA) + G1(RB) + G2(RC) + G3(RD)    W = G0(WA) + G1(WB) + G2(WC) + G3(WD)
  Cy 1: R = G1(RA) + G2(RB) + G3(RC) + G0(RD)    W = G1(WA) + G2(WB) + G3(WC) + G0(WD)
  Cy 2: R = G2(RA) + G3(RB) + G0(RC) + G1(RD)    W = G2(WA) + G3(WB) + G0(WC) + G1(WD)
  Cy 3: R = G3(RA) + G0(RB) + G1(RC) + G2(RD)    W = G3(WA) + G0(WB) + G1(WC) + G2(WD)

  Per bank: ≤ 1R + 1W per cycle.  Two-port SRAM satisfied.  ✓
```

#### 5. Throughput

| Metric | Value |
|--------|-------|
| Per port per cycle | 8 banks × 64 B = **512 B** |
| Per port per epoch (4 cy) | 4 groups × 512 B = **2 KB** (1 tile) |
| Aggregate read BW | 4 × 512 B/cy = **2 KB/cy** |
| Aggregate write BW | 4 × 512 B/cy = **2 KB/cy** |
| Total per epoch | **8 tile ops** (4R + 4W), zero bank conflicts |

#### 6. Write-to-Read Bypass & Scheduling Constraint

**Same-phase bypass (hardware, zero-latency):**

The calendar guarantees that each group's reader and writer in any given cycle are always a same-phase port pair. When a same-phase read and write target the same `tile_idx`, SRAM write latency (1 cycle) would return stale data. A combinational bypass mux forwards the write data directly to the read output.

**Cross-phase RAW hazard (not resolved in hardware):**

For different-phase port pairs (e.g. RA reading tile T while WB writes tile T in the same epoch), the phase offset causes one group per pair to be **read before it is written**. The write data does not exist at the time of the read, so no combinational bypass can resolve it.

Example — RA (phase 0) and WB (phase 1) on the same tile:

```
  Group  │  RA reads  │  WB writes  │  Result
  ───────┼────────────┼─────────────┼──────────────────────────
  G0     │  epoch 0   │  epoch 3    │  Read 3 cy before write → STALE ✗
  G1     │  epoch 1   │  epoch 0    │  Write 1 cy before read → SRAM OK ✓
  G2     │  epoch 2   │  epoch 1    │  Write 1 cy before read → SRAM OK ✓
  G3     │  epoch 3   │  epoch 2    │  Write 1 cy before read → SRAM OK ✓
```

**Scheduling rule (enforced by upstream scheduler):**

> Within the same 4-cycle epoch, no two different-phase read/write ports shall operate on the same `tile_idx`. Same-phase pairs (RA/WA, RB/WB, RC/WC, RD/WD) are always safe and fully bypassed. Cross-phase pairs on the same tile must be separated by at least one full epoch (4 cycles).
