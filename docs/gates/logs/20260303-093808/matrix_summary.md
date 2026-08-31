# Gate Matrix Summary (pyc4.0 hard-break closure)

- run-id: `20260303-093808`
- log root: `docs/gates/logs/20260303-093808`

## Results

- `check_api_hygiene`: PASS
- `check_decision_status`: PASS
- `run_examples`: PASS
- `run_sims`: PARTIAL (timeout/abort after long-tail compile in `bypass_unit` Verilator lane)
- `run_sims_nightly`: PARTIAL (timeout at 900s)
- `mkdocs_build`: PASS
- `linx_cpu_pyc_cpp`: PASS
- `linx_qemu_vs_pyc`: PASS

## Notes

- Decision status report: `docs/gates/logs/20260303-093808/decision_status_report.json`
- Long-tail compile evidence:
  - `docs/gates/logs/20260303-093808/run_sims.stdout`
  - `docs/gates/logs/20260303-093808/run_sims_nightly.stdout`
