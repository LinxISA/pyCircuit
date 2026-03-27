# Tick/transfer simulation model (pyc4.0)

pyc4.0 simulation has two phases per cycle:

1. `tick()` — compute combinational logic, resolve nets, and produce next-state
2. `transfer()` — commit reg/mem state

Observation points:

- **TICK-OBS** (pre-transfer): after `tick()`, before `transfer()`
- **XFER-OBS** (post-transfer): after `transfer()`

Testbenches can sample at either point.

## Testbench sampling points

```python
from pycircuit import Tb, testbench

@testbench
def tb(t: Tb) -> None:
    t.clock("clk")
    t.reset("rst", cycles_asserted=2, cycles_deasserted=1)
    t.drive("in_valid", 1, at=0)
    t.expect("out_valid", 1, at=0, phase="pre")   # TICK-OBS
    t.expect("out_valid", 1, at=0, phase="post")  # XFER-OBS
    t.finish(at=1)
```

See `docs/TESTBENCH.md` for the full `Tb` API.

## Memory + reset semantics

- Memory is **tick-read / transfer-write** by default.
- Read-during-write defaults to **old-data** unless explicitly overridden.
- Reset/init semantics must be identical across backends (C++ and Verilog).

These contracts are enforced via MLIR-level verifiers/passes (see `docs/updatePLAN.md`).

## Occurrence cycles on combinational assigns

**Primary style:** `clk = m.clock(...)` returns a **`ClockHandle`**. Use
**`clk.next()`** to advance the domain’s **current occurrence cycle**. Assigns
to **`named_wire`** targets then get **`dst_cycle = clk.cycle`** and
**`src_cycle`** from the RHS expression; `pycc` runs **`pyc-cycle-balance`** to
insert shared `pyc.reg` delays when needed.

```python
clk = m.clock("clk")
raw = m.input("x", width=8)
clk.next()
w = m.named_wire("stage1_view", width=8)
m.assign(w, raw)
```

**Explicit** metadata is still supported:

```python
m.assign(w, raw, dst_cycle=1, src_cycle=0)
```

See `docs/pyCircuit_Tutorial.md` §3.1 and `docs/cycle_balance_improvement.md`.

