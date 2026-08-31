# pyCircuit 6

<p align="center">
  <img src="https://img.shields.io/badge/License-BSD--3--Clause-blue.svg" alt="License">
  <img src="https://img.shields.io/badge/Python-3.10%2B-green.svg" alt="Python">
  <img src="https://img.shields.io/badge/MLIR-22-orange.svg" alt="MLIR">
  <a href="https://github.com/PTO-ISA/pyCircuit/actions"><img src="https://github.com/PTO-ISA/pyCircuit/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="https://github.com/PTO-ISA/pyCircuit/actions/workflows/release.yml"><img src="https://github.com/PTO-ISA/pyCircuit/actions/workflows/release.yml/badge.svg" alt="Release"></a>
  <a href="https://github.com/PTO-ISA/pyCircuit/releases"><img src="https://img.shields.io/github/v/release/PTO-ISA/pyCircuit?display_name=tag" alt="Latest release"></a>
</p>

pyCircuit is a Python hardware construction and architecture-modeling
repository. The `pycircuit` frontend lowers cycle-aware designs to verified PYC
MLIR and emits synthesizable Verilog and a C++ cycle model. The retained
`agentic_circuit` frontend lowers architecture/process/queue descriptions to
ACIR, then targets either ACSim/gfsim or the pyCircuit 6 hardware flow.

