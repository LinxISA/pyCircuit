---
name: pe-int-pycircuiteval-flow
description: Unified PE_INT flow using PyCircuitEval. Use when implementing PE_INT from spec, generating RTL from PyCircuit, and running model/tb/tb_rtl validation.
---

# PE_INT x PyCircuitEval Unified Flow

This skill standardizes PE_INT flow across sessions and machines:

- Single spec source: `docs/spec.md`
- Implementation language: PyCircuit frontend
- Deliverable RTL source: PyCircuit build (`pycircuit.cli build` / `pycc`)
- Verification layers:
  - `model/`: golden/reference and model regressions
  - `tb/`: PyCircuit test flow
  - `tb_rtl/`: RTL-specific simulation flow

## 0) When to Use

Enable this skill when the user asks to:

- implement PE_INT from `spec.md`
- generate RTL from PyCircuit source
- add testbench/regression coverage
- reproduce PE_INT flow on another machine

## 1) Source-of-Truth and Forbidden Actions

1. `docs/spec.md` is the only behavior contract.
2. Do not handwrite deliverable RTL.
3. Do not accept model-generated RTL as final deliverable.
4. If implementation conflicts with spec, fix implementation (do not silently rewrite spec).

## 2) Fallback If Profile Is Missing

If no existing PyCircuitEval profile is available on a fresh machine/session:

1. Learn latest PyCircuit flow from `LinxISA/pyCircuit`.
2. Confirm understanding of:
   - module/circuit structure
   - `pycircuit.cli build` / `pycc` flow
   - minimal device+tb compile/run pattern
3. Then start PE_INT implementation.

## 3) Standard PE_INT Flow (Fixed Order)

### Step A: Spec Checklist

Extract executable checks from `docs/spec.md`:

- top-level ports and widths
- mode 2a/2b/2c/2d math semantics
- fixed `vld -> vld_out` mapping and latency
- cross-mode stage consistency
- mode-2a `out1` stability policy

### Step A.1: Generate Design Spec

When deriving `design_spec.md` from `docs/spec.md`, make the design spec a
low-level circuit-structure document first, not a business-behavior restatement.

Required content:

1. Decouple business functions from circuit modules. Describe the reusable
   circuit modules first, then describe how each PE_INT mode maps onto their
   parameters, selects, and data paths.
2. Define low-level module contracts for basic components such as `MUL`, `ADD`,
   `SHIFTER`, `MUX`, `DEMUX`, D flip-flops/registers, comparators, counters,
   valid/ready or backpressure blocks, and FSMs when they exist.
3. For every circuit component, state input/output bit widths, signedness,
   latency/register boundary, reset behavior if stateful, and overflow/truncation
   contract. Do not add arbitrary margin widths.
4. Specify intended structures/topologies, for example Booth multiplier,
   compressor tree, carry-lookahead/prefix/ripple adder, mux tree, shifter
   style, pipeline register placement, and whether a block is pure
   combinational or stateful.
5. Describe bus routing explicitly: how buses are sliced, packed, muxed,
   demuxed, sign/zero-extended, shifted, and merged across pipeline stages.
6. For backpressure or flow-control blocks, define ready/valid behavior, stall
   propagation, skid/hold behavior, and what state is allowed to update during
   stalls.
7. For FSMs, list states, transition conditions, outputs per state, reset state,
   and illegal-state handling.
8. Include typical scenario waveform diagrams showing the protocol timing,
   latency convention, valid/data alignment, reset behavior, and mode-specific
   output stability requirements.
9. If `docs/spec.md` does not specify a structural topology, mark that item as
   `Unspecified by source spec` in `design_spec.md`. Do not infer topology from
   prior chat history, generated RTL, existing implementation, or reviewer
   comments.
10. If a structural topology is required for implementation or review, require
    either an upstream `docs/spec.md` update or an explicit user-approved
    structural-policy source before generating it into `design_spec.md`. A
    documented Circuit Optimizer pass may be used as this source, but its
    selections must be labeled as optimizer decisions rather than source-spec
    contracts.
11. For every optimizer-selected topology, include implementation status:
    `Planned`, `Implemented`, `Partially implemented`, or `Deferred`.
