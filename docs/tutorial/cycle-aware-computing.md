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

See `docs/cycle_balance_improvement.md` and (for V5 logical cycles) `docs/PyCircuit_V5_Spec.md`.

## Hardware boundary and auto-alignment rules

pyCircuit does not remove the need to reason about hardware. The Python syntax
is an authoring surface for a static circuit graph: registers, memories,
combinational paths, clock domains, and D/Q boundaries are still explicit in the
lowered `pyc` IR. A valid design should be explainable as that hardware graph.

Occurrence cycles are metadata on values, not hidden simulation steps. They are
used to decide whether an earlier value must be delayed before it is combined
with a later value. When the compiler sees operands from different occurrences,
it inserts real balance registers on the earlier operands. This is intentional
for pipeline alignment and must be accounted for in area/latency expectations.

The practical rules are:

- A value created at the current occurrence carries that occurrence.
- A raw `Wire` or integer literal used in a cycle-aware expression is treated as
  current at the domain's current occurrence.
- A state register's Q output is readable at later occurrences without adding a
  balance register just to read Q.
- Combining values from different occurrences aligns to the maximum occurrence
  by inserting balance registers on earlier operands.
- `domain.next()` / `clk.next()` only advances the authoring occurrence counter;
  it does not create hardware by itself.
- Registers are created by explicit state APIs such as `m.out(...)` or
  `domain.signal(...)`; balance registers are created only when occurrence
  alignment requires them.

For ordinary next-state code, compute the next value from the register Q and
assign it to the register D input. Use occurrence advancement when a pipeline
stage boundary is semantically part of the design, not as a visual separator.
