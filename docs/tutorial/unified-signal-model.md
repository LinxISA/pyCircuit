# Circuit authoring basics (pyc4.0)

pyc4.0 uses an explicit structural model:

- **Ports** are declared with `m.input(...)` and `m.output(...)`.
- **Combinational logic** is expressed with Python operators over wires/values.
- **State** is explicit (`m.out(...)` for registers; `mem` primitives for memories).

## V5 cycle-aware style (optional, same compiler)

For pipelines that track **logical occurrence cycles** and automatic delay alignment, use **`CycleAwareCircuit` / `CycleAwareDomain` / `compile_cycle_aware`** (see `docs/PyCircuit_V5_Spec.md`). That layer is implemented in `compiler/frontend/pycircuit/v5.py` and lowers to the same `pyc` MLIR as `@module` designs.

Separately, **`ClockHandle`** + `clk.next()` + `m.assign(...)` is the primary **named-wire occurrence** model documented under tick/transfer simulation (`docs/tutorial/cycle-aware-computing.md`, `docs/cycle_balance_improvement.md`).

## Minimal counter

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

Key points:

- `m.out(...)` creates a register with an explicit clock/reset and init value.
- `.out()` reads the current value.
- `.set(next, when=cond)` updates the register conditionally (otherwise it holds).

## Structured IO (`spec`)

For larger designs, prefer `spec` + structured port declarations:

- `m.inputs(spec, prefix=...)`
- `m.outputs(spec, values, prefix=...)`

See `docs/FRONTEND_API.md` and `docs/SPEC_STRUCTURES.md`.
