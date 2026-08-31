# Linx CPU SV Fixtures

This folder contains Verilog-sim inputs for `linx_cpu_pyc`:

- `programs/*.memh` test programs
- `tb_linx_cpu_pyc.sv` self-checking SystemVerilog TB

Suggested flow:
- Use the scripts under `/Users/zhoubot/pyCircuit/contrib/linx/flows/tools/` to emit RTL and run simulation.
- Artifacts are written out-of-tree under `.pycircuit_out/`.
