# Tutorial

Welcome to the pyCircuit tutorial! This section provides in-depth coverage of pyCircuit's core concepts and advanced features.

## Overview

pyCircuit is a hardware description framework that brings software engineering best practices to hardware design. Its key innovation is the **Cycle-Aware Signal System** that automatically handles pipeline balancing.

## What You'll Learn

### 1. [Unified Signal Model](unified-signal-model.md)

Learn how pyCircuit uses a single signal definition syntax for both combinational logic (wires) and sequential elements (registers). Understand how the compiler automatically infers hardware type based on reset values and self-references.

### 2. [Cycle-Aware Computing](cycle-aware-computing.md)

Deep dive into the cycle-aware computing model. Learn how signals carry logical clock cycle annotations and how the compiler automatically inserts DFF chains when combining signals from different cycles.

### 3. [Primitives](primitives.md)

Explore pyCircuit's built-in primitives including:
- Memories (RAM, ROM)
- FIFOs and queues
- Register files
- Structural elements

### 4. [Testbench](testbench.md)

Learn to write cycle-accurate testbenches using pyCircuit's built-in testbench framework. Understand how to drive DUT inputs, sample outputs, and verify correct behavior.

## Prerequisites

- Basic understanding of digital logic design
- Familiarity with Python programming
- Completed the [Getting Started](../getting-started/index.md) guide

## Example Code

Here's a preview of what you'll be able to write after completing this tutorial:

```python
from pycircuit import CycleAwareCircuit, CycleAwareDomain, compile_cycle_aware, mux

def pipeline_example(m: CycleAwareCircuit, domain: CycleAwareDomain, STAGES: int = 3) -> None:
    """Multi-stage pipeline with automatic DFF insertion."""
    data_in = domain.input("data_in", width=16)
    valid_in = domain.input("valid_in", width=1)
    
    bus = data_in
    valid = valid_in
    
    for i in range(STAGES):
        stage_r = domain.signal(f"stage{i}_r", width=16, reset=0)
        valid_r = domain.signal(f"valid{i}_r", width=1, reset=0)
        
        domain.next()
        stage_r.set(bus)
        valid_r.set(valid)
        
        bus = stage_r
        valid = valid_r
    
    m.output("data_out", bus)
    m.output("valid_out", valid)

# Compile with custom parameters
circuit = compile_cycle_aware(pipeline_example, name="pipeline", STAGES=5)
```

## Next Steps

Start with the [Unified Signal Model](unified-signal-model.md) to understand pyCircuit's core philosophy, then proceed to [Cycle-Aware Computing](cycle-aware-computing.md) for the technical details.
