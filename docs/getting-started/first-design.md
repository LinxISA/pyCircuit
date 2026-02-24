# Your First Design: Calculator

In this tutorial, we'll build a simple calculator with arithmetic operations to demonstrate more advanced pyCircuit features.

## Design Specification

Our calculator will:
- Take two 8-bit inputs (a and b)
- Select an operation via a 2-bit operation select
- Output the result (16-bit to handle multiplication)

## Operations

| Op[1:0] | Operation |
|---------|-----------|
| 00 | a + b (add) |
| 01 | a - b (subtract) |
| 10 | a & b (and) |
| 11 | a * b (multiply) |

## Implementation

```python
from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    compile_cycle_aware,
    mux,
    ca_cat,
)

def calculator(m: CycleAwareCircuit, domain: CycleAwareDomain) -> None:
    """Simple calculator with add, subtract, and, multiply operations    # Input ports."""
    

    a = domain.input("a", width=8)
    b = domain.input("b", width=8)
    op = domain.input("op", width=2)
    
    # Constants
    zero = domain.const(0, width=8)
    one = domain.const(1, width=8)
    
    # Operation selection signals
    is_add = op.eq(zero)
    is_sub = op.eq(one)
    is_and = op.eq(domain.const(2, width=2))
    is_mul = op.eq(domain.const(3, width=2))
    
    # Compute operations
    result_add = a + b
    result_sub = a - b
    result_and = a & b
    result_mul = a * b
    
    # Select result using mux tree
    result = result_mul
    result = mux(is_and, result_and, result)
    result = mux(is_sub, result_sub, result)
    result = mux(is_add, result_add, result)
    
    # Output
    m.output("result", result)

# Compile and emit
circuit = compile_cycle_aware(calculator, name="calculator")
print(circuit.emit_mlir())
```

## Key Concepts Demonstrated

### 1. Constants

```python
zero = domain.const(0, width=8)
one = domain.const(1, width=8)
```

Constants are created with `domain.const(value, width)`. They automatically get assigned the current cycle.

### 2. Comparisons

```python
is_add = op.eq(zero)  # Equality comparison
```

Comparison operations return 1-bit signals that can be used in mux selection.

### 3. Mux Trees

```python
result = mux(is_add, result_add, result)
result = mux(is_sub, result_sub, result)
```

The `mux(sel, true_val, false_val)` function creates a multiplexer. We chain them to implement priority selection.

### 4. Bitwidth Extension

Notice that `a * b` produces a 16-bit result (8 + 8), while other operations produce 8-bit results. pyCircuit handles this automatically through its type system.

## Running the Design

```bash
# Emit MLIR
PYTHONPATH=compiler/frontend python -m pycircuit.cli emit \
    designs/examples/calculator/calculator.py -o calculator.pyc

# Compile to C++
./build/bin/pycc calculator.pyc --emit=cpp -o calculator_build/

# Run simulation
cd calculator_build
make
./tb_calculator
```

## Adding a Register

Let's modify the design to add an output register:

```python
def calculator_reg(m: CycleAwareCircuit, domain: CycleAwareDomain) -> None:
    """Calculator with registered output."""
    
    # Input ports
    a = domain.input("a", width=8)
    b = domain.input("b", width=8)
    op = domain.input("op", width=2)
    
    # ... compute result (same as before) ...
    
    # Register the result
    result_reg = domain.signal("result_reg", width=16, reset=0)
    
    domain.next()  # Advance to next cycle
    result_reg.set(result)  # Register assignment
    
    m.output("result", result_reg)
```

The key changes:
1. Create a register signal with `reset=0`
2. Call `domain.next()` to advance one cycle
3. Call `.set()` to assign the D input

## What's Next?

- [Tutorial](../tutorial/index.md) - Learn more about the unified signal model
- [Examples](../examples/index.md) - Explore more complex designs like FIFO and CPU
