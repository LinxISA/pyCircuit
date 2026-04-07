# outerCube Design

## 1. Overview & Key Features

The outerCube Matrix Unit (MXU) is a large-scale **outer-product accumulation engine** containing **4096 base 16-bit MAC units** organized into **8 banks** of 8×64. Sub-word partitioning scales throughput to 8,192 (FP8) and 32,768 (MXFP4/HiFP4) MAC operations per cycle. All formats accumulate into unified **32-bit FP32** accumulators with **ping-pong double buffering**.

**Key features:**

- **Multi-format**: FP16, BF16, FP8 (E4M3/E5M2), MXFP4, HiFP4 — MAC scaling 1 : 2 : 8
- **Dual-mode**: Mode A (K-parallel, 8-bank reduction) / Mode B (M-parallel, independent banks)
- **Unified 32-bit accumulator** (FP32) for all input formats
- **Reduced-precision adder tree** — up to 62% narrower than FP32 (see §3.2)
- **Ping-pong accumulator** — stall-free overlapped drain and compute
- **Tile-level ISA** — `CUBE.OPA` consumes tile registers directly; hardware manages K-loop
- **Optional structured sparsity** (2:4 or 4:8) for FP8 and MXFP4 modes

### 1.1 Key Parameters

| Parameter | Value |
|-----------|-------|
| Base MAC unit | 16-bit × 16-bit → 32-bit product |
| Banks / Array per bank | 8 / 8 rows × 64 columns |
| Total base MACs | 8 × 8 × 64 = **4,096** (16-bit) |
| Effective MACs/cycle | FP16: 4,096 / FP8: 8,192 / MXFP4: 32,768 (all @ 100%) |
| Accumulator | 32-bit FP32, ping-pong: 2 × 16 KB = **32 KB** |
| Output tile | Mode A: 8×64 / Mode B: 64×64 (8 independent 8×64) |
| Clock target | ≥ 1.5 GHz (5 nm) |
| Peak FP16 throughput | 4096 × 2 × 1.5 G = **12.3 TFLOPS** |
| Peak FP8 throughput | 8192 × 2 × 1.5 G = **24.6 TOPS** |
| Peak MXFP4 throughput | 32768 × 2 × 1.5 G = **98.3 TOPS** (all formats @ 100% with TRegFile-4K) |

### 1.2 Comparison with ARM SME

| Parameter | ARM SME (SVL=512b) | outerCube MXU |
|-----------|-------------------|---------------|
| Base MAC units | 256 (FP16) | **4096** (FP16) |
| FP8 MACs/cycle | 1024 | **8192** |
| FP4 MACs/cycle | N/A | **32768** |
| Accumulator | Mixed (type-dependent) | **Always 32-bit (FP32)** |
| Banks | 1 | **8** |
| Output tile (FP16) | 16×16 | **8×64** (Mode A) or **64×64** (Mode B) |
| Formats | FP64–INT8 | **FP16, BF16, FP8, MXFP4, HiFP4** |

### 1.3 Top-Level Block Diagram

All data flows through the **TRegFile-4K** (8R + 8W ports, each 512 B/cycle, 4 KB tiles, 8-cycle epoch). The CUBE uses up to **5 read ports** (1 for A, 4 for B). See `tregfile4k.md` for TRegFile design and §5.1 for port mapping.

```
 ┌───────────────────────────┐       ┌─── Control ───┐
 │  TRegFile-4K (1 MB)        │       │ mode, format,  │
 │  8R + 8W @ 512 B/cy       │       │ tile_cmd       │
 │  4 KB tiles, 8-cy epoch   │       └───────┬───────┘
 │                             │               │
 │  R0 ──── A (512B/cy) ────▶│─┐   ┌─────────┴────────────────────────────┐
 │                             │ │   │  A Staging Reg (4 KB)                │
 │  R1 ──┐                    │ │   │    filled 1 epoch, reused 2 epochs   │
 │  R2 ──┤                    │ │   │              ╲                        │
 │  R3 ──┼─ B (2 KB/cy) ────▶│─┼──▶│  B Double-Buf (2 × 16 KB = 32 KB)   │
 │  R4 ──┘                    │ │   │    4 tiles/epoch, ping-pong           │
 │                             │ │   │              ╲                        │
 │  R5,R6,R7: TILE.ST / free │ │   │  ┌──────────────────────────────┐    │
 │                             │ │   │  │    8 × Bank [8×64 MACs]      │    │
 │                             │ │   │  │  Bank 0  Bank 1 ... Bank 7   │    │
 │                             │ │   │  └─────┬──────┬──────────┬──────┘    │
 │                             │ │   │        ▼      ▼          ▼          │
 │                             │ │   │  ┌──────────────────────────────┐    │
 │                             │ │   │  │ Inter-Bank Adder Tree         │    │
 │                             │ │   │  │ (Mode A: 8→1; Mode B: bypass)│    │
 │                             │ │   │  └────────────┬─────────────────┘    │
 │                             │ │   │               ▼                      │
 │                             │ │   │  ┌──────────────────────────────┐    │
 │                             │ │   │  │ Accumulator SRAM (Ping-Pong) │    │
 │                             │ │   │  │ 32 KB total (2 × 16 KB)      │    │
 │                             │ │   │  └────────────┬─────────────────┘    │
 │  W0 ◀── C (512B/cy) ──────│◀┘   │  Drain Controller                    │
 │  W1–W7: TILE.LD / free    │     └──────────────────────────────────────┘
 └───────────────────────────┘
```

---

## 2. Data Formats & MAC Scaling

### 2.1 Supported Formats

| Format | Element bits | Sign | Exponent | Mantissa | Shared exp | Notes |
|--------|-------------|------|----------|----------|------------|-------|
| **FP16** | 16 | 1 | 5 | 10 | — | IEEE 754 half |
| **BF16** | 16 | 1 | 8 | 7 | — | Brain float |
| **FP8 (E4M3)** | 8 | 1 | 4 | 3 | — | OCP FP8 |
| **FP8 (E5M2)** | 8 | 1 | 5 | 2 | — | OCP FP8 |
| **MXFP4** | 4 (+8 shared) | 1 | 2 | 1 | 8-bit per 32-elem block | OCP MX format |
| **HiFP4** | 4 | 1 | 1 | 2 | — | High-mantissa FP4 (no shared exponent) |

### 2.2 Sub-Word Partitioned MAC (1 : 2 : 8)

Each physical 16-bit MAC unit is a 16×16 → 32-bit multiplier with accumulator. For narrower formats the multiplier datapath is sub-word partitioned:

```
 Physical MAC unit (16-bit base):
 ┌────────────────────────────────────────────────────────────────┐
 │                                                                │
 │  16-bit mode (FP16/BF16):                                      │
 │    A[15:0] × B[15:0] → P[31:0]                                │
 │    1 product per cycle → accumulate into ACC[31:0]             │
 │                                                                │
 │  8-bit mode (FP8):                                             │
 │    A_hi[15:8] × B_hi[15:8] → P0[15:0]  ┐                     │
 │    A_lo[ 7:0] × B_lo[ 7:0] → P1[15:0]  ┤→ P0+P1 → ACC[31:0] │
 │    2 products per cycle (2-element dot product)                │
 │                                                                │
 │  4-bit mode (MXFP4/HiFP4):                                    │
 │    Partition 16b into 4 × 4b sub-words:                        │
 │      P0 = A[15:12]×B[15:12]                                   │
 │      P1 = A[11: 8]×B[11: 8]                                   │
 │      P2 = A[ 7: 4]×B[ 7: 4]                                   │
 │      P3 = A[ 3: 0]×B[ 3: 0]                                   │
 │    Each 8-bit half → 4 products; 2 halves → 8 products        │
 │    8 products per cycle → reduce via adder tree → ACC[31:0]   │
 │                                                                │
 └────────────────────────────────────────────────────────────────┘
```

### 2.3 K Consumption per Cycle

The 1:2:8 ratio translates to K-dimension throughput:

| Format | Products/MAC/cycle | K consumed/MAC/cycle | Total K/cycle Mode A (8 banks) | Total K/cycle Mode B |
|--------|-------------------|---------------------|-------------------------------|---------------------|
| FP16 | 1 | 1 | 8 | 1 |
| BF16 | 1 | 1 | 8 | 1 |
| FP8 | 2 | 2 | 16 | 2 |
| MXFP4 | 8 | 8 | 64 | 8 |
| HiFP4 | 8 | 8 | 64 | 8 |

### 2.4 Equivalent Dimensions by Format

| Format | Elem bits | MACs/MAC/cyc | Effective MACs/cycle | K/step (Mode A) | Output tile (A) | Output tile (B) |
|--------|----------|-------------|---------------------|----------------|----------------|----------------|
| FP16 | 16b | 1 | 4,096 | 8 | 8×64 | 64×64 |
| BF16 | 16b | 1 | 4,096 | 8 | 8×64 | 64×64 |
| FP8 | 8b | 2 | 8,192 | 16 | 8×64 | 64×64 |
| MXFP4 | 4b | 8 | 32,768 | 64 | 8×64 | 64×64 |
| HiFP4 | 4b | 8 | 32,768 | 64 | 8×64 | 64×64 |

---

## 3. Architecture

### 3.1 Multi-Format MAC Unit (Base Cell)

Each physical MAC unit (4096 total) contains a sub-word partitioned multiplier, a format-dependent intra-MAC adder tree, and an FP32 accumulator:

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Base MAC Unit (1 per array position, 4096 total)                       │
│                                                                         │
│  ┌─────────────────────────────────────────────────────┐               │
│  │  Sub-Word Partitioned Multiplier                     │               │
│  │                                                     │               │
│  │  A_in[15:0] ──┐     ┌─── FP16 mode: 1 × (16×16)   │               │
│  │                ├─MUX─┤    FP8 mode:  2 × (8×8)     │               │
│  │  B_in[15:0] ──┘     └─── FP4 mode:  8 × (4×4)     │               │
│  │                                                     │               │
│  │  Products:  FP16: P[31:0]                            │               │
│  │             FP8:  P0[15:0], P1[15:0]                │               │
│  │             FP4:  p0..p7 [7:0] each                  │               │
│  └─────────────────────────┬───────────────────────────┘               │
│                            ▼                                            │
│  ┌─────────────────────────────────────────────────────┐               │
│  │  Intra-MAC Adder Tree (format-dependent reduction)   │               │
│  │                                                     │               │
│  │  FP16: pass-through (1 product → accumulator)        │               │
│  │  FP8:  2-input adder (P0+P1 → partial sum)          │               │
│  │  FP4:  8-input tree (p0+..+p7 → partial sum)        │               │
│  │                                                     │               │
│  │  ★ Reduced precision: see §3.2                       │               │
│  │                                                     │               │
│  │  Output: partial_sum (reduced-width FP)              │               │
│  └─────────────────────────┬───────────────────────────┘               │
│                            ▼                                            │
│  ┌─────────────────────────────────────────────────────┐               │
│  │  FP32 Accumulator                                    │               │
│  │  ACC[31:0] += widen(partial_sum)                     │               │
│  │  (final addition at full 32-bit precision)           │               │
│  └─────────────────────────────────────────────────────┘               │
└─────────────────────────────────────────────────────────────────────────┘
```

### 3.2 Adder Tree Precision Optimization

The intra-MAC adder tree does **not** need full 32-bit precision. Only the final accumulation into the FP32 register uses 32 bits.

**Multiplier product precision:**

| Format | Mantissa (each input) | Product mantissa | Product exponent range |
|--------|----------------------|-----------------|----------------------|
| FP16 (S.E5.M10) | 11 bits (1+10) | 22 bits | [-30, +30] → 6 bits |
| BF16 (S.E8.M7) | 8 bits (1+7) | 16 bits | [-254, +254] → 9 bits |
| FP8 (S.E4.M3) | 4 bits (1+3) | 8 bits | [-14, +14] → 5 bits |
| MXFP4 (S.E2.M1+shared) | 2 bits (1+1) | 4 bits | shared_exp ± 6 → 4 bits |
| HiFP4 (S.E1.M2) | 3 bits (1+2) | 6 bits | [-1, +1] → 2 bits |

**Intra-MAC adder tree internal width:**

| Format | Products to reduce | Tree depth | Internal mantissa | Internal exponent | **Total internal width** | vs FP32 savings |
|--------|-------------------|-----------|-------------------|-------------------|------------------------|----------------|
| FP16 | 1 (no tree) | 0 | 22 | 6 | **28 bits** | 12.5% |
| BF16 | 1 (no tree) | 0 | 16 | 9 | **25 bits** | 22% |
| FP8 | 2 | 1 | 9 | 5 | **15 bits** | 53% |
| MXFP4 | 8 | 3 | 7 | 4+8=12 | **20 bits** | 37% |
| HiFP4 | 8 | 3 | 9 | 2 | **12 bits** | 62% |

**Design principle:** Multiplier produces full-precision product → intra-MAC adder tree uses reduced mantissa + exponent → tree output widened to FP32 only at final accumulation (zero-padding LSBs, no precision loss).

### 3.3 Inter-Bank Adder Tree (Mode A)

In Mode A, 8 banks each produce a partial product for the same C[i][j] position. An 8→1 adder tree (3 stages, +1 pipeline latency) reduces them before accumulation:

```
 Bank 0 partial ──┐
 Bank 1 partial ──┤
 Bank 2 partial ──┤    ┌───────────────────────┐
 Bank 3 partial ──┼───▶│  8→1 Adder Tree       │──▶ ACC[i][j]
 Bank 4 partial ──┤    │  (per C position)      │    (FP32)
 Bank 5 partial ──┤    │                        │
 Bank 6 partial ──┤    │  Internal width:        │
 Bank 7 partial ──┘    │  = intra-MAC width + 3  │
                        │  (log₂(8) = 3 extra    │
                        │   mantissa bits)         │
                        └───────────────────────┘

 Mode B: bypassed — each bank → own ACC (M-parallel)
