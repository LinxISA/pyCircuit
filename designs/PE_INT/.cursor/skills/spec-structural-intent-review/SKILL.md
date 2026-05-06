---
name: spec-structural-intent-review
description: Reviews implementation against structural intent in specs, not just functional behavior. Use when specs mention pipeline stages, latency, logic depth, compressor trees, hierarchy, arithmetic topology, datapath width contracts, or other implementation structure.
---

# Spec Structural Intent Review

## Instructions

Use this skill when a spec or design spec describes how the hardware should be structured.

Passing functional tests is not enough when the spec requires a particular structure. Check whether the implementation matches the intended pipeline, hierarchy, arithmetic topology, and width/depth contracts.

## Review Checklist

1. Identify structural requirements in the spec before reading code:
   - pipeline stage boundaries
   - latency definition
   - control/data alignment
   - arithmetic topology
   - logic depth guidance
   - submodule hierarchy
   - datapath input/output widths
2. Compare source implementation with each structural requirement.
3. Compare generated RTL with the same requirements when available.
4. For latency, count actual registers on the real signal path.
5. For arithmetic, verify the requested topology is present, not just functionally equivalent.
6. For width contracts, verify each operation's input/output width has no arbitrary margin and no overflow in covered scenarios.
7. Review generated RTL warnings for unused signals/bits/ports. Do not ignore
   them only because simulation passes; unused logic increases later dead-code
   cleanup cost and can hide source-structure mistakes.

## When To Warn

Warn the user when:

- A linear reduction is used where a tree/compressor structure was specified.
- A pipeline stage label exists but the actual register path differs.
- Multiple comb blocks are merged without checking logic depth.
- A generated design adds hidden balancing registers that change effective latency.
- A datapath expression relies on implicit or private width behavior.
- Generated RTL exposes unused signals, unused high bits, or unused clock/reset
  ports on combinational submodules.

## Required Evidence

Report both:

- Functional result: whether model/PyCircuit/RTL tests pass.
- Structural result: whether the implementation matches the spec's structural intent.
