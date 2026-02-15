# Linx CPU (pyCircuit)

In-order Linx bring-up CPU modeled in pyCircuit.

## Structure

- `linx_cpu_pyc.py`: top-level build entry
- `decode.py`, `isa.py`: decode + ISA constants
- `regfile.py`, `memory.py`: state/memory blocks
- `pipeline.py`, `stages/*.py`: IF/ID/EX/MEM/WB logic
- `tb_linx_cpu_pyc.cpp`: self-checking C++ TB

## Run C++ regression

```bash
bash flows/tools/run_linx_cpu_pyc_cpp.sh
```

Run with explicit image:

```bash
bash flows/tools/run_linx_cpu_pyc_cpp.sh \
  --memh designs/examples/linx_cpu/programs/test_or.memh \
  --expected 0x0000ff00
```

Run from ELF (auto converts to memh):

```bash
bash flows/tools/run_linx_cpu_pyc_cpp.sh --elf /path/to/test.o --expected 0x0000ff00
```

Artifacts are written under:

- `.pycircuit_out/examples/linx_cpu_pyc/`
