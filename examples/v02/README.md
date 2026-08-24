# Queue/Var DavinciOO-like model

`davincioo_queue_model.py` is the first executable v0.2 topology generated
from serial Python. It uses only repository-owned common building blocks:
`ac.transform`, `ac.route`, `ac.merge`, `ac.queue`, and `ac.sink`.

Generate one canonical typed C++ model:

```bash
PYTHONPATH=src tools/ac-queue-cxxgen.py \
  examples/v02/davincioo_queue_model.py \
  --system davincioo_queue_model \
  --acir-output build/davincioo_queue_model.ac.mlir \
  --plan-output build/davincioo_queue_model.queue-plan.json \
  --acir-opt build/dev-llvm22/bin/acir-opt \
  --queue-plan-tool build/dev-llvm22/bin/acir-queue-plan \
  --queue-cxxgen-tool build/dev-llvm22/bin/acir-queue-cxxgen \
  -o build/davincioo_queue_model.cpp
```

The generated class owns every interconnect as a typed `gfsim::SimQueue<T>`.
Lexical scopes become `gfsim::Module` hierarchy nodes. Engine blocks borrow
Queue references; they do not allocate or own sibling interconnect.

The example models the reference shape at building-block level:

```text
trace -> frontend -> 4-way dispatch
                         | scalar
                         | vector
                         | cube
                         | tma
                    merge -> retire -> sink
```

This milestone validates topology, typed payload updates, finite queues,
backpressure, Work/arbitrate/Xfer barriers, and deterministic generated C++.
It does not yet claim the imported DavinciOO 15-record/453-cycle performance
contract. That gate requires the remaining official scheduler, dependency,
ROB, memory, and observation blocks.

## PYC and Verilog slice

`pyc_queue_pipeline.py` exercises the initial scalar hardware lowering. Generate
frozen ACIR first, then run `acir-queue-pycgen` or the bundled
`tools/ac-queue-pyc-build.py` command. The bundle command validates the pinned
toolchain lock, invokes external `pycc` for C++ and Verilog, compiles the C++
source, runs Verilator lint, and writes a canonical hash manifest.

The pinned toolchain is recorded in `toolchains/pyc-v0.2.lock.json`. Build that
exact pyCircuit commit with LLVM 19 before running the PYC gate.

`pyc_struct_pipeline.py` verifies deterministic packed struct layout.
`pyc_route_merge_pipeline.py` verifies static selector demux and priority merge
logic, including forward valid and backward ready paths.
`pyc_atomic_pipeline.py` verifies multi-source/multi-sink atomic firing.
`pyc_fork_pipeline.py` verifies decoupled fanout with per-output delivered state.
