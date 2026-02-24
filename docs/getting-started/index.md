# Getting Started

Welcome to the pyCircuit getting started guide! This section will help you set up your development environment and build your first hardware design.

## Prerequisites

- Python 3.9 or later
- LLVM/MLIR 17+ (for compiler backend)
- CMake 3.20+
- Ninja build system

## What's Covered

1. [Installation](installation.md) - Set up your development environment
2. [Quickstart](quickstart.md) - Build and run your first design
3. [First Design](first-design.md) - Write a complete hardware design

## Installation Options

### Option 1: Full Development Setup

```bash
# Install system dependencies (Ubuntu)
sudo apt-get install cmake ninja-build python3 python3-pip clang
sudo apt-get install llvm-dev mlir-tools libmlir-dev

# Clone and build
git clone https://github.com/LinxISA/pyCircuit.git
cd pyCircuit

# Build the compiler
bash flows/scripts/pyc build
```

### Option 2: Python Frontend Only

```bash
# Install Python package
pip install -e .

# Use the frontend to emit MLIR
PYTHONPATH=compiler/frontend python -m pycircuit.cli emit your_design.py
```

## Next Steps

Once you have pyCircuit installed, proceed to the [Quickstart](quickstart.md) guide to build and run your first design!
