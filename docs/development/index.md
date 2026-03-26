# Development Guide

This page lists the active pyc4.0 development entrypoints and gate commands.

## Core references

- `docs/rfcs/pyc4.0-decisions.md`
- `docs/updatePLAN.md`
- `docs/gates/README.md`
- `docs/gates/decision_status_v40.md`

## Build and gate commands

- `bash flows/scripts/pyc build`
- `bash flows/scripts/run_examples.sh`
- `bash flows/scripts/run_sims.sh`
- `bash flows/scripts/run_sims_nightly.sh`

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
- `docs/TESTBENCH.md`
- `docs/IR_SPEC.md`
- `docs/DIAGNOSTICS.md`
- `designs/examples/README.md`

## Getting Help

- GitHub Issues: Report bugs and request features
- GitHub Discussions: Ask questions and share ideas
- Discord: Join our community chat
