# Contributing to pyCircuit

Thank you for your interest in contributing to pyCircuit! This guide will help you get started with development.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Setup](#development-setup)
- [Coding Standards](#coding-standards)
- [Pull Request Process](#pull-request-process)
- [Reporting Bugs](#reporting-bugs)
- [Feature Requests](#feature-requests)

## Code of Conduct

Please read and follow our [Code of Conduct](CODE_OF_CONDUCT.md). We expect all contributors to be respectful and professional.

## Getting Started

pyCircuit is a Python-based hardware description framework that compiles Python to RTL through MLIR. Before contributing, please:

1. Read the [Documentation](docs/)
2. Try the [Quickstart Guide](docs/getting-started/quickstart.md)
3. Explore the [Examples](designs/examples/)

## Development Setup

### Prerequisites

- Python 3.9 or later
- LLVM/MLIR 17+ (for compiler backend)
- CMake 3.20+
- Ninja build system
- Git

### Clone and Install

```bash
# Clone the repository
git clone https://github.com/LinxISA/pyCircuit.git
cd pyCircuit

# Install Python package in development mode
pip install -e ".[dev]"

# Build the compiler
bash flows/scripts/pyc build
```

### Running Tests

```bash
# Run all examples (smoke test)
bash flows/scripts/run_examples.sh

# Run simulations
bash flows/scripts/run_sims.sh

# Run Linx CPU regression
bash contrib/linx/flows/tools/run_linx_cpu_pyc_cpp.sh

# Run Python tests (if available)
pytest
```

## Coding Standards

### Python Style

We follow Python's PEP 8 with some modifications:

- Line length: 88 characters (Black default)
- Use type hints where beneficial
- Use docstrings for all public functions

### Code Formatting

We use several tools to maintain code quality:

```bash
# Format code
black .
ruff check --fix .

# Run linters
ruff check .
mypy .
```

### Git Conventions

- Use meaningful commit messages
- Keep commits atomic (one feature/fix per commit)
- Use present tense: "Add feature" not "Added feature"
- Reference issues in commits: "Fix #123"

## Pull Request Process

### Before Submitting

1. **Run tests**: Ensure all tests pass
2. **Format code**: Run Black and Ruff
3. **Update documentation**: If adding new features, update docs
4. **Update examples**: If changing codegen, regenerate examples

### PR Description

Include in your PR description:

1. **Summary**: What does this PR do?
2. **Motivation**: Why is this change needed?
3. **Testing**: How did you test this change?
4. **Screenshots**: If applicable, show before/after

### PR Checklist

- [ ] Tests pass
- [ ] Code is formatted
- [ ] Documentation updated
- [ ] Examples regenerated (if applicable)
- [ ] No new warnings

## Reporting Bugs

When reporting bugs, please include:

1. **Environment**: OS, Python version, LLVM version
2. **Steps to reproduce**: Detailed reproduction steps
3. **Expected behavior**: What you expected to happen
4. **Actual behavior**: What actually happened
5. **Logs**: Any relevant error messages

Use the [GitHub Issue Tracker](https://github.com/LinxISA/pyCircuit/issues) to report bugs.

## Feature Requests

We welcome feature requests! Please include:

1. **Use case**: What problem are you solving?
2. **Proposed solution**: How do you think it should work?
3. **Alternatives**: What other approaches have you considered?

## Directory Structure

```
pyCircuit
├── compiler/
│   ├── frontend/          # Python frontend (pycircuit package)
│   │   └── pycircuit/    # Core DSL implementation
│   └── mlir/             # MLIR backend
│       ├── lib/           # Dialect definitions and passes
│       └── tools/         # pycc, pyc-opt compilers
├── runtime/
│   ├── cpp/              # C++ simulation runtime
│   └── verilog/          # Verilog primitives
├── designs/
│   └── examples/         # Example designs
├── docs/                 # Documentation
└── flows/               # Build and test scripts
```

## Communication

- **Issues**: GitHub Issues for bugs and features
- **Discussions**: GitHub Discussions for questions
- **Discord**: Join our community for real-time chat

## License

By contributing to pyCircuit, you agree that your contributions will be licensed under the [MIT License](LICENSE).
