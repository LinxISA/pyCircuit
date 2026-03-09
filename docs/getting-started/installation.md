# Installation Guide

This guide covers setting up the pyCircuit development environment.

## System Requirements

| Component | Minimum Version | Recommended Version |
|-----------|---------------|---------------------|
| Python | 3.9 | 3.10+ |
| LLVM | 19 | 19 |
| CMake | 3.20 | 3.28+ |
| Ninja | 1.10 | Latest |

## Install System Dependencies

### Ubuntu/Debian

```bash
# Update package lists
sudo apt-get update

# Install build tools
sudo apt-get install -y cmake ninja-build python3 python3-pip clang wget

# Install LLVM/MLIR 19 (Ubuntu 22.04+)
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 19
sudo apt-get install -y llvm-19-dev mlir-19-tools libmlir-19-dev

# Verify installation
llvm-config-19 --version
mlir-opt --version
```

### macOS

```bash
# Install Homebrew if not already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install build tools
brew install cmake ninja python@3

# Install LLVM 19 with MLIR
brew install llvm@19
# Add LLVM to PATH
echo 'export PATH="$(brew --prefix llvm@19)/bin:$PATH"' >> ~/.zshrc
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
LLVM_DIR="$(llvm-config-19 --cmakedir)"
MLIR_DIR="$(dirname "$LLVM_DIR")/mlir"

cmake -G Ninja -S . -B .pycircuit_out/toolchain/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/.pycircuit_out/toolchain/install" \
  -DLLVM_DIR="$LLVM_DIR" \
  -DMLIR_DIR="$MLIR_DIR"

# Build and stage the compiler toolchain
ninja -C .pycircuit_out/toolchain/build pycc pyc-opt pyc4_runtime
cmake --install .pycircuit_out/toolchain/build --prefix "$PWD/.pycircuit_out/toolchain/install"

# Verify the build
./.pycircuit_out/toolchain/install/bin/pycc --version
```

## Alternative: Use Build Script

```bash
# The project includes a build script that handles LLVM detection
bash flows/scripts/pyc build
```

## Alternative: Install a Release Wheel

```bash
python3 -m pip install pycircuit-<version>-<platform>.whl

# The wheel ships the matching toolchain inside site-packages.
pycc --version
python3 -m pycircuit.cli --help
```

The wheel is platform-specific because it embeds `pycc`, the runtime archive,
and the required LLVM/MLIR shared libraries. Use the wheel that matches your
OS and architecture.

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
cmake -G Ninja -S . -B .pycircuit_out/toolchain/build ...
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
rm -rf .pycircuit_out/toolchain
cmake -G Ninja -S . -B .pycircuit_out/toolchain/build ...
ninja -C .pycircuit_out/toolchain/build clean
ninja -C .pycircuit_out/toolchain/build pycc
```