12. For each topology status, include evidence pointers to PyCircuit symbols,
    generated RTL modules/signals, reports, or deferred-risk notes.
13. Do not state a topology as implemented if the optimizer report or generated
    RTL shows it is deferred or only partially implemented.

### Step A.2: Circuit Optimizer Topology Selection

After generating the base `design_spec.md`, launch an independent Circuit
Optimizer sub-agent for items marked `Unspecified by source spec` when the user
asks for implementation topology selection or optimization. The implementation
agent must not silently make these topology choices itself.

Required inputs:

1. `docs/spec.md` and base `docs/design_spec.md`.
2. Logic-depth rules and project timing targets.
3. User optimization objective (`logic-depth`, `area`, `power`, or `balanced`).
4. User-specified maximum iteration count. If absent, use the project default:
   pass 0 before implementation plus 2 post-build optimizer iterations.
5. Generated RTL and synthesis reports for post-build optimization passes.

Rules:

1. Invoke the Circuit Optimizer as a dedicated sub-agent, titled
   `Circuit Optimizer`, with explicit inputs and requested output format.
2. Pass 0 may propose topology before implementation, using estimated
   logic-depth/area/power tradeoffs.
3. Write topology choices into an optimizer-owned section of `design_spec.md`
   with provenance such as `Optimizer-subagent-selected, pass 0`.
4. Keep the sub-agent output in an optimizer report so later implementation and
   review can trace decisions back to the optimizer pass.
5. Do not rewrite source-derived contracts as optimizer decisions.
6. After PyCircuit build, RTL generation, and functional regression pass, rerun
   the optimizer sub-agent using generated RTL and available reports.
7. If the optimizer recommends a topology or pipeline change, update
   `design_spec.md`, regenerate PyCircuit/RTL, and rerun regression.
8. If the optimizer selects a Wallace-style compression tree, expand it into
   cell-level contracts in `design_spec.md`: allowed `CMPE42`/`FA`/`HA` cells,
   cell input/output pins, bit-weight movement, `Cix`/`Cox` chaining policy,
   residual-column handling, compression termination, and final CPA boundary.
   The implementation must not silently replace this with generic `+` operator
   trees without reporting a limitation.
9. Before stopping, verify every optimizer-selected topology is either
   implemented and evidenced, or explicitly marked `Deferred` with reason, risk,
   and next required evidence in both `design_spec.md` and the optimizer report.
10. Stop when the user/default iteration limit is reached, the objective is met, or no
   material improvement remains.

### Step B: Implement in PyCircuit, Then Build RTL

1. Implement in `python/`.
2. Build through `pycircuit.cli build` / `pycc`.
3. After each build, sync deliverable RTL artifacts from the build output tree into `rtl/build/`.
4. Refresh `filelist/pe_int.f` after each sync so simulations consume the latest deliverable set.
5. Do not manually patch final RTL deliverables.
6. Use uppercase Verilog/PyCircuit module identifiers for deliverable RTL modules (for example `PE_INT`).
7. Use lowercase snake_case filenames for RTL deliverables (for example `pe_int.v`).

### Step B.1: PyCircuit Structure Rules

1. Keep pipeline stage boundaries explicit (`*_s0/*_s1/...` style).
2. Keep submodules separated under `python/pe_int/`.
3. Keep submodule core logic around 200 lines or less when practical.
4. Keep top-level focused on stage connection/alignment.
5. Keep control-path and datapath depth-aligned.
6. Pure combinational submodules should not expose or propagate `clk`/`rst`.
   Only stateful submodules should carry clock/reset ports; if a PyCircuit API
   forces clock/reset on a combinational hierarchy boundary, document it as a
   framework limitation and avoid adding internal logic that depends on them.

### Step B.2: Mandatory Stage Contract

Top-level must be reviewable as:

`input -> comb0 -> reg0 -> comb1 -> reg1 -> ... -> output`

No hidden full register-chain helper loops.

### Step B.3: Structural Self-Audit

After coding and before declaring implementation closure:

1. Compare the optimizer topology table in `docs/design_spec.md` against
   PyCircuit source.
