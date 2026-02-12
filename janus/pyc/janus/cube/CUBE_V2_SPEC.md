# Cube v2 Architecture Specification

> Matrix Multiplication Accelerator with MATMUL Block Instruction Support

---

## 1. Overview

Cube v2 is a matrix multiplication accelerator that supports large matrix operations through tiled computation. It receives MATMUL block instructions and decomposes them into micro-operations (uops) that can be executed on the 16×16 systolic array.

### 1.1 Key Features

- **MATMUL Block Instruction**: Supports A[M×K] × B[K×N] = C[M×N] operations
- **Tiled Computation**: Large matrices decomposed into 16×16 uops
- **Out-of-Order Execution**: 64-entry issue queue for uop scheduling
- **Triple Buffering**: L0A (left matrix), L0B (right matrix), ACC (accumulator)
- **64-bit MMIO**: Simplified interface for C++ emitter compatibility

### 1.2 Specifications Summary

| Parameter | Value |
|-----------|-------|
| Systolic Array Size | 16 × 16 (256 PEs) |
| PE Clusters | 4 clusters (4-stage pipeline) |
| PEs per Cluster | 16 × 4 = 64 PEs |
| Throughput | 1 uop/cycle (after pipeline fill) |
| Pipeline Latency | 4 cycles |
| L0A Buffer | 64 entries × 16×16 × 16-bit |
| L0B Buffer | 64 entries × 16×16 × 16-bit |
| ACC Buffer | 64 entries × 16×16 × 32-bit |
| Issue Queue | 64 entries |
| Data Width (Input) | 16-bit |
| Data Width (Output) | 32-bit |
| MMIO Read Bandwidth | 64-bit/cycle |
| MMIO Write Bandwidth | 64-bit/cycle |

### 1.3 Code Generation Optimization

通过模块复用 (`@module` + `m.instance()`) 大幅减少生成代码量：

| 阶段 | Verilog 大小 | 代码行数 | 模块数 | 减少比例 |
|------|-------------|----------|--------|----------|
| 原始 (无复用) | 48MB | 1,082,274 | 1 | - |
| PE 模块复用 | 45MB | ~1M | 2 | 6% |
| PE + L0 模块复用 | 22MB | 506,117 | 3 | 54% |

生成的模块结构：
```
janus_cube_pyc (顶层)
├── L0Entry × 128 (64 L0A + 64 L0B entries)
└── CubePE × 256 (4 clusters × 64 PEs)
```

---

## 2. Architecture Block Diagram

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              CUBE v2 Architecture                               │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                 │
│  ┌─────────────────────────────────────────────────────────────────────────┐   │
│  │                        MATMUL Instruction Decoder                        │   │
│  │                                                                          │   │
│  │   MATMUL(M, K, N) ──► Decompose into uops ──► Issue Queue (64 entries)  │   │
│  │                                                                          │   │
│  │   uop = { l0a_idx, l0b_idx, acc_idx, is_first, is_last }                │   │
│  │                                                                          │   │
│  └─────────────────────────────────────────────────────────────────────────┘   │
│                                      │                                          │
│                                      ▼                                          │
│  ┌─────────────────────────────────────────────────────────────────────────┐   │
│  │                          Issue Queue (64 entries)                        │   │
│  │                                                                          │   │
│  │   ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐                    │   │
│  │   │ uop │ uop │ uop │ uop │ ... │ uop │ uop │ uop │                    │   │
│  │   │  0  │  1  │  2  │  3  │     │ 61  │ 62  │ 63  │                    │   │
│  │   └──┬──┴──┬──┴──┬──┴──┬──┴─────┴──┬──┴──┬──┴──┬──┘                    │   │
│  │      │     │     │     │           │     │     │                        │   │
│  │      └─────┴─────┴─────┴───────────┴─────┴─────┘                        │   │
│  │                          │                                               │   │
│  │                    Ready Check (L0A & L0B data available?)              │   │
│  │                          │                                               │   │
│  │                          ▼                                               │   │
│  │                    Issue to Systolic Array                              │   │
│  │                                                                          │   │
│  └─────────────────────────────────────────────────────────────────────────┘   │
│                                      │                                          │
│         ┌────────────────────────────┼────────────────────────────┐            │
│         │                            │                            │            │
│         ▼                            ▼                            ▼            │
│  ┌─────────────────┐   ┌─────────────────────────┐   ┌─────────────────┐      │
│  │      L0A        │   │    16×16 Systolic       │   │      L0B        │      │
│  │  (Left Matrix)  │   │        Array            │   │  (Right Matrix) │      │
│  │                 │   │                         │   │                 │      │
│  │  64 entries     │──►│   A_tile × B_tile       │◄──│  64 entries     │      │
│  │  16×16×16-bit   │   │         ↓               │   │  16×16×16-bit   │      │
│  │  each           │   │    C_tile (partial)     │   │  each           │      │
│  │                 │   │                         │   │                 │      │
│  └────────┬────────┘   └───────────┬─────────────┘   └────────┬────────┘      │
│           │                        │                          │               │
│           │                        ▼                          │               │
│           │            ┌─────────────────────────┐            │               │
│           │            │         ACC             │            │               │
│           │            │    (Accumulator)        │            │               │
│           │            │                         │            │               │
│           │            │    64 entries           │            │               │
│           │            │    16×16×32-bit each    │            │               │
│           │            │                         │            │               │
│           │            │  acc[idx] += C_tile     │            │               │
│           │            │  (when is_last: final)  │            │               │
│           │            │                         │            │               │
│           │            └───────────┬─────────────┘            │               │
│           │                        │                          │               │
│           ▼                        ▼                          ▼               │
│  ┌─────────────────────────────────────────────────────────────────────────┐   │
│  │                         MMIO Interface                                   │   │
│  │                                                                          │   │
│  │   LOAD L0A:  64 requests × 64-bit = 4096-bit (16×16×16-bit)             │   │
│  │   LOAD L0B:  64 requests × 64-bit = 4096-bit (16×16×16-bit)             │   │
│  │   STORE ACC: 128 requests × 64-bit = 8192-bit (16×16×32-bit)            │   │
│  │                                                                          │   │
│  └─────────────────────────────────────────────────────────────────────────┘   │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. MATMUL Instruction Format

