# Development Guide

This page lists the active pyc4.0 development entrypoints and gate commands.

## Core references

- `docs/rfcs/pyc4.0-decisions.md`
- `docs/updatePLAN.md`
- `docs/gates/README.md`
- `docs/gates/decision_status_v40.md`

## Build and gate commands

- `bash flows/scripts/pyc build`
- `bash flows/scripts/run_examples.sh` — G1 (includes semantic regressions unless `PYC_SKIP_SEMANTIC_REGRESSIONS=1`)
- `bash flows/scripts/run_sims.sh` — G2 merge-blocker
- `bash flows/scripts/run_sims_nightly.sh` — G3 nightly
- `python3 flows/tools/summarize_gate_run.py --run-id <id>` — render gate matrix

### PR CI gates

GitHub Actions (`.github/workflows/ci.yml`) runs G0 + G1 + G2 on every PR.
G3 runs in `.github/workflows/gates-nightly.yml` (schedule / manual).
See `docs/gates/README.md` for the level mapping and artifact names.

Local reproduce (Linux, after toolchain build):

```bash
export PYC_TOOLCHAIN_ROOT="$PWD/.pycircuit_out/toolchain/install"
export PATH="$PYC_TOOLCHAIN_ROOT/bin:$PATH"
export PYC_GATE_RUN_ID="local-$(date +%Y%m%d-%H%M%S)"
unset PYC_SKIP_SEMANTIC_REGRESSIONS
bash flows/scripts/run_examples.sh
bash flows/scripts/run_sims.sh
```

## Repository layout

pyCircuit is organized as follows:

```
pyCircuit
├── compiler/
│   ├── frontend/          # Python-based frontend
│   │   └── pycircuit/    # Core DSL implementation
│   └── mlir/             # MLIR-based backend
│       ├── lib/          # Dialect definitions
│       └── tools/        # Compiler tools
├── runtime/
│   ├── cpp/              # C++ simulation runtime
│   └── verilog/          # Verilog primitives
├── designs/
│   └── examples/         # Example designs
└── docs/                 # Documentation
```

## Quick Links

- `docs/FRONTEND_API.md`
- `docs/PyCircuit_V5_Spec.md`
- `docs/TESTBENCH.md`
- `docs/IR_SPEC.md`
- `docs/DIAGNOSTICS.md`
- `designs/examples/README.md`

## Getting Help

- GitHub Issues: Report bugs and request features
- GitHub Discussions: Ask questions and share ideas
- Discord: Join our community chat

## Sidecar testbench schedule tests

The sidecar schedule container is covered by:

```bash
PYTHONPATH=compiler/frontend:. python -m pytest tests/test_sidecar_sections.py -q
```