```

**Combined adder tree width (intra-MAC + inter-bank, Mode A):**

| Format | Intra-MAC products | Inter-bank | Total reductions | Final tree mantissa | Final tree total |
|--------|-------------------|-----------|-----------------|--------------------|-----------------|
| FP16 | 1 | 8 | 8 | 22+3 = 25 | **31 bits** |
| FP8 | 2 | 8 | 16 | 9+4 = 13 | **18 bits** |
| MXFP4 | 8 | 8 | 64 | 7+6 = 13 | **25 bits** |
| HiFP4 | 8 | 8 | 64 | 9+6 = 15 | **17 bits** |

> Even the widest case (FP16 Mode A) is 31 bits — still narrower than FP32.

### 3.4 Accumulator & Ping-Pong

```
 ┌──────────────────────────────────────────────────────────────────┐
 │  Accumulator RAM (per bank)                                       │
 │                                                                  │
 │  Mode A: 8 banks share ONE accumulator (after adder tree)        │
 │    Size: 8 × 64 × 32b = 2 KB (PING) + 2 KB (PONG) = 4 KB       │
 │                                                                  │
 │  Mode B: each bank has its OWN accumulator (M-parallel)          │
 │    Size: 8 banks × 8 × 64 × 32b = 16 KB (PING) + 16 KB (PONG)  │
 │    Total: 32 KB                                                  │
 │                                                                  │
 │  Physical SRAM: 32 KB (sized for Mode B worst case)               │
 │  Mode A uses only 4 KB of the 32 KB (rest power-gated)           │
 └──────────────────────────────────────────────────────────────────┘
```

All accumulators are 32-bit (FP32) regardless of input format.

**Drain latency (via W0, 8 cycles per rotation):**

| Mode | Drain size | Drain cycles | Stall-free condition (FP16) |
|------|-----------|-------------|----------------------------|
| A | 2 KB | **8** (1 rotation) | K ≥ 32 |
| B | 16 KB | **32** (4 rotations) | K ≥ 32 |

**Ping-pong (stateless):** The accumulator has two buffers (z0, z1). Software explicitly selects which buffer to use in every `CUBE.ZERO`, `CUBE.OPA`, `CUBE.DRAIN`, and `CUBE.WAIT` instruction via the `zd` field. There is no hidden ping/pong state. While `CUBE.DRAIN z0` drains buffer 0 via W0, software can issue `CUBE.ZERO z1` + `CUBE.OPA z1, ...` to accumulate the next tile into buffer 1, and vice versa.

### 3.5 Hardware Sharing Across Formats

```
 ┌──────────────────────────────────────────────────────────────────────┐
 │  Shared Resources (per MAC unit)                                      │
 │                                                                      │
 │  ┌──────────────────┐   Reused across all formats:                    │
 │  │  16-bit Multiplier│   - Datapath wires (16b A, 16b B inputs)       │
 │  │  (sub-word part.) │   - Carry-save adder array (forms products)    │
 │  └────────┬─────────┘   - Only the partition controls change          │
 │           ▼                                                          │
 │  ┌──────────────────┐   Reused:                                       │
 │  │  Adder Tree       │   - Same adder tree structure, width varies    │
 │  │  (depth 0/1/3)    │   - FP16: depth 0 (pass-through)              │
 │  │                   │   - FP8:  depth 1 (2-input)                    │
 │  │                   │   - FP4:  depth 3 (8-input)                    │
 │  └────────┬─────────┘   - Upper stages power-gated when unused        │
 │           ▼                                                          │
 │  ┌──────────────────┐   Reused:                                       │
 │  │  FP32 Accumulator │   - Same 32-bit register for all formats       │
 │  │  (32-bit)         │   - Input widening MUX selects tree output     │
 │  └──────────────────┘   - Single FP32 adder for accumulation          │
 │                                                                      │
 │  Format-specific logic (small):                                       │
 │  - Exponent handling: bias subtraction, shared-exponent injection     │
 │  - Mantissa alignment: right-shift for FP addition                    │
 │  - Sub-word partition control: MUXes for 16b/8b/4b boundaries         │
 └──────────────────────────────────────────────────────────────────────┘
```

---

## 4. Dual-Mode Operation

### 4.1 Mode A: K-Parallel

Each bank receives a **different K-group** of A and B. The inter-bank adder tree reduces 8 partial products per C position into a shared accumulator.

```
  C[M][N] += A[M][K] × B[K][N]

  Output tile: 8 × 64  (M_tile=8, N_tile=64)
  K consumed per step: K_step = 8 × K_mac
    (FP16: 8, FP8: 16, MXFP4: 64)

  Bank 0: A[i0:i0+8, k+0·K_mac : k+1·K_mac] ⊗ B[k+0·K_mac : k+1·K_mac, j0:j0+64]
  Bank 1: A[i0:i0+8, k+1·K_mac : k+2·K_mac] ⊗ B[k+1·K_mac : k+2·K_mac, j0:j0+64]
    ...
  Bank 7: A[i0:i0+8, k+7·K_mac : k+8·K_mac] ⊗ B[k+7·K_mac : k+8·K_mac, j0:j0+64]
           ↓
       Inter-bank adder tree → ACC[8×64] += Σ of 8 partial products

  A via R0     (FP16): 8 rows × 8 banks × 1 × 2B = 128 B/step  (≤ 512 B port)
  B via R1–R4  (FP16): 8 banks × 64 cols × 1 × 2B = 1024 B/step (4 × 512 B ports)
```

### 4.2 Mode B: M-Parallel (Independent Banks)

Each bank processes a **different M-block** (different 8 rows of A). All banks use the **same B data** (each bank reads B independently — no broadcast MUX). The adder tree is bypassed; each bank accumulates into its own ACC.

```
  Output tile: 64 × 64  (8 banks × 8 rows × 64 cols)
  K consumed per step: K_step = K_mac
    (FP16: 1, FP8: 2, MXFP4: 8)

  Bank 0: A[i0+0  : i0+8,  k:k+K_mac] ⊗ B[k:k+K_mac, j0:j0+64]
  Bank 1: A[i0+8  : i0+16, k:k+K_mac] ⊗ B[k:k+K_mac, j0:j0+64]   ← same B data
    ...
  Bank 7: A[i0+56 : i0+64, k:k+K_mac] ⊗ B[k:k+K_mac, j0:j0+64]
           ↓
       Each bank → own ACC_b[8×64] (adder tree bypassed)

  A via R0     (FP16): 8 banks × 8 rows × 1 × 2B = 128 B/step  (≤ 512 B port)
  B via R1–R4  (FP16): 8 banks × 64 cols × 1 × 2B = 1024 B/step (same as Mode A)
```

### 4.3 Comparison & Mode Selection

| Property | Mode A | Mode B (M-parallel) |
|----------|--------|---------------------|
| A distribution | 8 different K-groups | **8 different M-blocks** (8 rows each) |
| B distribution | 8 banks same N range | **same B data**, each bank reads independently |
| Output tile | 8 × 64 | **64 × 64** (8 independent 8×64) |
| K per step (FP16) | **8** | 1 |
| K per step (FP8) | **16** | 2 |
| K per step (MXFP4) | **64** | 8 |
| Inter-bank adder tree | active (8→1) | bypassed |
| Accumulator usage | 2 KB (shared) | **16 KB** (8 × 2 KB independent) |
| A bandwidth (via R0) | 128 B/step (FP16) | 128 B/step (same) |
| B bandwidth (via R1–R4) | **1024 B/step** (FP16) | **1024 B/step** (same, no broadcast) |

**Mode selection guidelines:**

- **Mode A preferred** when K is large and aligned to K\_step (transformer decode, deep CNN layers, pointwise conv). ⌈M/8⌉ ceiling is small for typical M.
- **Mode B preferred** when M is large (≥ 64) and K is small / non-aligned (CNN early layers). Processes K at K\_mac granularity, eliminating K-ceiling waste.

| Scenario | Mode A K-waste | Mode B advantage | Condition for B win |
|----------|---------------|-----------------|---------------------|
| K = 9, FP16 | ⌈9/8⌉=2, 43.8% waste | 0% waste, 1.78× faster | M ≥ 64 |
| K = 27 | ⌈27/8⌉=4, 15.6% waste | 0% waste, 1.18× faster | M ≥ 64 |
| K = 1 | ⌈1/8⌉=1, 87.5% waste | 0% waste, 8× faster | M ≥ 64 |
| K = 128 (aligned) | 0% waste | no advantage | Mode A preferred |

Mode B is **not suited for small M**: ⌈M/64⌉ ceiling wastes M-rows (e.g., M=4 → 6.25% utilization). For transformer decode (M = 4–256, K aligned), **Mode A is always preferred**.

### 4.4 Hardware Cost of Dual-Mode

```
  Adder Tree Bypass:   512 MUXes (select tree output vs bank output)

  Accumulator SRAM:    Mode A needs 2 KB, Mode B needs 16 KB
                       Physical: 16 KB × 2 (ping-pong) = 32 KB
                       Mode A uses only 4 KB (rest power-gated)

  B path (R1–R4):    Same bandwidth as Mode A (no broadcast MUX)
                       Each bank reads B independently via ganged TRegFile ports
```

### 4.5 Design Decision: No B Broadcast MUX

**结论：Mode B 不采用 B broadcast MUX。**

曾考虑增加 broadcast MUX（约 8 Kbit），将单份 B 数据复制到 8 个 bank，降低 B 端口带宽需求。经分析：

```
  B Broadcast 对 Mode B 计算效率的影响 (TRegFile-4K, 4 B-read ports)
  ──────────────────────────────────────────────────────────────────

  1. FP16 / BF16 / FP8:
     无 broadcast:  B 需求 = 1024 B/step, supply = 2048 B/cy → 1.0 步/cy
     有 broadcast:  B 需求 = 128 B/step → 1.0 步/cy（相同）
     → 收益 = 0%

  2. MXFP4 / HiFP4:
     无 broadcast:  B 需求 = 2048 B/step, supply = 2048 B/cy (4 ports)
                   → 1.0 步/cy（100% 吞吐）
     有 broadcast:  B 需求 = 256 B/step → 1.0 步/cy（相同）
     → 收益 = 0%（4 B 端口已消除带宽瓶颈）

  3. Mode B 目标场景（K < 64）仍受 drain 瓶颈约束：
     K=9, FP16:  compute = 9 cy,  drain = 32 cy → drain-limited
     K=9, MXFP4: compute = 2 cy,  drain = 32 cy → drain-limited

  ──────────────────────────────────────────────────────────────────
  总结：
    所有格式: broadcast 性能收益 = 0%（4 B 端口已足够）
    设计收益: 简化数据通路，Mode A/B 共用 TRegFile B 端口
  ──────────────────────────────────────────────────────────────────
```

> **Broadcast 的潜在收益仅限于功耗**：broadcast 可减少 B 端口动态读取功耗（8× fewer reads），但对性能无贡献。TRegFile-4K 的 4 B 端口已消除所有格式的带宽瓶颈。

---

## 5. Interfaces

### 5.1 TRegFile → CUBE Port Mapping

All operands flow through the **TRegFile-4K** (8R + 8W, each 512 B/cycle). Each port delivers one complete **4 KB tile over an 8-cycle epoch** (512 B/cy × 8 cy). The CUBE uses up to **5 read ports** (1 for A, 4 for B):

```
  TRegFile port    CUBE function      Per-cycle    Per-epoch (8 cy)
  ──────────────   ────────────────   ──────────   ────────────────
  R0               A operand          512 B/cy     4 KB (1 tile)
  R1 + R2 + R3 + R4  B operand       2048 B/cy    16 KB (4 tiles)
  R5, R6, R7       TILE.ST / free     1536 B/cy    12 KB (3 tiles)
  W0               C drain            512 B/cy     4 KB (1 tile)
  W1–W7            TILE.LD / free     3584 B/cy    28 KB (7 tiles)
