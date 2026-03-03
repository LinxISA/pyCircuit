# Examples

This directory contains folderized pyCircuit examples.

## Layout contract

Each example case `X` is a folder:
- `X/X.py`: design (`@module build(...)`)
- `X/tb_X.py`: testbench (`@testbench def tb(...)`)
- `X/X_config.py`: default params + TB presets + `SIM_TIER`

## Smoke checks

Compiler smoke (`emit + pycc`):

```bash
bash /Users/zhoubot/pyCircuit/flows/scripts/run_examples.sh
```

Simulation smoke (strict normal-tier examples, C++ + Verilator):

```bash
bash /Users/zhoubot/pyCircuit/flows/scripts/run_sims.sh
```

Nightly simulation smoke (normal + heavy tiers):

```bash
bash /Users/zhoubot/pyCircuit/flows/scripts/run_sims_nightly.sh
```

Semantic closure lane (v4.0 deferred-decision regressions):

```bash
bash /Users/zhoubot/pyCircuit/flows/scripts/run_semantic_regressions_v40.sh
```

## Refresh Procedure (pyc4.0)

Use a single run-id to refresh compile/sim evidence and decision coverage artifacts:

```bash
RUN_ID=20260303-pyc40-refresh
PYC_GATE_RUN_ID="${RUN_ID}" \
PYC_DECISION_STATUS_STRICT=1 \
bash /Users/zhoubot/pyCircuit/flows/scripts/run_examples.sh
```

Strict decision coverage can also be invoked directly:

```bash
python3 /Users/zhoubot/pyCircuit/flows/tools/check_decision_status.py \
  --status /Users/zhoubot/pyCircuit/docs/gates/decision_status_v40.md \
  --out /Users/zhoubot/pyCircuit/.pycircuit_out/gates/${RUN_ID}/decision_status_report.json \
  --require-no-deferred \
  --require-all-verified \
  --require-concrete-evidence \
  --require-existing-evidence
```

## Semantic smoke examples (v4.0)

- `xz_value_model_smoke`: validates v3 trace value payload (`value`, `known`, `z`) emission.
- `reset_invalidate_order_smoke`: validates reset/invalidate ordering in trace events.
- `net_resolution_depth_smoke`: validates hierarchical combinational depth propagation in a simple chain.

## Artifact policy

Generated artifacts are local-only and written under:
- `.pycircuit_out/`

They are intentionally not checked into git.

## Linx/board-related designs

Linx CPU / LinxCore / board bring-up examples are kept under `contrib/` and are
not part of the core example smoke gates.
