# pyCircuit v3.4

pyCircuit v3.4 is a strict fresh-start frontend:
- hard API contract enforcement before hardware lowering
- compile-time metaprogramming with `@const`
- split C++/Verilog emission for large designs

## Core model

- `@module`: hierarchy boundary
- `@function`: inline hardware helper
- `@const`: compile-time pure helper (no IR emission, no module mutation)
- inter-module boundaries use connectors only

## Canonical API

Top-level:
- `module`, `function`, `const`, `compile`, `ct`, `meta`

Circuit grammar:
- `inputs(...)`, `outputs(...)`, `state(...)`, `pipe(...)`
- `new(...)` for a single instance
- `array(...)` for deterministic instance collections
- `connect(...)` for strict connector wiring

Meta connect helpers:
- `meta.bind(...)`, `meta.ports(...)`
- `meta.inputs(...)`, `meta.outputs(...)`, `meta.state(...)`
- `meta.connect(...)`

## Build and run

```bash
bash /Users/zhoubot/pyCircuit/flows/scripts/pyc build
```

Emit MLIR:

```bash
PYTHONPATH=/Users/zhoubot/pyCircuit/compiler/frontend python3 -m pycircuit.cli emit \
  /Users/zhoubot/pyCircuit/designs/examples/template_module_collection_demo.py \
  -o /tmp/template_module_collection_demo.pyc
```

Compile split C++:

```bash
/Users/zhoubot/pyCircuit/compiler/mlir/build2/bin/pyc-compile /tmp/template_module_collection_demo.pyc \
  --emit=cpp --out-dir /tmp/template_module_collection_demo_cpp --cpp-split=module
```

## Strict contract gate

```bash
python3 /Users/zhoubot/pyCircuit/flows/tools/check_api_hygiene.py
```

Scan downstream project roots:

```bash
python3 /Users/zhoubot/pyCircuit/flows/tools/check_api_hygiene.py \
  --scan-root /Users/zhoubot/LinxCore src
```

## Main docs

- `/Users/zhoubot/pyCircuit/docs/USAGE.md`
- `/Users/zhoubot/pyCircuit/docs/COMPILER_FLOW.md`
- `/Users/zhoubot/pyCircuit/docs/TEMPLATE_METAPROGRAMMING.md`
- `/Users/zhoubot/pyCircuit/docs/META_STRUCTURES.md`
- `/Users/zhoubot/pyCircuit/docs/META_COLLECTIONS.md`
- `/Users/zhoubot/pyCircuit/docs/DIAGNOSTICS.md`

## Regressions

- `bash /Users/zhoubot/pyCircuit/flows/scripts/run_examples.sh`
- `bash /Users/zhoubot/pyCircuit/flows/tools/run_linx_cpu_pyc_cpp.sh`
- `bash /Users/zhoubot/pyCircuit/flows/tools/run_fastfwd_pyc_cpp.sh`
- `python3 /Users/zhoubot/pyCircuit/flows/tools/perf/run_perf_smoke.py`
