---
name: expression-width-mismatch-debug
description: Debugs arithmetic expression width mismatches in RTL, PyCircuit, HLS, and generated hardware. Use when arithmetic results differ from the model, shifts/scales behave incorrectly, small-bit operands overflow unexpectedly, or generated RTL inferred narrower intermediate widths than intended.
---

# Expression Width Mismatch Debug

## Trigger

Use this when:

- RTL arithmetic differs from the Python/C golden model.
- Only specific modes, scale factors, shift paths, or boundary values fail.
- Failures are value-only after timing, latency, and valid/data alignment are proven correct.
- Results differ by scale factors such as `x1/x2/x4`.
- Small expressions such as `1-bit + 1-bit`, shifts, masks, sign extension, or zero extension are involved.

## Debug Flow

1. Rule out timing first.
   - Confirm output protocol, latency, and scoreboard math are correct.
   - Confirm valid/control and data outputs describe the same transaction.
   - If timing is wrong, use a protocol/alignment debug flow before this skill.

2. Reduce to the first failing transaction.
   - Freeze seed and vector files.
   - Extract mode, operands, control fields, expected output, and actual output.
   - Prefer a one-vector or ten-vector testcase with waveform enabled.

3. Compute model intermediate values.
   - Decode operands.
   - Compute partial products.
   - Compute reductions.
   - Compute shift/control values.
   - Compute final scaled/muxed outputs.

4. Compare RTL internal signals.
   - Probe decoded operands, partial sums, shift amounts, scaled values, and final output mux inputs.
   - Find the first intermediate signal where RTL diverges from the model.

5. Inspect expression widths.
   - Check generated RTL declarations for intermediate wire widths.
   - Check whether operands were sign-extended or zero-extended before arithmetic.
   - Check whether constants were inferred too narrow.
   - Remember that hardware DSLs may keep `1-bit + 1-bit` as a 1-bit result unless explicitly extended.

6. Fix the narrowest incorrect expression.
   - For signed data arithmetic, sign-extend before multiply/add.
   - For control, shift, mask, and index arithmetic, zero-extend before add/compare/shift selection.
   - Do not add arbitrary extra margin; choose the minimum width that covers the specified scenarios.

## Common Pattern

Bad pattern:

```text
shift = e1_a_bit + e1_b_bit
```

If both operands are 1-bit hardware values, `1 + 1` may overflow to `0`.

Preferred pattern:

```text
shift = zext(e1_a_bit, 2) + zext(e1_b_bit, 2)
```

This covers shift values `{0,1,2}` without extra margin.

## Done Criteria

- First failing transaction matches the model at every inspected intermediate point.
- Generated RTL shows intended intermediate widths.
- Boundary cases for carry, sign bit, max shift, and min/max operands pass.
- Focused testcase passes.
- Full affected regression passes.
