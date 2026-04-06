### Tile Register File (TRegFile-4K)

The TRegFile-4K is an **8-read / 8-write tile register file** built from **64 physical 1R1W SRAM banks** at **1× core clock**. Storage is organized into **4 KB tiles**, each interleaved across all 64 banks (64 B per bank). An **8-cycle synchronized calendar** rotates port-to-group assignments so that every bank sees exactly **1R + 1W per cycle**. Each port accepts one `reg_idx` which is latched and drives the next **8-cycle epoch**; a new address is accepted every **8 cycles** (one per epoch boundary), enabling zero-bubble back-to-back tile accesses.

#### 1. Core Parameters

| Parameter | Value |
|-----------|-------|
| SRAM instance | **256 × 512 bits** (64 B wide, depth 256, 1R1W) |
| Banks | **64** (1 SRAM per bank), 8 groups × 8 banks |
| Total size | 64 × 16 KB = **1 MB** |
| Tile size / count | **4 KB** (4096 B) / **256** tiles (tile\_idx 0..255) |
| Read ports | **8** (R0–R7) — 512 B/cy each |
| Write ports | **8** (W0–W7) — 512 B/cy each |
| Calendar | **8 cycles**, synchronized; 1 new `reg_idx` / port / 8 cycles (epoch-aligned) |

#### 2. Tile Layout & Physical Organization

Each 4 KB tile is striped across all 64 banks. Bank select is pure wiring (zero decode logic):

```
  bank[5:0]    = chunk_offset[5:0]       ← pure wiring
  SRAM addr    = tile_idx[7:0]           ← 8 bits → 256 rows

  Bank groups (8 banks each):
    G0 = banks  0– 7    (c[5:3] = 000)
    G1 = banks  8–15    (c[5:3] = 001)
    G2 = banks 16–23    (c[5:3] = 010)
    G3 = banks 24–31    (c[5:3] = 011)
    G4 = banks 32–39    (c[5:3] = 100)
    G5 = banks 40–47    (c[5:3] = 101)
    G6 = banks 48–55    (c[5:3] = 110)
    G7 = banks 56–63    (c[5:3] = 111)

  1 bank  → 64 B   (one chunk)
  1 group → 512 B  (8 banks, one cycle per port)
  8 groups → 4 KB  (full tile, 8 cycles)
```

