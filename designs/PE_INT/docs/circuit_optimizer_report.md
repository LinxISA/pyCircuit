# PE_INT Circuit Optimizer Report

Pass: `0`

Provenance: `Optimizer-subagent-selected, pass 0`

Scope: topology optimization decisions only. No source-spec behavior is changed,
and no topology decision here is claimed as derived from `docs/spec.md`.

## Inputs Reviewed

- `docs/spec.md`, baseline v2.0.6
- Current `docs/design_spec.md`
- `.cursor/skills/circuit-optimizer/SKILL.md`
- `.cursor/skills/pe-int-pycircuiteval-flow/SKILL.md`, Step A.1/A.2
- Project logic-depth policy: input prelogic `<= 8`, main pipeline stage target
  about 25 effective logic layers

## Objective

Objective: `balanced`

Priority:

1. Meet fixed latency `L=4`.
2. Keep input-pin to first-register logic `<= 8`.
3. Keep main stages near about 25 effective logic layers.
4. Then reduce area and power.

Default optimizer budget:

- Pass 0 topology selection before implementation.
- 2 post-build optimizer iterations after generated RTL and verification
  evidence exist unless the user explicitly specifies another budget.

## Pass 0 Decisions

| Item | Decision | Reason |
|---|---|---|
| Multiplier | Radix-4 Booth is the pass-0 target for S8-involved products; current implementation remains natural-width signed shift/add/sub style. S5*S5 uses direct signed partial products. | Keep structural Booth intent for timing/area optimization, but defer implementation until synthesis/timing/area evidence justifies the rewrite risk. |
| Dot8 reduction | Wallace-style carry-save compression tree with explicit `CMPE42`/`FA`/`HA` cells. | Avoids serial carry propagation through multiple adders and supports the 25-layer stage target. |
| Final CPA | Brent-Kung prefix adder for 16-bit and 19-bit final sums. | Better depth than RCA with lower wire/area pressure than Kogge-Stone. |
| Mode 2c shifter | Fixed shift-by-0/1/2 wiring plus 3-way mux. | Matches the bounded E1 scale requirement without full barrel-shifter cost. |
| Mode output MUX | Balanced staged 2:1 mux tree with pipelined mode decode. | Predictable shallow select path; avoids long priority chains. |
| Mode 2a `out1` policy | Hold previous registered `out1` value on valid mode-2a commit. | Minimizes unnecessary output toggles and satisfies stability without queue/FIFO state. |

## Estimated Logic Depth

These are pass 0 estimates only; multiplier and mapped prefix-adder depth must
be checked after RTL/synthesis.

| Cone | Estimate | Risk |
|---|---:|---|
| Input prelogic | About 2 to 6 effective layers | Should fit `<= 8` if limited to slicing, sign decode, mode decode, and shift-select decode. |
| S8-involved multiplier stage | Design-dependent, roughly medium depth | Highest unknown before RTL; Booth sign correction and local reduction must be inspected. |
| S5*S5 multiplier stage | Lower than S8 paths | Low to medium risk. |
| Dot8 Wallace compression | About 2 to 3 compressor layers | Medium risk if generated as serial `+` tree instead of explicit compression. |
| Brent-Kung final CPA 16/19-bit | About 15 to 20 effective layers | Expected to fit one stage with muxing, but close enough to require post-build inspection. |
| Mode 2c shift + merge | About 2 mux layers plus small CSA merge | Medium risk if placed in the same cone as final CPA without a register boundary. |
| Output mux | About 2 mux layers | Low risk if implemented as balanced tree. |

## Area / Power Tradeoff

- Booth S8 multipliers reduce partial-product rows and switching in wider paths,
  at the cost of recoder and correction logic.
- Direct S5*S5 multipliers avoid over-optimizing tiny lanes and should reduce
  area/power versus Booth for that case.
- Wallace compression uses more structured compressor cells and wiring than a
  simple adder tree, but avoids repeated carry propagation.
- Brent-Kung CPA is the balanced choice: less area/wire than Kogge-Stone and
  much lower depth than ripple for 16/19-bit sums.
- Mode 2c fixed shifters avoid the area/power of generic barrel shifters.
- Mode-2a `out1` hold adds a feedback mux but avoids toggling the registered
  output unnecessarily.

## Post-Build Checks

Run these after PyCircuit implementation, RTL generation, and functional
regression:

1. Confirm real register count gives `vld -> vld_out` latency `L=4` for all
   modes.
2. Confirm `out0`, `out1`, and `vld_out` are registered at the same boundary.
3. Inspect whether multiplier RTL matches the selected Booth/direct split.
4. Inspect whether Dot8 reduction uses compressor structure; flag any generic
   serial `+` tree as an implementation limitation.
5. Check that `CMPE42.Cox`/`Cix` chaining preserves bit weight and does not drop
   MSB chain carry.
6. Check mode 2c shift/merge is bounded shift-by-0/1/2, not a generic barrel
   shifter.
7. Check final CPA topology or inferred structure; compare Brent-Kung intent
   against generated RTL and synthesis mapping.
