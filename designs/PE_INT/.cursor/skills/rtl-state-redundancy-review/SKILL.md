---
name: rtl-state-redundancy-review
description: Reviews RTL/PyCircuit state for redundant registers and feedback paths. Use when output hold logic, valid/data alignment, state registers, feedback muxes, or pipeline output commits are added or modified.
---

# RTL State Redundancy Review

## Instructions

Use this skill when reviewing or editing stateful RTL or DSL-generated hardware.

Functional tests may pass even when the implementation contains duplicated registers. Inspect state ownership and generated RTL register names directly.

## Review Checklist

1. Identify every state/register introduced by the change.
2. For each register, ask what unique value it owns.
3. Check whether an output register can serve as its own feedback/hold state.
4. Check whether two registers are loaded from the same next-value expression under the same clock/reset.
5. Check whether removing a register changes latency, valid alignment, reset value, or hold behavior.
6. After refactor, search generated RTL to confirm the redundant register disappeared.

## Common Failure Patterns

- Separate `*_hold` and output registers store the same committed value.
- Valid/data signals use different commit boundaries.
- A helper register is added to make a mux easier but later becomes redundant.
- A state register is kept for "stability" even though the output register already provides stability.

## Required Validation

After removing or merging state:

1. Run source-level regression.
2. Regenerate RTL.
3. Search generated RTL for removed register names.
4. Run RTL regression.
5. Report whether output protocol and latency remained unchanged.
