# Diagnostics (v3.4)

pyCircuit v3.4 uses one structured diagnostic shape across:
- API hygiene checker
- CLI source contract scanner
- JIT compile errors
- MLIR frontend-contract verification

## Diagnostic format

Human-readable diagnostics default to:

- `path:line:col: [CODE] message`
- `stage=<stage>`
- optional source snippet
- optional `hint: ...`

## Main stages

- `api-hygiene`: repository/static scan (`check_api_hygiene.py`)
- `api-contract`: CLI pre-JIT source/import scan
- `jit`: AST/JIT/frontend semantic errors
- MLIR pass errors from `pyc-check-frontend-contract` with codes `PYC901`-`PYC910`

## Common failure examples

- Removed legacy API use (pre-v3.4 names)
- `@const` purity violations (IR emission or module mutation)
- connector boundary/type mismatches
- MLIR missing `pyc.frontend.api` or mismatched `--require-frontend-api`

## Tools

Run hygiene scan:

```bash
python3 /Users/zhoubot/pyCircuit/flows/tools/check_api_hygiene.py
```

Compile with strict frontend epoch check (default already v3.4):

```bash
/Users/zhoubot/pyCircuit/compiler/mlir/build2/bin/pyc-compile in.pyc --emit=cpp --require-frontend-api=v3.4
```