```

> **Data arrives as a stream, not as an instantaneous wide bus.** Each cycle, a port delivers 512 B from one of the 8 SRAM bank groups (8 banks × 64 B). Over 8 cycles, all 8 groups are visited — completing one 4 KB tile. The CUBE must stage incoming data before computation can begin (see §5.6).

**Per-step operand bandwidth demand by format:**

| Format | Elem B | A demand/step | B demand/step | B ports needed (of R1–R4) |
|--------|-------|--------------|--------------|--------------------------|
| FP16 | 2 B | 8×8×1 × 2B = **128 B** | 8×64×1 × 2B = **1024 B** | **2** (R1+R2 sufficient) |
| BF16 | 2 B | 128 B | 1024 B | **2** |
| FP8 | 1 B | 8×8×2 × 1B = **128 B** | 8×64×2 × 1B = **1024 B** | **2** |
| MXFP4 | ½ B | 8×8×8 × ½B = **256 B** | 8×64×8 × ½B = **2048 B** | **4** (R1–R4, all needed) |
| HiFP4 | ½ B | 256 B | 2048 B | **4** |

With 4 B ports delivering 4 tiles/epoch: all formats achieve **1.0 OPA steps/cycle** (see §5.5). For FP16/BF16/FP8, only 2 of the 4 B ports are needed — R3 and R4 are free for concurrent TILE.ST.

> Mode B has identical bandwidth demand per step (no broadcast MUX — each bank reads B independently).

### 5.2 Tile Register File

The CUBE operates with **TRegFile-4K** — see `tregfile4k.md` for full design.

| Parameter | Value |
|-----------|-------|
| Tile size | **4 KB** (4096 B) |
| Tile count | **256** (tile\_idx 0..255) |
| Total capacity | **1 MB** |
| Read ports | **8** (R0–R7), 512 B/cycle each |
| Write ports | **8** (W0–W7), 512 B/cycle each |
| Banks | 64 × 64 B (256×512-bit SRAM, 1R+1W per bank) |
| Calendar | 8-cycle epoch; each port reads/writes one full 4 KB tile per epoch |

A, B operands are read from TRegFile tiles; C drain results are written back. The 4 KB tile is the fundamental data quantum.

### 5.3 Variable-Shape Tiles (4 KB)

Each 4 KB tile stores a sub-matrix whose shape depends on the data format.

**A operand tile shapes (M\_tile = 8 rows):**

| Format | Elem bytes | A tile shape (M × Ka) | K elements per A tile |
|--------|-----------|----------------------|----------------------|
| FP16 | 2 | **8 × 256** | 256 |
| BF16 | 2 | **8 × 256** | 256 |
| FP8 | 1 | **8 × 512** | 512 |
| MXFP4 | 0.5 | **8 × 1024** | 1024 |
| HiFP4 | 0.5 | **8 × 1024** | 1024 |

When M\_actual < 8, the A tile is zero-padded. The CUBE still computes all 8 rows but only rows 0..M\_actual−1 produce valid results.

**B operand tile shapes (N\_tile = 64 columns):**

| Format | Elem bytes | B tile shape (Kb × N) | K rows per B tile |
|--------|-----------|----------------------|-------------------|
| FP16 | 2 | **32 × 64** | 32 |
| BF16 | 2 | **32 × 64** | 32 |
| FP8 | 1 | **64 × 64** | 64 |
| MXFP4 | 0.5 | **128 × 64** | 128 |
| HiFP4 | 0.5 | **128 × 64** | 128 |

**C drain tile shape (FP32 accumulator output):**

| Item | Shape | Size | Tiles |
|------|-------|------|-------|
| Mode A | 8 × 64 × FP32 | **2 KB** | ½ tile (upper 2 KB unused) |
| Mode B | 64 × 64 × FP32 | **16 KB** | 4 tiles (one per 2 banks) |

### 5.4 CUBE ↔ TRegFile Data Mapping

**OPA step data consumption:**

| Format | K\_step (Mode A) | A bytes/step | B bytes/step | Steps per B tile | Steps per A tile |
|--------|-----------------|-------------|-------------|-----------------|-----------------|
| FP16 | 8 | 128 | 1024 | **4** | **32** |
| BF16 | 8 | 128 | 1024 | **4** | **32** |
| FP8 | 16 | 128 | 1024 | **4** | **32** |
| MXFP4 | 64 | 256 | 2048 | **2** | **16** |
| HiFP4 | 64 | 256 | 2048 | **2** | **16** |

> **Key invariant: 8 B tiles are consumed per 1 A tile across all formats.** This ratio is format-independent and the hardware uses it to advance tile register pointers.

**K coverage per tile:**

| Format | K per B tile | K per A tile | B tiles per A tile |
|--------|-------------|-------------|-------------------|
| FP16 | 32 | 256 | **8** |
| BF16 | 32 | 256 | **8** |
| FP8 | 64 | 512 | **8** |
| MXFP4 | 128 | 1024 | **8** |
| HiFP4 | 128 | 1024 | **8** |

**Tiles required per output tile (Mode A, one 8×64 output, full K):**

| Format | K | B tiles (Nb) | A tiles (Na = ⌈Nb/8⌉) | OPA steps | C (half-tiles) |
|--------|------|-------------|----------------------|-----------|----------------|
| FP16 | 256 | 8 | 1 | 32 | 1 |
| FP16 | 4096 | 128 | 16 | 512 | 1 |
| FP16 | 6144 | 192 | 24 | 768 | 1 |
| FP8 | 4096 | 64 | 8 | 256 | 1 |
| MXFP4 | 4096 | 32 | 4 | 64 | 1 |

**Tiles required per output tile (Mode B, one 64×64 output, full K):**

| Format | K | B tiles (Nb) | A tiles/bank (⌈Nb/8⌉) | Total A tiles (8×) | OPA steps | C tiles |
|--------|---|-------------|----------------------|-------------------|-----------|---------|
| FP16 | 9 | 1 | 1 | 8 | 9 | 4 |
| FP16 | 27 | 1 | 1 | 8 | 27 | 4 |
| FP16 | 128 | 4 | 1 | 8 | 128 | 4 |

> **Mode B tile layout**: A tiles are organized as 8 contiguous groups (one per bank). B tiles contain the same data for all banks — each bank reads B independently. Each bank reads its own A tile group.

### 5.5 Effective CUBE Throughput

With 4 B ports (R1–R4) delivering 4 tiles per 8-cycle epoch, **all formats run at 1.0 steps/cycle**:

| Format | Steps/B tile | B tiles/epoch (4 ports) | Steps/epoch | **Steps/cy** | Eff. MACs/cy | vs. peak |
|--------|-------------|------------------------|------------|-------------|-------------|---------|
| FP16 | 4 | 4 | 16 | **1.0** (MAC-limited) | 4,096 | 100% |
| BF16 | 4 | 4 | 16 | **1.0** | 4,096 | 100% |
| FP8 | 4 | 4 | 16 | **1.0** | 8,192 | 100% |
| MXFP4 | 2 | 4 | 8 | **1.0** | 32,768 | 100% |
| HiFP4 | 2 | 4 | 8 | **1.0** | 32,768 | 100% |

For FP16/BF16/FP8, 4 tiles × 4 steps = 16 steps/epoch, but the MAC array fires at most 8 steps/epoch (1/cy) — B supply is 2× overprovisioned. Only 2 of the 4 B ports (R1+R2) are needed; R3, R4 are free for TILE.ST.

For MXFP4/HiFP4, 4 tiles × 2 steps = 8 steps/epoch = exactly 1 step/cy. All 4 B ports required.

**Port C drain** uses write port W0. Mode A drains 2 KB in 8 cycles (1 rotation); Mode B drains 16 KB in 32 cycles (4 rotations).

### 5.6 CUBE Epoch-Aligned Pipeline

Each TRegFile port delivers a tile as an **8-cycle stream** (not an instantaneous wide bus). The CUBE stages incoming data with double-buffered registers.

#### Staging Registers (Pipeline Buffers — Not Architecturally Visible)

The staging registers are internal pipeline SRAM buffers that decouple TRegFile's 8-cycle streaming interface from the 1-step/cycle MAC compute pipeline. They are **not visible to the programmer** — no instruction reads or writes them directly; they exist solely as hardware-managed pipeline storage within the CUBE datapath.

Both Port A and Port B staging registers have **double-buffer** capability. One buffer is read by the MAC array while the other is simultaneously filled from TRegFile, eliminating stalls between consecutive tile operations.

```
  ┌───────────────────────────────────────────────────────────────────────┐
  │  CUBE Staging Registers (pipeline-only, NOT architecturally visible) │
  │                                                                       │
  │  Port A — A Double-Buffer (2 × 4 KB = 8 KB)                          │
  │  ┌───────────┐  ┌───────────┐                                        │
  │  │ A-Buf[0]  │  │ A-Buf[1]  │                                        │
  │  │   4 KB    │  │   4 KB    │                                        │
  │  └─────┬─────┘  └─────┬─────┘                                        │
  │        └───── sel ─────┘                                              │
  │              │                                                        │
  │  R0 ───▶ fill inactive buf    MAC ◀── read active buf (row broadcast)│
  │                                                                       │
  │  Filled from R0 over 1 epoch (8 cy, 512 B/cy × 8 = 4 KB).           │
  │  Ping-pong: while compute reads A-Buf[i], next A loads to A-Buf[j].  │
  │  A-tile reuse: when same A tile, skip reload (active buf stays valid).│
  │                                                                       │
  │  Port B — B Double-Buffer (2 × 16 KB = 32 KB)                        │
  │  ┌───────────┐  ┌───────────┐                                        │
  │  │ B-Buf[0]  │  │ B-Buf[1]  │                                        │
  │  │  16 KB    │  │  16 KB    │                                        │
  │  └─────┬─────┘  └─────┬─────┘                                        │
  │        └───── sel ─────┘                                              │
  │              │                                                        │
  │  R1–R4 ──▶ fill inactive buf  MAC ◀── read active buf (col distribute)│
  │                                                                       │
  │  Each buffer holds 1 batch = 4 B tiles (one per port, 4 KB each).    │
  │  Filled from R1–R4 over 1 epoch (8 cy, 4×512 B/cy × 8 = 16 KB).     │
  │  Ping-pong: batch N fills buf[N%2], compute reads buf[(N-1)%2].      │
  │                                                                       │
  │  Total baseline staging SRAM: 8 + 32 = 40 KB                         │
  └───────────────────────────────────────────────────────────────────────┘
```

| Buffer | Per-buffer size | Count | Total SRAM | Source | Fill time |
|--------|----------------|-------|------------|--------|-----------|
| A-Buf | 4 KB (1 A tile) | 2 (double) | **8 KB** | R0 | 8 cy |
| B-Buf | 16 KB (4 B tiles) | 2 (double) | **32 KB** | R1–R4 | 8 cy |
| **Baseline total** | | | **40 KB** | | |

For the pre-load optimization (§5.7.5), B-Buf extends to **triple-buffer** (3 × 16 KB = 48 KB), yielding a total of **56 KB**. A-Buf remains double-buffer in all configurations.

#### Epoch Pipeline Timing (FP16 Mode A, steady state)

```
  Epoch │  R0 (A, 512 B/cy)        │  R1–R4 (B, 2048 B/cy)  │  MAC Array
  ──────┼───────────────────────────┼─────────────────────────┼────────────────────────
    0   │  Fill A-Buf[0] (4 KB)     │  Fill B-Buf[0] (16 KB)  │  — (pipeline fill)
    1   │  (idle / pre-load next A) │  Fill B-Buf[1]          │  compute from B-Buf[0]
    2   │  Fill A-Buf[1] (new tile) │  Fill B-Buf[0]          │  compute from B-Buf[1]
    3   │  (idle / pre-load next A) │  Fill B-Buf[1]          │  compute from B-Buf[0]
   ...  │  A-Buf ping-pong          │  B-Buf ping-pong        │
