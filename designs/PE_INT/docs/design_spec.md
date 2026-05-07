# PE_INT Design Spec

Version: v0.8
Generated from: `docs/spec.md` baseline v2.0.6
Generation rule: `.cursor/skills/pe-int-pycircuiteval-flow/SKILL.md`

This document is the structure-first design spec derived from `docs/spec.md`.
It describes low-level circuit contracts, widths, bus routing, pipeline
boundaries, and protocol timing. Topologies not stated by the source spec are
first marked as `Unspecified by source spec`. Concrete topology choices may then
be added in an optimizer-owned section with explicit provenance from an
independent Circuit Optimizer sub-agent.

---

## 1. Scope and Source Boundary

`docs/spec.md` defines behavior. This design spec maps that behavior onto
low-level circuit modules and register boundaries.

Rules:

1. Business modes (`2a/2b/2c/2d`) share one fixed-latency pipeline.
2. Circuit modules are described before mode-specific parameter mapping.
3. Widths use the minimum lossless contract derivable from `docs/spec.md`.
4. Arbitrary operand or result margin widths are forbidden.
5. Any topology not stated by `docs/spec.md` remains `Unspecified by source
   spec` in source-derived sections.
6. Optimizer-selected topologies are design decisions, not source-spec
   contracts, and must be labeled with sub-agent provenance, pass number, and
   objective.

---

## 2. Top-Level Module Contract

| Port | Dir | Width | Signed | Circuit Contract |
|------|-----|-------|--------|------------------|
| `clk` | in | 1 | no | Single clock domain |
| `rst_n` | in | 1 | no | Active-low reset intent |
| `vld` | in | 1 | no | Input transaction qualifier |
| `mode` | in | 2 | no | Per-transaction mode selector |
| `a` | in | 80 | packed | Shared A input bus |
| `b` | in | 80 | packed | Shared B/B0 input bus |
| `b1` | in | 80 | packed | B1 input bus for mode 2c |
| `e1_a` | in | 2 | no | Mode 2c A group scale bits |
| `e1_b0` | in | 2 | no | Mode 2c B0 group scale bits |
| `e1_b1` | in | 2 | no | Mode 2c B1 group scale bits |
| `vld_out` | out | 1 | no | Registered output valid |
| `out0` | out | 19 | yes | Registered output 0 |
| `out1` | out | 16 | yes | Registered output 1 |

Reset and flow control:

- `rst_n` is the only top-level reset port required by the source spec.
- `out0`, `out1`, and `vld_out` are registered outputs.
- There is no `ready` signal and no backpressure interface.
- A new transaction is sampled only when `vld=1`.
- Independent FIFO/queue structures are forbidden.

---

## 3. Bus Decode and DEMUX

### 3.1 Physical Lane Container

Each 80-bit bus contains 16 physical 5-bit lanes:

`lane[i] = bus[5*i+4 : 5*i]`, for `i=0..15`.

### 3.2 Decode Components

| Component | Input Slice | Output Width | Signed | Used By |
|-----------|-------------|--------------|--------|---------|
| `DEC_S8` | two adjacent lane nibbles `[4:1]` | 8 | yes | S8 operands |
| `DEC_S4` | one lane nibble `[4:1]` | 4 | yes | S4 operands |
| `DEC_S5` | one full lane `[4:0]` | 5 | yes | S5 operands |

Decode outputs are natural-width signed operands. They are not widened to
top-level output width before multiplication.

### 3.3 Mode to Bus Mapping

| Mode | A Path | B/B0 Path | B1 Path |
|------|--------|-----------|---------|
| 2a | `8 x DEC_S8(a)` | `8 x DEC_S8(b)` | unused |
| 2b | `8 x DEC_S8(a)` | `B0=8 x DEC_S4(b[39:0])`, `B1=8 x DEC_S4(b[79:40])` | unused |
| 2c | `16 x DEC_S5(a)` | `16 x DEC_S5(b)` | `16 x DEC_S5(b1)` |
| 2d | `8 x DEC_S8(a)` | `B0=8 x DEC_S5(b[39:0])`, `B1=8 x DEC_S5(b[79:40])` | unused |

Mode DEMUX:

- The mode field selects output branch results at the aligned merge stage.
- All in-flight transactions preserve FIFO order.

---

## 4. Low-Level Circuit Components

### 4.1 Signed Multiplier

The source spec defines signed operand types. Therefore multiplier input widths
are natural operand widths, and product widths are exact signed product widths.

| Product | Operand A | Operand B | Product Width | Signed |
|---------|-----------|-----------|---------------|--------|
| `P2A` | S8 | S8 | 16 | yes |
| `P2B0` | S8 | S4 | 12 | yes |
| `P2B1` | S8 | S4 | 12 | yes |
| `P2C0` | S5 | S5 | 10 | yes |
| `P2C1` | S5 | S5 | 10 | yes |
| `P2D0` | S8 | S5 | 13 | yes |
| `P2D1` | S8 | S5 | 13 | yes |

