# pyCircuit v4.0 Tutorial (Hard-Break)

This tutorial is the v4.0 (`pyc0.40`) guide for authoring, building, and testing
pyCircuit designs.

CycleAware APIs were removed in pyc4.0 and are not part of v4 authoring.

## 1. What pyc4.0 enforces

- `@module` defines hierarchy boundaries that lower to `pyc.instance`.
- Simulation follows a two-phase model:
  - `tick()` computes next state
  - `transfer()` commits state
- Python control-flow is allowed during authoring, but backend IR must be static
  hardware (no residual dynamic SCF/index in backend lanes).
- DFX/probe behavior is first-class and controlled by hardened metadata + trace DSL.

Authoritative references:

- `docs/rfcs/pyc4.0-decisions.md`
- `docs/updatePLAN.md`
- `docs/FRONTEND_API.md`
- `docs/TESTBENCH.md`
- `designs/examples/README.md`

## 2. Environment and quick gate loop

Build `pycc`:

```bash
bash /Users/zhoubot/pyCircuit/flows/scripts/pyc build
```

Run compiler smoke:

```bash
bash /Users/zhoubot/pyCircuit/flows/scripts/run_examples.sh
```

Run simulation smoke:

```bash
bash /Users/zhoubot/pyCircuit/flows/scripts/run_sims.sh
```

Run semantic regression lane:

```bash
bash /Users/zhoubot/pyCircuit/flows/scripts/run_semantic_regressions_v40.sh
```

## 3. Minimal module

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

Key points:

- `m.out(...)` creates explicit sequential state.
- `.out()` reads current state.
- `.set(next, when=...)` sets next state with hold-by-default behavior.

## 4. Authoring with Python control flow

You can use `if` and `for` in `@module` bodies as authoring sugar.

```python
from pycircuit import Circuit, module, u

@module
def build(m: Circuit, rounds: int = 4) -> None:
    a = m.input("a", width=8)
    b = m.input("b", width=8)
    op = m.input("op", width=2)

    acc = a
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

The compiler must lower this to static hardware before backend emission.

## 5. Structured interfaces

For larger modules, prefer `spec` + structured IO to keep port conventions
stable and tool-visible.

```python
from pycircuit import Circuit, module, spec

Pair = spec.struct("pair").field("x", width=8).field("y", width=8).build()

@module
def build(m: Circuit) -> None:
    ins = m.inputs(Pair, prefix="in_")
    m.outputs(Pair, {"x": ins["x"], "y": ins["y"]}, prefix="out_")
```

See `docs/SPEC_STRUCTURES.md` and `docs/SPEC_COLLECTIONS.md` for full patterns.

## 6. Testbench flow

Write a host-side `@testbench` with `Tb`:

```python
from pycircuit import Tb, testbench

@testbench
def tb(t: Tb) -> None:
    t.clock("clk")
    t.reset("rst", cycles_asserted=2, cycles_deasserted=1)
    t.timeout(64)

    t.drive("enable", 1, at=0)
    t.expect("count", 1, at=0, phase="pre")
    t.expect("count", 1, at=0, phase="post")
    t.finish(at=8)
```

Observation points:

- `phase="pre"` = TICK-OBS
- `phase="post"` = XFER-OBS

## 7. End-to-end build via CLI

Build a device + TB project:

```bash
PYTHONPATH=/Users/zhoubot/pyCircuit/compiler/frontend \
python3 -m pycircuit.cli build \
  /Users/zhoubot/pyCircuit/designs/examples/counter/tb_counter.py \
  --out-dir /tmp/pyc_counter \
  --target both \
  --jobs 8
```

Important artifacts:

- `project_manifest.json`
- `device/modules/*.pyc`
- `device/cpp/**` and/or `device/verilog/**`
- `trace_plan.json` (when trace config is enabled)
- `probe_manifest.json`

## 8. Trace and probe workflow

When trace config is enabled, pyc4.0 emits binary `.pyctrace` plus manifest data.

Decode with external manifest mode:

```bash
python3 /Users/zhoubot/pyCircuit/flows/tools/dump_pyctrace.py \
  /tmp/pyc_counter/tb_tb_counter_top/tb_tb_counter_top.pyctrace \
  --manifest /tmp/pyc_counter/probe_manifest.json
```

Use `designs/examples/trace_dsl_smoke/*`,
`designs/examples/bundle_probe_expand/*`,
`designs/examples/xz_value_model_smoke/*`, and
`designs/examples/reset_invalidate_order_smoke/*` as reference patterns.

## 9. Required gate mindset (v4.0)

For semantic or IR-contract changes:

1. Update verifier/pass gates first.
2. Implement behavior in dialect/passes, not backend-only fixups.
3. Re-run smoke + simulation gates and preserve logs.
4. Keep decision status current in `docs/gates/decision_status_v40.md`.
5. Run semantic closure regressions (`run_semantic_regressions_v40.sh`) before status promotion.

## 10. Troubleshooting checklist

- `pycc` not found: run `flows/scripts/pyc build` or set `PYCC`.
- Backend IR legality failures: inspect `pyc-check-no-dynamic` and
  `pyc-check-flat-types` diagnostics.
- Hierarchy contract failures: ensure module boundaries are authored with
  `@module` and instance creation paths remain explicit.
- Trace decoding failures: verify `.pyctrace` header + `probe_manifest.json`
  consistency.

## 11. Next reading

- `docs/QUICKSTART.md`
- `docs/FRONTEND_API.md`
- `docs/TESTBENCH.md`
- `docs/IR_SPEC.md`
- `docs/tutorial/index.md`
- `designs/examples/README.md`
