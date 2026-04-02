# pyCircuit (pyc5 surface) agent instructions

This repo follows the current pyc5 frontend surface while retaining the
`pyc4.0` decision corpus and gate evidence as the active semantic source of
truth until those documents are renamed.

## Read first (mandatory)

- `docs/updatePLAN.md`
- `docs/rfcs/pyc4.0-decisions.md`
- `docs/development/contributing-workflow.md`
- `docs/development/testing-and-gates.md`
- `docs/development/review-and-merge.md`

## Codex skills (mandatory)

- Apply `$pyc5` first for hard contracts and evidence expectations.
- Use `$pyc-build-v50` when running builds or gate lanes.
- Use `$linx-pycircuit` when touching Linx integration flows.

## Task mapping

- Issue fix or feature work: identify affected decision IDs first, then map the
  change to the required gates in `docs/development/testing-and-gates.md`.
- Code review: prioritize semantic regressions, missing gate coverage,
  incorrect evidence paths, and documentation drift before style issues.
- PR preparation: include decision IDs, gate commands, evidence paths, doc
  updates, and compatibility or risk notes.
- Documentation updates: keep governance docs, contributor docs, and README
  guidance aligned with the real repo workflow.

## Hard rules

- Gate-first: add or extend MLIR verifiers or passes before changing semantics.
- No backend-only semantic fixes: semantics live in the dialect plus MLIR
  passes and verifiers.
- Worktree-only builds: build and test from the current checkout. Never copy
  staged toolchains, `.so` files, or generated artifacts from another tree.
- No temporary test scripts or ad-hoc examples in the repo root. Tests belong
  in the existing test structure, examples belong under `designs/examples/`,
  docs belong under `docs/`.
- Public examples are product surface, not scratch space. New examples must
  serve user-facing design coverage, compile-flow coverage, or semantic
  evidence.
- Semantic or decision-bearing changes must reference the affected decision IDs
  and attach gate evidence under `docs/gates/logs/<run-id>/`.
- Keep the repo hard-break only. Do not reintroduce legacy compatibility APIs,
  flags, or tutorial surfaces that pyc5 removed.
- Do not add AI co-author lines to commits or PR text.

## When to stop and ask the user

- The requested change conflicts with the decision corpus or existing gate
  evidence.
- The task would require changing documented semantics without a clear decision
  update path.
- The worktree contains unrelated user changes in the same files and the merge
  strategy is ambiguous.
- Required external tooling or credentials are missing and block validation or
  publishing.

## Working expectations

- Prefer the smallest reproducer and the narrowest gate lane that proves the
  change, then widen only as needed.
- Keep generated logs bounded and archive only reviewable evidence.
- If behavior changes, update the relevant docs in the same change.
- If a blocker is non-critical and local to the task, document it clearly in
  the final handoff instead of silently working around it.
