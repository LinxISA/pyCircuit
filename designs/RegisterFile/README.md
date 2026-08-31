# RegisterFile

`RegisterFile` is a structural pyCircuit design that implements a PTAG-indexed
register file with:
- a read-only constant PTAG window
- a writable storage PTAG window
- parameterized read/write port counts

Files:
- design: `/Users/zhoubot/pyCircuit/designs/RegisterFile/regfile.py`
- testbench: `/Users/zhoubot/pyCircuit/designs/RegisterFile/tb_regfile.py`
- library block: `/Users/zhoubot/pyCircuit/compiler/frontend/pycircuit/lib/regfile.py`

## Run (Verilator)

```bash
PYTHONPATH=/Users/zhoubot/pyCircuit/compiler/frontend \
python3 -m pycircuit.cli build \
  /Users/zhoubot/pyCircuit/designs/RegisterFile/tb_regfile.py \
  --out-dir /tmp/regfile_build \
  --target verilator \
  --jobs 8 \
  --logic-depth 256 \
  --run-verilator
```

