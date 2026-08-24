# Agentic Circuit

Agentic Circuit is a Python and MLIR-based architecture construction system
that generates a structured, pure C++ graph-flow simulator named `gfsim`.
The v0.2 upgrade branch uses exact global contract epoch `0.2` and rejects
legacy epoch `0.1` artifacts.

## Development baseline

The repository is locked to LLVM/MLIR 22.1.8. On Apple Silicon, the default
prefix is `/opt/homebrew/opt/llvm`. On another supported host, pass the
equivalent package explicitly with
`-DMLIR_DIR=/path/to/llvm/lib/cmake/mlir`.

```sh
scripts/bootstrap-dev.sh
source .venv/bin/activate
python -m unittest tests.contracts.test_contracts -v
cmake --preset dev-llvm22
cmake --build --preset dev-llvm22
```

Use `release-llvm22` for a release configuration. The exact upstream release,
commit, archive digest, supported host triples, and version policy are recorded
in `toolchains/llvm.lock.json`.

Historical v0.1 specifications:

- [Specification index](docs/specs/README.md)
- [Interface Evolution v0.1](docs/specs/interface-evolution-v0.1.md)
- [ACIR Core v0.1](docs/specs/acir-core-v0.1.md)
- [Python-to-ACIR Lowering v0.1](docs/specs/python-to-acir-lowering-v0.1.md)
- [Agentic Python and CLI v0.1](docs/specs/agentic-python-cli-v0.1.md)
- [ACIR Standard Library v0.1](docs/specs/acir-stdlib-v0.1.md)
- [ACSim and gfsim Lowering v0.1](docs/specs/acsim-gfsim-lowering-v0.1.md)
- [gfsim Model Library Contract v0.1](docs/specs/gfsim-runtime-abi-v0.1.md)
- [PTO Trace Schema v0.1](docs/specs/pto-trace-schema-v0.1.md)
- [ACIR Process-State Plan v0.1](docs/specs/acir-process-state-plan-v0.1.md)

Queue/Var v0.2 candidate contract:

- [Agentic Circuit Queue/Var v0.2 Specification Manual](docs/specs/agentic-circuit-v0.2.md)
- [Agentic Circuit Queue/Var Architecture v0.2 Proposal Manual](docs/specs/agentic-circuit-queue-var-v0.2-proposal.md)

Canonical machine-readable schemas:

- [ACPy](schemas/acpy.schema.json)
- [Capabilities](schemas/capabilities.schema.json)
- [ComponentSchema](schemas/component.schema.json)
- [Official opcode catalog schema](schemas/opcode-catalog.schema.json)
- [Official Queue building-block catalog](schemas/opcodes-v0.2.json)
- [PTO trace](schemas/pto-trace.schema.json)
- [Build manifest](schemas/build-manifest.schema.json)
- [Run manifest](schemas/run-manifest.schema.json)
- [Run result](schemas/run-result.schema.json)
- [Diagnostic](schemas/diagnostic.schema.json)
- [ACSim binding](schemas/acsim-binding.schema.json)
- [ACIR process-state plan](schemas/acir-process-state-plan.schema.json)

## Project policies

Contributions are accepted under the [Apache License 2.0](LICENSE). See
[Contributing](CONTRIBUTING.md), the [Code of Conduct](CODE_OF_CONDUCT.md),
[Security policy](SECURITY.md), and [Support policy](SUPPORT.md) before opening
a change or report.