8. Check output mux depth and mode-2a `out1` hold behavior.
9. Collect area/timing/power reports if synthesis is available.
10. If reports show a critical path or excessive area/power, perform post-build
    optimizer iteration 1 of 2.

## Pass 1 (Post-Build) — Decision: `modify`

Pass: `1`

Provenance: `Optimizer-subagent-selected, pass 1`

Inputs:

- Generated RTL and post-build functional regression evidence
- Prior pass 0 topology intent
- Iteration 1 required fixes for CPA boundary and mode-2c merge structure

### Pass 1 Applied Changes

| Item | Pass 1 Decision | Implementation Status |
|---|---|---|
| Mode-2c low/high merge | Replace generic `+` with explicit `brent_kung_cpa_truncated(lo_scaled, hi_scaled, width)` | Done in `python/pe_int/lane_mac.py::sum_shift_pair()` |
| Dot8 terminal `Cox` policy | Apply explicit fixed-width truncation policy at final CPA boundary when carry weight is `2^W` and `W` is reducer output width | Done in `python/pe_int/lane_mac.py::_wallace_dot8_reduce()` and documented in `docs/design_spec.md` section 10.3 |
| Multiplier topology | Keep current natural-width signed functional implementation; defer full radix-4 Booth structural realization for S8-involved products | Deferred (`needs-synthesis / deferred structural improvement`) |

### Multiplier Deferred Note

Current multiplier implementation is functionally correct and natural-width with
sign-correction behavior, but does not yet provide a fully explicit radix-4
Booth structural realization for all S8-involved products in generated RTL.
This item is deferred to a follow-up structural iteration with synthesis-backed
evidence (`needs-synthesis / deferred structural improvement`).

### Rationale

1. The explicit BK CPA replacement removes mode-2c generic adder merge at the
   intended final-merge boundary.
2. The terminal `Cox` handling is now explicit policy rather than implicit
   behavior: out-of-range carry beyond fixed output width is intentionally
   truncated.
3. Multiplier structural conversion risk is non-trivial relative to current
   all-pass functional baseline; therefore deferred to avoid destabilizing RTL
   while preserving correctness.

## Pass 2

Provenance: `Optimizer-subagent-selected, pass 2`

Decision: `stop / keep current`

### Inputs Reviewed

- `docs/spec.md`
- `docs/design_spec.md`
- `docs/circuit_optimizer_report.md`
- `.cursor/skills/circuit-optimizer/SKILL.md`
- `python/pe_int/lane_mac.py`
- `python/pe_int/mac_modes.py`
- `python/pe_int/top.py`
- `rtl/build/pe_int.v`
- `rtl/build/pe_int_wallace_dot8_tree_w16.v`
- `rtl/build/pe_int_wallace_dot8_tree_w19.v`
- `filelist/pe_int.f`

### Pass 2 Findings

| Item | Status | Decision |
|---|---|---|
| Mode-2c final merge | Pass 1 fix is effective. `sum_shift_pair()` now calls `brent_kung_cpa_truncated(...)`; generated RTL shows bounded shift muxing followed by bit-level prefix carry logic, not a generic final `+`. | Keep current |
| Dot8 terminal `Cox` policy | Pass 1 fix is effective. `_wallace_dot8_reduce()` explicitly treats terminal `Cox` as weight `2^W` and intentionally truncates it at the fixed-width CPA boundary. This is now documented policy, not an accidental drop. | Keep current |
| Unused-bit / Verilator risk | No new structural unused-bit risk found in the inspected generated RTL. The latest final pass has no Verilator warning. | Keep current |
| Actual radix-4 Booth | Not implemented as explicit radix-4 Booth in generated RTL. Current multiplier remains natural-width signed shift/add plus sign-correction structure. Functional regressions pass, but structural Booth benefit cannot be proven without synthesis/timing/area evidence. | Defer to synthesis |
| Dot8 Wallace reducers | Generated W16/W19 reducer modules are explicit gate-level compressor/prefix structures with no `+` operator matches. | Keep current |
| Output commit / mode-2a `out1` hold | Generated RTL keeps `out1` as a registered state with valid-gated hold and mode-2a feedback hold. | Keep current |
| Model protocol coverage (mode-2a `out1`) | Added explicit model unittest coverage for hold/stable policy at valid commit boundary (`model/test_pe_int.py::test_mode2a_out1_hold_policy_on_vld_out`). | Keep current |

### RTL Proxy Metrics

No synthesis timing, area, or power report was available; the following are RTL
proxy estimates only.