2. Build generated RTL and compare PyCircuit source against `rtl/build/`.
3. For every topology item, report `Implemented`, `Partially implemented`, or
   `Deferred`.
4. If not implemented, update `docs/design_spec.md` and
   `docs/circuit_optimizer_report.md` before claiming closure.
5. Check natural-width product widths, multiplier topology, Wallace
   `CMPE42`/`FA`/`HA` structure, terminal carry/truncation policy, mode-2c
   shift/merge topology, final CPA topology, mode-2a `out1` hold, and absence of
   generic `+` trees where the design spec forbids them.

### Step C: Build Verification Assets

- `model/`: math + timing model regression
- `tb/`: PyCircuit testbench comparison flow
- Validate:
  - one-to-one valid mapping
  - fixed latency and cross-mode consistency
  - no bubble under full-pipeline working window
  - mode-2a `out1` hold/stable policy at model and RTL level
  - random valid timing and back-to-back mode switching
  - signed boundary vectors for S8/S4/S5 products

Model tests must validate protocol-visible behavior, not only math. If a
mathematical output is don't-care, the protocol model must still check the
chosen stability policy.

### Step C.1: Latency Counting Rule (Mandatory)

When checking latency / stage counts, always count the **actual number of
registers on the real signal path** in generated RTL.

Do **not** infer latency only from stage naming (`s0/s1/...`) or comment labels.

Required checks:

1. Count register depth separately for control (`vld_out`) and data (`out0/out1`).
2. Confirm control/data path depths are aligned at output boundary.
3. If mismatch exists, report effective latency based on register count.

### Step D (Optional): RTL Simulator Validation

Only run this step after user confirms explicitly:

1. Build RTL from PyCircuit source.
2. Run `tb_rtl/` testcases on both `iverilog` and `verilator`.
3. Report pass/fail and key outputs.
4. Save or update a regression summary file such as
   `docs/regression_report.md` containing command lines, date/time, worktree
   status or git hash, PyCircuit build result, PyCircuit TB result, model
   unittest result, vector generation result, iverilog case results, Verilator
   case results, and warning summary.

## 4) Current Vector Baseline

Current project baseline:

- `tc_mode2a_sanity`: 1000
- `tc_mode2a_sanity_rand_timing`: 1000
- `tc_mode2b_sanity`: 1000
- `tc_mode2b_sanity_rand_timing`: 1000
- `tc_mode2c_sanity`: 1000
- `tc_mode2c_sanity_rand_timing`: 1000
- `tc_mode2d_sanity`: 1000
- `tc_mode2d_sanity_rand_timing`: 1000
- `tc_mode_switch_random`: 1000

Simulation script baseline:

- multi-seed runs supported (default 10)
- runtime vector loading via `+GEN_DIR`
- logs: `sim/logs/<simulator>/...`
- waves: `sim/waves/<simulator>/...`

## 5) Acceptance Checklist

All of the following must pass:

1. PyCircuit source can reproducibly rebuild RTL.
2. Deliverable RTL is generated by PyCircuit/pycc, not handwritten.
3. `model/` and `tb/` regressions pass.
4. README documents:
   - build commands
   - test commands
   - fixed latency/alignment behavior

If optional Step D is approved:

- `tb_rtl/` testcases run successfully
- `iverilog` passes
- `verilator` passes
- `docs/regression_report.md` records the latest full regression evidence

No flow stage may claim closure by functional PASS alone. If `design_spec.md`
contains optimizer-selected topology, closure requires spec/design/code/RTL/model/test
status to be either implemented and evidenced, or explicitly deferred with risk
and next required evidence.

## 5.1) Pre-Push Review Gate

Before any `git push` or PR creation, apply the project skill
`pre-push-code-review-gate`.

Mandatory rule:

1. Launch a dedicated `codereviewer` sub-agent before pushing.
2. Provide changed files, source spec, design spec, optimizer report when
   present, generated RTL context, and latest regression evidence.
3. Fix all blocking findings before push.
4. If the reviewer reports only non-blocking findings, record the rationale for
   proceeding.
5. Do not push if the `codereviewer` sub-agent has not run.

## 6) Fixed Debug SOP

On fail, always use:

`Seed -> Model -> PyCircuit -> RTL`

No step skipping.

