# Quickstart Guide

This guide walks you through building and running your first pyCircuit design.

## Build the Compiler

If you haven't built the compiler yet:

```bash
bash flows/scripts/pyc build
```

## Your First Design: 8-bit Counter

Create a file named `counter.py`:

```python
from pycircuit import CycleAwareCircuit, CycleAwareDomain, compile_cycle_aware, mux

def counter(m: CycleAwareCircuit, domain: CycleAwareDomain) -> None:
    """An 8-bit counter with enable input."""
    # Input ports
    enable = domain.input("enable", width=1)
    
    # Register with reset value
    count = domain.signal("count", width=8, reset=0)
    
    # Combinational logic: increment or hold
    next_count = mux(enable, count + 1, count)
    
    # Advance one clock cycle
    domain.next()
    
    # Register assignment (D input)
    count.set(next_count)
    
    # Output port
    m.output("count", count)

# Compile the design
circuit = compile_cycle_aware(counter, name="counter")

# Emit MLIR
print(circuit.emit_mlir())
```

## Running the Design

### Step 1: Emit to MLIR

```bash
# From the pyCircuit root directory
PYTHONPATH=compiler/frontend python -m pycircuit.cli emit designs/examples/counter/counter.py -o counter.pyc
```

### Step 2: Compile to C++

```bash
# Compile to C++ for simulation
./build/bin/pycc counter.pyc --emit=cpp -o counter_build/
```

### Step 3: Run Simulation

```bash
# Build and run the testbench
cd counter_build
make
./tb_counter
```

## Compile to Verilog

For synthesis:

```bash
# Compile to Verilog
./build/bin/pycc counter.pyc --emit=verilog -o counter.v
```

You can now view `counter.v` in your favorite text editor or synthesis tool.

## Using the Testbench

pyCircuit includes a powerful testbench framework. Here's how to use it:

```python
from pycircuit import testbench, compile

@testbench
def counter_tb(dut):
    """Testbench for the counter design."""
    # Reset
    dut.enable = 0
    yield dut.tick()
    
    # Enable and count
    dut.enable = 1
    for _ in range(10):
        yield dut.tick()
        print(f"count = {dut.count}")

# Run the testbench
compile(counter_tb, "counter_tb", dut=counter)
```

## Running Examples

The project includes many examples you can try:

```bash
# Run all examples
bash flows/scripts/run_examples.sh

# Run specific example
PYTHONPATH=compiler/frontend python -m pycircuit.cli emit designs/examples/calculator/calculator.py -o calc.pyc
./build/bin/pycc calc.pyc --emit=cpp -o calc_build/

# Run simulations
bash flows/scripts/run_sims.sh
```

## What's Next?

- [First Design Tutorial](first-design.md) - Build a more complex design
- [Tutorial](../tutorial/index.md) - Deep dive into pyCircuit concepts
- [Examples](../examples/index.md) - Explore more design examples