```

Per epoch the MAC fires **8 OPA steps** (FP16), or **8 steps** (MXFP4, but from 4 tiles × 2 steps):

| Format | Steps/B tile | B tiles/epoch (4 ports) | Steps/epoch | Steps/cy |
|--------|-------------|------------------------|------------|---------|
| FP16 | 4 | 4 | 16 (MAC-limited to 8) | **1.0** |
| BF16 | 4 | 4 | 16 (MAC-limited to 8) | **1.0** |
| FP8 | 4 | 4 | 16 (MAC-limited to 8) | **1.0** |
| MXFP4 | 2 | 4 | **8** | **1.0** |
| HiFP4 | 2 | 4 | **8** | **1.0** |

#### A-Tile Reuse

The A double-buffer holds one A tile per buffer. A is loaded once into the active buffer and reused for **2 epochs** (8 B tiles ÷ 4 tiles/epoch). R0 loads A in 1 epoch → busy every other epoch (50%). When A changes between output tiles, R0 pre-loads the new A into the inactive buffer during a free epoch.

| Format | Steps / A tile | B tiles / A tile | A-load epochs | R0-free epochs |
|--------|---------------|-----------------|--------------|---------------|
| FP16 | 32 | 8 | 1 | **1** (of 2) |
| FP8 | 32 | 8 | 1 | **1** |
| MXFP4 | 16 | 8 | 1 | **1** |

#### Drain Latency

CUBE.DRAIN writes accumulator data back to TRegFile via W0. Each drain operation takes **8 cycles** (one full bank-group rotation):

| Mode | Drain data | Drain cycles | W0 operations |
|------|-----------|-------------|---------------|
| A | 2 KB (½ tile) | **8** | 1 rotation (4 groups active, 4 idle) |
| B | 16 KB (4 tiles) | **32** | 4 rotations (8 cy each) |

Drain overlaps with OPA compute (ping-pong accumulators). W0 is a write port, independent of compute read ports R0–R4. See §5.7 for detailed pipeline timing.

### 5.7 CUBE Execution Pipeline

#### 5.7.1 19-Stage Pipeline Decomposition

The CUBE pipeline decomposes into **19 sub-stages**, each exactly **1 cycle**. The two bookend macro-phases (OF, AD) expand into 8 sub-stages each—one per TRegFile bank-group calendar slot—while the three inner stages (MUL, RED, ACC) each take 1 cycle:

```
OF0→OF1→OF2→OF3→OF4→OF5→OF6→OF7→MUL→RED→ACC→AD0→AD1→AD2→AD3→AD4→AD5→AD6→AD7
←───── Operand Fetch (8 cy) ─────→           ←───── Acc Drain (8 cy) ──────→
     TRegFile → staging regs       MAC→RED→ACC    ACC SRAM → TRegFile
```

| Sub-stage | Macro | Cy# | Function |
|-----------|-------|-----|----------|
| OF0 | OF | 1/8 | R0–R4 read bank-group 0 → staging regs (512 B/port) |
| OF1 | OF | 2/8 | R0–R4 read bank-group 1 → staging regs |
| OF2 | OF | 3/8 | R0–R4 read bank-group 2 → staging regs |
| OF3 | OF | 4/8 | R0–R4 read bank-group 3 → staging regs |
| OF4 | OF | 5/8 | R0–R4 read bank-group 4 → staging regs |
| OF5 | OF | 6/8 | R0–R4 read bank-group 5 → staging regs |
| OF6 | OF | 7/8 | R0–R4 read bank-group 6 → staging regs |
| OF7 | OF | 8/8 | R0–R4 read bank-group 7; staging regs full (4 KB/port) |
| MUL | CMP | 1 | 4096 parallel MACs (sub-word partitioned for FP8/MXFP4) |
| RED | CMP | 1 | Inter-bank adder tree (Mode A: 8→1) or bypass (Mode B) |
| ACC | CMP | 1 | FP32 add to ping-pong accumulator SRAM (z0 or z1) |
| AD0 | AD | 1/8 | W0 writes bank-group 0 from accumulator (512 B) |
| AD1 | AD | 2/8 | W0 writes bank-group 1 |
| AD2 | AD | 3/8 | W0 writes bank-group 2 |
| AD3 | AD | 4/8 | W0 writes bank-group 3 |
| AD4 | AD | 5/8 | W0 writes bank-group 4 |
| AD5 | AD | 6/8 | W0 writes bank-group 5 |
| AD6 | AD | 7/8 | W0 writes bank-group 6 |
| AD7 | AD | 8/8 | W0 writes bank-group 7; drain complete (4 KB written) |

**End-to-end latency**: 8 + 1 + 1 + 1 + 8 = **19 cycles**.
**Inner compute throughput**: MUL → RED → ACC pipelined at **1 step/cycle**.

Step-level inner pipeline overlap (MUL/RED/ACC):

```
  Cycle     │ c   │ c+1 │ c+2 │ c+3 │ c+4 │
  ──────────┼─────┼─────┼─────┼─────┼─────┤
  Step i    │ MUL │ RED │ ACC │     │     │
  Step i+1  │     │ MUL │ RED │ ACC │     │
  Step i+2  │     │     │ MUL │ RED │ ACC │
```

#### 5.7.2 CUBE.OPA Execution Flow

`CUBE.OPA zd, Ta, Tb, Nb` drives the pipeline through three macro-phases:

**Phase 1 — OF0–OF7: Operand Fetch (8 cycles per batch)**

Each OF fetches one **batch** of 4 B tiles via R1–R4 into B-buf (16 KB total). The first OF also loads A via R0 (in parallel). Each sub-stage OFi reads one bank-group per TRegFile calendar slot:

```
  OF0: R0–R4 → bank-group 0  (512 B/port)
  OF1: R0–R4 → bank-group 1  (512 B/port)
  OF2: R0–R4 → bank-group 2  (512 B/port)
  OF3: R0–R4 → bank-group 3  (512 B/port)
  OF4: R0–R4 → bank-group 4  (512 B/port)
  OF5: R0–R4 → bank-group 5  (512 B/port)
  OF6: R0–R4 → bank-group 6  (512 B/port)
  OF7: R0–R4 → bank-group 7  (512 B/port)  ← staging regs full
```

**TRegFile epoch-aligned address model:** Each TRegFile port accepts one `reg_idx` per **8-cycle epoch** — the address is latched into a pending register and promotes to active at the next epoch boundary (see `tregfile4k.md` §3). The port then delivers 512 B/cy × 8 cy = 4 KB for that tile. The CUBE issues batch tile addresses to R0–R4 at the start of each OF epoch; the next batch's address is written to the pending register during the current epoch so the port transitions with **zero bubble**:

```
  Port R1 example (back-to-back batches, 1 new addr / 8 cy):
  Cycle:   0              7   8             15  16  ...
  Addr:   [B0 → active]      [B4 → active]      [B8 → active]
  Data:    B0.G0 ···· B0.G7   B4.G0 ···· B4.G7  B8.G0 ···
           └─ epoch 0 ──┘     └─ epoch 1 ──┘     └─ ...
                            ↑ zero bubble (pending → active)
```

With B double-buffering, consecutive batch OFs overlap with compute:

```
  Batch 0: OF0–OF7  R0(A) + R1-R4(B[0-3]) → buf[0]    (8 cy)
  Batch 1: OF0–OF7  R1-R4(B[4-7]) → buf[1]             (8 cy, overlaps compute)
  ...     (next batch addr written to pending during current epoch → zero-bubble)
```

If A-Reg already holds valid data (A-tile reuse), R0 is idle during OF.

**Phase 2 — MUL/RED/ACC: Compute (N = Nb × S steps at 1 step/cy)**

After OF7 of the first batch, the inner pipeline fires 1 step/cycle from staged data. While batch i computes from `buf[i%2]`, the next OF0–OF7 loads batch i+1 into `buf[(i+1)%2]`:

```
  Steps per batch (S steps per tile × 4 tiles):
    FP16/FP8: 4 × 4 = 16 cy  (2× OF duration → no stall)
    MXFP4:    4 × 2 = 8  cy  (= OF duration → exact match, no stall)
```

**Pipeline flush**: after the last step enters MUL, 2 more cycles for RED → ACC.

**Phase 3 — AD0–AD7: Accumulator Drain (8 cycles)**

After the last ACC write, the AD phase drains accumulator SRAM to TRegFile via W0. The CUBE issues the destination `reg_idx` to W0; the port latches it and writes one bank-group per cycle over the next 8 cycles:

```
  AD0: W0 → bank-group 0  (512 B)
  AD1: W0 → bank-group 1  (512 B)
  AD2: W0 → bank-group 2  (512 B)
  AD3: W0 → bank-group 3  (512 B)
  AD4: W0 → bank-group 4  (512 B)
  AD5: W0 → bank-group 5  (512 B)
  AD6: W0 → bank-group 6  (512 B)
  AD7: W0 → bank-group 7  (512 B)  ← drain complete
```

AD uses W0 (write port), completely independent of R0–R4 (read ports). W0 accepts one `reg_idx` per 8-cycle epoch; the next drain's address is written to W0's pending register during the current epoch, enabling zero-bubble consecutive drains.

**CUBE.OPA total duration:**

```
  Total = 8 (OF0–OF7) + N (compute steps) + 2 (flush) + 8 (AD0–AD7)
        = N + 18
  where N = Nb × S (total OPA steps)
```

| Format | S | K=256 (Nb) | Total cy | K=1024 (Nb) | Total cy | K=4096 (Nb) | Total cy |
|--------|---|-----------|----------|-------------|----------|-------------|----------|
| FP16 | 4 | 8 | **50** | 32 | **146** | 128 | **530** |
| FP8 | 4 | 4 | **34** | 16 | **82** | 64 | **274** |
| MXFP4 | 2 | 2 | **22** | 8 | **34** | 32 | **82** |

#### 5.7.3 Pipeline Overlap: AD0–AD7 ∥ OF0–OF7 (Ping-Pong)

With ping-pong accumulators (z0/z1), AD of tile j and OF of tile j+1 run **simultaneously** — they use independent resources (W0 vs R0–R4) and independent accumulator buffers. At the sub-stage level, each cycle ADi and OFi execute in parallel on different ports:

```
  Cycle:  c    c+1  c+2  c+3  c+4  c+5  c+6  c+7
  ────── ──── ──── ──── ──── ──── ──── ──── ────
  j   AD: AD0  AD1  AD2  AD3  AD4  AD5  AD6  AD7   (W0, z_old)
  j+1 OF: OF0  OF1  OF2  OF3  OF4  OF5  OF6  OF7   (R0-R4, z_new)
          └────────── 8 cycles fully overlapped ──────────┘
```

The overlapped AD/OF saves 8 cycles per tile.
**Steady-state per-tile interval** = N + 2 (compute + flush) + 8 (AD ∥ OF) = **N + 10**.

#### 5.7.4 Pipeline Timing Diagrams

**Diagram 1: FP16 Mode A, K=256, Nb=8 (N=32 steps), single output tile**

```
  Cycle: 0  1  2  3  4  5  6  7  8  9 10 ···· 39 40 41 42 43 44 45 46 47 48 49
    OF: OF0 OF1 OF2 OF3 OF4 OF5 OF6 OF7
   MUL:                                  s0 s1 s2 ···· s31
   RED:                                     s0 s1 ···· s30 s31
   ACC:                                        s0 ···· s29 s30 s31
    AD:                                                         AD0 AD1 AD2 AD3 AD4 AD5 AD6 AD7
        ├──────────────────────────────┼──────────────────┼─────┼──────────────────────────────┤
        ←──── OF0–OF7 (8 cy) ────→     ←── 32 steps ──→   fl   ←──── AD0–AD7 (8 cy) ────→

  Note: Batch 1 OF0–OF7 runs at cycles 8–15 (R1-R4 load B[4-7] → buf[1]),
        hidden behind compute which reads from buf[0].

  Total: 50 cycles.  MAC active: 32 of 50 cycles.
```

**Diagram 2: MXFP4 Mode A, K=1024, Nb=8 (N=16 steps), single output tile**

```
  Cycle: 0  1  2  3  4  5  6  7  8  9 ···· 23 24 25 26 27 28 29 30 31 32 33
    OF: OF0 OF1 OF2 OF3 OF4 OF5 OF6 OF7
   MUL:                                  s0 s1 ···· s15
   RED:                                     s0 ···· s14 s15
   ACC:                                        s0 ···· s14 s15
    AD:                                                     AD0 AD1 AD2 AD3 AD4 AD5 AD6 AD7
        ├──────────────────────────────┼─────────────┼─────┼──────────────────────────────┤
        ←──── OF0–OF7 (8 cy) ────→     ←─ 16 steps ─→  fl  ←──── AD0–AD7 (8 cy) ────→

  Note: Batch 1 OF0–OF7 at cycles 8–15, exactly overlaps with batch 0 compute.

  Total: 34 cycles.  MAC active: 16 of 34 cycles.
