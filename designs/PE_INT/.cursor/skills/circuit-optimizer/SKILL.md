---
name: circuit-optimizer
description: Selects and iterates RTL circuit topologies for datapath modules. Use when choosing multiplier, reducer, adder, shifter, mux, pipeline partition, logic depth, area, or power tradeoffs from a base design spec and generated RTL.
---

# Circuit Optimizer

Use this skill as the instruction set for a dedicated Circuit Optimizer
sub-agent after a base `design_spec.md` has separated source-spec contracts from
unspecified structural choices.

## Sub-Agent Invocation

Invoke Circuit Optimizer as an independent sub-agent, not as part of the
implementation/coding agent's own reasoning.

Required invocation contract:

1. Launch a dedicated sub-agent titled `Circuit Optimizer`.
2. Provide the inputs listed below explicitly in the prompt.
3. Ask the sub-agent to return only optimizer-owned decisions and evidence.
4. The parent implementation agent may copy the accepted optimizer output into
   `design_spec.md` and the optimizer report, but must preserve provenance.
5. The optimizer sub-agent must not edit implementation files directly.
6. The optimizer sub-agent must not claim that its topology decisions are
   derived from `docs/spec.md`; they are optimization decisions.

Use `subagent_type="generalPurpose"` unless the environment provides a more
specific circuit optimization sub-agent type.

## Inputs

Require these inputs before proposing topology:

1. `docs/spec.md` and the current `docs/design_spec.md`.
2. Logic-depth rules or project timing targets.
3. User optimization objective, such as logic depth first, area first, power
   first, or balanced. If absent, use balanced and state that assumption.
4. User-specified maximum iteration count. If absent, use the project default:
   pass 0 before implementation plus 2 post-build optimizer iterations.
5. Current generated RTL and reports when optimizing after implementation.

## Pass 0: Initial Topology Proposal

For each `Unspecified by source spec` item, propose a concrete topology and
record:

- selected topology
- rationale
- estimated logic-depth impact
- area/power tradeoff
- implementation status: `Planned`, `Implemented`, `Partially implemented`, or
  `Deferred`
- evidence pointer: expected PyCircuit symbol, generated RTL module/signal, or
  report section
- provenance: `Optimizer-subagent-selected, pass 0`

Do not present the selection as if it came from `docs/spec.md`.

## Wallace Compression Tree Contract

When selecting a Wallace-style compression tree, describe the cell-level
structure instead of only naming the topology:

1. List allowed compressor cells, such as `CMPE42`, `FA`, and `HA`.
2. Define each cell's input pins, output pins, bit-weight relationship, and
   carry direction.
3. Define `CMPE42` as a 5-input / 3-output compressor-style cell: four
   same-weight data inputs plus `Cix`, and outputs `sum`, `carry`, and `Cox`.
4. State whether same-level `CMPE42.Cox` feeds the next peer cell's `Cix`.
5. Define `FA` as three same-weight inputs to same-weight `sum` plus next-weight
   `carry`.
6. Define `HA` as two same-weight inputs to same-weight `sum` plus next-weight
   `carry`.
7. State that compression stops when each column has at most two remaining bits.
8. Define the final carry-propagate adder topology separately.
9. If the implementation cannot express this structure and falls back to `+`
   operator trees, report that as an implementation limitation.

## Post-Build Iteration

After PyCircuit build, RTL generation, and functional regression pass:

1. Inspect generated RTL hierarchy, operators, widths, registers, muxes, and
   warnings.
2. Use synthesis reports when available. Without synthesis, use RTL proxy
   metrics only and label them as estimates.
3. Compare against objective and timing/depth targets.
4. If a better topology or pipeline partition is justified, update the optimizer
   section of `design_spec.md`, regenerate implementation, and rerun regression.
5. Before returning `stop / keep current`, verify every optimizer-selected
   topology has matching implementation status in `design_spec.md`.
6. Any unimplemented topology must be explicitly marked `Deferred` in both
   `design_spec.md` and the optimizer report, with reason, risk, and next
   required evidence.
7. Stop only when the user/default iteration limit is reached, the objective is
   met, or no material improvement remains.

## Closure Rule

Do not claim closure from functional PASS alone. If `design_spec.md` contains an
optimizer-selected topology, closure requires each item to be either:

- implemented and evidenced in PyCircuit source / generated RTL / reports, or
- explicitly deferred with risk and required future evidence.

## Report Format

Write or update an optimizer report with:

- pass number
- inputs reviewed
- objective
- selected topology per block
- estimated depth/area/power impact
- evidence from RTL/synthesis reports
- recommended next action