```
 ┌────────────────────────────────────────────────────────────────────────────────────────┐
 │  TRegFile-4K:  256 tiles × 4 KB = 1 MB                                               │
 │                                                                                       │
 │  ┌──────────────────────────┐  ┌──────────────────────────┐                           │
 │  │  Group G0 (banks 0–7)    │  │  Group G1 (banks 8–15)   │                           │
 │  │  ┌────┐┌────┐ ... ┌────┐ │  │  ┌────┐┌────┐ ... ┌────┐ │                           │
 │  │  │Bk0 ││Bk1 │     │Bk7 │ │  │  │Bk8 ││Bk9 │     │Bk15│ │                           │
 │  │  │64B ││64B │     │64B │ │  │  │64B ││64B │     │64B │ │                           │
 │  │  │×256││×256│     │×256│ │  │  │×256││×256│     │×256│ │                           │
 │  │  └────┘└────┘     └────┘ │  │  └────┘└────┘     └────┘ │                           │
 │  └──────────────────────────┘  └──────────────────────────┘                           │
 │  ┌──────────────────────────┐  ┌──────────────────────────┐                           │
 │  │  Group G2 (banks 16–23)  │  │  Group G3 (banks 24–31)  │                           │
 │  │  ┌────┐┌────┐ ... ┌────┐ │  │  ┌────┐┌────┐ ... ┌────┐ │                           │
 │  │  │Bk16││Bk17│     │Bk23│ │  │  │Bk24││Bk25│     │Bk31│ │                           │
 │  │  │64B ││64B │     │64B │ │  │  │64B ││64B │     │64B │ │                           │
 │  │  │×256││×256│     │×256│ │  │  │×256││×256│     │×256│ │                           │
 │  │  └────┘└────┘     └────┘ │  │  └────┘└────┘     └────┘ │                           │
 │  └──────────────────────────┘  └──────────────────────────┘                           │
 │  ┌──────────────────────────┐  ┌──────────────────────────┐                           │
 │  │  Group G4 (banks 32–39)  │  │  Group G5 (banks 40–47)  │                           │
 │  │  ┌────┐┌────┐ ... ┌────┐ │  │  ┌────┐┌────┐ ... ┌────┐ │                           │
 │  │  │Bk32││Bk33│     │Bk39│ │  │  │Bk40││Bk41│     │Bk47│ │                           │
 │  │  │64B ││64B │     │64B │ │  │  │64B ││64B │     │64B │ │                           │
 │  │  │×256││×256│     │×256│ │  │  │×256││×256│     │×256│ │                           │
 │  │  └────┘└────┘     └────┘ │  │  └────┘└────┘     └────┘ │                           │
 │  └──────────────────────────┘  └──────────────────────────┘                           │
 │  ┌──────────────────────────┐  ┌──────────────────────────┐                           │
 │  │  Group G6 (banks 48–55)  │  │  Group G7 (banks 56–63)  │                           │
 │  │  ┌────┐┌────┐ ... ┌────┐ │  │  ┌────┐┌────┐ ... ┌────┐ │                           │
 │  │  │Bk48││Bk49│     │Bk55│ │  │  │Bk56││Bk57│     │Bk63│ │                           │
 │  │  │64B ││64B │     │64B │ │  │  │64B ││64B │     │64B │ │                           │
 │  │  │×256││×256│     │×256│ │  │  │×256││×256│     │×256│ │                           │
 │  │  └────┘└────┘     └────┘ │  │  └────┘└────┘     └────┘ │                           │
 │  └──────────────────────────┘  └──────────────────────────┘                           │
 │                                                                                       │
 │  ════════════════════════════════════════════════════════════════════════════════════   │
 │    Rotating group mux: each port gets 1 group per cycle                               │
 │  ════════════════════════════════════════════════════════════════════════════════════   │
 │  ▼(8bk) ▼(8bk) ▼(8bk) ▼(8bk) ▼(8bk) ▼(8bk) ▼(8bk) ▼(8bk)                         │
 │  R0     R1     R2     R3     R4     R5     R6     R7                                  │
 │  512B   512B   512B   512B   512B   512B   512B   512B                                │
 │                                                                                       │
 │  ▲(8bk) ▲(8bk) ▲(8bk) ▲(8bk) ▲(8bk) ▲(8bk) ▲(8bk) ▲(8bk)                         │
 │  W0     W1     W2     W3     W4     W5     W6     W7                                  │
 │  512B   512B   512B   512B   512B   512B   512B   512B                                │
 └────────────────────────────────────────────────────────────────────────────────────────┘
```

#### 3. Port Interface

Each port presents **512 B per cycle** (one group of 8 banks × 64 B). A port accepts one `reg_idx[7:0]` which is **latched** internally at the epoch boundary. The latched address then drives data delivery (read) or acceptance (write) for the addressed tile over the next **8 consecutive cycles** — one bank-group per cycle per the calendar rotation. Since a 4 KB tile requires 8 × 512 B reads, the port is occupied for the full epoch and can only accept a **new `reg_idx` every 8 cycles**.

**Epoch-aligned address acceptance:** The port contains a **pending** address register and an **active** address register. A client can write a new `reg_idx` into the pending register at any time during the current epoch. At the next epoch boundary (`cy[2:0]=0`), pending promotes to active and the port begins serving the new tile with **zero bubble**:

```
  Port Rp — back-to-back tile reads (zero gap):

  Cycle:  0    1    2    3    4    5    6    7    8    9   10  ...  15   16  ...
  Addr:  [T0 latched at boundary]              [T1 latched at boundary]  [T2 ...]
  Data:  T0   T0   T0   T0   T0   T0   T0   T0   T1   T1   T1  ...  T1   T2  ...
         .G0  .G1  .G2  .G3  .G4  .G5  .G6  .G7  .G0  .G1  .G2      .G7  .G0
         └──── epoch 0 (tile T0) ────┘  └──── epoch 1 (tile T1) ────┘  └── ...
                                     ↑ zero bubble: T1 starts immediately
```

- T0 address is written to pending before epoch 0; it promotes to active at the boundary.
- T1 address can be written to pending at any point during epoch 0; it takes effect at cycle 8.
- **One new tile address per port every 8 cycles** — the port is fully occupied delivering 512 B/cy × 8 cy = 4 KB for the current tile.

