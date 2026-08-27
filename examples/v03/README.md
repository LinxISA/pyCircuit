# Explicit memory example (epoch 0.3)

`memory_simple.py` declares one root-owned, 16-entry `u16` memory and connects
two typed logical endpoints from child scopes. Writer endpoint ordinal 0 has
fixed priority over reader endpoint ordinal 1.

The harness makes both endpoints valid together. Two writes to address `3`
return old values `0` and `42`; only after those responses complete can the
reader run, and it observes the final value `99`. This exercises:

- one physical memory shared by multiple logical request endpoints;
- root-to-descendant scope visibility;
- canonical fixed-priority arbitration;
- one outstanding request and global busy backpressure;
- endpoint-specific write policies and response demultiplexing;
- old-data write responses.

From the repository root:

```bash
PYTHONPATH=src \
  /home/lc/opt/agentic-circuit-toolchain/python-env/bin/python \
  tools/ac-queue-cxxgen.py examples/v03/memory_simple.py \
  --system memory_simple \
  --acir-output /tmp/memory_simple.mlir \
  --plan-output /tmp/memory_simple.plan.json \
  --acir-opt build/gcc14/bin/acir-opt \
  --queue-plan-tool build/gcc14/bin/acir-queue-plan \
  --queue-cxxgen-tool build/gcc14/bin/acir-queue-cxxgen \
  --output examples/v03/memory_simple.generated.cpp

/home/lc/opt/gcc14/bin/aarch64-conda-linux-gnu-g++ \
  -std=c++20 -Iinclude -Iexamples/v03 \
  examples/v03/memory_simple_harness.cpp \
  -o /tmp/memory_simple_sim

/tmp/memory_simple_sim
```

Expected output:

```text
cycles=8 write_old_values=0,42 read_after_priority=99
```

The generated `memory_simple.generated.cpp` is a build artifact and is not
checked in.

## Current boundary

The 0.3 contract intentionally supports one physical port and one outstanding
request per memory instance. It does not provide true multi-port access,
round-robin fairness, response reordering, byte enables, or non-zero
initialization. A continuously valid higher-priority endpoint can starve lower
ordinals. All endpoints of an instance must use the same payload struct and
the memory data/address widths are limited to 64 bits.