Rules:

- Do not implement `S8*S8`, `S8*S4`, `S8*S5`, or `S5*S5` by first extending
  operands to `out0` or `out1` width.
- The signed product is extended only when entering a wider reduction path.
- Multiplier topology: `Unspecified by source spec`.

### 4.2 Reduction

Reduction sums signed products in Dot8 groups.

| Reduction Path | Inputs | Input Width at Reduction | Output Width |
|----------------|--------|--------------------------|--------------|
| 2a out0 | 8 x `P2A` | sign-extend product to 19 | 19 signed |
| 2b out0 | 8 x `P2B0` | sign-extend product to 19 | 19 signed |
| 2b out1 | 8 x `P2B1` | sign-extend product to 16 | 16 signed |
| 2c out0 low/high | 8 x `P2C0` per group | sign-extend product to 19 | 19 signed partial |
| 2c out1 low/high | 8 x `P2C1` per group | sign-extend product to 16 | 16 signed partial |
| 2d out0 | 8 x `P2D0` | sign-extend product to 19 | 19 signed |
| 2d out1 | 8 x `P2D1` | sign-extend product to 16 | 16 signed |

Topology:

- Compressor tree topology: `Unspecified by source spec`.
- Adder tree topology: `Unspecified by source spec`.
- Final carry-propagate adder topology: `Unspecified by source spec`.

### 4.3 Mode 2c Shifter

Mode 2c group scaling:

- `shift_lo = e1_a[0] + e1_bx[0]`
- `shift_hi = e1_a[1] + e1_bx[1]`
- legal shift values are `0`, `1`, and `2`

The circuit needs only bounded x1/x2/x4 scaling. Exact shifter topology is
`Unspecified by source spec`.

### 4.4 MUX / DEMUX

Mode selection routes reduced branch results to output candidates:

- `out0` candidates: 2a, 2b0, 2c0, 2d0
- `out1` candidates: 2b1, 2c1, 2d1, plus the mode-2a stability policy

Exact mux-tree topology is `Unspecified by source spec`.

### 4.5 Registers / D Flip-Flops

Required state:

- Input capture registers.
- Pipeline registers to maintain fixed mode-invariant latency.
- Registered `vld_out`, `out0`, and `out1`.
- `out1` hold state or an equivalent documented stable-constant policy for
  mode 2a.

All modes traverse the same number of real register boundaries.

---

## 5. Pipeline Contract

The flow requires an auditable full pipeline:

`input -> comb0 -> reg0 -> comb1 -> reg1 -> comb2 -> reg2 -> comb3 -> reg3/output`

Latency:

- Fixed `L=4`.
- 0-based convention: a transaction sampled at `t0` produces committed output at
  `t0+4`.
- `vld_out`, `out0`, and `out1` update at the same output register boundary.

Input prelogic:

- Effective logic depth from input pins to the first register is `<= 8` layers.

Stage logic:

- Main target for each pipeline stage is around 25 effective logic layers.
- Exact topology choices remain source-bound in Section 4 and optimizer-owned
  in Section 10.

---

## 6. Business Mode Mapping to Circuit Parameters

| Mode | Multiplier Parameters | Reduction Parameters | Post-Scale | Output Rule |
|------|-----------------------|----------------------|------------|-------------|
| 2a | 8 x S8*S8 -> P16 | sign-extend P16 to 19, sum 8 | none | `out0=sum`, `out1` stable |
| 2b | 8 x S8*S4 -> P12 per path | B0: sign-extend to 19; B1: sign-extend to 16 | none | dual output |
| 2c | 16 x S5*S5 -> P10 per path | split low/high Dot8 groups | x1/x2/x4 per group | dual output |
| 2d | 8 x S8*S5 -> P13 per path | B0: sign-extend to 19; B1: sign-extend to 16 | none | dual output |

---

## 7. Flow Control and FSM

There is no multi-state controller FSM implied by `docs/spec.md`.

Valid behavior:

- `vld=1` samples one input transaction.
- `vld=0` samples no new transaction.
- Back-to-back `vld=1` with mode changes is allowed.
- Transaction ordering remains FIFO at output.
- Under sustained valid traffic after pipeline fill, no output bubbles are
  allowed.

Mode 2a `out1` behavior:

- `out1` is mathematically don't-care in mode 2a.
- The circuit must avoid unnecessary toggles by holding the previous value or by
  using a documented stable constant.

---

## 8. Typical Waveforms

### 8.1 Single Transaction Latency

