# PE_INT Specification (SPEC)

This document defines the functional and timing contract for the PE_INT fixed-point / integer vector MAC unit.

**Spec baseline: v2.0.5 (frozen baseline).**

---

## 1. End-to-End Data Semantics

### 1.1 Mode 2a (`mode=2'b00`): `A:S8 x B:S8`

- `A` and `B` are both `8 x S8` (two's complement).
- Output math:
  - `out0 = sum_{i=0..7}(A_s8[i] * B_s8[i])`
  - `out1` is don't-care semantically in this mode (see stability rule in Section 5.1c).

### 1.2 Mode 2b (`mode=2'b01`): `A:S8 x B:S4` (dual path)

- `A` is `8 x S8`.
- `B0` and `B1` are each `8 x S4`.
- Output math:
  - `out0 = sum_{i=0..7}(A_s8[i] * B0_s4[i])`
  - `out1 = sum_{i=0..7}(A_s8[i] * B1_s4[i])`

### 1.3 Mode 2c (`mode=2'b10`): `A:S5 x B:S5` with E1 scaling (dual path)

- `A`, `B0`, `B1` are each `16 x S5`.
- Each 16-lane vector is split into two 8-lane groups with E1 bits:
  - group 0: lanes `[0..7]`
  - group 1: lanes `[8..15]`
- `e1_a[1:0]`, `e1_b0[1:0]`, `e1_b1[1:0]`, with each bit in `{0,1}`.
- Output math:
  - `out0 = sum_lo(A,B0) * 2^(e1_a[0]+e1_b0[0]) + sum_hi(A,B0) * 2^(e1_a[1]+e1_b0[1])`
  - `out1 = sum_lo(A,B1) * 2^(e1_a[0]+e1_b1[0]) + sum_hi(A,B1) * 2^(e1_a[1]+e1_b1[1])`
  - where `sum_lo(X,Y)=sum_{i=0..7}(X_s5[i]*Y_s5[i])`, `sum_hi(X,Y)=sum_{i=8..15}(X_s5[i]*Y_s5[i])`

### 1.4 Mode 2d (`mode=2'b11`): `A:S8 x B:S5` (dual path)

- `A` is `8 x S8`.
- `B0` and `B1` are each `8 x S5`.
- Output math:
  - `out0 = sum_{i=0..7}(A_s8[i] * B0_s5[i])`
  - `out1 = sum_{i=0..7}(A_s8[i] * B1_s5[i])`

---

## 2. Mode Summary

| mode | Name | Datapath |
|------|------|----------|
| `2'b00` | 2a | `S8 x S8`, single effective output |
| `2'b01` | 2b | `S8 x S4`, dual output |
| `2'b10` | 2c | `S5 x S5 + E1`, dual output |
| `2'b11` | 2d | `S8 x S5`, dual output |

---

## 3. Output Width (Lossless Math)

- Mode 2a: `out0` requires 19-bit signed.
- Mode 2b: `out0/out1` each require 15-bit signed.
- Mode 2c: `out0/out1` each require 16-bit signed.
- Mode 2d: `out0/out1` each require 16-bit signed.

Unified top-level output widths across modes:

- `out0[18:0]` (19-bit signed)
- `out1[15:0]` (16-bit signed)
- `vld_out` (1-bit)

Narrower mode results must be sign-extended to unified top-level width.

---

## 4. Top-Level Ports

| Port | Dir | Width | Description |
|------|-----|-------|-------------|
| `clk` | in | 1 | Single clock domain |
| `rst_n` | in | 1 | Active-low reset (async assert, sync release) |
| `vld` | in | 1 | Input valid, no `ready` backpressure |
| `mode` | in | 2 | Mode selector |
| `a` | in | 80 | Shared A input bus |
| `b` | in | 80 | Shared B/B0 packed bus |
| `b1` | in | 80 | B1 bus (used by mode 2c) |
| `e1_a` | in | 2 | E1 for A groups (mode 2c) |
| `e1_b0` | in | 2 | E1 for B0 groups (mode 2c) |
| `e1_b1` | in | 2 | E1 for B1 groups (mode 2c) |
| `out0` | out | 19 | Unified signed output 0 |
| `out1` | out | 16 | Unified signed output 1 |
| `vld_out` | out | 1 | Output valid aligned with output registers |

---

## 5. Pipeline and Implementation Constraints

### 5.0 Clock and Reset

- Single clock domain only.
- `rst_n` uses asynchronous assertion and synchronous release.

### 5.1 Fully-Pipelined Datapath

- Must be implemented as a full pipeline datapath.
- Independent FIFO/queue used to bypass fixed pipeline semantics is forbidden.
- End-to-end pipeline latency is fixed and mode-invariant.
- Internal pipeline stage count is mode-invariant.

### 5.1a Registered Top Outputs (Mandatory)

- `out0`, `out1`, and `vld_out` must be driven by registers.
- `vld_out` and outputs must update at the same output register boundary.

### 5.1b Input-Pin to First Register Logic Depth

- Input-side prelogic before first pipeline register is allowed.
- Effective logic depth from input pins to first register must be `<= 8` layers.

### 5.1c `vld_out` and Mode-2a `out1` Stability

- `vld_out=1` marks valid output data in current output cycle.
- In mode 2a, `out1` is mathematically don't-care, but must avoid unnecessary toggles.
- Acceptable strategy: hold previous value or drive documented stable constant.

### 5.2 Input Valid Handshake

- No `ready` signal.
- A new transaction is sampled only when `vld=1`.

### 5.3 Back-to-Back Mode Switching

- Back-to-back `vld=1` with mode changes is allowed.
- Transaction ordering must remain FIFO at output.
- No mode/data cross-contamination between in-flight transactions.

### 5.4 Stage Logic Depth Target

- Main target for each pipeline stage is around 25 effective logic layers.
- Exact counting rules follow project logic-depth policy (`logic-depth-rules`).

---

## 6. Packing Rules

### 6.1 Lane Definition

- Lane granularity is `lane[k] = x[5*k+4 : 5*k]`.
- `k = 0..15` across 80-bit buses.

### 6.2 Modes 2a / 2b / 2d (`A` as S8 lanes)

- `A` S8 values are packed by 4-bit halves into lane `[4:1]` fields.
- For mode 2b, `b[39:0]` carries B0 `8 x S4`, `b[79:40]` carries B1 `8 x S4`.
- For mode 2d, `b[39:0]` carries B0 `8 x S5`, `b[79:40]` carries B1 `8 x S5`.

### 6.3 Mode 2c (`A/B0/B1` as `16 x S5`)

- `x[5*i+4 : 5*i]` maps to element `i` for each of `a`, `b`, and `b1`.
- E1 group mapping:
  - bit `[0]` applies to lanes `[0..7]`
  - bit `[1]` applies to lanes `[8..15]`

---

## 7. Validity and Latency Contract

- One sampled `vld=1` transaction maps to one `vld_out=1` transaction.
- `vld -> vld_out` latency is fixed as `L` cycles.
- `L` must be identical for modes 2a/2b/2c/2d.
- Under sustained valid traffic after pipeline fill, no output bubbles are allowed.

---

## 8. Revision Notes

| Version | Date | Notes |
|---------|------|-------|
| `2.0.5` | 2026-04-17 | Added `vld_out` bundle requirement and mode-2a `out1` stability rule. |
| `2.0.4` | 2026-04-17 | Added `vld` contract and reset/clock-domain clarification. |
| `2.0.3` | 2026-04-17 | Enforced registered outputs and input-to-first-register depth cap. |
| `2.0.2` | 2026-04-17 | Added full-pipeline and per-stage depth target requirements. |
| `2.0.1` | 2026-04-17 | Unified top-level outputs to `out0[18:0]`, `out1[15:0]`. |
| `2.0.0` | 2026-04-17 | Frozen baseline refresh. |
