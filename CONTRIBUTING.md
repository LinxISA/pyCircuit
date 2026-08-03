# Contributing to Agentic Circuit

Thank you for helping improve Agentic Circuit. Contributions must preserve the
exact global contract epoch and the reproducible LLVM toolchain boundary.

## Development setup

1. Run `scripts/bootstrap-dev.sh` to create `.venv` and install the locked
   development requirements.
2. Activate `.venv`.
3. Run `python -m unittest tests.contracts.test_contracts -v`.
4. Configure with `cmake --preset dev-llvm22`.

Before submitting a change, run the contract tests, the relevant build and
tests, and `git diff --check`. Changes to a public contract must update every
affected normative specification and machine-readable schema in the same
change.

By contributing, you agree that your contribution is licensed under the
Apache License 2.0 and that you will follow the Code of Conduct.
