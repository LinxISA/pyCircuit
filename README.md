# Agentic Circuit

Agentic Circuit is a Python and MLIR-based architecture construction system
that generates a structured, pure C++ graph-flow simulator named `gfsim`.
It also emits canonical PYC IR for downstream C++ and Verilog generation.

The source tree is intentionally release-neutral: product versions belong to
Git tags and GitHub Releases, not directory names, filenames, symbols, or test
names. Serialized artifacts still carry an exact contract epoch because that
field is part of their wire-format compatibility contract.

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

## Documentation

- [Specification index](docs/spec/README.md)
- [Agentic Circuit specification manual](docs/spec/agentic-circuit.md)
- [Agentic Circuit 团队 Specification 手册](docs/spec/agentic-circuit.zh-CN.md)
- [NDF release-layout decision](docs/spec/decisions/D-RELEASE-LAYOUT-001.md)
- [Historical specification reference](docs/spec/refs/history.md)
- [Examples by semantic role](examples/README.md)

Canonical machine-readable schemas:

- [ACPy](schemas/acpy.schema.json)
- [Capabilities](schemas/capabilities.schema.json)
- [ComponentSchema](schemas/component.schema.json)
- [Official opcode catalog schema](schemas/opcode-catalog.schema.json)
- [Official Queue building-block catalog](schemas/opcodes.json)
- [PTO trace](schemas/pto-trace.schema.json)
- [Build manifest](schemas/build-manifest.schema.json)
- [Run manifest](schemas/run-manifest.schema.json)
- [Run result](schemas/run-result.schema.json)
- [Diagnostic](schemas/diagnostic.schema.json)
- [ACSim binding](schemas/acsim-binding.schema.json)
- [ACIR process-state plan](schemas/acir-process-state-plan.schema.json)

The repository uses a hard-break layout. Removed implementation-phase and
product-version paths have no aliases or compatibility symlinks. Historical
documents remain recoverable from the Git revision recorded by the NDF
historical reference.

## Project policies

Contributions are accepted under the [Apache License 2.0](LICENSE). See
[Contributing](CONTRIBUTING.md), the [Code of Conduct](CODE_OF_CONDUCT.md),
[Security policy](SECURITY.md), and [Support policy](SUPPORT.md) before opening
a change or report.
