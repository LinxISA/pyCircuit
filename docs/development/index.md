# Development Guide

Welcome to the pyCircuit development guide! This section covers everything you need to know about contributing to pyCircuit and setting up a development environment.

## Contributing

We welcome contributions! Please see our [Contributing Guide](contributing.md) for detailed information on:
- Setting up your development environment
- Code style and conventions
- Submitting pull requests
- Running tests

## Building from Source

See the [Build Guide](build.md) for detailed instructions on:
- Installing LLVM/MLIR dependencies
- Configuring CMake
- Building the compiler
- Troubleshooting build issues

## Testing

The [Testing Guide](testing.md) covers:
- Running unit tests
- Running integration tests
- Writing new tests
- Debugging test failures

## Architecture

pyCircuit is organized as follows:

```
pyCircuit
├── compiler/
│   ├── frontend/          # Python-based frontend
│   │   └── pycircuit/    # Core DSL implementation
│   └── mlir/             # MLIR-based backend
│       ├── lib/          # Dialect definitions
│       └── tools/        # Compiler tools
├── runtime/
│   ├── cpp/              # C++ simulation runtime
│   └── verilog/          # Verilog primitives
├── designs/
│   └── examples/         # Example designs
└── docs/                 # Documentation
```

## Quick Links

- [Contributing](contributing.md) - How to contribute
- [Build Guide](build.md) - Build from source
- [Testing](testing.md) - Run tests

## Getting Help

- GitHub Issues: Report bugs and request features
- GitHub Discussions: Ask questions and share ideas
- Discord: Join our community chat