| Metric | RTL Proxy Observation | Estimate |
|---|---|---|
| Input prelogic depth | Top RTL captures raw buses directly and only predecodes mode equality before `reg0`. | Fits `<= 8` effective layers. |
| Comb1 multiplier depth | Multipliers are unrolled shift/add/sub structures. Example S8 path shows serial add chain plus sign corrections. | Highest remaining depth risk; acceptable only because regressions pass and no synthesis evidence says it fails. |
| Comb2 Dot8 reduction depth | 5 W19 and 4 W16 Wallace reducer instances. Reducer files contain compressor/prefix gate structure and no `+` operator. | Better than serial adder tree; likely within intended reduction-stage target, but STA required. |
| Comb3 mode2c merge depth | Shift-by-0/1/2 muxing plus explicit 16/19-bit Brent-Kung-style prefix CPA and 2-level output mux. | Expected near but reasonable for ~25-layer target; STA required for closure. |
| Area proxy | Top RTL has 106 `PYC_REG` instances, 5 W19 reducers, 4 W16 reducers; top still contains many multiplier add/sub/mux nodes. | Area dominated by replicated natural-width multipliers and reducer instances. |
| Power proxy | Mode-2a `out1` hold reduces output toggling; fixed shifters avoid barrel shifter cost. Multipliers still likely toggle more than a true Booth implementation. | Current power is acceptable for functional closure; Booth may improve later if synthesis supports it. |

### Stop Condition

No further Pass 2 modification is recommended. The requested two post-build
optimizer iterations are complete, functional regressions are passing, Pass 1's
targeted risks are resolved, and the only meaningful remaining improvement is
structural radix-4 Booth. That item is deferred until synthesis/timing/area
evidence exists instead of being forced into the current all-pass RTL baseline.

## Current Topology / Status Audit

Decision: `stop / keep current`

The default optimizer budget is satisfied: pass 0 topology selection plus two
post-build optimizer iterations. Current closure is based on PyCircuit source
and generated RTL proxy evidence; no synthesis timing, area, or power report is
available.

Reset note: reset behavior is treated as a user-accepted known PyCircuit
framework limitation for this audit and is not a blocking topology issue.

Outstanding deferred item: explicit radix-4 Booth structural multiplier
implementation for S8-involved products. The current natural-width signed
multiplier implementation is functionally retained; structural Booth conversion
should wait for synthesis/timing/area evidence.

## Post-Build CircuitOpt Rerun — 2 Iterations

Provenance: `Optimizer-subagent-rerun, post-build default 2 iterations`

Scope: read-only topology audit against current `docs/design_spec.md`,
PyCircuit source, generated `rtl/build/*.v`, model tests, and latest regression
evidence. No DS/code/RTL/test changes were made by this optimizer rerun.

### Iteration 1/2 — Decision: `keep current`

Current synced RTL already contains the prior targeted post-build fixes:
mode-2c low/high merge uses explicit `brent_kung_cpa_truncated(...)`, and Dot8
terminal `Cox` is documented/implemented as intentional fixed-width truncation
at the final CPA boundary. No new blocking topology mismatch was found.

Keep-current evidence:

| Topology Item | Status | Evidence |
|---|---|---|
| Multiplier | `Partially implemented / Deferred` | PyCircuit uses natural-width signed shift/add/sub style product generation. Explicit radix-4 Booth for S8-involved products remains deferred until synthesis/timing/area evidence justifies the structural rewrite. |
| Dot8 Wallace reducer | `Implemented` | `PE_INT_WALLACE_DOT8_TREE_W19` and `PE_INT_WALLACE_DOT8_TREE_W16` generated modules are explicit compressor/prefix gate structures; no generic `+`/`*` operator is present in the reducer modules. |
| Final CPA | `Implemented` | `brent_kung_cpa_truncated()` is used for reducer final sums and mode-2c low/high merge; generated RTL shows bit-level prefix carry logic. |
| Mode2c shifter/merge | `Implemented` | Generated RTL uses bounded shift-by-0/1/2 plus muxing, then explicit prefix CPA merge. |
| Output mux | `Implemented` | Generated RTL uses staged 2:1 mux selection for mode output merge. |
| Mode2a `out1` hold | `Implemented` | Generated RTL keeps `out1` as registered feedback state with valid-gated mode-2a hold; model includes commit-boundary hold coverage. |
| Unused signal risk | `Keep current` | Latest regression evidence reports Verilator `-Wall`, no `-Wno-UNUSEDSIGNAL`, and no observed `UNUSEDSIGNAL`. |
| Regression evidence | `PASS` | Model unittest PASS, PyCircuit build/RTL sync PASS, compiled PyCircuit TB PASS, full RTL regression PASS on iverilog + Verilator. |

Reset note: reset behavior is treated as a user-accepted known PyCircuit
framework limitation for this audit and is not a blocking topology issue.

### Iteration 2/2 — Decision: `stop / keep current`

No further post-build modification is recommended within the default
two-iteration budget. The remaining meaningful topology improvement is explicit
radix-4 Booth structural realization for S8-involved multipliers, but current
evidence is functional/regression-only and does not include synthesis timing,
area, or power reports. Forcing that rewrite now would risk destabilizing an
all-pass baseline without proof of QoR benefit.

Stop condition:

- Default post-build iteration budget is complete.
- All required functional/regression evidence is passing.
- Optimizer-selected topologies are either implemented and evidenced, or
  explicitly deferred.
- No new unused-signal warning risk is reported.
- Reset limitation is noted as accepted framework behavior, not a blocking
  topology issue.

Next optimization candidate, if synthesis evidence becomes available: compare
current natural-width signed multiplier cones against explicit radix-4 Booth
implementations for timing, area, and power before changing PyCircuit source or
regenerated RTL.

