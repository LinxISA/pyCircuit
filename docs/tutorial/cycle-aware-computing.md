# Cycle-Aware Computing

The **Cycle-Aware Signal System** is pyCircuit's key innovation. Each signal carries a logical clock cycle annotation, and the compiler automatically inserts pipeline registers (DFFs) when combining signals from different cycles.

## Understanding Cycles

In pyCircuit, time is measured in clock cycles, not absolute time. Each signal is associated with a specific cycle where its value is valid:

```python
# At cycle 0
data_in = domain.input("data_in", width=16)  # data_in.cycle = 0

domain.next()  # Advance to cycle 1

# At cycle 1
stage1 = domain.signal("stage1", width=16, reset=0)  # stage1.cycle = 1
```

## Automatic DFF Insertion

When signals from different cycles are combined, the compiler automatically inserts DFF chains to align them:

```python
# Assume: a.cycle = 0, b.cycle = 2, current_cycle = 2
result = a + b  # Compiler inserts 2 DFFs on 'a'

# After compilation:
# result = (a << 2) + b  // a delayed by 2 cycles
```

### Balancing Rules

| Signal Cycle | Current Cycle | Action |
|--------------|---------------|--------|
| < current | > current | Insert DFF chain (forward balancing) |
| = current | = current | Use directly |
| > current | < current | Use directly (feedback) |

## Practical Example: Multi-Stage Pipeline

```python
def pipeline(m: CycleAwareCircuit, domain: CycleAwareDomain, STAGES: int = 3) -> None:
    """Three-stage pipeline with automatic balancing."""
    data_in = domain.input("data_in", width=16)
    
    # Stage 0: Input registration
    stage0 = domain.signal("stage0", width=16, reset=0)
    domain.next()
    stage0.set(data_in)
    
    # Stage 1: Processing
    stage1 = domain.signal("stage1", width=16, reset=0)
    domain.next()
    stage1.set(stage0 * 2)  # Automatic: stage0 is balanced to current cycle
    
    # Stage 2: Output
    stage2 = domain.signal("stage2", width=16, reset=0)
    domain.next()
    stage2.set(stage1 + 1)
    
    m.output("data_out", stage2)
```

## Feedback Signals

Feedback signals (like branch predictions going back to fetch) are handled automatically because their cycle is greater than the current cycle:

```python
# At cycle 4: Branch resolution
branch_target = domain.input("branch_target", width=64)  # cycle 4

# At cycle 0: Using feedback (4 > 0, so no DFF inserted)
current_pc = domain.input("pc", width=64)  # cycle 0
predicted_pc = branch_target  # Direct connection - no DFF needed!

# This is exactly what we want for feedback!
```

## Using `domain.next()`

The `domain.next()` function advances the current cycle by 1:

```python
domain.current_cycle  # = 0

domain.next()  # = 1
domain.next()  # = 2
domain.prev()  # = 1
```

## Using `domain.push()` and `domain.pop()`

For complex designs, you may need to define signals at specific cycles:

```python
# Define a signal at cycle 3 for feedback
domain.push()  # Save current cycle (0)
domain.next()  # = 1
domain.next()  # = 2
domain.next()  # = 3

feedback = domain.signal("feedback", width=32)  # cycle 3

domain.pop()  # Restore to cycle 0

# Now use feedback in cycle 0 (3 > 0, direct connection)
result = data_in + feedback
```

## Cycle-Aware Arithmetic

All arithmetic operations automatically handle cycle balancing:

```python
# a.cycle = 0, b.cycle = 1, current_cycle = 1
c = a + b  # Inserts 1 DFF on 'a'
# c.cycle = 1

# a.cycle = 0, b.cycle = 2, current_cycle = 2
d = a + b  # Inserts 2 DFFs on 'a'
# d.cycle = 2
```

## Constants and Cycles

Constants automatically get the current cycle:

```python
domain.next()  # = 1
c = domain.const(42, width=8)  # c.cycle = 1
```

This prevents unnecessary DFF insertion when using constants in expressions.

## Debugging Cycle Issues

Use the `.cycle` attribute to inspect signal cycles:

```python
sig = domain.input("data", width=8)
print(f"Signal {sig.name} is at cycle {sig.cycle}")
print(f"Current domain cycle is {domain.current_cycle}")
```

## Summary

- Each signal carries a `.cycle` attribute
- `domain.next()` advances the current cycle
- Signals from earlier cycles get automatic DFF insertion
- Feedback signals (future cycles) connect directly
- Use `push()`/`pop()` to define signals at specific cycles

Next: [Primitives](primitives.md) covers built-in hardware primitives like memories and FIFOs.