### 3.1 Instruction Fields

```
MATMUL M, K, N, addr_A, addr_B, addr_C

Fields:
  M      : Number of rows in left matrix A (and result C)
  K      : Number of columns in A / rows in B (reduction dimension)
  N      : Number of columns in right matrix B (and result C)
  addr_A : Base address of matrix A in memory
  addr_B : Base address of matrix B in memory
  addr_C : Base address of result matrix C in memory
```

### 3.2 Matrix Dimensions

```
    Matrix A          Matrix B          Matrix C
    [M × K]           [K × N]           [M × N]

    ┌─────────┐       ┌─────────┐       ┌─────────┐
    │         │       │         │       │         │
    │    A    │   ×   │    B    │   =   │    C    │
    │         │       │         │       │         │
    └─────────┘       └─────────┘       └─────────┘
      M rows            K rows            M rows
      K cols            N cols            N cols
```

---

## 4. Uop Decomposition

### 4.1 Tiling Strategy

Large matrices are decomposed into 16×16 tiles for processing on the systolic array.

```
Number of tiles:
  M_tiles = ceil(M / 16)
  K_tiles = ceil(K / 16)
  N_tiles = ceil(N / 16)

Total uops = M_tiles × K_tiles × N_tiles
```

### 4.2 Uop Structure

```
struct Uop {
    uint8_t  l0a_idx;     // Index into L0A buffer (0-63)
    uint8_t  l0b_idx;     // Index into L0B buffer (0-63)
    uint8_t  acc_idx;     // Index into ACC buffer (0-63)
    bool     is_first;    // First uop for this ACC entry (clear accumulator)
    bool     is_last;     // Last uop for this ACC entry (result ready)
    uint8_t  m_tile;      // Tile index in M dimension
    uint8_t  k_tile;      // Tile index in K dimension
    uint8_t  n_tile;      // Tile index in N dimension
};
```

### 4.3 Uop Generation Example

For MATMUL with M=32, K=48, N=32:

```
M_tiles = ceil(32/16) = 2
K_tiles = ceil(48/16) = 3
N_tiles = ceil(32/16) = 2

Total uops = 2 × 3 × 2 = 12 uops

Uop sequence (for one output tile C[0,0]):
  uop0: A[0,0] × B[0,0] → ACC[0] (is_first=1, is_last=0)
  uop1: A[0,1] × B[1,0] → ACC[0] (is_first=0, is_last=0)
  uop2: A[0,2] × B[2,0] → ACC[0] (is_first=0, is_last=1)

  C[0,0] = A[0,0]×B[0,0] + A[0,1]×B[1,0] + A[0,2]×B[2,0]
```

---

## 5. Buffer Specifications

### 5.1 L0A Buffer (Left Matrix Input)

