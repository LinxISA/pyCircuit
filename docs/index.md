# Welcome to pyCircuit (pyc4.0 / pyc0.40)

pyCircuit is a Python-based hardware construction DSL that compiles to MLIR and
emits:

- **C++ functional simulation** (module instances become SimObjects with `tick()` / `transfer()`)
- **Verilog** (for RTL integration and Verilator)

pyc4.0 is a hard-break release focused on **ultra-large designs**, scalable DFX,
and strict IR legality gates.

## Core ideas (pyc4.0)

- `@module` is the hierarchy boundary and maps 1:1 to a simulation object.
- Two-phase simulation: **tick** (compute / resolve) then **transfer** (commit state).
- Observation points:
  - **TICK-OBS**: post-tick, pre-transfer
  - **XFER-OBS**: post-transfer
- Python control flow is allowed as authoring sugar, but must lower to **static hardware**
  (no residual dynamic control flow in backend IR).

## Minimal example

```python
from pycircuit import Circuit, module, u

@module
def build(m: Circuit) -> None:
    clk = m.clock("clk")
    rst = m.reset("rst")
    en = m.input("enable", width=1)

    count = m.out("count_q", clk=clk, rst=rst, width=8, init=u(8, 0))
    count.set(count.out() + 1, when=en)
    m.output("count", count)
```

## Quick links

- `docs/QUICKSTART.md`
- `docs/FRONTEND_API.md`
- `docs/TESTBENCH.md`
- `docs/IR_SPEC.md`
- `docs/development/index.md`
- `docs/development/testing-and-gates.md`
- `designs/examples/README.md`
- `docs/rfcs/pyc4.0-decisions.md` and `docs/updatePLAN.md` (contracts + execution plan)