```

**Diagram 3: FP16 N-loop, Nb=8, ping-pong steady state (3 tiles)**

```
  Cycles     │ j=0 (z0)       │ j=1 (z1)       │ j=2 (z0)
  ───────────┼─────────────── ┼─────────────── ┼───────────────
   0  –  7   │ OF0 – OF7      │                 │
   8  – 39   │ s0  – s31      │                 │
  40  – 41   │ flush          │                 │
  42  – 49   │ AD0 – AD7      │ OF0 – OF7       │              ← overlap
  50  – 81   │                │ s0  – s31       │
  82  – 83   │                │ flush           │
  84  – 91   │                │ AD0 – AD7       │ OF0 – OF7    ← overlap
  92  – 123  │                │                 │ s0  – s31
  124 – 125  │                │                 │ flush
  126 – 133  │                │                 │ AD0 – AD7

  AD_j (AD0–AD7) ∥ OF_j+1 (OF0–OF7): W0 vs R0-R4, 8 cycles fully overlapped.

  Per-tile interval: 42 cycles.  MAC utilization: 32/42 = 76.2%
```

**Diagram 4: MXFP4 N-loop, Nb=8, ping-pong steady state (3 tiles)**

```
  Cycles     │ j=0 (z0)       │ j=1 (z1)       │ j=2 (z0)
  ───────────┼─────────────── ┼─────────────── ┼───────────────
   0  –  7   │ OF0 – OF7      │                 │
   8  – 23   │ s0  – s15      │                 │
  24  – 25   │ flush          │                 │
  26  – 33   │ AD0 – AD7      │ OF0 – OF7       │              ← overlap
  34  – 49   │                │ s0  – s15       │
  50  – 51   │                │ flush           │
  52  – 59   │                │ AD0 – AD7       │ OF0 – OF7    ← overlap
  60  – 75   │                │                 │ s0  – s15
  76  – 77   │                │                 │ flush
  78  – 85   │                │                 │ AD0 – AD7

  Per-tile interval: 26 cycles.  MAC utilization: 16/26 = 61.5%
```

#### 5.7.5 B-Port Pre-Loading Optimization (FP16/FP8)

For FP16 and FP8, only **2 of the 4 B ports** (R1+R2) suffice for compute (B supply 2× overprovisioned). The spare ports R3+R4 can **pre-load** the next tile's B data into a third B-buffer during the current tile's compute:

```
  j compute (R1+R2 supply data, R3+R4 idle):
    R3+R4 pre-load j+1 batch 0 → buf[2]   (OF0–OF7: 8 cy)
    R3+R4 pre-load j+1 batch 1 → buf[0]   (OF0–OF7: 8 cy, freed by j)

  j flush + AD0–AD7:
    j+1 data already staged → j+1 MUL starts immediately after flush
```

With pre-loading, the per-tile overhead drops to the **2-cycle flush** only — AD0–AD7 runs concurrently with the next tile's compute (different ACC buffer):

```
  j (z0):   [OF0–OF7][── compute N cy ──][fl][AD0–AD7 ← concurrent with j+1 compute]
  j+1(z1):            data pre-loaded    [── compute N cy ──][fl][AD0–AD7 ← concurrent...]
                                          ↑ starts right after j flush
```

**Pre-load steady-state per tile: N + 2 cycles.**

| Config | Staging SRAM | FP16 util (K=256) | MXFP4 util (K=1024) |
|--------|-------------|-------------------|---------------------|
| Double-buffer (baseline) | A(8) + B(32) = **40 KB** | 76.2% | 61.5% |
| Triple-buffer (pre-load) | A(8) + B(48) = **56 KB** | **94.1%** | 61.5% (unchanged) |

Not available for MXFP4/HiFP4 (all 4 B ports occupied during compute).

#### 5.7.6 Throughput and Latency Summary

**Single output tile latency (first tile, no overlap):**

```
  First_tile = 8 (OF0–OF7) + N (compute) + 2 (flush) + 8 (AD0–AD7)  =  N + 18
```

**Steady-state per-tile cycles (N-loop with ping-pong):**

```
  Baseline (double-buffer):    N + 10       (AD0–AD7 ∥ OF0–OF7 overlap)
  Pre-load (triple-buffer):    N + 2        (FP16/FP8 only; AD hidden behind next compute)
```

| Format | K | Nb | Steps (N) | 1st tile | Steady (baseline) | MAC util | Steady (pre-load) | MAC util |
|--------|------|------|----------|---------|-------------------|---------|-------------------|---------|
| FP16 | 256 | 8 | 32 | 50 | **42** | 76.2% | **34** | 94.1% |
| FP16 | 4096 | 128 | 512 | 530 | **522** | 98.1% | **514** | 99.6% |
| FP8 | 256 | 4 | 16 | 34 | **26** | 61.5% | **18** | 88.9% |
| FP8 | 4096 | 64 | 256 | 274 | **266** | 96.2% | **258** | 99.2% |
| MXFP4 | 1024 | 8 | 16 | 34 | **26** | 61.5% | — | — |
| MXFP4 | 4096 | 32 | 64 | 82 | **74** | 86.5% | — | — |

> For K ≥ 4096, pipeline overhead < 4% across all formats. For small K, the pre-load optimization lifts FP16/FP8 utilization above 88%. MXFP4 at small K has lower utilization but delivers 8× more MACs per step, so absolute throughput still exceeds FP16.

**Effective TOPS at 1.5 GHz (steady-state, K=4096):**

| Format | Baseline | Pre-load |
|--------|---------|---------|
| FP16 | 12.1 TFLOPS (98.1%) | 12.3 TFLOPS (99.6%) |
| FP8 | 23.7 TOPS (96.2%) | 24.4 TOPS (99.2%) |
| MXFP4 | 85.1 TOPS (86.5%) | — |

---

## 6. Instruction Set

### 6.1 Overview

The CUBE uses **tile-level instructions** where each `CUBE.OPA` consumes one or more consecutive tile registers for A and B, executing all K-loop OPA steps internally. A GPR operand specifies the **B tile count (Nb)**, controlling how many K-steps the instruction executes. The hardware derives A tile count from the fixed 8:1 ratio.

```
┌───────────────────────────────────────────────────────────────────────────────┐
│  CUBE + TILE ISA                                                              │
├──────────────────────────────┬────────────────────────────────────────────────┤
│  CUBE.CFG   mode, fmt        │  Set operating mode and data format            │
│             [, Mactive]      │  mode: A (K-parallel) / B (M-parallel)         │
│                              │  fmt: FP16/BF16/FP8_E4M3/FP8_E5M2/MXFP4/HiFP4│
│                              │  Mactive: active rows 1–8 (default 8)          │
│                              │  Persists until next CUBE.CFG                  │
├──────────────────────────────┼────────────────────────────────────────────────┤
│  CUBE.OPA   zd, Ta, Tb, Rn  │  Outer Product Accumulate with tile iteration  │
│                              │  zd: accumulator buffer (z0 or z1)             │
│                              │  Ta: starting tile register index for A        │
│                              │  Tb: starting tile register index for B        │
│                              │  Rn: GPR holding N_b (B tile count)            │
│                              │  Hardware reads T[Tb]..T[Tb+Nb-1] for B,      │
│                              │  Mode A: T[Ta]..T[Ta+⌈Nb/8⌉-1] for A         │
│                              │  Mode B: 8 groups of ⌈Nb/8⌉ A tiles from Ta   │
│                              │  Executes Nb × S OPA steps                     │
│                              │  (S = 4 for FP16/BF16/FP8, 2 for MXFP4/HiFP4)│
├──────────────────────────────┼────────────────────────────────────────────────┤
│  CUBE.DRAIN zd, Tc           │  Drain accumulator buffer → tile registers     │
│                              │  Mode A: 2 KB FP32 to lower half of T[Tc]     │
│                              │  Mode B: 16 KB FP32 to T[Tc]..T[Tc+3]        │
├──────────────────────────────┼────────────────────────────────────────────────┤
│  CUBE.ZERO  zd               │  Zero accumulator buffer zd (1 cycle)          │
├──────────────────────────────┼────────────────────────────────────────────────┤
│  CUBE.WAIT  zd               │  Stall until drain of buffer zd completes      │
├──────────────────────────────┼────────────────────────────────────────────────┤
│  TILE.LD   Td, [Rbase]       │  Load 4 KB tile from memory (contiguous)       │
│  TILE.LD   Td, [Rbase], Rs   │  Load 4 KB tile from memory (strided)         │
├──────────────────────────────┼────────────────────────────────────────────────┤
│  TILE.ST   [Rbase], Ts       │  Store 4 KB tile to memory (contiguous)        │
│  TILE.ST   [Rbase], Ts, Rs   │  Store 4 KB tile to memory (strided)          │
└──────────────────────────────┴────────────────────────────────────────────────┘
```

### 6.2 CUBE.CFG — Configuration

```
  CUBE.CFG  mode, fmt [, Mactive]

  mode:     0 = Mode A (K-parallel across 8 banks)
            1 = Mode B (M-parallel, independent banks)
  fmt:      000 = FP16       001 = BF16       010 = FP8_E4M3
            011 = FP8_E5M2   100 = MXFP4      101 = HiFP4
  Mactive:  1–8 (number of valid A-operand rows; default 8)
            Rows beyond Mactive are zero-masked, saving dynamic power.
```

### 6.3 CUBE.OPA — Outer Product Accumulate

```
  CUBE.OPA  zd, Ta, Tb, Rn

  31    27 26 25    17 16     8 7      3 2    0
 ┌───────┬──┬───────┬────────┬────────┬──────┐
 │opcode │zd│  Ta   │   Tb   │   Rn   │ func │
 │ 5 bits│1b│ 9 bits│ 9 bits │ 5 bits │3 bits│
 └───────┴──┴───────┴────────┴────────┴──────┘
```

**Semantics:**

1. Read `Nb` from GPR `Rn`.
2. Derive per-bank A tile count: `Na = ⌈Nb / 8⌉`
3. **Mode A** — for each B tile `b_idx = 0 .. Nb−1`:
   - A tile index: `a = b_idx / 8`
   - Read B from `T[Tb + b_idx]`, A from `T[Ta + a]`
   - Execute S OPA sub-steps; adder tree reduces 8 banks → shared ACC
4. **Mode B** — for each B tile `b_idx = 0 .. Nb−1`:
   - A tile index per bank: `a = b_idx / 8`
   - Read B from `T[Tb + b_idx]` (same data, each bank reads independently)
   - Bank `b` reads A from `T[Ta + b*Na + a]` (different M-block)
   - Execute S OPA sub-steps; each bank accumulates independently
   - Total A tiles consumed: `8 × Na`
5. Total OPA steps = `Nb × S` (S=4 for 16-bit/8-bit, S=2 for 4-bit, with 4 KB tiles)

**Per-instruction duration:**

| Format | Steps per B tile (S) | Cycles/B tile (Mode A) | Cycles/B tile (Mode B) |
|--------|---------------------|----------------------|----------------------|
| FP16 | 4 | 4 | 4 |
| BF16 | 4 | 4 | 4 |
| FP8 | 4 | 4 | 4 |
| MXFP4 | 2 | 2 | 2 |
| HiFP4 | 2 | 2 | 2 |

### 6.4 CUBE.DRAIN — Drain to Tile Registers

```
  CUBE.DRAIN  zd, Tc

  Mode A: writes 2 KB (8×64 FP32) to lower half of T[Tc]
          8 cycles (1 bank-group rotation via W0)

  Mode B: writes 16 KB (8 × 8×64 FP32) to T[Tc]..T[Tc+3]
          32 cycles (4 rotations via W0)
```

**Stall-free condition (compute ≥ drain):**

| Mode | Drain cycles | Min OPA steps to hide drain (FP16) | Min Nb (FP16) |
|------|-------------|-----------------------------------|--------------|
| A | 4 | 4 | 2 |
| B | 32 | 32 | 16 |

### 6.5 CUBE.ZERO / CUBE.WAIT / TILE.LD / TILE.ST

```
  CUBE.ZERO  zd       ; clear accumulator buffer zd, 1 cycle
  CUBE.WAIT  zd       ; stall until pending DRAIN on buffer zd completes

  TILE.LD  Td, [Rbase]         ; contiguous load: 4 KB from Rbase
  TILE.LD  Td, [Rbase], Rs     ; strided load: rows of width W at stride Rs
  TILE.ST  [Rbase], Ts         ; contiguous store
  TILE.ST  [Rbase], Ts, Rs     ; strided store

  Duration: 4 cycles per tile (one TRegFile port, 512 B/cycle)
  With 4 write ports: up to 4 TILE.LD in parallel per epoch