```
┌─────────────────────────────────────────────────────────────────┐
│                         L0A Buffer                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Entries:     128                                               │
│  Entry Size:  16 × 16 × 16-bit = 4096 bits = 512 bytes         │
│  Total Size:  128 × 512 = 65536 bytes = 64 KB                  │
│                                                                 │
│  Entry Layout (row-major):                                      │
│  ┌────────────────────────────────────────────────────────────┐│
│  │ a[0,0]  a[0,1]  a[0,2]  ... a[0,15]  │  Row 0 (256 bits)  ││
│  │ a[1,0]  a[1,1]  a[1,2]  ... a[1,15]  │  Row 1             ││
│  │ ...                                   │  ...               ││
│  │ a[15,0] a[15,1] a[15,2] ... a[15,15] │  Row 15            ││
│  └────────────────────────────────────────────────────────────┘│
│                                                                 │
│  Status bits per entry:                                         │
│    valid    : Data loaded and ready                            │
│    loading  : Load in progress                                 │
│    ref_count: Number of pending uops using this entry          │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 5.2 L0B Buffer (Right Matrix Input)

```
┌─────────────────────────────────────────────────────────────────┐
│                         L0B Buffer                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Entries:     128                                               │
│  Entry Size:  16 × 16 × 16-bit = 4096 bits = 512 bytes         │
│  Total Size:  128 × 512 = 65536 bytes = 64 KB                  │
│                                                                 │
│  Entry Layout (column-major for efficient streaming):           │
│  ┌────────────────────────────────────────────────────────────┐│
│  │ b[0,0]  b[1,0]  b[2,0]  ... b[15,0]  │  Col 0 (256 bits)  ││
│  │ b[0,1]  b[1,1]  b[2,1]  ... b[15,1]  │  Col 1             ││
│  │ ...                                   │  ...               ││
│  │ b[0,15] b[1,15] b[2,15] ... b[15,15] │  Col 15            ││
│  └────────────────────────────────────────────────────────────┘│
│                                                                 │
│  Status bits per entry:                                         │
│    valid    : Data loaded and ready                            │
│    loading  : Load in progress                                 │
│    ref_count: Number of pending uops using this entry          │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 5.3 ACC Buffer (Accumulator Output)

```
┌─────────────────────────────────────────────────────────────────┐
│                         ACC Buffer                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Entries:     128                                               │
│  Entry Size:  16 × 16 × 32-bit = 8192 bits = 1024 bytes        │
│  Total Size:  128 × 1024 = 131072 bytes = 128 KB               │
│                                                                 │
│  Entry Layout:                                                  │
│  ┌────────────────────────────────────────────────────────────┐│
│  │ c[0,0]  c[0,1]  c[0,2]  ... c[0,15]  │  Row 0 (512 bits)  ││
│  │ c[1,0]  c[1,1]  c[1,2]  ... c[1,15]  │  Row 1             ││
│  │ ...                                   │  ...               ││
│  │ c[15,0] c[15,1] c[15,2] ... c[15,15] │  Row 15            ││
│  └────────────────────────────────────────────────────────────┘│
│                                                                 │
│  Status bits per entry:                                         │
│    valid      : Result ready for store                         │
│    computing  : Computation in progress                        │
│    pending_k  : Number of remaining K-dimension uops           │
│    storing    : Store in progress                              │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 6. Issue Queue

### 6.1 Structure

```
┌─────────────────────────────────────────────────────────────────┐
│                      Issue Queue (64 entries)                   │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Entry Structure:                                               │
│  ┌────────────────────────────────────────────────────────────┐│
│  │  valid     : 1 bit   - Entry contains valid uop            ││
│  │  l0a_idx   : 6 bits  - L0A buffer index (0-63)             ││
│  │  l0b_idx   : 6 bits  - L0B buffer index (0-63)             ││
│  │  acc_idx   : 6 bits  - ACC buffer index (0-63)             ││
│  │  is_first  : 1 bit   - Clear ACC before accumulate         ││
│  │  is_last   : 1 bit   - Mark ACC as complete after          ││
│  │  l0a_ready : 1 bit   - L0A data available                  ││
│  │  l0b_ready : 1 bit   - L0B data available                  ││
│  │  acc_ready : 1 bit   - ACC available for write             ││
│  │  issued    : 1 bit   - Uop has been issued                 ││
│  └────────────────────────────────────────────────────────────┘│
│                                                                 │
│  Issue Logic (Out-of-Order):                                    │
│  ─────────────────────────────                                  │
│  for each entry in queue:                                       │
│    if (valid && !issued && l0a_ready && l0b_ready && acc_ready):│
│      issue_to_systolic_array(entry)                            │
│      entry.issued = 1                                          │
│                                                                 │
│  Priority: Lower index has higher priority (FIFO-like)         │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 6.2 Ready Check Logic

