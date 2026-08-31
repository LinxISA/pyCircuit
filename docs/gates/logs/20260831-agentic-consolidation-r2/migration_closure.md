# Agentic Circuit Consolidation Closure

Run ID: `20260831-agentic-consolidation-r2`
Integration revision: `83f7f746`
Decision: 0150

## Passed AC closure

- Agentic Circuit Python frontend: 113 passed, 1 skipped.
- Agentic Circuit CLI: 52 passed.
- ACIR/ACSim lit: 128/128 passed.
- AC CTest: 14/14 passed, including gfsim and workspace E2E.
- AC G2 canonical cases: arbiter, atomic transform, and popcount.
- Each AC G2 case passed ACIR-to-PYC, pyc6 C++ generation/syntax checking,
  pyc6 Verilog generation, and Verilator lint.
- The isolated install tree contains `agentic-circuit`, public `acir-*` tools,
  ACIR/ACSim/gfsim libraries, and `libpyc6_runtime`.

Primary command:

```bash
PYC_GATE_RUN_ID=20260831-agentic-consolidation-r2 \
  bash flows/scripts/run_agentic_circuit.sh
```

## Passed PYC closure

- `pytest tests/unit -m unit`: 51 passed.
- `pytest tests/system -m system`: 3 passed.
- API hygiene: passed.
- `mkdocs build --strict`: passed.
- Examples: passed.
- Normal simulations: passed.
- Nightly simulations: passed.
- V6 semantic regressions: passed.
- Linx CPU pyc6 C++ smoke: passed.
- pyCircuit interface contract 2.0: passed.
- LinxTrace SemVer contract 1.0: passed.

## Open blocking gate

The QEMU-versus-pyCircuit trace gate is not closed.

The current `linx_cpu_pyc` design is a contract smoke and does not execute the
ELF supplied to `run_linx_qemu_vs_pyc.sh`; the gate therefore uses the archived
`MODEL-SCALAR-MCOPY-MSET` pyCircuit trace. The migration added an explicit
legacy-schema normalizer and instruction-boundary collapse, but the archived
trace is semantically stale against the current QEMU/assembler line:

- at PC `0x10020`, QEMU reports instruction `0x164`, while the archived PYC
  trace records `0x154`;
- a later current instruction is six bytes, while the archived trace advances
  by four bytes and has different writeback data.

Regenerating the PYC trace from QEMU or ignoring instruction/length/writeback
would make the comparison circular or weaken the gate, so neither workaround
is accepted.

## Retirement decision

Decision 0150 is `implemented-verified` for repository consolidation, semantic
boundaries, build/install integration, and AC/PYC closure. The standalone
Agentic Circuit repository must nevertheless remain public and active until a
current independent PYC commit-trace producer or reviewed current PYC fixture
makes the QEMU/PYC gate pass. Do not disable publishing authority, change
visibility, close the source PRs, or archive the repository before that gate
passes.
