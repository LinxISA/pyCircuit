# Gate Evidence Framework (pyc4.0)

This directory standardizes pyc4.0 gate evidence and decision-status tracking.

## Log Root Contract

- Required root: `docs/gates/logs/<run-id>/`
- `<run-id>` format recommendation: `YYYYMMDD-HHMMSS` (override via env if needed)

## Required Artifacts Per Run

Each run directory must include:

- `commands.txt`: exact commands executed in order
- `<gate>.stdout` and `<gate>.stderr`: raw command outputs
- `summary.json`: pass/fail summary with durations
- `decision_status_report.json`: output from `flows/tools/check_decision_status.py`
- `cases/run_sims/<case>/...`: per-case logs for `flows/scripts/run_sims.sh`
- `cases/run_sims_nightly/<case>/...`: per-case logs for `flows/scripts/run_sims_nightly.sh`

## Decision Status Source

- Status file: `docs/gates/decision_status_v40.md`
- Contract source: `docs/rfcs/pyc4.0-decisions.md`

`check_decision_status.py` enforces:

1. Every decision ID in the RFC appears exactly once in the status table.
2. Status values are in the allowed set:
   - `implemented-verified`
   - `implemented-unverified`
   - `gap-in-scope`
   - `deferred`
3. No row remains `gap-in-scope`.

For decision-complete closure, run strict mode:

- `python3 flows/tools/check_decision_status.py --status docs/gates/decision_status_v40.md --out .pycircuit_out/gates/<run-id>/decision_status_report.json --require-no-deferred --require-all-verified --require-concrete-evidence --require-existing-evidence`

## Notes

- Deep semantic items intentionally deferred in this phase remain marked
  `deferred` with explicit next actions.
- Gate outputs under `.pycircuit_out/` are transient; curated evidence for review
  should be mirrored into `docs/gates/logs/<run-id>/`.
- For decision-complete closure runs, include semantic lane evidence from
  `flows/scripts/run_semantic_regressions_v40.sh`.
