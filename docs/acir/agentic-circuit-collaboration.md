# Agentic Circuit Collaboration Migration

Source repository: `PTO-ISA/agentic-circuit`  
Inventory baseline: `756002e2998b11dfe1fed14dc3d63cdad8be694c`

## Open pull requests

| Source | Title | Source head | Migration disposition |
| --- | --- | --- | --- |
| [#18](https://github.com/PTO-ISA/agentic-circuit/pull/18) | Popcount and round-robin ACIR-to-PYC lowering | `feature/issue11-pyc-phase1-wsl` | Preserve branch/commit; recreate or record as superseded only after exact diff validation against imported main |
| [#23](https://github.com/PTO-ISA/agentic-circuit/pull/23) | ACIR-to-C++ generation pipeline | `xiekunpeng:feature/acir-emit-cxx` | Rebase the four unique commits onto the integrated component and open a linked pyCircuit PR |

Replacement pyCircuit PR URLs and final commit SHAs must be added before either
source PR is closed.

## Open issues

| Source | Title | Target |
| --- | --- | --- |
| [#7](https://github.com/PTO-ISA/agentic-circuit/issues/7) | Generated C++ embeds full SHA-256 in names | Transfer to pyCircuit with `area:acir` |
| [#8](https://github.com/PTO-ISA/agentic-circuit/issues/8) | Add atomic `ac.try_transfer` primitive | Transfer to pyCircuit with `area:acir` |
| [#14](https://github.com/PTO-ISA/agentic-circuit/issues/14) | Parameterized blocks, SimQueue ABI and provider specialization | Transfer to pyCircuit with `area:acir` |
| [#26](https://github.com/PTO-ISA/agentic-circuit/issues/26) | Suggested ACPy/ACIR table design | Transfer to pyCircuit with `area:acir` |
| [#27](https://github.com/PTO-ISA/agentic-circuit/issues/27) | Correlated messages across independent queues | Transfer to pyCircuit with `area:acir` |

Update this table with target issue URLs after transfer.

## Branch and access audit

- Preserve `agentic-circuit/import-0.3` at the source main baseline.
- Record every source branch as imported, migrated, superseded, or intentionally
  retained before repository closure.
- Revoke old Actions, environments, deploy keys, webhooks and publishing
  credentials after the final gate bundle passes.
- Remove teams and outside collaborators before changing visibility.
- Verify effective private-repository access after cutover and attach the audit
  result to the final migration evidence.