```

### 6.6 Encoding Summary

All instructions fit in **32 bits**:

| Instruction | Fields | Bits |
|-------------|--------|------|
| CUBE.CFG | opcode(7) + mode(1) + fmt(3) + Mactive(3) + reserved(18) | 32 |
| CUBE.OPA | opcode(5) + zd(1) + Ta(9) + Tb(9) + Rn(5) + func(3) | 32 |
| CUBE.DRAIN | opcode(7) + zd(1) + Tc(9) + reserved(15) | 32 |
| CUBE.ZERO | opcode(7) + zd(1) + reserved(24) | 32 |
| CUBE.WAIT | opcode(7) + zd(1) + reserved(24) | 32 |
| TILE.LD | opcode(7) + Td(9) + Rbase(5) + Rs(5) + reserved(6) | 32 |
| TILE.ST | opcode(7) + Ts(9) + Rbase(5) + Rs(5) + reserved(6) | 32 |

- `Ta`, `Tb`, `Tc`, `Td`, `Ts`: 9-bit tile register index (0–511)
- `Rn`, `Rbase`, `Rs`: 5-bit GPR index (r0–r31)
- `zd`: 1-bit accumulator buffer select (z0 or z1)

---

## 7. Performance

### 7.1 Peak & Effective Throughput

**Effective throughput (with TRegFile-4K, 4 B-read ports):**

| Format | MACs/step | Steps/cy | Effective MACs/cy | @ 1.5 GHz | @ 1.0 GHz |
|--------|----------|---------|-------------------|-----------|-----------|
| FP16 | 4,096 | 1.0 | 4,096 | **12.3 TFLOPS** | 8.2 TFLOPS |
| BF16 | 4,096 | 1.0 | 4,096 | **12.3 TFLOPS** | 8.2 TFLOPS |
| FP8 | 8,192 | 1.0 | 8,192 | **24.6 TOPS** | 16.4 TOPS |
| MXFP4 | 32,768 | 1.0 | 32,768 | **98.3 TOPS** | 65.5 TOPS |
| HiFP4 | 32,768 | 1.0 | 32,768 | **98.3 TOPS** | 65.5 TOPS |

> All formats run at **100% of hardware peak**. The 4 B-port allocation (R1–R4) in TRegFile-4K eliminates the B-bandwidth bottleneck that limited the old 2-port design.

### 7.2 GEMM Cycle Formulas

GEMM: `C(M×N) += A(M×K) × B(K×N)`

```
  Mode A (K-parallel):
    K_step = 8 × K_mac     (K_mac = 1/2/8 for FP16/FP8/MXFP4)
    Cycles = ⌈M/8⌉ × ⌈N/64⌉ × ⌈K / K_step⌉
    (all formats: 1 step/cycle — no BW bottleneck)

  Mode B (M-parallel):
    K_step = K_mac
    Cycles = ⌈M/64⌉ × ⌈N/64⌉ × ⌈K / K_mac⌉
```

### 7.3 MAC Utilization Model

```
  Util_total = Util_M × Util_K × Util_N × Util_BW

  Util_M  = M / (⌈M/8⌉ × 8)          M-dimension row utilization
  Util_K  = K / (⌈K/K_step⌉ × K_step)  K-dimension ceiling waste
  Util_N  = N / (⌈N/64⌉ × 64)         N-dimension ceiling waste
  Util_BW = min(1.0, TRegFile_BW / CUBE_demand)  bandwidth efficiency
```

For typical transformer dimensions, K and N are perfectly aligned → **Util\_K = Util\_N = 100%**. Util\_BW = **100% for all formats** with the 4 B-port TRegFile-4K configuration.

**M-dimension utilization (the dominant factor):**

| M | ⌈M/8⌉ × 8 | Util\_M |
|---|-----------|---------|
| 4 | 8 | **50.0%** |
| 8 | 8 | **100%** |
| 16 | 16 | **100%** |
| 64 | 64 | **100%** |
| 256 | 256 | **100%** |

### 7.4 Cross-Format Speedup

| Case (M×K×N) | FP16 cycles | FP8 cycles | MXFP4 cycles | FP8 speedup | MXFP4 speedup |
|--------------|------------|-----------|-------------|------------|--------------|
| 4×256×4096 | 2,048 | 1,024 | 256 | **2.0×** | **8.0×** |
| 4×4096×4096 | 32,768 | 16,384 | 4,096 | **2.0×** | **8.0×** |
| 8×4096×4096 | 32,768 | 16,384 | 4,096 | **2.0×** | **8.0×** |
| 64×4096×4096 | 262,144 | 131,072 | 32,768 | **2.0×** | **8.0×** |
| 256×4096×4096 | 1,048,576 | 524,288 | 131,072 | **2.0×** | **8.0×** |

FP8 consistently delivers **2× speedup** over FP16. MXFP4 delivers the full **8×** speedup — no B-bandwidth bottleneck with 4 B-read ports.

### 7.5 Representative GEMM Cycle Counts

**FP16 (K\_mac=1):**

| GEMM (M×K×N) | Mode A cycles | Mode B cycles | Winner |
|--------------|--------------|--------------|--------|
| 64×64×64 | 8×1×8 = 64 | 1×1×64 = 64 | tie |
| 256×256×256 | 32×4×32 = 4096 | 4×4×256 = 4096 | tie |
| 256×9×256 | 32×4×2 = 256 | 4×4×9 = 144 | **B (1.78×)** |
| 50176×27×64 | 6272×1×4 = 25088 | 785×1×27 = 21195 | **B (1.18×)** |
| 64×1×2048 | 8×32×1 = 256 | 1×32×1 = 32 | **B (8×)** |
| 4×128×4096 | 1×64×16 = 1024 | 1×64×128 = 8192 | **A (8×)** |

**FP8 (K\_mac=2):**

| GEMM (M×K×N) | Mode A cycles | Mode B cycles | Winner |
|--------------|--------------|--------------|--------|
| 256×256×256 | 32×4×16 = 2048 | 4×4×128 = 2048 | tie |
| 256×9×256 | 32×4×1 = 128 | 4×4×5 = 80 | **B (1.6×)** |

**MXFP4 (K\_mac=8):**

| GEMM (M×K×N) | Mode A cycles | Mode B cycles | Winner |
|--------------|--------------|--------------|--------|
| 256×256×256 | 32×4×4 = 512 | 4×4×32 = 512 | tie |
| 256×9×256 | 32×4×1 = 128 | 4×4×2 = 32 | **B (4×)** |

---

## 8. Workload Analysis

### 8.1 Transformer Decode

#### 8.1.1 Typical GEMM Dimensions

Transformer decode inference: `C[M×N] += A[M×K] × B[K×N]` where M (batch × beam) = 4–256, K = 128–6144, N = 1024–4096.

| Layer | Description | M | K | N |
|-------|-------------|---|---|---|
| Attention Q×Kᵀ | Score computation | 4–256 | 128, 192 | 1024–4096 |
| Attention × V | Context aggregation | 4–256 | 1024–4096 | 128, 192 |
| QKV projection | Fused query/key/value | 4–256 | 4096, 6144 | 4096 |
| Output projection | Multi-head → hidden | 4–256 | 4096, 6144 | 4096 |
| FFN up/gate | MLP first layer | 4–256 | 4096, 6144 | 4096 |
| FFN down | MLP second layer | 4–256 | 4096 | 4096, 6144 |

#### 8.1.2 Mode Selection

K is always aligned to K\_step\_A for these dimensions. M is small (4–256), so Mode B ⌈M/64⌉ ceiling wastes far more than Mode A ⌈M/8⌉.

> **All transformer decode analysis uses Mode A exclusively.**

#### 8.1.3 Comprehensive Case Analysis (Mode A)

**FP16 (K\_step = 8, 4096 MACs/step, 4 KB tiles: B=32×64, A=8×256):**

| M | K | N | M\_blk | N\_blk | K\_steps | **Total steps** | B tiles/N | A tiles/M | MAC util |
|---|---|---|--------|--------|---------|----------------|----------|----------|---------|
| 4 | 128 | 1024 | 1 | 16 | 16 | **256** | 4 | 1 | 50% |
| 4 | 128 | 4096 | 1 | 64 | 16 | **1,024** | 4 | 1 | 50% |
| 4 | 4096 | 4096 | 1 | 64 | 512 | **32,768** | 128 | 16 | 50% |
| 4 | 6144 | 4096 | 1 | 64 | 768 | **49,152** | 192 | 24 | 50% |
| 8 | 128 | 4096 | 1 | 64 | 16 | **1,024** | 4 | 1 | **100%** |
| 8 | 4096 | 4096 | 1 | 64 | 512 | **32,768** | 128 | 16 | **100%** |
| 64 | 128 | 4096 | 8 | 64 | 16 | **8,192** | 4 | 1 | **100%** |
| 64 | 4096 | 4096 | 8 | 64 | 512 | **262,144** | 128 | 16 | **100%** |
| 256 | 4096 | 4096 | 32 | 64 | 512 | **1,048,576** | 128 | 16 | **100%** |

**FP8 (K\_step = 16, 8192 MACs/step):**

| M | K | N | M\_blk | N\_blk | K\_steps | **Total steps** | MAC util |
|---|---|---|--------|--------|---------|----------------|---------|
| 4 | 128 | 4096 | 1 | 64 | 8 | **512** | 50% |
| 4 | 4096 | 4096 | 1 | 64 | 256 | **16,384** | 50% |
| 8 | 4096 | 4096 | 1 | 64 | 256 | **16,384** | **100%** |
| 256 | 4096 | 4096 | 32 | 64 | 256 | **524,288** | **100%** |

**MXFP4 (K\_step = 64, 32768 MACs/step, 1.0 steps/cycle with 4 B ports):**

| M | K | N | M\_blk | N\_blk | K\_steps | **Total steps** | MAC util |
|---|---|---|--------|--------|---------|----------------|---------|
| 4 | 1024 | 4096 | 1 | 64 | 16 | **1,024** | 50% |
| 4 | 4096 | 4096 | 1 | 64 | 64 | **4,096** | 50% |
| 8 | 4096 | 4096 | 1 | 64 | 64 | **4,096** | **100%** |
| 256 | 4096 | 4096 | 32 | 64 | 64 | **131,072** | **100%** |

> MXFP4 Util\_BW = **100%** with 4 B-read ports. MAC util = Util\_M only (M=4 → 50%, M≥8 → 100%).

#### 8.1.4 Tile Register Budget

| Format | K | B tiles (Nb) | A tiles | C (half-tiles) | **Total live tiles** | % of 256 |
|--------|------|-------------|---------|----------------|---------------------|---------|
| FP16 | 256 | 8 | 1 | 1 | **10** | 4% |
| FP16 | 4096 | 128 | 16 | 1 | **145** | 57% |
| FP8 | 4096 | 64 | 8 | 1 | **73** | 29% |
| MXFP4 | 4096 | 32 | 4 | 1 | **37** | 14% |

With double-buffering (ping-pong drain + pre-load next B tiles), multiply by ~1.5×. The worst case (FP16 K=4096, ~218 tiles) fits comfortably within the 256-tile TRegFile.

For K=6144 FP16: Nb=192 + Na=24 = 216 live tiles (84% of 256). Requires careful scheduling with streaming B tiles from memory overlapped with compute.

#### 8.1.5 Instruction Sequences

**Case: M=4, K=128, N=4096, FP16 (Attention Q×Kᵀ)**

```
  Tiles: 1 A tile (partial 8×128 in 8×256), 4 B tiles/N-group, 64 N-groups
  Total OPA steps: 1024,  Cycles: 1024,  MAC util: 50%
```

```asm
; C[4×4096] += A[4×128] × B[128×4096], FP16, Mode A
;
; Tile allocation:
;   T0        : A (8×256, rows 4-7 zero, K=128 of 256 used)
;   T4..T7    : B buffer 0 (4 tiles for current N-group)
;   T8..T11   : B buffer 1 (4 tiles for next N-group, pre-load)
;   T12       : C drain half-tile

    CUBE.CFG  modeA, FP16, Mactive=4
    MOV       r1, #4                        ; Nb = 4 B tiles per N-group
    TILE.LD   T0, [r2]                      ; load A (4 KB, once)

    ; pre-load first B group into buffer 0
    TILE.LD   T4,  [r3 + 0*4096]
    ...
    TILE.LD   T7,  [r3 + 3*4096]

    MOV       r5, #0                        ; j = 0
.loop:
    ; pre-load next B group into buffer 1 (overlapped with compute)
    ADD       r6, r3, r5+1, LSL #14        ; (j+1) × 4 × 4096 = (j+1) << 14
    TILE.LD   T8,  [r6 + 0*4096]
    ...
    TILE.LD   T11, [r6 + 3*4096]

    CUBE.ZERO  z0
    CUBE.OPA   z0, T0, T4, r1              ; 4 B tiles → 16 OPA steps
    CUBE.DRAIN z0, T12
    TILE.ST   [r4 + r5*4096], T12

    ADD       r5, r5, #1
    CMP       r5, #64
    BLT       .loop
    CUBE.WAIT  z0
```

**Performance**: 64 N-groups × 16 OPA steps = 1024 cycles. B load (4 tiles, 1 epoch) fully hidden behind 16-cycle compute.

**Case: M=8, K=4096, N=4096, FP8 (FFN Projection, batch=8)**

```
  Tiles: 8 A tiles (8×512), 64 B tiles/N-group (64×64), 64 N-groups
  Total OPA steps: 16384,  Cycles: 16384,  MAC util: 100%
