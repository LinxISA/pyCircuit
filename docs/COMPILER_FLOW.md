# Compiler Flow (v3.4)

## 1) Frontend contract

Frontend entrypoint must satisfy v3.4 contract:
- `@module` top
- helpers restricted to `@function` / `@const`
- connector-only inter-module boundaries
- canonical API names only (`inputs/outputs/state/pipe/new/array`)

## 2) Source and JIT validation

`pycircuit.cli emit` performs strict API contract scan before compile:
- scans entry file and local imports
- rejects removed APIs with actionable hints

JIT elaboration then validates:
- decorator legality
- connector-boundary requirements
- `@const` purity/no-emission guarantees

## 3) Design assembly

`compile(...)` builds multi-function MLIR from specialized `@module` symbols.

Frontend emits required attrs:
- module attr: `pyc.frontend.api = "v3.4"`
- func attrs: `pyc.frontend.api`, `pyc.kind`, `pyc.inline`, `pyc.params`, `pyc.base`

## 4) MLIR compile pipeline

`pyc-compile` starts with frontend contract verification:
- `pyc-check-frontend-contract`
- configured by `--require-frontend-api` (default `v3.4`)

Then runs legalization/optimization/check passes and emits split C++/Verilog artifacts.

## 5) Emission

- C++: module-split artifacts + compile manifest
- Verilog: module-split artifacts + manifest

## 6) Failure model

v3.4 intentionally shifts failures left:
- source/API violations fail in CLI scanner
- frontend semantic violations fail in JIT with source locations
- MLIR version/contract mismatches fail at start of `pyc-compile`

This reduces late backend-stage failures in large hardware compiles.