```
l0a_ready = L0A[uop.l0a_idx].valid
l0b_ready = L0B[uop.l0b_idx].valid
acc_ready = !ACC[uop.acc_idx].computing ||
            (ACC[uop.acc_idx].computing && current_uop_for_acc_done)
```

---

## 7. Pipelined Systolic Array Architecture

### 7.1 4-Stage PE Cluster Pipeline

The systolic array is organized as 4 PE Clusters in a pipeline configuration, enabling 1 uop/cycle throughput after initial pipeline fill.

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                    4-Stage Pipelined Systolic Array                             │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                 │
│  L0A (16×16)                                              L0B (16×16)          │
│  ┌─────────┐                                              ┌─────────┐          │
│  │ Row 0-3 │──┐                                      ┌────│ All 16  │          │
│  │ Row 4-7 │──┼──┐                                   │    │ columns │          │
│  │ Row 8-11│──┼──┼──┐                                │    └─────────┘          │
│  │Row12-15 │──┼──┼──┼──┐                             │                         │
│  └─────────┘  │  │  │  │                             │                         │
│               │  │  │  │                             │                         │
│               ▼  │  │  │                             ▼                         │
│  ┌────────────────────────────────────────────────────────────────────────┐   │
│  │                        PE Cluster 0 (Stage 0)                          │   │
│  │                                                                        │   │
│  │   ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐   │
│  │   │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │   │
│  │   │0,0 │0,1 │0,2 │0,3 │0,4 │0,5 │0,6 │0,7 │0,8 │0,9 │0,10│0,11│0,12│0,13│0,14│0,15│   │
│  │   ├────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┤   │
│  │   │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │   │
│  │   │1,0 │1,1 │1,2 │1,3 │1,4 │1,5 │1,6 │1,7 │1,8 │1,9 │1,10│1,11│1,12│1,13│1,14│1,15│   │
│  │   ├────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┤   │
│  │   │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │   │
│  │   │2,0 │2,1 │2,2 │2,3 │2,4 │2,5 │2,6 │2,7 │2,8 │2,9 │2,10│2,11│2,12│2,13│2,14│2,15│   │
│  │   ├────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┤   │
│  │   │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │PE  │   │
│  │   │3,0 │3,1 │3,2 │3,3 │3,4 │3,5 │3,6 │3,7 │3,8 │3,9 │3,10│3,11│3,12│3,13│3,14│3,15│   │
│  │   └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘   │
│  │                                                                        │   │
│  │   Rows 0-3: 4 rows × 16 cols = 64 PEs                                 │   │
│  │   Each PE: 16-bit × 16-bit MAC → 32-bit partial sum                   │   │
│  │                                                                        │   │
│  └────────────────────────────────┬───────────────────────────────────────┘   │
│                                   │ Partial sums (4×16 = 64 × 32-bit)         │
│                                   ▼                                           │
│  ┌────────────────────────────────────────────────────────────────────────┐   │
│  │                        PE Cluster 1 (Stage 1)                          │   │
│  │                                                                        │   │
│  │   Rows 4-7: 4 rows × 16 cols = 64 PEs                                 │   │
│  │   Input: Partial sums from Stage 0 + L0A rows 4-7 + L0B all cols      │   │
│  │                                                                        │   │
│  └────────────────────────────────┬───────────────────────────────────────┘   │
│                                   │                                           │
│                                   ▼                                           │
│  ┌────────────────────────────────────────────────────────────────────────┐   │
│  │                        PE Cluster 2 (Stage 2)                          │   │
│  │                                                                        │   │
│  │   Rows 8-11: 4 rows × 16 cols = 64 PEs                                │   │
│  │   Input: Partial sums from Stage 1 + L0A rows 8-11 + L0B all cols     │   │
│  │                                                                        │   │
│  └────────────────────────────────┬───────────────────────────────────────┘   │
│                                   │                                           │
│                                   ▼                                           │
│  ┌────────────────────────────────────────────────────────────────────────┐   │
│  │                        PE Cluster 3 (Stage 3)                          │   │
│  │                                                                        │   │
│  │   Rows 12-15: 4 rows × 16 cols = 64 PEs                               │   │
│  │   Input: Partial sums from Stage 2 + L0A rows 12-15 + L0B all cols    │   │
│  │   Output: Complete 16×16 result tile → ACC buffer                     │   │
│  │                                                                        │   │
│  └────────────────────────────────┬───────────────────────────────────────┘   │
│                                   │                                           │
│                                   ▼                                           │
│                          ┌─────────────────┐                                  │
│                          │   ACC Buffer    │                                  │
│                          │  (16×16×32-bit) │                                  │
│                          └─────────────────┘                                  │
│                                                                               │
└───────────────────────────────────────────────────────────────────────────────┘
```

### 7.2 Pipeline Timing

```
Cycle:    0     1     2     3     4     5     6     7     8    ...
          │     │     │     │     │     │     │     │     │
          ▼     ▼     ▼     ▼     ▼     ▼     ▼     ▼     ▼

