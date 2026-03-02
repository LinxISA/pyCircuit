# pyCircuit (pyc4.0 / pyc0.40)

<p align="center">
  <img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License">
  <img src="https://img.shields.io/badge/Python-3.9+--green.svg" alt="Python">
  <img src="https://img.shields.io/badge/MLIR-17+-orange.svg" alt="MLIR">
  <a href="https://github.com/LinxISA/pyCircuit/actions"><img src="https://github.com/LinxISA/pyCircuit/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
</p>

pyCircuit is a Python-based hardware construction DSL that compiles Python
modules to an MLIR hardware dialect and emits:

- **C++ functional simulation** (module instances become SimObjects with `tick()` / `transfer()`)
- **Verilog** (RTL integration + Verilator)

pyc4.0 is a hard-break release focused on **ultra-large designs**, scalable DFX,
and strict IR legality gates.

## Key features (pyc4.0)

- **Hierarchy-preserving `@module` boundaries** (1:1 with simulation objects)
- **Two-phase cycle model**: `tick()` then `transfer()`
- **Gate-first compiler**: static-hardware IR legality, comb-cycle checks, logic-depth propagation
- **Structured interfaces** via `spec` (Bundle/Struct/Signature) with deterministic flattening
- **Integrated `@testbench`** flow (device + TB compiled together)

## Quick start

Build the backend tool (`pycc`):

```bash
bash flows/scripts/pyc build
```

Run the smoke gates:

```bash
bash flows/scripts/run_examples.sh
bash flows/scripts/run_sims.sh
```

### Minimal design snippet (counter)

```python
from pycircuit import Circuit, module, u

@module
def build(m: Circuit, width: int = 8) -> None:
    clk = m.clock("clk")
    rst = m.reset("rst")
    en = m.input("enable", width=1)

    count = m.out("count_q", clk=clk, rst=rst, width=width, init=u(width, 0))
    count.set(count.out() + 1, when=en)
    m.output("count", count)
```

Build a multi-module project (device + TB):

```bash
PYTHONPATH=compiler/frontend \
python3 -m pycircuit.cli build \
  designs/examples/counter/tb_counter.py \
  --out-dir /tmp/pyc_counter \
  --target both \
  --jobs 8
```

For more end-to-end commands, see `docs/QUICKSTART.md`.

## Repo layout

```
pyCircuit
├── compiler/
│   ├── frontend/          # Python frontend (pycircuit package)
│   └── mlir/              # MLIR dialect + passes + tools (pycc, pyc-opt)
├── runtime/
│   ├── cpp/               # C++ simulation runtime
│   └── verilog/           # Verilog primitives
├── designs/
│   └── examples/          # Example designs
└── docs/                  # Documentation
```

## Documentation

- `docs/QUICKSTART.md`
- `docs/FRONTEND_API.md`
- `docs/TESTBENCH.md`
- `docs/IR_SPEC.md`
- `docs/updatePLAN.md` and `docs/rfcs/pyc4.0-decisions.md`

## Examples

| Example | Description |
|---------|-------------|
| [Counter](designs/examples/counter/) | Basic counter with enable |
| [Calculator](designs/examples/calculator/) | Stateful keypad calculator |
| [FIFO Loopback](designs/examples/fifo_loopback/) | FIFO queue with loopback |
| [Digital Clock](designs/examples/digital_clock/) | Time-of-day clock display |
| [FastFWD](designs/examples/fastfwd/) | Network packet forwarding |
| [Linx CPU](contrib/linx/designs/examples/linx_cpu_pyc/) | Full 5-stage pipeline CPU |

## License

pyCircuit is licensed under the MIT License. See `LICENSE`.

