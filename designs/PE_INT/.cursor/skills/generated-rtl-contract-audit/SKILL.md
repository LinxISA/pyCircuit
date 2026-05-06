---
name: generated-rtl-contract-audit
description: Audits generated RTL deliverables against module contracts. Use after PyCircuit/DSL/HLS RTL builds, before PR review, or when checking generated Verilog reset style, ports, module names, filenames, filelists, stale artifacts, and deliverable compliance.
---

# Generated RTL Contract Audit

## Instructions

Use this skill whenever implementation source generates RTL deliverables.

Do not treat source-level tests or model regressions as sufficient proof that deliverable RTL matches the contract. After every RTL generation step, inspect generated artifacts directly.

## Audit Checklist

1. Confirm top-level module ports, widths, clock, reset, valid/ready, and naming match the spec.
2. Confirm reset polarity and reset assertion/release style in generated registers and primitives.
3. Confirm module identifiers follow the project naming rule.
4. Confirm RTL filenames follow the project filename rule.
5. Confirm filelists reference the latest generated deliverables.
6. Confirm stale generated modules/files are removed or excluded.
7. Confirm submodules instantiated by the top are present in the filelist.
8. Confirm generated RTL has no unexpected compatibility wrapper unless explicitly required.
9. Check simulator/lint warnings for unused signals, unused bits, and unused
   ports. Treat `UNUSEDSIGNAL` as a review item even when functionality passes.

## Required Evidence

When closing the task, report:

- Which generated files were audited.
- The key contract points checked.
- The command or search used to prove there are no stale names/files.
- Any unused-signal warnings found, why they exist, and whether source changes
  can remove them instead of suppressing them.
- Regression status after the audit.

## Common Failure Patterns

- A build passes but filelist still points at an older RTL file.
- Generated primitives use a reset style different from the spec.
- Top module name is patched but submodule or primitive names are not.
- Source module names are correct but deliverable filenames violate repo convention.
- Old build directories leak stale modules into synced RTL.
- Generated RTL contains unused ports/signals/bits that come from stale
  intermediates, over-wide expressions, or copied submodule interfaces.