uop0:   [C0]──[C1]──[C2]──[C3]──►ACC
uop1:         [C0]──[C1]──[C2]──[C3]──►ACC
uop2:               [C0]──[C1]──[C2]──[C3]──►ACC
uop3:                     [C0]──[C1]──[C2]──[C3]──►ACC
uop4:                           [C0]──[C1]──[C2]──[C3]──►ACC
...

Legend:
  [C0] = PE Cluster 0 processing
  [C1] = PE Cluster 1 processing
  [C2] = PE Cluster 2 processing
  [C3] = PE Cluster 3 processing
  ►ACC = Write to ACC buffer

Pipeline characteristics:
  - Latency: 4 cycles (from uop issue to ACC write)
  - Throughput: 1 uop/cycle (after pipeline fill)
  - In-flight uops: Up to 4 (one per stage)
```

### 7.3 PE Cluster Data Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         PE Cluster Internal Structure                        │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  Inputs per cycle:                                                          │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  From L0A: 4 rows × 16 elements × 16-bit = 1024 bits               │   │
│  │  From L0B: 16 cols × 16 elements × 16-bit = 4096 bits              │   │
│  │  From prev cluster: 4 rows × 16 cols × 32-bit = 2048 bits (partial)│   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│  Computation per cycle (64 PEs):                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  For each PE[row][col] (row in 0..3, col in 0..15):                │   │
│  │                                                                     │   │
│  │    // Each PE computes dot product of A row segment × B column     │   │
│  │    partial_sum = 0                                                  │   │
│  │    for k in 0..15:                                                  │   │
│  │      partial_sum += A[row][k] × B[k][col]                          │   │
│  │                                                                     │   │
│  │    // Add incoming partial sum from previous cluster               │   │
│  │    result[row][col] = partial_sum + prev_partial[row][col]         │   │
│  │                                                                     │   │
│  │  Total MACs per cycle: 64 PEs × 16 MACs = 1024 MACs                │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│  Output per cycle:                                                          │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  To next cluster: 4 rows × 16 cols × 32-bit = 2048 bits (partial)  │   │
│  │  (Cluster 3 outputs to ACC buffer instead)                         │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 7.4 Pipeline Registers

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         Pipeline Register Structure                          │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  Each pipeline stage has registers to hold:                                 │
│                                                                             │
│  struct PipelineReg {                                                       │
│      valid: 1 bit           // Stage has valid data                        │
│      uop_id: 6 bits         // Which uop is in this stage                  │
│      acc_idx: 7 bits        // Target ACC buffer index                     │
│      is_first: 1 bit        // Clear ACC before write                      │
│      is_last: 1 bit         // Mark ACC complete after write               │
│      partial_sums: 64×32 bits  // 4 rows × 16 cols partial results        │
│  };                                                                         │
│                                                                             │
│  Pipeline advancement (every cycle):                                        │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  pipe_reg[3] → ACC buffer (if valid)                               │   │
│  │  pipe_reg[2] → pipe_reg[3]                                         │   │
│  │  pipe_reg[1] → pipe_reg[2]                                         │   │
│  │  pipe_reg[0] → pipe_reg[1]                                         │   │
│  │  new_uop    → pipe_reg[0] (if issue_valid)                         │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 7.5 Throughput Analysis

```
Performance metrics:

  Single uop computation:
    - 16×16 tile = 256 output elements
    - Each element requires 16 MACs (K=16)
    - Total: 256 × 16 = 4096 MACs per uop

  Pipeline throughput:
    - 4 clusters × 64 PEs × 16 MACs = 4096 MACs per cycle
    - 1 uop completes per cycle (after 4-cycle fill)

  Example: MATMUL(64, 64, 64)
    - Tiles: 4 × 4 × 4 = 64 uops
    - Pipeline fill: 4 cycles
    - Steady state: 64 uops × 1 cycle = 64 cycles
    - Total: 4 + 64 = 68 cycles
    - Throughput: 64 × 4096 MACs / 68 cycles = 3855 MACs/cycle

  Peak throughput:
    - 4096 MACs/cycle (after pipeline fill)
    - At 1 GHz: 4.096 TMAC/s (INT16)