[`PTO-ISA/pyCircuit`](https://github.com/PTO-ISA/pyCircuit) is the canonical
repository, release authority, and only active source of truth for both
pyCircuit and Agentic Circuit.
[`LinxISA/pyCircuit`](https://github.com/LinxISA/pyCircuit) is its downstream
fork for Linx integration work.

## Why pyCircuit 6

- **Cycle-aware signals:** `CycleAwareSignal` carries logical-cycle provenance.
- **Automatic pipeline balancing:** mixed-cycle expressions lower to explicit
  delay registers.
- **Inferred state:** `domain.signal()` plus `<<=` or `.assign()` derives the
  required register structure.
- **One semantic IR:** C++ and Verilog consume the same verified `pyc` MLIR.
- **Preserved hierarchy:** module instances remain visible to simulation, DFX,
  and emitted RTL.
- **Scalable validation:** legality, cycle, depth, clock-domain, trace, and
  backend-equivalence gates are part of the repository workflow.

## Install

The canonical pyCircuit 6 source installation is:

```bash
git clone https://github.com/PTO-ISA/pyCircuit.git
cd pyCircuit
python3 -m pip install -e ".[dev,docs]"
pre-commit install
bash flows/scripts/pyc build
```

The Agentic Circuit frontend remains a separate distribution and Python
namespace in the same source repository:

```bash
python3 -m pip install -e components/agentic-circuit
agentic-circuit --help
```

Release wheels, once published, use the distribution name `pycircuit-hisi`;
the Python import remains `pycircuit`. The repository does not claim a PyPI
release until the corresponding PTO-ISA release workflow has completed.

The staged compiler is installed under
`.pycircuit_out/toolchain/install/`. Set `PYC_TOOLCHAIN_ROOT` to that directory
when running end-to-end builds from a source checkout.

## Agentic Circuit and ACIR

ACIR remains an independent, upper-level MLIR dialect. It is not folded into
the PYC dialect and does not replace the Cycle-Aware Signal model:

```text
agentic_circuit frontend -> ACPy 0.3 -> ACIR
                                         |-> ACSim -> gfsim
                                         `-> PYC -> pycc -> pyc6 C++ / Verilog

pycircuit frontend -> Cycle-Aware Signal -> PYC -> pycc -> pyc6 C++ / Verilog
```

The public `agentic_circuit` import and `agentic-circuit` CLI remain distinct
from `pycircuit`. AC symbols are not re-exported from `pycircuit.__init__`.
See the [ACIR architecture overview](docs/acir/index.md) and
[migration record](docs/acir/migration.md).

## First cycle-aware design

```python
from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    compile_cycle_aware,
    wire_of,
)


def counter(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    width: int = 8,
) -> None:
    enable = cas(domain, m.input("enable", width=1), cycle=0)
    count = domain.signal(width=width, reset_value=0, name="count")

    m.output("count", wire_of(count))
    domain.next()
    count.assign(count + 1, when=enable)


if __name__ == "__main__":
    design = compile_cycle_aware(counter, name="counter", eager=True)
    print(design.emit_mlir())
```

`domain.next()` advances the authoring-time logical cycle. The assignment to
`count` therefore creates a one-stage state update. When values from different
logical cycles meet, the compiler inserts the delay chain needed to align them.

## Build and test

Build the repository counter example for both backends:

```bash
export PYC_TOOLCHAIN_ROOT="$PWD/.pycircuit_out/toolchain/install"
PYTHONPATH=compiler/frontend \
python3 -m pycircuit.cli build \
  designs/examples/counter/tb_counter.py \
  --out-dir /tmp/pyc_counter \
  --target both \
  --jobs 8
```

Run the normal contributor lanes:

```bash
pre-commit run --files <changed-file> [<changed-file> ...]
pytest tests/unit -m unit
bash flows/scripts/run_examples.sh
bash flows/scripts/run_sims.sh
```

Run the Agentic Circuit frontend and contract lane:

```bash
PYTHONPATH=components/agentic-circuit/src \
python3 -m unittest discover \
  -s components/agentic-circuit/tests \
  -p 'test_*.py'
```

System tests require a built toolchain and Verilator:

```bash
pytest tests/system -m system
```

## Documentation

- [V6 language specification](docs/v6_PyCircuit_Specification.md)
- [V6 tutorial](docs/v6_PyCircuit_Tutorial.md)
- [V6 software architecture](docs/v6_PyCircuit_Software_Architecture.md)
- [Frontend API](docs/FRONTEND_API.md)
- [Testbench API](docs/TESTBENCH.md)
- [IR specification](docs/IR_SPEC.md)
- [pyCircuit 6 decisions](docs/rfcs/pyc6-decisions.md)
- [pyCircuit 6 evolution plan](docs/pyc6-plan.md)
- [ACIR architecture and frontend](docs/acir/index.md)
- [Agentic Circuit migration](docs/acir/migration.md)

## Repository governance

PTO-ISA owns product decisions, both Python distributions, releases, package
publication, and the default branch. Linx integration changes should be
developed so they can be reviewed upstream; the LinxISA fork follows the
upstream default branch. The former standalone Agentic Circuit repository is
retired only after both AC and PYC closure gates pass.

- [Contribution workflow](docs/development/contributing-workflow.md)
- [Testing and gates](docs/development/testing-and-gates.md)
- [Review and merge](docs/development/review-and-merge.md)
- [Repository management](docs/development/repository-management.md)

Historical gate logs retain their original directory names. Active runtime,
trace, and gate contracts use `libpyc6_runtime`, `PYC6TRC3`, and
`run_semantic_regressions_v6.sh`.

## Repository layout

```text
pyCircuit/
├── compiler/frontend/pycircuit/  # Python language frontend
├── compiler/mlir/                # pyc dialect, passes, pycc, and emitters
├── components/agentic-circuit/   # AC frontend, ACIR/ACSim, gfsim, and AC tools
├── runtime/                      # C++ simulation and Verilog primitives
├── designs/examples/             # Supported product examples
├── flows/                        # Build and validation orchestration
├── tests/                        # Unit, integration, and system tests
└── docs/                         # Product and contributor documentation
```

## License

pyCircuit, including the integrated Agentic Circuit sources, is licensed under
the BSD 3-Clause License. See [LICENSE](LICENSE) and the
[relicensing record](docs/legal/AC-RELICENSE-BSD-3-CLAUSE.md).
