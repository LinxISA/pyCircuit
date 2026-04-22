---
name: pe-int-implementation
description: Project-level implementation guidance for PE_INT. Use for PE_INT coding choices, pipeline structure, latency alignment, power/area trade-offs, and debug order.
---

# PE_INT Implementation Guidance (Non-Formal)

This is a project skill for implementation intent and practical trade-offs.
It does **not** override frozen rules in `docs/spec.md`.
If there is any conflict, `docs/spec.md` wins.

## Hard Rule: RTL Source Ownership

1. Deliverable RTL must be generated from PyCircuit source through `pycircuit.cli build` / `pycc`.
2. Do not accept model-generated RTL as deliverable output.
3. Do not hand-edit generated RTL for final delivery.
4. If spec is ambiguous, ask the user for clarification before changing behavior.

## Fixed Debug SOP (Seed -> Model -> PyCircuit -> RTL)

When any testcase fails, follow this order:

1. Record failing seed, testcase, and simulator.
2. Reproduce with the same seed and regenerate vectors.
3. Run model checks first; if model fails, fix model/spec alignment first.
4. If model passes, run PyCircuit checks.
5. If PyCircuit fails, fix `python/` and rebuild RTL.
6. Only debug RTL-level issues after model and PyCircuit both pass.
7. Re-verify failing case first, then run full regression.

Use `pass/fail` wording in reports.

## Regression Notes

- Sanity and random vectors are seed-dependent and reproducible.
- Runtime vector loading (`$readmemh` + `+GEN_DIR`) is preferred over compile-time baked expected data.
- Default output paths:
  - logs: `sim/logs/`
  - waves: `sim/waves/`

## `vld` to `vld_out` and Pipeline Depth

- Enforce one-to-one mapping from sampled `vld` to `vld_out`.
- Keep fixed latency `L` across all modes.
- Keep internal pipeline stage count identical across all modes.
- Use full pipeline datapath; no independent FIFOs for reordering.

## Power and Area Priority

After functionality and logic-depth constraints are satisfied:

1. Reduce dynamic power as primary priority.
2. Reduce area as secondary priority.

If area and dynamic power conflict, prioritize dynamic power.

## PyCircuit Structure Requirements

1. Stage boundaries must be explicit and reviewable.
2. Keep submodules split into separate files.
3. Keep each submodule core logic around 200 lines or less when possible.
4. Keep top-level focused on stage orchestration and alignment.
5. Control and datapath must advance at matching stage depth.

## Mandatory Stage Coding Contract

Top-level coding must be explicit:

`input -> comb0 -> reg0 -> comb1 -> reg1 -> ... -> combN -> regN -> output`

Rules:

1. Stage boundaries must be visible at top-level.
2. Each stage must define combinational work before register transfer.
3. Datapath and control-path must both be reviewable per stage.
4. `vld/mode/enable/select` control signals must stay aligned with payload.
5. Do not hide full pipeline register chains inside opaque helper loops.

