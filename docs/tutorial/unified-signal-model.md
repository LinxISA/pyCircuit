# Unified Signal Model

pyCircuit's core innovation is the **Unified Signal Model** - a single signal definition syntax that works for both combinational logic (wires) and sequential elements (registers). The compiler automatically infers the hardware implementation based on how signals are used.

## Core Concept: Everything is a Signal

In traditional HDLs, you must explicitly choose between wire and register:

```verilog
// Verilog - explicit distinction
wire [7:0] combinational_result;
reg  [7:0] sequential_counter;
```

In pyCircuit, you simply define a signal and let the compiler infer the type:

```python
# pyCircuit - unified syntax
result = domain.signal("result", width=8)      # Compiler infers wire
counter = domain.signal("counter", width=8, reset=0)  # Compiler infers register
```

## How Type Inference Works

The compiler uses two information sources to infer hardware type:

### 1. Reset Value

A signal with a `reset` parameter is inferred as a D flip-flop (register):

```python
# With reset -> register (DFF)
count = domain.signal("count", width=8, reset=0)
```

### 2. Self-Reference

A signal that references itself in an assignment creates a feedback loop, which requires a register:

```python
# Self-reference -> register (feedback loop)
acc = domain.signal("acc", width=8)
acc.set(acc + 1)  # Self-reference: acc appears on RHS
```

## Inference Rules

| Definition | Assignment | Inferred Type |
|------------|-----------|---------------|
| No reset, no self-ref | `sig.set(expr)` | Wire (combinational) |
| With reset | `sig.set(expr)` | Register (sequential) |
| No reset, with self-ref | `sig.set(sig + ...)` | Error: needs reset |
| With reset, no self-ref | (no assignment) | Error: reset meaningless |

## Examples

### Combinational (Wire)

```python
# ALU result - combinational logic
alu_result = domain.signal("alu_result", width=64)
alu_result.set(src_left + src_right, when=op_is_add)
alu_result.set(src_left - src_right, when=op_is_sub)
```

### Sequential (Register)

```python
# Counter - sequential logic
counter = domain.signal("counter", width=8, reset=0)
domain.next()  # Advance one cycle
counter.set(counter + 1, when=enable)
```

### Error Cases

```python
# ERROR: reset without self-reference
temp = domain.signal("temp", width=8, reset=0)
temp.set(a + b)  # No self-reference - reset is meaningless!
# Compiler error: "Signal with reset must have self-reference"

# ERROR: self-reference without reset
acc = domain.signal("acc", width=8)
acc.set(acc + 1)  # Self-reference - needs reset!
# Compiler error: "Self-referential signal must have reset"
```

## Why This Design?

The unified signal model offers several advantages:

1. **Reduced Syntax**: One syntax to learn instead of two
2. **DRY Principle**: Don't Repeat Yourself - cycle annotations already specify timing
3. **Explicit Intent**: The `reset` parameter makes timing intent clear
4. **Compiler Safety**: The compiler catches timing errors at compile time

## Naming Conventions

While not required, we recommend using naming conventions to improve readability:

```python
# Convention: _r suffix for registers (sequential signals)
fetch_pc_r = domain.signal("fetch_pc_r", width=64, reset=0)
alu_result_r = domain.signal("alu_result_r", width=64, reset=0)

# No suffix for wires (combinational signals)
next_pc = ...
alu_result = ...
```

## Summary

The unified signal model is pyCircuit's key innovation:

- **Single syntax** for both wires and registers
- **Automatic inference** based on reset value and self-reference
- **Compile-time safety** catches timing errors early
- **Cleaner code** that directly expresses hardware intent

Next: [Cycle-Aware Computing](cycle-aware-computing.md) explains how pyCircuit handles signals across different clock cycles.