| Ports | Direction | Data Width | Address | Addr Rate |
|-------|-----------|------------|---------|-----------|
| **R0–R7** | Read | 512 B (4096 bits) / cy | `reg_idx[7:0]` | 1 addr / 8 cy |
| **W0–W7** | Write | 512 B (4096 bits) / cy | `reg_idx[7:0]` + `w_en` | 1 addr / 8 cy |

**Per-port sustained throughput:** 1 tile (4 KB) every 8 cycles = 512 B/cy.
**Address registers:** 1 pending + 1 active (double-register for zero-bubble epoch chaining).

**Port microarchitecture (read port Rp):**

```
                 reg_idx[7:0]
                      │
                      ▼
              ┌───────────────┐
              │  Addr Latch   │◄── write any time during epoch
              │  (pending)    │
              └──────┬────────┘
                     │ epoch boundary: pending → active
                     ▼
              ┌───────────────┐     ┌──────────────────────────────┐
              │  Addr Active  │────▶│  Bank-Group Mux (calendar)   │
              │  (current)    │     │  cy[2:0] selects G(p+cy)%8   │
              └───────────────┘     └──────────┬───────────────────┘
                                               │ 8 banks × 64 B
                                               ▼
                                       ┌──────────────┐
                                       │  512 B / cy   │──▶ data out
                                       │  read data    │
                                       └──────────────┘

  Timing:
    Cycle c     : client writes reg_idx → Addr Latch (pending)
    Cycle c'    : next epoch boundary (cy[2:0]=0) → pending promotes to active
    Cycle c'..c'+7 : active addr drives 8 bank-group reads (OF0–OF7)
    Cycle c'..c'+7 : client may write next reg_idx → Addr Latch (next pending)
    Cycle c'+8  : next epoch boundary → new pending promotes to active
```

Write port Wp is identical except data flows inward (client → bank-group mux → SRAM write).

#### 4. 8-Cycle Synchronized Calendar

All 16 ports share a global 3-bit **epoch counter** (`cy[2:0]`). Read and write ports follow the **same** rotation pattern — port *p* (phase offset *p*) accesses group `(p + cy) % 8`:

| Cycle | Phase 0 (R0/W0) | Phase 1 (R1/W1) | Phase 2 (R2/W2) | Phase 3 (R3/W3) | Phase 4 (R4/W4) | Phase 5 (R5/W5) | Phase 6 (R6/W6) | Phase 7 (R7/W7) |
|-------|----------------|----------------|----------------|----------------|----------------|----------------|----------------|----------------|
| 0 | **G0** | **G1** | **G2** | **G3** | **G4** | **G5** | **G6** | **G7** |
| 1 | **G1** | **G2** | **G3** | **G4** | **G5** | **G6** | **G7** | **G0** |
| 2 | **G2** | **G3** | **G4** | **G5** | **G6** | **G7** | **G0** | **G1** |
| 3 | **G3** | **G4** | **G5** | **G6** | **G7** | **G0** | **G1** | **G2** |
| 4 | **G4** | **G5** | **G6** | **G7** | **G0** | **G1** | **G2** | **G3** |
| 5 | **G5** | **G6** | **G7** | **G0** | **G1** | **G2** | **G3** | **G4** |
| 6 | **G6** | **G7** | **G0** | **G1** | **G2** | **G3** | **G4** | **G5** |
| 7 | **G7** | **G0** | **G1** | **G2** | **G3** | **G4** | **G5** | **G6** |

Over 8 cycles each port visits all 8 groups exactly once → reads/writes one complete 4 KB tile.

**Epoch chaining (pipelined address):** The epoch counter is free-running and global. A port's active address drives all 8 cycles of the current epoch. At the next `cy[2:0]=0` boundary, the pending address (latched at any point during the previous epoch) automatically promotes to active. This produces **zero-bubble back-to-back tile accesses** — the port never idles between consecutive tiles:

```
  cy[2:0]: 0  1  2  3  4  5  6  7  0  1  2  3  4  5  6  7  0  1 ...
  Active:  ──── tile T0 ─────────  ──── tile T1 ─────────  ── T2 ...
  Pending:       [T1 latched]            [T2 latched]
                                  ↑                        ↑
                           T1 promotes                T2 promotes
```

