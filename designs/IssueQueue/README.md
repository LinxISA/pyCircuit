# IssueQueue

This directory contains a parameterized issue queue design and a pyc-native
`@testbench` that checks forward progress and correct wakeup behavior.

Files:
- design: `/Users/zhoubot/pyCircuit/designs/IssueQueue/issq.py`
- config: `/Users/zhoubot/pyCircuit/designs/IssueQueue/issq_config.py`
- testbench: `/Users/zhoubot/pyCircuit/designs/IssueQueue/tb_issq.py`

## Run (Verilator)

```bash
PYTHONPATH=/Users/zhoubot/pyCircuit/compiler/frontend \
python3 -m pycircuit.cli build \
  /Users/zhoubot/pyCircuit/designs/IssueQueue/tb_issq.py \
  --out-dir /tmp/issq_build \
  --target verilator \
  --jobs 8 \
  --logic-depth 256 \
  --run-verilator
```

