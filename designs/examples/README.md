# Examples

This directory contains small frontend demos and larger reference designs.

## Main demos

- `counter.py`: minimal register + output
- `fifo_loopback.py`: ready/valid FIFO loopback
- `wire_ops.py`: core wire/arithmetic ops
- `jit_control_flow.py`: static `if/for` lowering
- `jit_pipeline_vec.py`: staged pipeline with vectors
- `hier_modules.py`: multi-module hierarchy/instantiation
- `template_struct_transform_demo.py`: immutable struct-transform metaprogramming
- `template_module_collection_demo.py`: module-family vector elaboration
- `template_instance_map_demo.py`: keyed module-map/module-dict elaboration
- `issue_queue_2picker.py`: issue queue example
- `linx_cpu_pyc/`: in-order Linx CPU reference
- `fastfwd_pyc/`: FastFwd reference + TBs

## One-command flows

Build compiler:

```bash
flows/scripts/pyc build
```

Regenerate local example artifacts:

```bash
python3 flows/tools/pyc_flow.py regen --examples
```

Run C++ regressions:

```bash
python3 flows/tools/pyc_flow.py cpp-test --cpu --fastfwd
```

Run Verilog sims:

```bash
python3 flows/tools/pyc_flow.py verilog-sim fastfwd_pyc +max_cycles=500 +max_pkts=1000 +seed=1
python3 flows/tools/pyc_flow.py verilog-sim issue_queue_2picker
python3 flows/tools/pyc_flow.py verilog-sim linx_cpu_pyc --tool verilator \
  +memh=designs/examples/linx_cpu/programs/test_or.memh +expected=0000ff00
```

## Artifact policy

Generated artifacts are local-only and written under:

- `.pycircuit_out/examples/...`

They are intentionally not checked into git.

## Useful knobs

- `PYC_BUILD_PROFILE=dev|release` (default: `release`)
- `PYC_TRACE=1` enable textual traces (where supported)
- `PYC_VCD=1` enable VCD dumping
- `PYC_KONATA=1` enable Konata traces
- `PYC_TRACE_DIR=/path` override trace output directory

## FastFwd FE count override

```bash
FASTFWD_N_FE=8 python3 flows/tools/pyc_flow.py regen --examples
```

## Optional trace-diff flow

If you have Linx QEMU + LLVM `llvm-mc`:

```bash
bash flows/tools/run_linx_qemu_vs_pyc.sh /path/to/test.s
```
