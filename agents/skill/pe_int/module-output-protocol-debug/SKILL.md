---
name: module-output-protocol-debug
description: Debugs hardware module output protocol violations. Use when module outputs do not match the waveform protocol described by the spec, including latency, valid/data alignment, hold behavior, handshake timing, output stability, or transaction ordering issues.
---

# Module Output Protocol Debug

## Trigger

Use this when a module's observed output waveform does not comply with its specified output protocol, including:

- Output latency mismatch.
- Valid, ready, enable, or control signal timing mismatch.
- Data and qualifier signals not committed at the same transaction boundary.
- Output hold or stability policy violation.
- Unexpected output toggling.
- Missing, early, late, or reordered output transaction.
- Scoreboard reports protocol-like errors such as `unexpected valid`, `missing valid`, or a consistent cycle offset.

## Required Spec Contract

Before debugging, check whether the corresponding spec describes the waveform protocol.

The spec should define, when applicable:

- Input sampling point.
- Output commit point.
- Latency convention, for example `input sampled at t0 -> output valid at t0 + L`.
- Which output signals must be aligned.
- Valid, ready, and enable semantics.
- Output hold or stability policy.
- Reset behavior.
- Transaction ordering rules.

If the spec does not define the waveform protocol clearly, stop. Do not edit any spec from this debug flow. Ask the user to clarify the design intent and to update the upstream spec source before continuing.

## Debug Flow

1. Check the spec first.
   - Find the latency and waveform protocol description.
   - Confirm the latency counting convention.
   - Confirm which signals are required to be aligned or stable.
   - If the spec is ambiguous, stop and ask the user for the design intent.
   - Do not modify `design_spec.md`, generated specs, or their source specs in this debug flow.
   - Ask the user to update the upstream source spec and regenerate derived specs before continuing.

2. Use waveform evidence to identify the violating signal.
   - Generate a small waveform case, preferably 1 to 10 transactions.
   - Mark the first accepted input transaction.
   - Mark the expected output cycle from the spec.
   - Compare each output and control signal against the spec.
   - Classify the violation by signal, not by assumption.

3. Separate possible causes.
   - If DUT waveform matches spec but scoreboard fails, debug the testbench or scoreboard.
   - If a control or valid signal violates spec, debug the control path.
   - If a data signal violates spec, debug the datapath.
   - If only a hold or stability rule fails, debug the output update policy.
   - If ordering fails, debug transaction queueing or pipeline alignment.

4. Count real RTL register paths.
   - Count registers on each violating signal path.
   - Count control and data paths separately.
   - Do not infer latency from signal names, comments, or stage labels.
   - In generated RTL, look for hidden balance registers such as `_v5_bal_*`.

5. Fix only the violating path.
   - Do not delay or alter signals that already match the spec.
   - If `valid` matches the spec but data is late, fix the data path.
   - If data matches the spec but `valid` is early or late, fix the control path.
   - If the scoreboard has the wrong latency convention, fix the scoreboard, not the DUT.

6. Re-verify with waveform first.
   - Re-run the small waveform case.
   - Confirm all protocol signals match the spec.
   - Then run focused regression.
   - Then run full regression.

## Done Criteria

- Spec contains a clear waveform protocol before implementation/debug fixes proceed.
- Any spec ambiguity was clarified by the user and resolved outside this debug flow.
- Waveform shows all output signals comply with the spec.
- Real RTL path register counts support the observed latency.
- Scoreboard latency convention matches the spec.
- Focused waveform test passes.
- Full regression passes.
