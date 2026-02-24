# Welcome to pyCircuit

pyCircuit is a Python-based hardware description framework that brings modern software engineering practices to hardware design. Inspired by Chisel and pyMTL, it provides a unique **Cycle-Aware Signal System** that simplifies complex pipeline design.

## Why pyCircuit?

- **Pythonic**: Write hardware in Python with full IDE support
- **Automatic Pipeline Balancing**: Signals automatically align across clock cycles
- **MLIR-Powered**: Robust compilation infrastructure with optimization passes
- **Dual Output**: Generate C++ for fast simulation or Verilog for synthesis

## Key Concepts

### Unified Signal Model

Define both combinational logic and registers with the same syntax:

```python
# Wire (no reset) - combinational logic
result = domain.signal("result", width=32)
result.set(a + b)

# Register (with reset) - sequential logic
counter = domain.signal("counter", width=8, reset=0)
domain.next()
counter.set(counter + 1)
```

### Cycle-Aware Computing

Each signal tracks its logical clock cycle, enabling automatic DFF insertion:

```python
# Signals from different cycles are automatically balanced
stage0_data = domain.input("data_in", width=16)  # cycle 0
domain.next()
stage1_data = domain.signal("stage1", width=16, reset=0)  # cycle 1
domain.next()
# result combines cycle 0 and cycle 2 data
result = stage0_data + stage1_data  # DFFs inserted automatically
```

## Quick Links

- [Installation Guide](getting-started/installation.md) - Set up your development environment
- [Quickstart](getting-started/quickstart.md) - Build and run your first design
- [Tutorial](tutorial/index.md) - Deep dive into pyCircuit programming
- [Examples](examples/index.md) - Complete design examples
- [API Reference](api/frontend-api.md) - Detailed API documentation

## Performance

pyCircuit is designed for high-performance hardware simulation:

- **Fast C++ Simulation**: Generated C++ code runs at millions of cycles per second
- **Verilator Integration**: Optional Verilog simulation for timing-accurate validation
- **VCD Waveform Debugging**: Built-in waveform generation for signal tracing

## Community

- GitHub: https://github.com/LinxISA/pyCircuit
- Issues: https://github.com/LinxISA/pyCircuit/issues
- Discussions: https://github.com/LinxISA/pyCircuit/discussions
