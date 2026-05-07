---
name: pre-push-code-review-gate
description: Requires a codereviewer sub-agent review before git push. Use when preparing PE_INT changes for push, PR creation, or any remote publication after implementation, generated RTL, docs, or test changes.
---

# Pre-Push Code Review Gate

Use this skill before any `git push` or PR creation that publishes PE_INT
changes to a remote repository.

## Mandatory Gate

Before pushing:

1. Confirm the intended push scope and current changed files.
2. Confirm required builds/regressions have been run or explicitly record why
   they were not run.
3. Launch a dedicated `codereviewer` sub-agent.
4. Provide the reviewer with:
   - changed files and a concise change summary
   - relevant specs: `docs/spec.md`, `docs/design_spec.md`
   - relevant reports: `docs/circuit_optimizer_report.md` when present
   - latest build/regression evidence
   - known deferred items or accepted limitations
   - optimizer topology implementation status and evidence
5. Treat blocking findings as must-fix before push.
6. If findings are non-blocking, record the rationale for proceeding.
7. Only push after the review result is clean or all blocking findings are
   resolved and rechecked.

## Reviewer Prompt Requirements

The `codereviewer` prompt must ask for:

- functional correctness risks
- spec/design_spec mismatch
- generated RTL consistency with PyCircuit source
- model/testbench/golden-reference mismatch
- latency/control-data alignment issues
- width/sign-extension/truncation risks
- unused/dead signal risks
- missing regression evidence
- optimizer-selected topology that is claimed but not implemented
- mode-2a `out1` hold coverage in model and RTL tests
- whether the changes are safe to push

Known limitation handling:

- The current PE_INT reset release mismatch caused by PyCircuit framework reset
  generation may be marked as a known deferred framework limitation when the
  user has explicitly accepted it for the current push.
- The reviewer should still report it, but it should not block by itself unless
  new reset behavior was changed or the limitation is no longer documented.

## Stop Conditions

Do not push if:

- the `codereviewer` sub-agent has not run
- the review reports unresolved blocking issues
- generated RTL was changed but not rebuilt from PyCircuit
- required regression evidence is missing without explicit user approval
- `design_spec.md` claims an optimizer-selected topology is implemented while
  PyCircuit source or generated RTL shows it is deferred or absent

