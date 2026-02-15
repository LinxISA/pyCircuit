# Cube Matrix Multiplication Accelerator

A 16×16 systolic array matrix multiplication accelerator implemented in pyCircuit.

## Versions

| Version | Description | Status |
|---------|-------------|--------|
| **Cube v1** | Basic 16×16 systolic array with weight-stationary dataflow | Stable |
| **Cube v2** | 4-stage pipelined systolic array with MATMUL block instruction support | **New** |

---

## Cube v2 (Recommended)

Cube v2 is a high-performance matrix multiplication accelerator with MATMUL block instruction support.

### Key Features

- **4-stage pipelined systolic array** (4 PE Clusters × 64 PEs each)
- **1 uop/cycle throughput** after 4-cycle pipeline fill
- **Peak performance**: 4096 MACs/cycle
- **MATMUL block instruction**: Supports A[M×K] × B[K×N] = C[M×N]
- **Triple buffering**: L0A (64 entries), L0B (64 entries), ACC (64 entries)
- **64-entry issue queue** with out-of-order execution
- **64-bit MMIO interface** (simplified for C++ emitter compatibility)

### Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    4-Stage Pipelined Systolic Array                          │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  L0A Buffer (64 entries)               L0B Buffer (64 entries)              │
│  ┌─────────────────────┐               ┌─────────────────────┐              │
│  │ 16×16×16-bit tiles  │               │ 16×16×16-bit tiles  │              │
│  └─────────┬───────────┘               └─────────┬───────────┘              │
│            │                                     │                           │
│            ▼                                     ▼                           │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  PE Cluster 0 (Rows 0-3)   → 64 PEs × 16 MACs = 1024 MACs/cycle    │    │
│  └─────────────────────────────────────┬───────────────────────────────┘    │
│                                        │ Partial sums (4×16×32-bit)         │
│                                        ▼                                     │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  PE Cluster 1 (Rows 4-7)   → 64 PEs × 16 MACs = 1024 MACs/cycle    │    │
│  └─────────────────────────────────────┬───────────────────────────────┘    │
│                                        │                                     │
│                                        ▼                                     │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  PE Cluster 2 (Rows 8-11)  → 64 PEs × 16 MACs = 1024 MACs/cycle    │    │
│  └─────────────────────────────────────┬───────────────────────────────┘    │
│                                        │                                     │
│                                        ▼                                     │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  PE Cluster 3 (Rows 12-15) → 64 PEs × 16 MACs = 1024 MACs/cycle    │    │
│  └─────────────────────────────────────┬───────────────────────────────┘    │
│                                        │                                     │
│                                        ▼                                     │
│                          ┌─────────────────────────┐                        │
│                          │  ACC Buffer (64 entries) │                        │
│                          │  16×16×32-bit results    │                        │
│                          └─────────────────────────┘                        │
│                                                                              │
│  Total: 4 clusters × 1024 MACs = 4096 MACs/cycle                            │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Pipeline Timing

```
Cycle:    0     1     2     3     4     5     6     7     8    ...
          │     │     │     │     │     │     │     │     │
          ▼     ▼     ▼     ▼     ▼     ▼     ▼     ▼     ▼

uop0:   [C0]──[C1]──[C2]──[C3]──►ACC
uop1:         [C0]──[C1]──[C2]──[C3]──►ACC
uop2:               [C0]──[C1]──[C2]──[C3]──►ACC
uop3:                     [C0]──[C1]──[C2]──[C3]──►ACC
uop4:                           [C0]──[C1]──[C2]──[C3]──►ACC

Pipeline: 4-cycle latency, 1 uop/cycle throughput
```

### Benchmark Results (64×64×64 MATMUL)

| PE Array | Uops | Actual Cycles | Efficiency |
|----------|------|---------------|------------|
| 16×16 | 64 | 74 | 90.54% |
| 8×8 | 512 | 579 | 88.95% |
| 4×4 | 4096 | 4163 | 98.46% |

