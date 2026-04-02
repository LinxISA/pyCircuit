# Development Guide

This section collects the repo-facing workflow rules for pyCircuit contributors
and coding agents.

The public frontend surface is the current pyc5 API. The active semantic
evidence corpus remains:

- `docs/rfcs/pyc4.0-decisions.md`
- `docs/updatePLAN.md`
- `docs/gates/decision_status_v40.md`

## Read in this order

1. `docs/development/contributing-workflow.md`
2. `docs/development/testing-and-gates.md`
3. `docs/development/review-and-merge.md`
4. `docs/gates/README.md`

## Standard commands

- `bash flows/scripts/pyc build`
- `bash flows/scripts/run_examples.sh`
- `bash flows/scripts/run_sims.sh`
- `bash flows/scripts/run_sims_nightly.sh`
- `bash flows/scripts/run_semantic_regressions_v40.sh`
- `python3 flows/tools/check_api_hygiene.py compiler/frontend/pycircuit designs/examples docs README.md`

## Guide map

- `contributing-workflow.md`: contributor workflow, blocker handling, and
  documentation expectations
- `testing-and-gates.md`: required validation matrix by change type
- `review-and-merge.md`: review standard, PR content, and merge blockers

## Related references

- `docs/FRONTEND_API.md`
- `docs/TESTBENCH.md`
- `docs/IR_SPEC.md`
- `docs/DIAGNOSTICS.md`
- `docs/gates/README.md`
