# pyCircuit (pyc4.0 / 0.40) agent instructions

This repo follows the pyc4.0 (0.35 → 0.40) hard-break upgrade plan.

## Read first (mandatory)

- `docs/updatePLAN.md`
- `docs/rfcs/pyc4.0-decisions.md`

## Codex skills (mandatory)

- Apply `$pyc4` first for the decision IDs + non-negotiable contracts.
- Use `$pyc-build-v40` when running builds/gates.
- Use `$linx-pycircuit` when touching Linx integration flows.

## Ground rules

- Gate-first: add/extend MLIR verifiers/passes before changing semantics.
- No backend-only semantic fixes: semantics live in the dialect + MLIR passes.

