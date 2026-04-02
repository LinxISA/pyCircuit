# Contributing to pyCircuit

pyCircuit is a hardware construction and compile-flow project. Contributions are
expected to preserve MLIR semantics, gate evidence, and the public pyc5 authoring
surface.

Start with these repo-native references:

- `docs/updatePLAN.md`
- `docs/rfcs/pyc4.0-decisions.md`
- `docs/development/contributing-workflow.md`
- `docs/development/testing-and-gates.md`
- `docs/development/review-and-merge.md`

## Environment setup

Prerequisites:

- Python 3.10+
- LLVM/MLIR 19
- CMake 3.20+
- Ninja
- Verilator and Icarus Verilog for simulation lanes

Recommended bootstrap:

```bash
python3 -m pip install -e ".[dev,docs]"
pre-commit install
bash flows/scripts/pyc build
```

Editable install is frontend-only. Build the staged toolchain in the current
worktree and point `PYC_TOOLCHAIN_ROOT` at
`.pycircuit_out/toolchain/install`, or use a release wheel.

## Core working rules

- Build and test from the current worktree. Do not copy compiled artifacts from
  another checkout.
- Treat `docs/rfcs/pyc4.0-decisions.md` and `docs/updatePLAN.md` as the active
  semantic evidence base until renamed.
- Semantic changes must be encoded in dialect semantics and MLIR passes or
  verifiers. Backend-only semantic patches are not acceptable.
- Keep the repo hard-break only. Do not add legacy compatibility flags or
  compatibility APIs back into pyc5.
- Keep documentation current when user-visible behavior, gate expectations, or
  contributor workflow changes.

## Tests, examples, and docs have different jobs

- Tests validate correctness and regressions.
- Examples demonstrate supported design and flow behavior.
- Docs explain contracts, workflow, and user-facing guidance.

Do not create temporary test scripts, one-off playground examples, or Markdown
files outside the documented locations. Reuse the existing test structure,
`designs/examples/`, and `docs/`.

## Required checks by change type

Use `docs/development/testing-and-gates.md` as the canonical matrix. The minimum
expectation is:

| Change type | Minimum checks |
| --- | --- |
| Docs, templates, contributor workflow | `mkdocs build`; run `python3 flows/tools/check_api_hygiene.py compiler/frontend/pycircuit designs/examples docs README.md` when touching docs or README |
| Frontend, CLI, manifest, packaging, example discovery | API hygiene gate plus `bash flows/scripts/run_examples.sh` |
| Testbench flow, examples, simulation entrypoints | `bash flows/scripts/run_examples.sh`, `bash flows/scripts/run_sims.sh`, `bash flows/scripts/run_sims_nightly.sh` |
| Dialect, passes, runtime, codegen, semantic behavior | `bash flows/scripts/run_examples.sh`, `bash flows/scripts/run_sims.sh`, `bash flows/scripts/run_sims_nightly.sh`, `bash flows/scripts/run_semantic_regressions_v40.sh`, and strict decision-status validation |
| Linx integration flows | Run the required `linx-pycircuit` gates in addition to the pyCircuit lanes |

For multi-lane validation, prefer a shared `PYC_GATE_RUN_ID=<run-id>` so all
artifacts land under one evidence bundle.

## Fast local validation

Use the lightweight local checks before opening a PR:

```bash
pre-commit run --files <changed-file> [<changed-file> ...]
pytest tests/unit -m unit
pytest tests/system -m system
mkdocs build
```

`tests/system` requires a built toolchain plus simulation tools. Export
`PYC_TOOLCHAIN_ROOT` or `PYCC` before running it locally.
Run `pre-commit run --all-files` only when you are intentionally sweeping the
repo for broader hygiene debt. CI runs the pre-commit lane on the PR or push
diff.

## Decision IDs and evidence

PRs that affect semantics, legality, flow behavior, examples, or documented
contracts must include:

- the affected decision IDs from `docs/rfcs/pyc4.0-decisions.md`
- the commands you ran
- evidence paths under `docs/gates/logs/<run-id>/`
- documentation updates, if behavior or workflow changed
- compatibility or risk notes when the change is hard-break visible

If a change is docs-only or template-only, say so explicitly in the PR and list
the checks you ran.

## Commit and branch expectations

- Keep commits focused and reviewable.
- Use present-tense commit messages, preferably `type(scope): description`.
- Do not add AI co-author lines.
- Do not mix unrelated cleanup with semantic or flow changes unless the cleanup
  is required for the fix.

## Pull request expectations

Use the pull request template. A merge-ready PR should make it easy for a
reviewer to answer:

1. What changed?
2. Which decisions or contracts are affected?
3. Which gates ran?
4. Where is the evidence?
5. What documentation changed?
6. What risk or compatibility impact remains?

## Reporting bugs and feature requests

Use the GitHub issue templates and include enough detail to reproduce or scope
the work:

- design or testbench path
- backend or toolchain path involved
- expected vs actual behavior
- relevant gate or command output
- minimal reproducer when available

## License

By contributing to pyCircuit, you agree that your contributions are licensed
under the [MIT License](LICENSE).
