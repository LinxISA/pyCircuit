# Your First Design: A Simple ALU (pyc4.0)

This tutorial builds a tiny ALU-style block to demonstrate pyCircuit v4.0
authoring with Python control flow that lowers to **static hardware**.

## Design specification

Inputs:
- `a`: 8-bit
- `b`: 8-bit
- `op`: 2-bit selector

Output:
- `result`: 8-bit

Operations:

| `op` | Operation |
|---:|---|
| 0 | `a + b` |
| 1 | `a - b` |
| 2 | `a ^ b` |
| 3 | `a & b` |

## Implementation

This design exists in the repo at `designs/examples/jit_control_flow/jit_control_flow.py`:

```python
from pycircuit import Circuit, module, u

@module
def build(m: Circuit, rounds: int = 4) -> None:
    a = m.input("a", width=8)
    b = m.input("b", width=8)
    op = m.input("op", width=2)

    acc = a + u(8, 0)
    if op == u(2, 0):
        acc = a + b
    elif op == u(2, 1):
        acc = a - b
    elif op == u(2, 2):
        acc = a ^ b
    else:
        acc = a & b

    for _ in range(rounds):
        acc = acc + 1

    m.output("result", acc)
```

Notes:
- The `if/elif/else` and `for` loop are **authoring sugar**. The compiler must
  lower the design to static hardware (no residual dynamic control flow).
- `rounds` is a compile-time parameter.

## Build + run

Run the standard gates:

```bash
bash flows/scripts/run_examples.sh
bash flows/scripts/run_sims.sh
```

Or build just this example via the CLI:

```bash
PYTHONPATH=compiler/frontend \
python3 -m pycircuit.cli build \
  designs/examples/jit_control_flow/tb_jit_control_flow.py \
  --out-dir /tmp/pyc_jit_control_flow \
  --target both \
  --jobs 8
```

