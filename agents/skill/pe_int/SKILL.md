---
name: pipeline-latency-depth-guard
description: Checks latency consistency and stage logic-depth reasonableness for pipelined hardware designs. Use when editing stage boundaries, valid/data alignment, pipeline partitioning, or latency/depth contracts.
---

# Pipeline Latency and Depth Guard

## 1) Latency Definition Must Be Explicit
- Define latency convention before implementation (0-based or 1-based).
- Define clear reference points (for example: input sample -> output commit).
- If ambiguous, stop and ask user to clarify.

## 2) Contract Consistency Before Coding
Do this before implementation:
1. Read the latency contract in functional spec.
2. Read the stage contract in design spec.
3. Confirm both use the same semantics.
4. If mismatch exists, fix docs first.

## 3) Real-Path Latency Validation
- Validate latency by counting real register boundaries on generated RTL signal paths.
- Never infer latency from stage names, comments, or signal naming only.
- Validate at least:
  - control path (`valid` or equivalent handshake)
  - each committed output datapath

## 4) Commit-Point Alignment
- Control qualifier and committed outputs must come from the same transaction boundary.
- Avoid cross-stage mixing (early control with later data, or vice versa).
- Hold/stall policy updates must align with output commit control point.

## 5) Logic-Depth Guidance (Guideline, Not Hard Constraint)
- Stage logic-depth target is around 25 layers.
- Merging multiple combinational blocks into one stage is allowed.
- After any merge/partition change, re-check stage logic depth.

Decision policy:
- `<= 25`: within target, no action needed.
- `26 ~ 30`: acceptable as guideline overflow; must warn user and suggest optional repartition.
- `> 30`: strong risk; propose repartition or structural optimization and request user decision.

## 6) Definition of Done (DoD)
- [ ] Latency convention is explicitly documented.
- [ ] Functional spec and design spec latency semantics are consistent.
- [ ] Real RTL path register-count evidence is provided.
- [ ] Stage logic depth check is recorded (including warning if 26~30).
- [ ] Regression/simulation passes.
- [ ] No transaction reorder under continuous valid traffic.