```

```asm
; C[8×4096] += A[8×4096] × B[4096×4096], FP8, Mode A
;
; Tile allocation:
;   T0..T7     : A tiles (8 tiles, 8×512 FP8 each, covering K=4096)
;   T16..T79   : B tiles (64 tiles for current N-group)
;   T80        : C drain half-tile
; Note: 8 + 64 + 1 = 73 tiles < 256 ✓

    CUBE.CFG  modeA, FP8_E4M3
    MOV       r1, #64

    for a = 0 to 7:
      TILE.LD  T[a], [a_base + a*4096]

    for j = 0 to 63:
      for b = 0 to 63:
        TILE.LD  T[16+b], [b_base + (j*64+b)*4096]

      CUBE.ZERO  z0
      CUBE.OPA   z0, T0, T16, r1           ; 64 B tiles → 256 OPA steps
      CUBE.DRAIN z0, T80
      TILE.ST  [c_base + j*4096], T80

    ; Total: 64 × 256 = 16,384 OPA steps ✓
```

**Performance**: 16,384 cycles at 100% MAC utilization. B load (64 tiles, 8 epochs) overlapped with compute (256 cycles).

**Case: M=4, K=4096, N=4096, MXFP4 (FFN, low precision)**

```asm
; C[4×4096] += A[4×4096] × B[4096×4096], MXFP4, Mode A
; 4 A tiles (8×1024), 32 B tiles/N-group (128×64), 64 N-groups
; Total: 4096 OPA steps = 4096 cycles, 50% MAC util (M=4)

    CUBE.CFG  modeA, MXFP4, Mactive=4
    MOV       r1, #32

    for a = 0 to 3:
      TILE.LD  T[a], [a_base + a*4096]

    for j = 0 to 63:
      for b = 0 to 31:
        TILE.LD  T[16+b], [b_base + (j*32+b)*4096]

      CUBE.ZERO  z0
      CUBE.OPA   z0, T0, T16, r1
      CUBE.DRAIN z0, T48
      TILE.ST  [c_base + j*4096], T48
```

**Performance**: MXFP4 4,096 cycles vs FP16 32,768 cycles → **8.0× speedup**.

**Case: M=64, K=128, N=4096, FP16 (Attention, larger batch)**

```asm
; C[64×4096] += A[64×128] × B[128×4096], FP16, Mode A
; 8 M-blocks, 4 B tiles/N-group (shared), 1 A tile/M-block (partial)
; Total: 8192 OPA steps, MAC util: 100%

    CUBE.CFG  modeA, FP16
    MOV       r1, #4

    for i = 0 to 7:
      TILE.LD  T[i], [a_base + i*4096]

    for j = 0 to 63:
      for b = 0 to 3:
        TILE.LD  T[16+b], [b_base + (j*4+b)*4096]

      for i = 0 to 7:
        CUBE.ZERO  z0
        CUBE.OPA   z0, T[i], T16, r1
        CUBE.DRAIN z0, T24
        TILE.ST  [c_base + (i*64+j)*4096], T24
```

**Case: M=256, K=4096, N=4096, FP16 (Large batch FFN)**

```asm
; C[256×4096] += A[256×4096] × B[4096×4096], FP16, Mode A
; 32 M-blocks, 128 B tiles/N-group (32×64), 16 A tiles/M-block (8×256)
; Total: 1,048,576 OPA steps, MAC util: 100%
; Live tiles: 16 + 128 + 1 = 145 < 256 ✓

    CUBE.CFG  modeA, FP16
    MOV       r1, #128

    for i = 0 to 31:
      for a = 0 to 15:
        TILE.LD  T[a], [a_base + (i*16+a)*4096]

      for j = 0 to 63:
        for b = 0 to 127:
          TILE.LD  T[24+b], [b_base + (j*128+b)*4096]

        CUBE.ZERO  z0
        CUBE.OPA   z0, T0, T24, r1
        CUBE.DRAIN z0, T160
        TILE.ST  [c_base + (i*64+j)*4096], T160
```

**Case: M=4, K=6144, N=4096, FP8 (Large hidden dim)**

```asm
; C[4×4096] += A[4×6144] × B[6144×4096], FP8, Mode A
; A tiles = 12 (8×512), B tiles/N-group = 96 (64×64)
; Live = 12 + 96 + 1 = 109 tiles < 256 ✓
; OPA steps: 64 × 384 = 24,576, MAC util: 50%

    CUBE.CFG  modeA, FP8_E4M3, Mactive=4
    MOV       r1, #96

    for a = 0 to 11:
      TILE.LD  T[a], [a_base + a*4096]

    for j = 0 to 63:
      for b = 0 to 95:
        TILE.LD  T[16+b], [b_base + (j*96+b)*4096]

      CUBE.ZERO  z0
      CUBE.OPA   z0, T0, T16, r1
      CUBE.DRAIN z0, T112
      TILE.ST  [c_base + j*4096], T112
```

### 8.2 CNN Convolution

Convolutions mapped via im2col produce GEMMs: M = H\_out × W\_out (spatial), K = C\_in × kH × kW, N = C\_out.

#### 8.2.1 Typical Dimensions

**Standard convolutions (im2col):**

| Case | Layer | Cin | Cout | H×W out | M | K | N |
|------|-------|-----|------|---------|------|-------|-------|
| C1 | 3×3 first layer | 3 | 64 | 224×224 | 50,176 | 27 | 64 |
| C2 | 3×3 ResNet early | 64 | 64 | 56×56 | 3,136 | 576 | 64 |
| C3 | 3×3 ResNet mid | 128 | 128 | 28×28 | 784 | 1,152 | 128 |
| C4 | 3×3 ResNet deep | 256 | 256 | 14×14 | 196 | 2,304 | 256 |
| C5 | 3×3 ResNet tail | 512 | 512 | 7×7 | 49 | 4,608 | 512 |
| C6 | 1×1 pointwise | 256 | 1024 | 14×14 | 196 | 256 | 1,024 |
| C7 | 1×1 bottleneck | 512 | 2048 | 7×7 | 49 | 512 | 2,048 |

**Depthwise convolutions (per-channel GEMM):**

| Case | Channels | H×W out | M | K | N |
|------|----------|---------|-----|------|---|
| D1 | 256 | 14×14 | 196 | **9** | 1 |
| D2 | 512 | 7×7 | 49 | **9** | 1 |
| D3 | 256 | 14×14 | 196 | **25** | 1 |

#### 8.2.2 Small-K Waste: Mode A vs Mode B

**K utilization by format:**

| K | Mode A FP16 (K\_step=8) | Mode B FP16 (K\_step=1) | Mode A MXFP4 (K\_step=64) | Mode B MXFP4 (K\_step=8) |
|---|------------------------|------------------------|--------------------------|------------------------|
| 9 | 9/16 = **56.3%** | **100%** | 9/64 = **14.1%** | 9/16 = **56.3%** |
| 25 | 25/32 = **78.1%** | **100%** | 25/64 = **39.1%** | 25/32 = **78.1%** |
| 27 | 27/32 = **84.4%** | **100%** | 27/64 = **42.2%** | 27/32 = **84.4%** |
| 576 | **100%** | **100%** | **100%** | **100%** |

**Mode B M-dimension cost (M < 64):**

| M | Mode A ⌈M/8⌉ | M-util A | Mode B ⌈M/64⌉ | M-util B |
|------|-------------|---------|--------------|---------|
| 49 | 7 | 87.5% | 1 | 76.6% |
| 196 | 25 | 98% | 4 | 76.6% |
| 3136 | 392 | 100% | 49 | 100% |
| 50176 | 6272 | 100% | 785 | 99.9% |

#### 8.2.3 Standard Convolution Analysis (FP16, Mode A)

| Case | M×K×N | M\_blk | N\_blk | K\_steps | Total steps | MAC util |
|------|-------|--------|--------|---------|------------|---------|
| C1 | 50176×27×64 | 6,272 | 1 | 4 | **25,088** | **84.4%** |
| C2 | 3136×576×64 | 392 | 1 | 72 | **28,224** | **100%** |
| C3 | 784×1152×128 | 98 | 2 | 144 | **28,224** | **100%** |
| C4 | 196×2304×256 | 25 | 4 | 288 | **28,800** | **98%** |
| C5 | 49×4608×512 | 7 | 8 | 576 | **32,256** | **87.5%** |
| C6 | 196×256×1024 | 25 | 16 | 32 | **12,800** | **98%** |
| C7 | 49×512×2048 | 7 | 32 | 64 | **14,336** | **87.5%** |

#### 8.2.4 Mode A vs Mode B Comparison (FP16)

| Case | M×K×N | Mode A steps | Mode B steps | Winner | Speedup |
|------|-------|-------------|-------------|--------|---------|
| C1 | 50176×27×64 | **25,088** | **21,195** | **B (1.18×)** | K-waste eliminated |
| C2 | 3136×576×64 | **28,224** | **28,224** | **tie** | K aligned |
| C4 | 196×2304×256 | **28,800** | **36,864** | **A (1.28×)** | M-waste in B |
| C5 | 49×4608×512 | **32,256** | **36,864** | **A (1.14×)** | M-waste in B |
| C6 | 196×256×1024 | **12,800** | **16,384** | **A (1.28×)** | M-waste in B |

> Mode B wins for large M, small K (C1: M=50176, K=27). Mode A wins when K is aligned or M is not a good multiple of 64.

#### 8.2.5 Depthwise Convolution

| Case | M×K×N | Mode A steps | Mode B steps | MAC util (best) |
|------|-------|-------------|-------------|----------------|
| D1 | 196×9×1 | 50 | 36 | **1.20%** |
| D2 | 49×9×1 | 14 | 9 | **1.20%** |
| D3 | 196×25×1 | 100 | 100 | **1.20%** |

> **DO NOT use the CUBE for depthwise convolution.** At ~1.2% MAC utilization (N=1 wastes 63 of 64 columns), depthwise conv is > 80× more efficiently handled by the **vector unit**.

#### 8.2.6 Convolution Instruction Sequences

**C4 (3×3, Cin=256, Cout=256, 14×14), Mode A:**

```asm
; GEMM: C[196×256] += A[196×2304] × B[2304×256], FP16, Mode A
; M_blk=25, N_blk=4, K_steps=288, Nb=72 B tiles per N-block
; Total: 28,800 OPA steps, MAC util ≈ 98%
;
; Tile allocation: A: 9, B: 72, C: 1 → 82 tiles (32% of TRegFile)

    CUBE.CFG  modeA, FP16
    MOV       r1, #72

    for i = 0 to 24:                          ; 25 M-blocks
      for a = 0 to 8:
        TILE.LD  T[a], [a_base + (i*9+a)*4096]

      for j = 0 to 3:                        ; 4 N-blocks
        for b = 0 to 71:
          TILE.LD  T[12+b], [b_base + (j*72+b)*4096]

        CUBE.ZERO  z0
        CUBE.OPA   z0, T0, T12, r1           ; 288 OPA steps
        CUBE.DRAIN z0, T84
        TILE.ST  [c_base + (i*4+j)*4096], T84

    ; B load: 72 tiles via W1–W7, Compute: 288 cy → load overlapped ✓
```

**C1 with Mode B (3×3, Cin=3, Cout=64, 224×224):**

```asm
; GEMM: C[50176×64] += A[50176×27] × B[27×64], FP16, Mode B
; 785 M-super-blocks, 1 N-block, K=27 steps
; Total: 21,195 OPA steps (vs Mode A 25,088 → 1.18× faster)
;
; Tile allocation: A: 8, B: 1 (K=27 ≤ 32), C: 4 → 13 tiles (5% of TRegFile)

    CUBE.CFG  modeB, FP16
    MOV       r1, #1                          ; Nb = 1 (covers K=27)

    TILE.LD  T100, [b_base + 0*4096]

    for i = 0 to 784:                         ; 785 M-super-blocks
      for bank = 0 to 7:
        TILE.LD  T[bank], [a_base + (i*8+bank)*4096]

      CUBE.ZERO  z0
      CUBE.OPA   z0, T0, T100, r1
      CUBE.DRAIN z0, T110

      for t = 0 to 3:
        TILE.ST  [c_base + (i*4+t)*4096], T[110+t]

    ; B tile loaded once, reused across all 785 M-super-blocks
    ; A load: 1 epoch per bank, Compute: 27 cy → load overlapped ✓