See [CUBE_V2_SPEC.md](CUBE_V2_SPEC.md#76-benchmark-results-64×64×64-matmul) for detailed analysis.

### Cube v2 File Structure

```
cube/
├── cube_v2.py              # Top-level module
├── cube_v2_types.py        # Dataclass definitions
├── cube_v2_consts.py       # Constants and addresses
├── cube_v2_decoder.py      # MATMUL instruction decoder
├── cube_v2_issue_queue.py  # 64-entry issue queue
├── cube_v2_l0.py           # L0A/L0B buffers (64 entries each)
├── cube_v2_acc.py          # ACC buffer (64 entries)
├── cube_v2_systolic.py     # 4-stage pipelined systolic array
├── cube_v2_mmio.py         # MMIO interface (64-bit)
├── tb_cube_v2.v            # Verilog testbench
└── CUBE_V2_SPEC.md         # Detailed architecture specification
```

### Cube v2 Memory Map

| Offset | Size | Description |
|--------|------|-------------|
| 0x0000 | 8B | Control Register |
| 0x0008 | 8B | Status Register |
| 0x0010 | 8B | MATMUL Instruction (M, K, N) |
| 0x0018 | 8B | Matrix A Address |
| 0x0020 | 8B | Matrix B Address |
| 0x0028 | 8B | Matrix C Address |
| 0x1000 | 8B | L0A Data Port (64-bit) |
| 0x2000 | 8B | L0B Data Port (64-bit) |
| 0x3000 | 8B | ACC Data Port (64-bit) |

For complete Cube v2 documentation, see [CUBE_V2_SPEC.md](CUBE_V2_SPEC.md).

---

## Cube v1 (Legacy)

The original Cube module implements a weight-stationary systolic array for matrix multiplication. It features:

- **16×16 Processing Element (PE) array** (256 PEs total)
- **16-bit integer inputs** (weights and activations)
- **32-bit accumulator** (prevents overflow for 16-bit × 16-bit operations)
- **Weight-stationary dataflow** (weights loaded once, activations stream through)
- **Memory-mapped interface** for CPU integration

## Architecture

### Systolic Array Structure

```
        a0  a1  a2  ... a15  (activations flow down)
         ↓   ↓   ↓       ↓
    w0→[PE][PE][PE]...[PE]→ partial sums flow right
    w1→[PE][PE][PE]...[PE]→
    w2→[PE][PE][PE]...[PE]→
    ...
   w15→[PE][PE][PE]...[PE]→
         ↓   ↓   ↓       ↓
        c0  c1  c2  ... c15  (results accumulate)
```

### Processing Element (PE)

Each PE performs multiply-accumulate (MAC) operations:

```python
product = weight * activation  # 16-bit × 16-bit = 32-bit
acc_next = acc + product + partial_sum_in  # 32-bit accumulation
```

**Note**: Due to current pyCircuit limitations, multiplication is implemented as addition (placeholder). This will be updated when multiplication support is added.

### Dataflow

1. **Load Phase**: Weights (W matrix) are loaded into all 256 PEs
2. **Compute Phase**: Activations (A matrix) stream vertically through columns (16 cycles)
3. **Drain Phase**: Partial sums propagate through the array (15 cycles)
4. **Done**: Results are available in the result buffer

## Memory Map

Base address: `0x80000000` (configurable)

| Address Range | Size | Description |
|--------------|------|-------------|
| `0x00` | 8 bytes | Control register (start, reset) |
| `0x08` | 8 bytes | Status register (done, busy) |
| `0x10 - 0x2F` | 32 bytes | Matrix A (activations): 16 × 16-bit |
| `0x210 - 0x40F` | 512 bytes | Matrix W (weights): 16×16 × 16-bit |
| `0x410 - 0x80F` | 1024 bytes | Matrix C (results): 16×16 × 32-bit |

### Control Register (0x00)

| Bit | Name | Description |
|-----|------|-------------|
| 0 | START | Start computation (write 1) |
| 1 | RESET | Reset accelerator (write 1) |
| 2-63 | Reserved | - |

### Status Register (0x08)

| Bit | Name | Description |
|-----|------|-------------|
| 0 | DONE | Computation complete (read-only) |
| 1 | BUSY | Accelerator busy (read-only) |
| 2-63 | Reserved | - |

## File Structure

```
cube/
├── __init__.py              # Package marker
├── util.py                  # Utility functions (Consts dataclass)
├── README.md                # This file
│
├── # Cube v1 (Legacy)
├── cube.py                  # Top-level module (main build function)
├── cube_types.py            # Dataclasses (CubeState, PERegs, FsmResult)
├── cube_consts.py           # Constants (states, addresses, array size)
│
├── # Cube v2 (Recommended)
├── cube_v2.py               # Top-level module
├── cube_v2_types.py         # Dataclass definitions
├── cube_v2_consts.py        # Constants and addresses
├── cube_v2_decoder.py       # MATMUL instruction decoder
├── cube_v2_issue_queue.py   # 64-entry issue queue
├── cube_v2_l0.py            # L0A/L0B buffers (128 entries each)
├── cube_v2_acc.py           # ACC buffer (128 entries)
├── cube_v2_systolic.py      # 4-stage pipelined systolic array
├── cube_v2_mmio.py          # MMIO interface (2048-bit)
├── tb_cube_v2.v             # Verilog testbench
└── CUBE_V2_SPEC.md          # Detailed architecture specification
```

### Key Components in cube.py

| Function | Decorator | Description |
|----------|-----------|-------------|
| `_make_pe_regs` | - | Create 16×16 PE register array |
| `_make_result_regs` | - | Create 256 result registers |
| `_make_weight_regs` | - | Create 256 weight registers |
| `_make_activation_regs` | - | Create 16 activation registers |
| `_build_pe` | `@jit_inline` | Build single PE (MAC operation) |
| `_build_array` | - | Build 16×16 systolic array |
| `_build_fsm` | `@jit_inline` | Build control FSM |
| `_build_mmio_read` | - | Build MMIO read logic |
| `_build_mmio_write` | - | Build MMIO write logic |
| `build` | - | Top-level build function |

## Usage

### Generating Outputs

```bash
# Generate Verilog and C++ from Python
bash janus/update_generated.sh
```

This produces:
- `janus/generated/janus_cube_pyc/janus_cube_pyc.v` - Verilog RTL
- `janus/generated/janus_cube_pyc/janus_cube_pyc_gen.hpp` - C++ simulation header

### Running Tests

```bash
# Run C++ testbench
bash janus/tools/run_janus_cube_pyc_cpp.sh

# With tracing enabled
PYC_TRACE=1 PYC_VCD=1 bash janus/tools/run_janus_cube_pyc_cpp.sh
```

### Operation Sequence

1. **Write weights** to addresses `0x210 - 0x40F` (256 × 16-bit values)
2. **Write activations** to addresses `0x10 - 0x2F` (16 × 16-bit values)
3. **Start computation** by writing `0x01` to control register at `0x00`
4. **Poll status** register at `0x08` until DONE bit is set
5. **Read results** from addresses `0x410 - 0x80F` (256 × 32-bit values)

### Timing

- **Load weights**: 1 cycle
- **Compute**: 16 cycles (streaming 16 rows of activations)
- **Drain**: 15 cycles (pipeline depth)
- **Total**: ~32 cycles for 16×16 matrix multiplication

## FSM States

| State | Value | Description |
|-------|-------|-------------|
| IDLE | 0 | Waiting for start signal |
| LOAD_WEIGHTS | 1 | Loading weights into PEs |
| COMPUTE | 2 | Streaming activations, computing |
| DRAIN | 3 | Draining pipeline |
| DONE | 4 | Computation complete |

## Implementation Details

### JIT Compilation Patterns

The code follows pyCircuit JIT compilation rules:

1. **Functions without `@jit_inline`** execute at Python time (before JIT compilation)
   - Used for register creation loops (`_make_pe_regs`, `_make_weight_regs`, etc.)
   - Can use Python for loops, list comprehensions, etc.

2. **Functions with `@jit_inline`** are compiled into hardware
   - Used for combinational logic (`_build_pe`, `_build_fsm`)
   - Must follow JIT rules: no tuple unpacking, return at top-level

3. **Dataclasses for return values** avoid tuple unpacking (not supported in JIT)
   - `FsmResult(load_weight, compute, done)`
   - `MmioWriteResult(start, reset_cube)`

4. **`Consts` dataclass** centralizes common constant values
   - `consts.zero32`, `consts.one1`, etc.

### Register Allocation

| Category | Count | Width | Total Bits |
|----------|-------|-------|------------|
| State registers | 4 | 1-8 bits | ~13 bits |
| PE weight registers | 256 | 16 bits | 4096 bits |
| PE accumulator registers | 256 | 32 bits | 8192 bits |
| Weight buffer | 256 | 16 bits | 4096 bits |
| Activation buffer | 16 | 16 bits | 256 bits |
| Result buffer | 256 | 32 bits | 8192 bits |

**Total**: ~24,845 bits (~3.1 KB)

## Testing

### C++ Testbench

Located at `janus/tb/tb_janus_cube_pyc.cpp`:

| Test | Description |
|------|-------------|
| `testIdentity` | Basic operation with zero weights |
| `testSimple2x2` | Simple 2×2 matrix operation |

### Debug Options

| Environment Variable | Description |
|---------------------|-------------|
| `PYC_TRACE=1` | Generate `.log` file with execution trace |
| `PYC_VCD=1` | Generate `.vcd` waveform file |
| `PYC_TRACE_DIR=<path>` | Output directory for trace files |

## Example: Matrix Multiplication

```python
# Matrix A (activations) - 16×16
A = [[a00, a01, ..., a0F],
     [a10, a11, ..., a1F],
     ...
     [aF0, aF1, ..., aFF]]

# Matrix W (weights) - 16×16
W = [[w00, w01, ..., w0F],
     [w10, w11, ..., w1F],
     ...
     [wF0, wF1, ..., wFF]]

# Result C = W × A (16×16)
C = [[c00, c01, ..., c0F],
     [c10, c11, ..., c1F],
     ...
     [cF0, cF1, ..., cFF]]

# Where: cij = Σ(wik * akj) for k=0 to 15
```

## Known Limitations

### Cube v1
1. **Multiplication operator**: Currently using addition as placeholder due to pyCircuit limitations
2. **No overflow detection**: 32-bit accumulator can overflow for large values
3. **Fixed array size**: 16×16 array is hardcoded
4. **Simplified memory interface**: Real CPU integration would require proper bus protocol

### Cube v2
1. **Multiplication operator**: Currently using addition as placeholder (same as v1)
2. **64-bit MMIO bandwidth**: Simplified from 2048-bit for C++ emitter compatibility
3. **No double buffering**: Data loading and computation cannot overlap yet

## References

- [Cube v2 Specification](CUBE_V2_SPEC.md) - Detailed architecture for Cube v2
- [pyCircuit Usage Guide](../../../../docs/USAGE.md)
- [Janus BCC CPU](../bcc/janus_bcc_pyc.py)
- [Systolic Array Architecture](https://en.wikipedia.org/wiki/Systolic_array)
- [Improvement Plan](IMPROVEMENT_PLAN.md) - Future development roadmap
- [VISUAL_GUIDE.md](VISUAL_GUIDE.md) - Intuitive visual guide with animations and examples
- [ARCHITECTURE.md](ARCHITECTURE.md) - Complete technical architecture with 15 detailed diagrams
