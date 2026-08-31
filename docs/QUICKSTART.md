# Quickstart

## 1) Build the backend tool (`pycc`)

From the **repository root** (set `REPO` to your clone path):

```bash
REPO=/path/to/pyCircuit
bash "$REPO/flows/scripts/pyc" build
```

## 2) Run compiler smoke (emit + pycc)

```bash
bash "$REPO/flows/scripts/run_examples.sh"
```

## 3) Run simulation smoke (Verilator + `@testbench`)

```bash
bash "$REPO/flows/scripts/run_sims.sh"
```

## 4) Minimal manual flow

Emit one module:

```bash
export PYTHONPATH="$REPO/compiler/frontend"
python3 -m pycircuit.cli emit "$REPO/designs/examples/counter/counter.py" -o /tmp/counter.pyc
```

Compile to C++:

```bash
export PYC_TOOLCHAIN_ROOT="$REPO/.pycircuit_out/toolchain/install"
"$PYC_TOOLCHAIN_ROOT/bin/pycc" /tmp/counter.pyc --emit=cpp --out-dir /tmp/counter_cpp
```

Build a multi-module project with a testbench:

```bash
export PYTHONPATH="$REPO/compiler/frontend"
export PYC_TOOLCHAIN_ROOT="$REPO/.pycircuit_out/toolchain/install"
python3 -m pycircuit.cli build \
  "$REPO/designs/examples/counter/tb_counter.py" \
  --out-dir /tmp/counter_build \
  --target both \
  --jobs 8
```