```

---

## 8. MMIO Interface

### 8.1 Bandwidth Calculations

```
MMIO Bandwidth: 64 bits per cycle (read and write)
(Simplified from 2048-bit for C++ emitter compatibility)

L0A/L0B Entry Size: 16 × 16 × 16-bit = 4096 bits
  → LOAD requires 64 cycles (64 × 64 bits)

ACC Entry Size: 16 × 16 × 32-bit = 8192 bits
  → STORE requires 128 cycles (128 × 64 bits)
```

### 8.2 Memory Map

```
Base Address: 0x80000000

┌─────────────────────────────────────────────────────────────────┐
│ Offset      │ Size    │ Description                            │
├─────────────┼─────────┼────────────────────────────────────────┤
│ 0x0000      │ 8B      │ Control Register                       │
│ 0x0008      │ 8B      │ Status Register                        │
│ 0x0010      │ 8B      │ MATMUL Instruction Register (M,K,N)    │
│ 0x0018      │ 8B      │ Address A Register                     │
│ 0x0020      │ 8B      │ Address B Register                     │
│ 0x0028      │ 8B      │ Address C Register                     │
│ 0x0030      │ 8B      │ Load L0A Command (entry_idx, addr)     │
│ 0x0038      │ 8B      │ Load L0B Command (entry_idx, addr)     │
│ 0x0040      │ 8B      │ Store ACC Command (entry_idx, addr)    │
│ 0x0048      │ 8B      │ Queue Status (entries used/free)       │
│ 0x0050      │ 8B      │ L0A Status (bitmap of valid entries)   │
│ 0x0058      │ 8B      │ L0B Status (bitmap of valid entries)   │
│ 0x0060      │ 8B      │ ACC Status (bitmap of ready entries)   │
│             │         │                                        │
│ 0x1000      │ 8B      │ L0A Data Port (64-bit)                 │
│ 0x2000      │ 8B      │ L0B Data Port (64-bit)                 │
│ 0x3000      │ 8B      │ ACC Data Port (64-bit)                 │
└─────────────┴─────────┴────────────────────────────────────────┘
```

### 8.3 Control Register Bits

```
Control Register (0x0000):
  Bit 0    : START      - Start MATMUL execution
  Bit 1    : RESET      - Reset accelerator
  Bit 2    : LOAD_L0A   - Trigger L0A load
  Bit 3    : LOAD_L0B   - Trigger L0B load
  Bit 4    : STORE_ACC  - Trigger ACC store
  Bit 7:5  : Reserved
  Bit 15:8 : Entry index for LOAD/STORE operations
  Bit 63:16: Reserved
```

### 8.4 Status Register Bits

```
Status Register (0x0008):
  Bit 0    : DONE       - MATMUL complete
  Bit 1    : BUSY       - Computation in progress
  Bit 2    : L0A_BUSY   - L0A load in progress
  Bit 3    : L0B_BUSY   - L0B load in progress
  Bit 4    : ACC_BUSY   - ACC store in progress
  Bit 5    : QUEUE_FULL - Issue queue full
  Bit 6    : QUEUE_EMPTY- Issue queue empty
  Bit 15:7 : Reserved
  Bit 23:16: Queue entries used
  Bit 31:24: Queue entries free
  Bit 63:32: Cycle counter
```

---

## 9. Execution Flow

### 9.1 Software Flow

```
1. Configure MATMUL parameters:
   - Write M, K, N to MATMUL Instruction Register
   - Write addr_A, addr_B, addr_C to Address Registers

2. Pre-load input tiles:
   - For each required A tile: LOAD_L0A(entry_idx, tile_addr)
   - For each required B tile: LOAD_L0B(entry_idx, tile_addr)
   - Wait for loads to complete (poll status)

3. Start computation:
   - Write START to Control Register
   - Hardware decomposes MATMUL into uops
   - Uops execute as inputs become ready

4. Store results:
   - Poll ACC status for completed entries
   - For each ready ACC entry: STORE_ACC(entry_idx, dest_addr)