```text
cycle      t0    t1    t2    t3    t4
vld        1     0     0     0     0
mode/data  M0    x     x     x     x
vld_out    0     0     0     0     1
out0/out1  hold  hold  hold  hold  result(M0)
```

### 8.2 Back-to-Back Mode Switch

```text
cycle      t0    t1    t2    t3    t4    t5
vld        1     1     0     0     0     0
mode       2b    2a    x     x     x     x
vld_out    0     0     0     0     1     1
out0       hold  hold  hold  hold  2b    2a
out1       hold  hold  hold  hold  2b    stable/hold
```

### 8.3 Mode 2c Group Scaling

```text
LO = sum lanes[0..7]
HI = sum lanes[8..15]
SH_LO = e1_a[0] + e1_bx[0]
SH_HI = e1_a[1] + e1_bx[1]
OUT = (LO << SH_LO) + (HI << SH_HI)
```

---

## 9. Source-Limited Structural Items

The following items are intentionally not specified because `docs/spec.md` does
not define them:

- Booth versus array versus other multiplier topology.
- Wallace/Dadda/compressor-tree versus adder-tree reduction topology.
- Carry-lookahead versus prefix versus ripple final adder topology.
- Exact bounded-shifter topology.
- Exact mux-tree shape.

If any of these topologies are required for implementation or review, update
`docs/spec.md`, provide an explicit user-approved structural-policy source, or
record a Circuit Optimizer decision, then regenerate this design spec.

---

## 10. Circuit Optimizer Pass 0 Topology Proposal

Provenance: `Optimizer-subagent-selected, pass 0`

This section records optimizer-owned topology decisions. These decisions are not
derived from `docs/spec.md`; they are selected by the independent Circuit
Optimizer for implementation guidance.

Objective: `balanced`

Priority order:

1. Meet fixed `L=4`.
2. Keep input-pin to first-register prelogic `<= 8` effective logic layers.
3. Keep each main pipeline stage near the project target of about 25 effective
   logic layers.
4. Then optimize area and power.

Iteration budget:

- Default optimizer budget: pass 0 topology selection before implementation plus
  2 post-build optimizer iterations after PyCircuit build, generated RTL, and
  functional regression evidence are available.

### 10.1 Selected Topologies and Current Status

| Block | Selection | Implementation Status | Evidence Pointer | Risk / Next Evidence | Provenance |
|---|---|---|---|---|---|
| Pipeline placement | Fixed `input -> comb0 -> reg0 -> comb1 -> reg1 -> comb2 -> reg2 -> comb3 -> reg3/output`, `L=4`. | Implemented | `python/pe_int/top.py::build()`, generated `rtl/build/pe_int.v` register boundaries. | Confirm with RTL regression latency checks. | `Optimizer-subagent-selected, pass 0`; latency from `docs/spec.md`. |
| Signed multipliers | Natural-width signed products. Radix-4 Booth remains the S8-involved structural optimization intent, but current RTL uses signed shift/add/sub style product generation. | Partially implemented / Deferred | `python/pe_int/lane_mac.py::booth_mul_signed()`, `_mul_signed_twos_complement()`, generated multiplier add/sub chains in `rtl/build/pe_int.v`. | Explicit radix-4 Booth is deferred until synthesis/timing/area evidence justifies the structural rewrite. | `Optimizer-subagent-selected, pass 0`; post-build status audit. |
| Dot8 reduction | Wallace-style carry-save compression tree using `CMPE42`, `FA`, and `HA`, followed by one final CPA. | Implemented | `python/pe_int/lane_mac.py::_wallace_dot8_reduce()`, `rtl/build/pe_int_wallace_dot8_tree_w16.v`, `rtl/build/pe_int_wallace_dot8_tree_w19.v`. | STA required for real timing depth. | `Optimizer-subagent-selected, pass 0`; pass 1 terminal carry policy retained. |
| Final CPA | Brent-Kung-style prefix CPA for W16/W19 reductions and mode-2c low/high merge. | Implemented | `python/pe_int/lane_mac.py::brent_kung_cpa_truncated()`, `sum_shift_pair()`, generated prefix-style RTL. | Synthesis may remap; next evidence is mapped timing/netlist. | `Optimizer-subagent-selected, pass 0`; pass 1 mode-2c merge fix. |
| Mode 2c shifter | Fixed shift-by-0/1/2 using wire shifts plus muxing before final CPA. | Implemented | `python/pe_int/lane_mac.py::shift_scale_x1_x2_x4()`, generated shift/mux RTL in `rtl/build/pe_int.v`. | Check comb3 timing with STA. | `Optimizer-subagent-selected, pass 0`. |
| Mode output MUX | Balanced staged 2:1 mux tree using pipelined mode decode. | Implemented | `python/pe_int/lane_mac.py::select_one_hot4()`, `python/pe_int/mac_modes.py::comb3_mode_merge()`, generated mux tree in `rtl/build/pe_int.v`. | Mapped timing/power evidence still pending. | `Optimizer-subagent-selected, pass 0`. |
| Mode 2a `out1` stable policy | Hold previous registered `out1` on valid mode-2a commit. | Implemented | `python/pe_int/top.py` output commit logic, generated `pe_int_out1` feedback register/mux, and model protocol coverage in `model/test_pe_int.py::test_mode2a_out1_hold_policy_on_vld_out`. | Keep RTL regression for mode switching and mode-2a stability. | `Optimizer-subagent-selected, pass 0`; stability requirement from `docs/spec.md`. |

