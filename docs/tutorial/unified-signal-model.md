# Circuit authoring basics (pyc4.0)

pyc4.0 uses an explicit structural model:

- **Ports** are declared with `m.input(...)` and `m.output(...)`.
- **Combinational logic** is expressed with Python operators over wires/values.
- **State** is explicit (`m.out(...)` for registers; `mem` primitives for memories).

There is no global “cycle-aware” signal system in pyc4.0.

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

