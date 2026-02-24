# Installation Guide

This guide covers setting up the pyCircuit development environment.

## System Requirements

| Component | Minimum Version | Recommended Version |
|-----------|---------------|---------------------|
| Python | 3.9 | 3.10+ |
| LLVM | 17 | 19 |
| CMake | 3.20 | 3.28+ |
| Ninja | 1.10 | Latest |

## Install System Dependencies

### Ubuntu/Debian

```bash
# Update package lists
sudo apt-get update

# Install build tools
sudo apt-get install -y cmake ninja-build python3 python3-pip clang

# Install LLVM/MLIR (Ubuntu 22.04+)
sudo apt-get install -y llvm-dev mlir-tools libmlir-dev

# Verify installation
llvm-config --version
mlir-opt --version
```

### macOS

```bash
# Install Homebrew if not already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install build tools
brew install cmake ninja python@3

# Install LLVM with MLIR
brew install llvm
# Add LLVM to PATH
echo 'export PATH="$(brew --prefix llvm)/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc

# Verify installation
llvm-config --version
```

## Clone and Build

```bash
# Clone the repository
git clone https://github.com/LinxISA/pyCircuit.git
cd pyCircuit

# Configure with CMake
LLVM_DIR="$(llvm-config --cmakedir)"
MLIR_DIR="$(dirname "$LLVM_DIR")/mlir"

cmake -G Ninja -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR="$LLVM_DIR" \
  -DMLIR_DIR="$MLIR_DIR"

# Build the compiler
ninja -C build pycc pyc-opt

# Verify the build
./build/bin/pycc --version
```

## Alternative: Use Build Script

```bash
# The project includes a build script that handles LLVM detection
bash flows/scripts/pyc build
```

## Install Python Package

```bash
# Install pycircuit in development mode
pip install -e .

# Verify installation
python -c "import pycircuit; print(pycircuit.__version__)"
```

## Verify Your Setup

```bash
# Run the smoke test
bash flows/scripts/run_examples.sh

# Should output something like:
# Compiling counter... OK
# Compiling calculator... OK
# Compiling fifo_loopback... OK
```

## Troubleshooting

### LLVM Not Found

If CMake can't find LLVM, set the paths explicitly:

```bash
export LLVM_DIR=/path/to/llvm/lib/cmake/llvm
export MLIR_DIR=/path/to/mlir/lib/cmake/mlir
cmake -G Ninja -S . -B build ...
```

### Python Version Issues

pyCircuit requires Python 3.9+. Check your version:

```bash
python3 --version
```

If you need to install a newer Python version:

```bash
# Ubuntu
sudo apt-get install python3.11 python3.11-venv

# macOS
brew install python@3.11
```

### Build Errors

Clean and rebuild:

```bash
rm -rf build
cmake -G Ninja -S . -B build ...
ninja -C build clean
ninja -C build pycc
```
