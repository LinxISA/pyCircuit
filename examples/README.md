# Examples

Emit `.pyc` (MLIR) from Python:

```bash
PYTHONPATH=../binding/python python3 -m pycircuit.cli emit counter.py -o /tmp/counter.pyc
PYTHONPATH=../binding/python python3 -m pycircuit.cli emit fifo_loopback.py -o /tmp/fifo_loopback.pyc
PYTHONPATH=../binding/python python3 -m pycircuit.cli emit multiclock_regs.py -o /tmp/multiclock_regs.pyc
PYTHONPATH=../binding/python python3 -m pycircuit.cli emit wire_ops.py -o /tmp/wire_ops.pyc
PYTHONPATH=../binding/python python3 -m pycircuit.cli emit jit_control_flow.py -o /tmp/jit_control_flow.pyc
PYTHONPATH=../binding/python python3 -m pycircuit.cli emit jit_pipeline_vec.py -o /tmp/jit_pipeline_vec.pyc
PYTHONPATH=../binding/python python3 -m pycircuit.cli emit jit_cache.py -o /tmp/jit_cache.pyc
```

Then compile to Verilog:

```bash
../pyc/mlir/build/bin/pyc-compile /tmp/counter.pyc --emit=verilog -o /tmp/counter.sv
```

## Checked-in generated outputs

This repo checks in generated outputs under `examples/generated/`:

```bash
bash examples/update_generated.sh
```

## Generated outputs (checked in)

This repo checks in generated `*.sv` and `*.hpp` outputs under `examples/generated/`.

Regenerate (all examples + Linx CPU):

```bash
bash examples/update_generated.sh
```

## Debug traces

- C++ CPU TB (`examples/linx_cpu_pyc/tb_linx_cpu_pyc.cpp`):
  - `PYC_TRACE=1` writes a commit log under `examples/generated/linx_cpu_pyc/`.
  - `PYC_VCD=1` writes a VCD waveform under `examples/generated/linx_cpu_pyc/`.
  - Optional: set `PYC_TRACE_DIR=/path/to/dir` to override the output directory.
- C++ FIFO TB (`examples/cpp/tb_fifo.cpp`) and issue-queue TB (`examples/cpp/tb_issue_queue_2picker.cpp`):
  - Write `*.log` and `*.vcd` under `examples/generated/tb_fifo/` and `examples/generated/tb_issue_queue_2picker/`.
  - Optional: set `PYC_TRACE_DIR=/path/to/dir` to override the output directory.
- SystemVerilog CPU TB (`examples/linx_cpu/tb_linx_cpu_pyc.sv`):
  - Dumps to `examples/generated/linx_cpu_pyc/` by default.
  - Disable with `+notrace` (VCD) and/or `+nolog` (log). Add `+logcycles` to log per-cycle CSV rows.
- SystemVerilog issue-queue TB (`examples/issue_queue_2picker/tb_issue_queue_2picker.sv`):
  - Dumps to `examples/generated/tb_issue_queue_2picker/` by default (override with `+trace_dir=<path>`).
  - Disable with `+notrace` (VCD) and/or `+nolog` (log). Add `+logcycles` to log per-cycle CSV rows.
