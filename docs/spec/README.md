# Agentic Circuit Specifications

This directory contains the human-readable contracts for Agentic Circuit.
Machine-readable schemas under [`schemas`](../../schemas) and MLIR ODS
definitions under [`include/acir`](../../include/acir) remain the executable
sources of truth.

The generated [official Queue building-block catalog](../../schemas/opcodes.json)
records the closed opcode roles, arity, constants, backend realizations, and
refinement observations.

## Current manuals

- [Agentic Circuit Specification Manual](agentic-circuit.md) defines the
  implementation-facing serial Python, ACIR, gfsim, PYC, and refinement
  contract.
- [Agentic Circuit 团队 Specification 手册](agentic-circuit.zh-CN.md)
  provides a Chinese teammate-facing overview, common patterns, executable
  examples, backend differences, and troubleshooting guidance.

## NDF decision spine

The repository follows the restricted NDF profile declared in
[`ndf.yaml`](ndf.yaml). Stable clause IDs connect the current architecture,
decisions, historical evidence, and verification:

- [`ARC-RELEASE-001`, `ARC-LAYOUT-001`, and `ARC-HISTORY-001`](00-charter/scope.md)
  define release ownership, semantic source partitions, and history policy.
- [`D-RELEASE-LAYOUT-001`](decisions/D-RELEASE-LAYOUT-001.md) records the
  hard-break release-neutral layout decision.
- [`D-BLOCK-MODEL-001`](decisions/D-BLOCK-MODEL-001.md) records the current
  Queue/Var building-block model.
- [`REF-HISTORY-001`](refs/history.md) pins removed historical specifications
  to an immutable Git revision.
- [`VER-LAYOUT-001`](50-verification/repository-layout.md) binds these rules to
  executable repository checks.
- [IR coverage ledger](50-verification/ir-coverage.md) is generated from the
  current ACIR/ACSim manifests and lit coverage.

Product releases are represented by Git tags and GitHub Releases. The source
tree does not retain product-version or implementation-phase paths, aliases,
or compatibility symlinks. Serialized contract epochs and external dependency
pins remain versioned where interoperability and reproducibility require them.