5. Repeat for next batch of tiles (if matrix larger than buffer)
```

### 9.2 Hardware Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                     Hardware Execution Flow                     │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. MATMUL Decode                                               │
│     ├── Parse M, K, N parameters                               │
│     ├── Calculate tile counts                                  │
│     └── Generate uops → Issue Queue                            │
│                                                                 │
│  2. Uop Scheduling (Out-of-Order)                              │
│     ├── Check L0A[l0a_idx].valid                               │
│     ├── Check L0B[l0b_idx].valid                               │
│     ├── Check ACC[acc_idx] available                           │
│     └── Issue ready uops to systolic array                     │
│                                                                 │
│  3. Systolic Array Execution                                    │
│     ├── Load weights from L0A entry                            │
│     ├── Stream activations from L0B entry                      │
│     ├── Compute 16×16 tile multiplication                      │
│     └── Write result to ACC entry                              │
│                                                                 │
│  4. Accumulation                                                │
│     ├── If is_first: ACC[idx] = result                         │
│     ├── Else: ACC[idx] += result                               │
│     └── If is_last: Mark ACC[idx] as ready                     │
│                                                                 │
│  5. Completion                                                  │
│     ├── All uops executed                                      │
│     ├── All ACC entries marked ready                           │
│     └── Set DONE status                                        │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 10. Pipelined Execution Timing

### 10.1 Detailed Pipeline Timing

```
Cycle:  0    1    2    3    4    5    6    7    8    9   10   11   ...
        │    │    │    │    │    │    │    │    │    │    │    │
        ▼    ▼    ▼    ▼    ▼    ▼    ▼    ▼    ▼    ▼    ▼    ▼

LOAD_L0A[0]:
        ├─────────┤
        │ 2 cycles│
        └─────────┘

LOAD_L0B[0]:
             ├─────────┤
             │ 2 cycles│
             └─────────┘

                       Pipeline Execution (1 uop/cycle throughput):
                       ─────────────────────────────────────────────

uop0:                  [C0]─[C1]─[C2]─[C3]─►ACC[0]
                            │    │    │    │
uop1:                       [C0]─[C1]─[C2]─[C3]─►ACC[1]
                                 │    │    │    │
uop2:                            [C0]─[C1]─[C2]─[C3]─►ACC[2]
                                      │    │    │    │
uop3:                                 [C0]─[C1]─[C2]─[C3]─►ACC[3]
                                           │    │    │    │
uop4:                                      [C0]─[C1]─[C2]─[C3]─►ACC[0]
                                                │    │    │    │
...                                             ...  ...  ...  ...

STORE_ACC[0]:                                                  ├────────────────┤
                                                               │  4 cycles (8KB)│
                                                               └────────────────┘

Legend:
  [C0] = PE Cluster 0 (rows 0-3)
  [C1] = PE Cluster 1 (rows 4-7)
  [C2] = PE Cluster 2 (rows 8-11)
  [C3] = PE Cluster 3 (rows 12-15)
  ►ACC = Write complete tile to ACC buffer
```

### 10.2 Pipeline Stall Conditions

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Pipeline Stall Conditions                          │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  The pipeline stalls when:                                                  │
│                                                                             │
│  1. L0A data not ready:                                                     │
│     - uop requires L0A[idx] but L0A[idx].valid = 0                         │
│     - Solution: Wait for LOAD_L0A to complete                              │
│                                                                             │
│  2. L0B data not ready:                                                     │
│     - uop requires L0B[idx] but L0B[idx].valid = 0                         │
│     - Solution: Wait for LOAD_L0B to complete                              │
│                                                                             │
│  3. ACC write conflict:                                                     │
│     - Two uops in pipeline target same ACC[idx]                            │
│     - Solution: Out-of-order issue avoids this by checking ACC availability│
│                                                                             │
│  4. Issue queue empty:                                                      │
│     - No ready uops to issue                                               │
│     - Pipeline drains but doesn't stall                                    │
│                                                                             │
│  Stall behavior:                                                            │
│     - Pipeline registers hold their values                                 │
│     - No new uop enters stage 0                                            │
│     - Downstream stages continue if they have valid data                   │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 10.3 Continuous Execution Example

```
Example: MATMUL(32, 32, 32) = 2×2×2 = 8 uops

Cycle:  0   1   2   3   4   5   6   7   8   9  10  11
        │   │   │   │   │   │   │   │   │   │   │   │
        ▼   ▼   ▼   ▼   ▼   ▼   ▼   ▼   ▼   ▼   ▼   ▼

uop0:  [C0][C1][C2][C3]►                              ACC[0] ready
uop1:      [C0][C1][C2][C3]►                          ACC[1] ready
uop2:          [C0][C1][C2][C3]►                      ACC[2] ready
uop3:              [C0][C1][C2][C3]►                  ACC[3] ready
uop4:                  [C0][C1][C2][C3]►              ACC[0] += (K accumulate)
uop5:                      [C0][C1][C2][C3]►          ACC[1] +=
uop6:                          [C0][C1][C2][C3]►      ACC[2] +=
uop7:                              [C0][C1][C2][C3]►  ACC[3] += (all done)