```

#### 8.2.7 Convolution Summary

```
  Standard conv (K = Cin × kH × kW):
  ┌──────────────────────────────────────────────────────────────────────────┐
  │ K ≥ 576 (Cin ≥ 64, 3×3): Mode A, MAC util 87–100%                      │
  │                            Mode B ties but adds M-waste for M < 64      │
  │                                                                        │
  │ K = 27  (Cin = 3, 3×3):  Mode A: 84.4% util (K-ceiling waste)          │
  │                           Mode B: ~100% util (1.18× faster) if M ≥ 3136 │
  │                                                                        │
  │ K = 9   (Cin = 1, 3×3):  Mode A: 56.3% util (K-ceiling waste)          │
  │                           Mode B: ~100% util (1.78× faster) if M ≥ 64   │
  └──────────────────────────────────────────────────────────────────────────┘

  Mode selection rule:
  ┌──────────────────────────────────────────────────────────────────────────┐
  │ Use Mode B when: M ≥ 64 AND K < K_step_A AND K is not aligned          │
  │ Use Mode A otherwise (transformer decode, deep CNN layers, pointwise)   │
  └──────────────────────────────────────────────────────────────────────────┘

  Depthwise conv (K = kH × kW, N = 1 per channel):
  ┌──────────────────────────────────────────────────────────────────────────┐
  │ MAC util < 1.2% on CUBE — DO NOT USE                                    │
  │ Route to vector unit for 80×+ better efficiency                          │
  └──────────────────────────────────────────────────────────────────────────┘

  1×1 pointwise conv (K = Cin):
  ┌──────────────────────────────────────────────────────────────────────────┐
  │ K ≥ 256: Mode A, MAC util 87–100%                                       │
  │ Identical to transformer projection layers — CUBE sweet spot             │
  └──────────────────────────────────────────────────────────────────────────┘
```

### 8.3 Summary

```
┌────────────────────────────────────────────────────────────────────────────────┐
│  CUBE Dual-Mode Performance Summary                                             │
│                                                                                │
│  Mode A (K-parallel):  output 8×64,  K_step = 8×K_mac                         │
│  Mode B (M-parallel):  output 64×64, K_step = K_mac, no broadcast             │
│                                                                                │
│  ═══════════════════════════════════════════════════════════════════════════    │
│  Transformer Decode (M=4–256, K=128–6144, N=1024–4096):                       │
│  ═══════════════════════════════════════════════════════════════════════════    │
│  • MODE: Mode A always preferred (K aligned, small M penalizes Mode B)        │
│  • M ≥ 8: 100% util (all formats); M = 4: 50%                                │
│  • FP8: 2.0× speedup; MXFP4: 8.0× speedup (4 B-ports, vs FP16)              │
│                                                                                │
│  ═══════════════════════════════════════════════════════════════════════════    │
│  CNN Convolution (M=196–50176, K=9–4608, N=64–2048):                          │
│  ═══════════════════════════════════════════════════════════════════════════    │
│  • K ≥ 576 (deep layers): Mode A — K aligned, no waste                        │
│  • K < 64, M ≥ 64 (early layers): Mode B — eliminates K-waste                │
│  • Depthwise (N=1): DO NOT USE CUBE — route to vector unit                    │
│                                                                                │
│  ═══════════════════════════════════════════════════════════════════════════    │
│  Hardware cost of dual-mode:                                                   │
│  • Accumulator SRAM: 32 KB total (Mode B: 16 KB × 2 ping-pong)               │
│  • Adder tree bypass: 512 MUXes                                               │
│  • No B broadcast MUX (same B TRegFile path for both modes)                   │
│                                                                                │
│  Effective throughput @ 1.5 GHz (4 B-read ports, TRegFile-4K):                 │
│     FP16:  12.3 TFLOPS  (100%)                                                │
│     FP8:   24.6 TOPS    (100%)                                                │
│     MXFP4: 98.3 TOPS    (100%)                                                │
└────────────────────────────────────────────────────────────────────────────────┘
```

---

## Appendix A: Design Rationale

### A.1 Why 8 × 64 × 8 Banks?

Given 4096 base MACs, M × N = 4096. The choice of M=8, N=64, Banks=8:

**Bandwidth analysis (16-bit formats):**

| Config | M | N | A+B bandwidth (B/cyc) | Compute/BW ratio |
|--------|---|---|----------------------|-----------------|
| D (8×512) | 8 | 512 | 1040 | 3.9 |
| E (16×256) | 16 | 256 | 544 | 7.5 |
| F (32×128) | 32 | 128 | 320 | 12.8 |
| **G (64×64)** | **64** | **64** | **256** | **16.0** |

> A+B bandwidth = 2(M+N), minimized at M=N=64.

We implement the 64×64 effective array as **8 rows × 64 cols × 8 banks** rather than a monolithic 64×64:

```
  Why M=8?
    - Port A: 8 rows = 8 A elements from GPR or TILE RF
    - 8 rows × 8 banks → Mode B effective M=64
    - Power-of-2 SIMD-friendly value

  Why N=64?
    - 64 cols × 2B(FP16) = 128 B/bank, 8 banks = 1024 B → TRegFile single port
    - N=64 is the minimum for efficient N-tiling

  Why 8 Banks?
    - Mode A: 8 banks → K_step = 8 (FP16), 16 (FP8), 64 (MXFP4)
    - Mode B: 8 banks → M coverage = 64 rows
    - 3-stage adder tree (vs 4-stage for 16 banks)
```

**Accumulator storage:**

| Mode | Active positions | Size | Ping-pong total |
|------|-----------------|------|----------------|
| Mode A | 8 × 64 = 512 | 512 × 4B = **2 KB** | 4 KB |
| Mode B | 8 × (8×64) = 4096 | 4096 × 4B = **16 KB** | 32 KB |

### A.2 Architecture Comparison: 8×64×8 vs 4×128×8 vs 2×128×16

All three contain 4096 base MACs with identical peak throughput.

```
 Config A: 8 × 64 × 8     Config B: 4 × 128 × 8     Config C: 2 × 128 × 16
 M=8, N=64, Banks=8         M=4, N=128, Banks=8         M=2, N=128, Banks=16
```

**Structural comparison:**

| Parameter | 8×64×8 (A) | 4×128×8 (B) | 2×128×16 (C) |
|-----------|-----------|------------|-------------|
| Rows per bank (M\_tile) | **8** | 4 | 2 |
| Cols per bank (N\_tile) | 64 | **128** | **128** |
| Banks | 8 | 8 | **16** |
| Mode A K/step (FP16) | 8 | 8 | **16** |
| Mode B output tile | 64×64 | 32×128 | 32×128 |
| B demand/step (FP16) | **1024 B** (2 TRF ports) | 2048 B (4 ports) | 4096 B (8 ports) |
| B demand/step (MXFP4) | **2048 B** (4 ports) | 4096 B (8 ports) | 8192 B (16 ports!) |
| Adder tree depth | 3 stages | 3 stages | **4 stages** |
| Tree extra latency | +1 | +1 | **+2** |

**Cycle count comparison (FP16, Mode A):**

| GEMM (M×K×N) | 8×64×8 | 4×128×8 | 2×128×16 | Winner |
|--------------|--------|---------|----------|--------|
| 256×256×256 | **4096** | **4096** | **4096** | tie |
| 1×256×256 | **128** | **64** | **32** | **C (4×)** |
| 1×4096×4096 | **32768** | **16384** | **8192** | **C (4×)** |
| 256×256×4 | **1024** | **2048** | **2048** | **A (2×)** |
| 256×256×64 | **1024** | **2048** | **2048** | **A (2×)** |
| 256×1×256 | **128** | **128** | **256** | A=B |

**Winner map:**

```
 ┌────────────────────────────────────────────────────────────────────────┐
 │               ▲ N                                                      │
 │               │                                                        │
 │   Small M,    │   Large M, Large N:                                    │
 │   Large N:    │   ALL CONFIGS TIE                                      │
 │   Config C    │                                                        │
 │   wins        │                                                        │
 │  ─────────────┼────────────────────────────▶ M                        │
 │   Small M,    │   Large M, Small N:                                    │
 │   Small N:    │   Config A (8×64×8) wins                               │
 │   Config C    │                                                        │
 │               ▼ K ────▶                                                │
 │                 Small K: A=B win (8-bank K waste < 16-bank)            │
 │                 Large K: C wins if M small (more K parallelism)        │
 └────────────────────────────────────────────────────────────────────────┘
```

**Pros & cons summary:**

| Config | Pros | Cons |
|--------|------|------|
| **A (8×64×8)** | Largest M\_tile (8); lowest B demand (1024 B, 2 TRF ports); shallow tree | Smallest N\_tile (64); M=1 wastes 7/8 rows |
| **B (4×128×8)** | 2× N\_tile vs A; lower A demand (64 B) | Not best at anything specific |
| **C (2×128×16)** | 2× K throughput; best for batch-1 | B = 4096 B (needs 8 TRF read ports!); 4-stage tree; small K ceiling waste |

**Workload recommendation:**

| Workload | Best config | Why |
|----------|------------|-----|
| Large batch training | **A = B = C** (tie) | Perfectly tiled |
| Small batch inference (M=1–4) | **C** | 2× K throughput, fewer wasted rows |
| Attention QKᵀ (M=8, K=64–128) | **A** | M=8 maps perfectly |
| Depthwise/pointwise conv | **A or B** | Small K penalizes C (mod 16 waste) |
| Tall skinny (M=256, N=8) | **A** | Smallest N\_tile, least waste |

**Final choice: 8 × 64 × 8 banks** — most balanced in B-operand TRegFile port demand (only 2 ports for FP16), adder tree depth, and large-M utilization. If workload is primarily batch-1 inference (M=1–2), 2×128×16 is superior but needs 8 TRegFile read ports just for B.

### A.3 Functional Requirements

#### R-MXU-001: Multi-Format Outer Product Accumulate (OPA)

The fundamental operation is: `ACC += A_vec ⊗ B_vec`

| Input format | Accum format | MACs/cycle | K consumed/step (Mode A) |
|-------------|-------------|-----------|-------------------------|
| FP16 | FP32 | 4,096 | 8 |
| BF16 | FP32 | 4,096 | 8 |
| FP8 (E4M3/E5M2) | FP32 | 8,192 | 16 |
| MXFP4 | FP32 | 32,768 | 64 |
| HiFP4 | FP32 | 32,768 | 64 |

#### R-MXU-002: Dual-Mode Operation

- **Mode A (K-parallel)**: 8 banks each process different K-indices. Adder tree reduces 8 partial products → one shared 8×64 output tile.
- **Mode B (M-parallel)**: 8 banks each process different M-blocks of A with the same B data (no broadcast MUX). Each bank has independent accumulator → effective 64×64 output tile.

#### R-MXU-003: Sparsity Support

- Optional **structured sparsity** (2:4 or 4:8 patterns) for FP8 and MXFP4 modes.
- Sparse metadata (bitmask) accompanies the A vector; the OPA skips zero-element multiplications.

### A.4 Outer-Loop Pseudo Code

```text
# C[M][N] += A[M][K] × B[K][N]    (all formats, 32-bit FP32 accumulators)
#
# K_mac = 1 (FP16/BF16), 2 (FP8), 8 (MXFP4/HiFP4)

# --- Mode A: K-parallel across 8 banks ---
for i0 in range(0, M, 8):
  for j0 in range(0, N, 64):
    zero ACC[8][64]                                       # FP32
    for k_base in range(0, K, 8 * K_mac):                # K_step = 8 × K_mac
      for b in 0..7:                                      # bank index
        for r in 0..7:                                    # row
          for c in 0..63:                                 # column
            for d in 0..K_mac-1:                          # dot-product depth
              kk = k_base + b * K_mac + d
              if kk < K:
                partial[b][r][c] += A[i0+r][kk] * B[kk][j0+c]
      # inter-bank reduction (adder tree)
      for r in 0..7:
        for c in 0..63:
          ACC[r][c] += Σ(partial[b][r][c] for b=0..7)    # FP32 accumulate
    drain ACC → C[i0:i0+8, j0:j0+64]

# --- Mode B: M-parallel, independent banks ---
for i0 in range(0, M, 64):                                # 8 banks × 8 rows = 64
  for j0 in range(0, N, 64):
    for b in 0..7: zero ACC_b[8][64]                       # FP32
    for k_base in range(0, K, K_mac):
      for b in 0..7:                                       # bank → M-block
        for r in 0..7:                                     # row within bank
          for c in 0..63:                                  # column
            for d in 0..K_mac-1:                           # dot-product depth
              kk = k_base + d
              if kk < K:
                ACC_b[r][c] += A[i0 + 8*b + r][kk] * B[kk][j0 + c]
    for b in 0..7:
      drain ACC_b → C[i0+8*b : i0+8*(b+1), j0 : j0+64]
```

> This micro-architecture-level pseudo code expands all spatial parallel dimensions. The §6 ISA pseudo code folds spatial dimensions into `CUBE.OPA` and adds data movement (`TILE.LD`) and control (`HWLOOP`, `CUBE.DRAIN`) instructions. Both correspond one-to-one in loop structure and data ranges.
