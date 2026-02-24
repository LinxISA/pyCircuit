# API Reference

This section provides detailed API documentation for pyCircuit.

## Core Modules

### `pycircuit` - Main Package

The main package containing all pyCircuit APIs.

```python
from pycircuit import (
    CycleAwareCircuit,    # Circuit builder
    CycleAwareDomain,      # Clock domain
    CycleAwareSignal,     # Signal type
    compile_cycle_aware,   # Compilation entry point
    mux,                   # Multiplexer
    ca_cat,                # Signal concatenation
    ca_bundle,             # Structure packing
)
```

### Frontend API

- [Frontend API Overview](frontend-api.md) - Main API documentation
- [IR Specification](ir-spec.md) - MLIR dialect specification
- [Diagnostics](diagnostics.md) - Compiler error messages

## Key Classes

### CycleAwareCircuit

The circuit builder for module-level operations.

| Method | Description |
|--------|-------------|
| `m.output(name, signal)` | Declare output port |
| `m.cat_signals(*signals)` | Concatenate signals |
| `m.ca_byte_mem(...)` | Create byte-addressable memory |
| `m.ca_queue(...)` | Create FIFO queue |
| `m.ca_bundle(...)` | Create struct/bundle |

### CycleAwareDomain

Clock domain that is the entry point for all signal declarations.

| Method | Description |
|--------|-------------|
| `domain.input(name, width)` | Create input port |
| `domain.signal(name, width, reset=None)` | Create signal |
| `domain.const(value, width)` | Create constant |
| `domain.next()` | Advance one cycle |
| `domain.prev()` | Go back one cycle |
| `domain.push()` | Save current cycle |
| `domain.pop()` | Restore saved cycle |

### CycleAwareSignal

The unique hardware signal type.

| Property | Description |
|----------|-------------|
| `.cycle` | Logical clock cycle |
| `.width` | Bit width |
| `.signed` | Signed/unsigned |
| `.name` | Debug name |

| Method | Description |
|--------|-------------|
| `.set(value, when=cond)` | Conditional assignment |
| `.eq(other)` | Equality comparison |
| `.ne(other)` | Not equal |
| `.lt(other)` | Less than |
| `.trunc(width)` | Truncate bits |
| `.zext(width)` | Zero extend |
| `.sext(width)` | Sign extend |
| `.slice(lsb, width)` | Bit slice |

## Compilation

### compile_cycle_aware

```python
circuit = compile_cycle_aware(
    design_fn,           # Design function
    name="my_module",    # Module name
    **jit_params        # Compile-time parameters
)
```

### Emitting Output

```python
# Emit MLIR
mlir_text = circuit.emit_mlir()

# Compile to C++ or Verilog via pycc
# pycc design.pyc --emit=cpp --output-dir build/
```

## Testbench

See [Testbench Guide](testbench.md) for detailed testbench API.
