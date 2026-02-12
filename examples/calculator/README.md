# 16-Digit Calculator (pyCircuit)

A 16-digit calculator implemented in pyCircuit's unified signal model,
with true RTL simulation and animated terminal visualization.

## Features

- **16-digit display** with 7-segment ASCII art
- **Four operations**: +, -, *, / (integer arithmetic)
- **Operator chaining**: `5 + 3 * 2 =` evaluates left-to-right as `(5+3)*2 = 16`
- **Error handling**: divide-by-zero, overflow (> 9,999,999,999,999,999)
- **AC (All Clear)** resets the calculator
- **Negative results** displayed with sign

## Architecture

Single-cycle design. Inputs are a 4-bit key code + 1-bit press strobe.
Internal state machine: `INPUT_A → OP_WAIT → INPUT_B → RESULT`.

| Register | Width | Description |
|----------|-------|-------------|
| display  | 64    | Current display value (unsigned magnitude) |
| accum    | 64    | Saved first operand |
| state    | 2     | FSM state |
| op       | 3     | Pending operator (0=none, 1=+, 2=-, 3=*, 4=/) |
| neg      | 1     | Display value is negative |
| err      | 1     | Error flag |

## Files

| File | Description |
|------|-------------|
| `calculator.py` | pyCircuit RTL design |
| `calculator_capi.cpp` | C API wrapper for compiled RTL |
| `emulate_calculator.py` | Terminal UI driving true RTL simulation |

## Build & Run

```bash
# 1. Compile RTL → C++
PYTHONPATH=python:. python -m pycircuit.cli emit examples/calculator/calculator.py \
    -o examples/generated/calculator/calculator.pyc
build/bin/pyc-compile examples/generated/calculator/calculator.pyc \
    --emit=cpp -o examples/generated/calculator/calculator_gen.hpp

# 2. Build shared library
cd examples/calculator
c++ -std=c++17 -O2 -shared -fPIC -I../../include \
    -o libcalculator_sim.dylib calculator_capi.cpp

# 3. Run emulator (true RTL simulation)
cd ../..
python examples/calculator/emulate_calculator.py
```

## Test Cases (10)

| # | Expression | Expected |
|---|-----------|----------|
| 1 | 123 + 456 | 579 |
| 2 | 1000 - 1 | 999 |
| 3 | 12 * 34 | 408 |
| 4 | 100 / 3 | 33 |
| 5 | 50 - 80 | -30 |
| 6 | 5 + 3 * 2 (L→R) | 16 |
| 7 | 9999999 * 9999999 | 99999980000001 |
| 8 | 42 / 0 | ERROR |
| 9 | 7 + 8 (after AC) | 15 |
| 10 | 10^15 - 1 | 999999999999999 |
```