Total cycles: 4 (fill) + 8 (uops) - 1 = 11 cycles
Throughput: 8 uops / 11 cycles = 0.73 uops/cycle
(Approaches 1.0 for larger matrices)
```

---

## 11. Implementation Modules

### 11.1 Module Hierarchy

```
cube_v2/
├── cube_v2_reuse.py        # Top-level module (with module reuse)
├── cube_v2_types.py        # Dataclass definitions
├── cube_v2_consts.py       # Constants and addresses
├── cube_v2_decoder.py      # MATMUL instruction decoder
├── cube_v2_issue_queue.py  # 64-entry issue queue
├── cube_v2_pe.py           # PE module definition (@module)
├── cube_v2_l0_entry.py     # L0 entry module definition (@module)
├── cube_v2_l0_reuse.py     # L0A/L0B buffer (using m.instance())
├── cube_v2_systolic_reuse.py # Systolic array (using m.instance())
├── cube_v2_acc.py          # ACC buffer (64 entries)
├── cube_v2_mmio.py         # MMIO interface
└── util.py                 # Utility functions
```

### 11.2 Module Reuse Implementation

PE 和 L0 Entry 使用 `@module` 装饰器定义为可复用模块：

```python
# cube_v2_pe.py - PE 模块定义
from pycircuit.module import module

@module(name="CubePE")
def build_pe(m: Circuit) -> None:
    """Single PE with tree-based dot product."""
    clk = m.clock("clk")
    rst = m.reset("rst")
    # 32 inputs: a0-a15, b0-b15
    # Tree-based reduction for 16-element dot product
    # Output: 32-bit result

# cube_v2_l0_entry.py - L0 Entry 模块定义
@module(name="L0Entry")
def build_l0_entry(m: Circuit) -> None:
    """Single L0 buffer entry (16×16 = 256 elements)."""
    clk = m.clock("clk")
    rst = m.reset("rst")
    load_valid = m.input("load_valid", width=1)
    load_row = m.input("load_row", width=4)
    load_col = m.input("load_col", width=4)
    load_data = m.input("load_data", width=16)
    # 256 data registers + valid status
    # Outputs: valid, d_r{row}_c{col} for all 256 elements
```

使用 `m.instance()` 实例化模块：

```python
# cube_v2_systolic_reuse.py - 256 PE instances
for cluster in range(4):
    for row in range(4):
        for col in range(16):
            pe = m.instance(
                build_pe,
                name=f"pe_c{cluster}_r{row}_c{col}",
                clk=clk, rst=rst,
                a0=a_row[0], ..., a15=a_row[15],
                b0=b_col[0], ..., b15=b_col[15],
                ...
            )

# cube_v2_l0_reuse.py - 64 L0 entry instances per buffer
for i in range(num_entries):
    entry = m.instance(
        build_l0_entry,
        name=f"{prefix}_entry_{i}",
        clk=clk, rst=rst,
        load_valid=entry_load_valid,
        load_row=load_row,
        load_col=load_col,
        load_data=load_data,
    )
```

### 11.3 Key Interfaces

```python
# L0A/L0B Buffer Interface
class BufferEntry:
    data: Wire[4096]      # 16×16×16-bit
    valid: Wire[1]
    loading: Wire[1]
    ref_count: Wire[8]

# ACC Buffer Interface
class AccEntry:
    data: Wire[8192]      # 16×16×32-bit
    valid: Wire[1]
    computing: Wire[1]
    pending_k: Wire[8]

# Uop Interface
class Uop:
    l0a_idx: Wire[7]
    l0b_idx: Wire[7]
    acc_idx: Wire[7]
    is_first: Wire[1]
    is_last: Wire[1]

# Issue Queue Interface
class IssueQueueEntry:
    valid: Wire[1]
    uop: Uop
    l0a_ready: Wire[1]
    l0b_ready: Wire[1]
    acc_ready: Wire[1]
    issued: Wire[1]
```

---

## 12. Future Extensions

1. **Double Buffering**: Overlap computation with data loading
2. **Multiple Systolic Arrays**: Parallel tile computation
3. **Sparsity Support**: Skip zero tiles
4. **Quantization**: INT8/INT4 support
5. **Fusion**: Fused MATMUL + activation functions

---

## References

- [Cube v1 Architecture](ARCHITECTURE.md)
- [Cube v1 Visual Guide](VISUAL_GUIDE.md)
- [pyCircuit Usage Guide](../../../../docs/USAGE.md)
