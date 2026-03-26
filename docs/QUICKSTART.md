# Quickstart

## 1) Build the backend tool (`pycc`)

```bash
bash /Users/zhoubot/pyCircuit/flows/scripts/pyc build
```

## 2) Run compiler smoke (emit + pycc)

```bash
bash /Users/zhoubot/pyCircuit/flows/scripts/run_examples.sh
```

## 3) Run simulation smoke (Verilator + `@testbench`)

```bash
bash /Users/zhoubot/pyCircuit/flows/scripts/run_sims.sh
```

## 4) Minimal manual flow

Emit one module:

```bash
PYTHONPATH=/Users/zhoubot/pyCircuit/compiler/frontend \
python3 -m pycircuit.cli emit /Users/zhoubot/pyCircuit/designs/examples/counter/counter.py -o /tmp/counter.pyc
```

Compile to C++:

```bash
PYC_TOOLCHAIN_ROOT=/Users/zhoubot/pyCircuit/.pycircuit_out/toolchain/install \
/Users/zhoubot/pyCircuit/.pycircuit_out/toolchain/install/bin/pycc /tmp/counter.pyc --emit=cpp --out-dir /tmp/counter_cpp
```

Build a multi-module project with a testbench:

```bash
PYTHONPATH=/Users/zhoubot/pyCircuit/compiler/frontend \
PYC_TOOLCHAIN_ROOT=/Users/zhoubot/pyCircuit/.pycircuit_out/toolchain/install \
python3 -m pycircuit.cli build \
  /Users/zhoubot/pyCircuit/designs/examples/counter/tb_counter.py \
  --out-dir /tmp/counter_build \
  --target both \
  --jobs 8
```
