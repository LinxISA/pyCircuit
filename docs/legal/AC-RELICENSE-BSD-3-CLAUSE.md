# Agentic Circuit BSD-3-Clause Relicensing Record

## Purpose

This record documents the repository-owner direction to relicense Agentic
Circuit from Apache License 2.0 to the BSD 3-Clause License while consolidating
Agentic Circuit into `PTO-ISA/pyCircuit`.

## Rights statement and direction

The repository owner has stated that PTO-ISA is the copyright owner for both
Agentic Circuit and pyCircuit and has directed that the consolidated source be
licensed uniformly under BSD-3-Clause. This record captures that owner
direction; it is not a fabricated signature, CLA, or representation that a
particular natural person executed a separate legal instrument.

The relicensing scope is pinned to these source objects:

| source | Git object | disposition |
| --- | --- | --- |
| Agentic Circuit `main` import baseline | `756002e2998b11dfe1fed14dc3d63cdad8be694c` | Relicense imported source under BSD-3-Clause |
| Agentic Circuit PR #18 head | `cb037558a6b900f4106f4b756c62b0bb0d56d83b` | Relicense any migrated changes under BSD-3-Clause |
| Agentic Circuit PR #23 head | `cfe048d6923bc69a1985b95f007330279b2fc8d1` | Relicense any migrated changes under BSD-3-Clause |

The original commits remain reachable for provenance. Replacing license text
does not rewrite their authorship, timestamps, or commit identities.

## Effective repository policy

- The root `LICENSE` is the canonical license for the consolidated repository.
- Imported Agentic Circuit source and future contributions are BSD-3-Clause.
- Package metadata, source archives, binary distributions, generated install
  trees, and component documentation must identify BSD-3-Clause consistently.
- Any independently licensed third-party dependency or vendored artifact keeps
  its own license; this decision does not purport to relicense third-party work.

## Authorization record

The repository-owner direction in the migration task is recorded as follows:

- Authorized representative: `zhoubot`
- PTO-ISA role or authority basis: active PTO-ISA GitHub organization admin
- Approval date: 2026-08-31
- Approved source scope: Agentic Circuit main baseline and PR #18/#23 heads
  listed above, plus their explicitly identified migrated successor commits in
  the reviewed pyCircuit migration pull requests

The organization role was verified through GitHub's organization-membership
API before this record was completed. The migration pull request remains the
reviewable approval record; this document does not invent a handwritten or
cryptographic signature.

## Verification

Release review must confirm that active first-party license declarations no
longer name Apache-2.0 or MIT and that produced distributions include the root
BSD-3-Clause license. This record is governance evidence, not evidence that the
ACIR, gfsim, PYC, C++, or Verilog test gates have passed.