**Conflict-free proof:** At every cycle, the 8 read ports cover {G0..G7} and the 8 write ports independently cover {G0..G7}. Each group sees exactly 1R + 1W. The reader and writer assigned to the same group are always the **same-phase** pair (R0/W0, R1/W1, ..., R7/W7).

```
  Cy 0: R = G0(R0) G1(R1) G2(R2) G3(R3) G4(R4) G5(R5) G6(R6) G7(R7)
         W = G0(W0) G1(W1) G2(W2) G3(W3) G4(W4) G5(W5) G6(W6) G7(W7)
  Cy 1: R = G1(R0) G2(R1) G3(R2) G4(R3) G5(R4) G6(R5) G7(R6) G0(R7)
         W = G1(W0) G2(W1) G3(W2) G4(W3) G5(W4) G6(W5) G7(W6) G0(W7)
  ...
  Cy 7: R = G7(R0) G0(R1) G1(R2) G2(R3) G3(R4) G4(R5) G5(R6) G6(R7)
         W = G7(W0) G0(W1) G1(W2) G2(W3) G3(W4) G4(W5) G5(W6) G6(W7)

  Per bank: ≤ 1R + 1W per cycle.  Two-port SRAM satisfied.  ✓
```

#### 5. Throughput

| Metric | Value |
|--------|-------|
| Per port data BW | 8 banks × 64 B = **512 B/cy** |
| Per port per epoch (8 cy) | 8 groups × 512 B = **4 KB** (1 tile) |
| Addr acceptance rate | **1 `reg_idx` / port / 8 cycles** (epoch-aligned) |
| Addr-to-data latency | 0–7 cy (depends on when within epoch the pending addr is written) |
| Sustained tile rate | 1 tile / 8 cy / port (zero-bubble epoch chaining) |
| Aggregate read BW | 8 ports × 512 B/cy = **4 KB/cy** |
| Aggregate write BW | 8 ports × 512 B/cy = **4 KB/cy** |
| Total per epoch | **16 tile ops** (8R + 8W), zero bank conflicts |

#### 6. Write-to-Read Bypass & Scheduling Constraint

**Same-phase bypass (hardware, zero-latency):**

The calendar guarantees that each group's reader and writer in any given cycle are always a same-phase port pair. When a same-phase read and write target the same `tile_idx`, SRAM write latency (1 cycle) would return stale data. A combinational bypass mux forwards the write data directly to the read output.

**Cross-phase RAW hazard (not resolved in hardware):**

For different-phase port pairs (e.g. R0 reading tile T while W1 writes tile T in the same epoch), the phase offset causes one or more groups per pair to be **read before they are written**. The write data does not exist at the time of the read, so no combinational bypass can resolve it.

Example — R0 (phase 0) and W1 (phase 1) on the same tile:

```
  Group  │  R0 reads  │  W1 writes  │  Result
  ───────┼────────────┼─────────────┼──────────────────────────
  G0     │  cycle 0   │  cycle 7    │  Read 7 cy before write → STALE ✗
  G1     │  cycle 1   │  cycle 0    │  Write 1 cy before read → SRAM OK ✓
  G2     │  cycle 2   │  cycle 1    │  Write 1 cy before read → SRAM OK ✓
  G3     │  cycle 3   │  cycle 2    │  Write 1 cy before read → SRAM OK ✓
  G4     │  cycle 4   │  cycle 3    │  Write 1 cy before read → SRAM OK ✓
  G5     │  cycle 5   │  cycle 4    │  Write 1 cy before read → SRAM OK ✓
  G6     │  cycle 6   │  cycle 5    │  Write 1 cy before read → SRAM OK ✓
  G7     │  cycle 7   │  cycle 6    │  Write 1 cy before read → SRAM OK ✓
```

**Scheduling rule (enforced by upstream scheduler):**

> Within the same 8-cycle epoch, no two different-phase read/write ports shall operate on the same `tile_idx`. Same-phase pairs (R0/W0, R1/W1, ..., R7/W7) are always safe and fully bypassed. Cross-phase pairs on the same tile must be separated by at least one full epoch (8 cycles).
