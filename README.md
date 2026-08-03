# Agentic Circuit

Agentic Circuit is a proposed Python and MLIR-based architecture construction
system that generates a structured, pure C++ graph-flow simulator named
`gfsim`.

The project is currently in specification phase. No implementation contract is
approved yet.

The current design is [Agentic Circuit v0.1](docs/superpowers/specs/2026-08-03-acir-v0.1-design.md).

Normative specifications:

- [Interface Evolution v0.1](docs/specs/interface-evolution-v0.1.md)
- [ACIR Core v0.1](docs/specs/acir-core-v0.1.md)
- [Python-to-ACIR Lowering v0.1](docs/specs/python-to-acir-lowering-v0.1.md)
- [Agentic Python and CLI v0.1](docs/specs/agentic-python-cli-v0.1.md)
- [ACIR Standard Library v0.1](docs/specs/acir-stdlib-v0.1.md)
- [ACSim and gfsim Lowering v0.1](docs/specs/acsim-gfsim-lowering-v0.1.md)
- [gfsim Model Library Contract v0.1](docs/specs/gfsim-runtime-abi-v0.1.md)
- [PTO Trace Schema v0.1](docs/specs/pto-trace-schema-v0.1.md)

Canonical machine-readable schemas:

- [ACPy](schemas/acpy.schema.json)
- [Capabilities](schemas/capabilities.schema.json)
- [ComponentSchema](schemas/component.schema.json)
- [PTO trace](schemas/pto-trace.schema.json)
- [Build manifest](schemas/build-manifest.schema.json)
- [Run manifest](schemas/run-manifest.schema.json)
- [Run result](schemas/run-result.schema.json)
- [Diagnostic](schemas/diagnostic.schema.json)
