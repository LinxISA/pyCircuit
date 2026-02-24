# pyCircuit

<p align="center">
  <img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License">
  <img src="https://img.shields.io/badge/Python-3.9+--green.svg" alt="Python">
  <img src="https://img.shields.io/badge/MLIR-17+-orange.svg" alt="MLIR">
  <a href="https://github.com/LinxISA/pyCircuit/actions"><img src="https://github.com/LinxISA/pyCircuit/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
</p>

pyCircuit is a Python-based hardware description framework that compiles Python functions to synthesizable RTL through MLIR intermediate representation. Inspired by [Chisel](https://github.com/chipsalliance/chisel3) and [pyMTL](https://github.com/pymtl/pymtl3), pyCircuit provides a unique **Cycle-Aware Signal System** where every signal carries a logical clock cycle annotation, and the compiler automatically inserts pipeline registers (DFFs) when combining signals from different pipeline stages.

## Key Features

- **Cycle-Aware Signal System**: Unified signal model without distinguishing between wires and registers - the compiler automatically infers hardware implementation
- **Automatic Pipeline Balancing**: Signals from different clock cycles are automatically aligned with DFF insertion
- **Multi-Level Abstraction**: From high-level Python to synthesizable Verilog/C++
- **Built-in Testbench Support**: Native `@testbench` decorator for cycle-accurate simulation
- **MLIR-Based IR**: Robust intermediate representation for hardware compilation
- **C++/Verilog Emission**: Generate high-quality C++ for simulation or Verilog for synthesis

## Quick Start

### Installation

```bash
# Clone the repository
git clone https://github.com/LinxISA/pyCircuit.git
cd pyCircuit

# Install Python dependencies
pip install -e .

# Build the compiler (requires LLVM+MLIR)
# See https://llvm.org/docs/GettingStarted.html for LLVM setup
bash flows/scripts/pyc build
```

### Your First Design: 8-bit Counter

```python
from pycircuit import CycleAwareCircuit, CycleAwareDomain, compile_cycle_aware, mux

def counter(m: CycleAwareCircuit, domain: CycleAwareDomain) -> None:
    enable = domain.input("enable", width=1)
    count = domain.signal("count", width=8, reset=0)
    next_count = mux(enable, count + 1, count)
    
    domain.next()  # Advance one cycle
    count.set(next_count)
    
    m.output("count", count)

# Compile to MLIR
circuit = compile_cycle_aware(counter, name="counter")
print(circuit.emit_mlir())
```

### Compile and Simulate

```bash
# Emit to MLIR
pycircuit emit designs/examples/counter/counter.py -o counter.pyc

# Compile to C++ for simulation
pycc counter.pyc --emit=cpp -o counter_build/

# Compile to Verilog for synthesis
pycc counter.pyc --emit=verilog -o counter.v
```

## Architecture

```
pyCircuit
├── compiler/
│   ├── frontend/          # Python-based frontend (pycircuit package)
│   │   └── pycircuit/     # Core DSL and API
│   └── mlir/              # MLIR-based compiler backend
│       ├── lib/           # Dialect definitions and passes
│       └── tools/         # pycc, pyc-opt compilers
├── runtime/
│   ├── cpp/               # C++ simulation runtime
│   └── verilog/           # Verilog simulation primitives
├── designs/
│   └── examples/          # Example designs
└── docs/                  # Documentation
```

## Documentation

- [Tutorial](doc/pyCircuit_Tutorial.md) - Comprehensive guide to pyCircuit programming
- [Quickstart](docs/QUICKSTART.md) - Build and run your first design
- [Frontend API](docs/FRONTEND_API.md) - Python API reference
- [IR Specification](docs/IR_SPEC.md) - MLIR dialect specification
- [Testbench Guide](docs/TESTBENCH.md) - Writing cycle-accurate testbenches

## Examples

| Example | Description |
|---------|-------------|
| [Counter](designs/examples/counter/) | Basic counter with enable |
| [Calculator](designs/examples/calculator/) | ALU with arithmetic operations |
| [FIFO Loopback](designs/examples/fifo_loopback/) | FIFO queue with loopback |
| [Digital Clock](designs/examples/digital_clock/) | Time-of-day clock display |
| [FastFWD](designs/examples/fastfwd/) | Network packet forwarding |
| [Linx CPU](contrib/linx/designs/examples/linx_cpu_pyc/) | Full 5-stage pipeline CPU |

## Design Philosophy

### Unified Signal Model

pyCircuit uses a **single signal definition syntax** for both combinational logic and sequential elements. The compiler automatically infers the hardware type based on:

1. **Reset value**: Signals with `reset` parameter become D flip-flops
2. **Self-reference**: Signals that reference themselves in assignments become registers

```python
# Combinational (wire) - no reset
alu_result = domain.signal("alu_result", width=64)
alu_result.set(a + b)

# Sequential (register) - with reset
counter = domain.signal("counter", width=8, reset=0)
domain.next()
counter.set(counter + 1)
```

### Cycle-Aware Computing

Each signal carries a `.cycle` annotation indicating its logical clock cycle. When signals from different cycles are combined, the compiler automatically inserts DFF chains for alignment:

```python
# a.cycle=0, b.cycle=2, domain.current_cycle=2
result = a + b  # Compiler inserts 2 DFFs on 'a'
```

## Development

### Building from Source

```bash
# Install LLVM/MLIR (Ubuntu)
sudo apt-get install llvm-dev mlir-tools libmlir-dev

# Configure and build
cmake -G Ninja -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR="$(llvm-config --cmakedir)" \
  -DMLIR_DIR="$(dirname $(llvm-config --cmakedir))/mlir"

ninja -C build pycc pyc-opt
```

### Running Tests

```bash
# Run all examples
bash flows/scripts/run_examples.sh

# Run simulations
bash flows/scripts/run_sims.sh

# Run Linx CPU regression
bash contrib/linx/flows/tools/run_linx_cpu_pyc_cpp.sh
```

### Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for detailed development guidelines.

## License

pyCircuit is licensed under the MIT License. See [LICENSE](LICENSE) for details.

## Related Projects

- [LinxISA](https://github.com/LinxISA) - RISC-V ISA implementation
- [MLIR](https://github.com/llvm/llvm-project) - Multi-Level Intermediate Representation
- [Chisel](https://github.com/chipsalliance/chisel3) - Scala-based hardware construction language
- [pyMTL](https://github.com/pymtl/pymtl3) - Python-based hardware modeling framework

## Citation

If you use pyCircuit in your research, please cite:

```bibtex
@software{pycircuit,
  title = {pyCircuit: A Python-based Hardware Description Framework},
  author = {LinxISA Contributors},
  url = {https://github.com/LinxISA/pyCircuit},
  year = {2024}
}
```
