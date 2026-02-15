# Linx CPU SV Fixtures

This folder contains Verilog-sim inputs for `linx_cpu_pyc`:

- `programs/*.memh` test programs
- `tb_linx_cpu_pyc.sv` self-checking SystemVerilog TB

Generate RTL artifacts:

```bash
python3 flows/tools/pyc_flow.py regen --examples
```

Run simulation:

```bash
python3 flows/tools/pyc_flow.py verilog-sim linx_cpu_pyc --tool verilator \
  +memh=designs/examples/linx_cpu/programs/test_or.memh +expected=0000ff00
```

Artifacts are in `.pycircuit_out/examples/linx_cpu_pyc/`.
