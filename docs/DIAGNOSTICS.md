# Diagnostics

pyCircuit uses one structured, source-located diagnostic style across:
- API hygiene scan (`flows/tools/check_api_hygiene.py`)
- CLI pre-JIT contract scan (`pycircuit emit/build`)
- JIT elaboration errors (Python frontend)
- MLIR pass errors in `pycc` (backend)

## Format

Human-readable diagnostics generally look like:

- `path:line:col: [CODE] message`
- `stage=<stage>`
- optional source snippet
- optional `hint: ...`

## Common stages

- `api-hygiene`: repository/static scan
- `api-contract`: CLI pre-JIT scan of entry file + local imports
- `jit`: frontend elaboration errors
- MLIR pass errors from `pycc` (for example `pyc-check-frontend-contract`)

## Frontend contract marker

All frontend-emitted `.pyc` files are stamped with a required module attribute:

- `pyc.frontend.contract = "pycircuit"`

If the backend sees a missing/mismatched contract marker, `pycc` fails early.

## Useful commands

Run hygiene scan:

```bash
python3 /Users/zhoubot/pyCircuit/flows/tools/check_api_hygiene.py
```

Emit + compile one module:

```bash
PYTHONPATH=/Users/zhoubot/pyCircuit/compiler/frontend \
python3 -m pycircuit.cli emit /Users/zhoubot/pyCircuit/designs/examples/counter/counter.py -o /tmp/counter.pyc

/Users/zhoubot/pyCircuit/.pycircuit_out/toolchain/install/bin/pycc /tmp/counter.pyc --emit=cpp --out-dir /tmp/counter_cpp
```