### 10.2 Pipeline Placement Intent

Use the fixed latency shape:

`input -> comb0 -> reg0 -> comb1 -> reg1 -> comb2 -> reg2 -> comb3 -> reg3/output`

Pass 0 placement:

| Region | Intended Work |
|---|---|
| `comb0` | Lane slicing, sign decode, mode one-hot/predecode, mode-2c shift-select decode. Must remain `<= 8` effective layers from input pins to `reg0`. |
| `comb1` | Multiplier partial-product generation and local small reduction for natural-width products. |
| `comb2` | Dot8 Wallace compression to final two-vector carry-save form. For mode 2c, also perform bounded shift wiring/muxing and low/high carry-save merge where practical. |
| `comb3` | Brent-Kung final CPA, sign extension to top-level width, balanced output mux, and output-register commit/hold policy. |

### 10.3 Wallace Dot8 Cell-Level Contract

The selected Dot8 reduction is a Wallace-style carry-save compression tree, not
a serial `+` operator tree.

Allowed cells:

| Cell | Pins | Bit-Weight Contract |
|---|---|---|
| `CMPE42[k]` | Inputs: `x0`, `x1`, `x2`, `x3`, `Cix`. Outputs: `sum`, `carry`, `Cox`. | `x0..x3` and `Cix` have weight `2^k`. `sum` has weight `2^k`. `carry` has weight `2^(k+1)`. `Cox` has weight `2^(k+1)` and may feed the next higher column peer cell's `Cix`. |
| `FA[k]` | Inputs: `x0`, `x1`, `x2`. Outputs: `sum`, `carry`. | Inputs and `sum` have weight `2^k`; `carry` has weight `2^(k+1)`. |
| `HA[k]` | Inputs: `x0`, `x1`. Outputs: `sum`, `carry`. | Inputs and `sum` have weight `2^k`; `carry` has weight `2^(k+1)`. |

Chaining policy:

- Within one compression layer, `CMPE42[k].Cox` may feed `CMPE42[k+1].Cix`.
- The least-significant column chain input is tied to zero.
- The most-significant `Cox` is preserved as a residual next-weight bit; it
  must not be dropped.
- `FA` and `HA` carries are forwarded to the next higher bit column in the next
  compression layer.

Residual-column handling:

- Use `CMPE42` where column height and adjacent-chain availability make it
  useful.
- Use `FA` for residual triples.
- Use `HA` only when it reduces the next layer's maximum height or is needed for
  termination.
- One or two remaining bits in a column may pass through unchanged.

Termination:

- Compression stops when every bit column has at most two remaining bits.
- The remaining bits form two aligned carry-save vectors.
- The final carry-propagate boundary is the selected Brent-Kung CPA.
- For fixed-width reducer outputs (`W=19` for `out0` paths, `W=16` for `out1`
  paths), any carry bit with weight `2^W` is outside the source-spec output
  width contract and is intentionally truncated at the fixed-width final CPA
  boundary. This is an explicit fixed-width arithmetic policy, not a silent
  implementation drop.
- For mode 2c, low/high Dot8 carry-save vectors should be shifted and merged in
  carry-save form before the final CPA when possible, so the path uses one final
  CPA rather than per-group CPAs plus another adder.

### 10.4 Post-Build Evidence Required

After PyCircuit coding, `pycc` RTL generation, and functional regression,
collect:

1. Generated RTL hierarchy and whether multiplier/reduction structure matches
   this pass 0 topology.
2. Evidence that Dot8 reductions are compressor trees, not generic serial `+`
   chains.
3. Register-boundary count for `out0`, `out1`, and `vld_out`; confirm fixed
   `L=4`.
4. Input-pin to first-register logic cone estimate; confirm `<= 8`.
5. Per-stage logic-depth estimate, especially multiplier stage and final CPA/mux
   stage.
6. Width audit for product, sign extension, shift, carry-save vectors, final
   CPA, and top-level outputs.
7. Mux-depth audit for mode output selection and mode-2a `out1` hold.
8. Functional regression results for all modes and back-to-back mode switching.
9. Synthesis timing/area/power reports when available; otherwise use RTL proxy
   metrics only.
