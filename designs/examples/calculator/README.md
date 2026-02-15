# Calculator Example

16-digit calculator implemented with the cycle-aware frontend + C++ simulation wrapper.

## Files

- `calculator.py`: pyCircuit design
- `calculator_capi.cpp`: C API wrapper around generated C++ model
- `emulate_calculator.py`: terminal emulator using `ctypes`

## Build + run

Emit + compile model:

```bash
PYTHONPATH=compiler/frontend python3 -m pycircuit.cli emit \
  designs/examples/calculator/calculator.py \
  -o .pycircuit_out/examples/calculator/calculator.pyc

build/bin/pyc-compile \
  .pycircuit_out/examples/calculator/calculator.pyc \
  --emit=cpp \
  -o .pycircuit_out/examples/calculator/calculator_gen.hpp
```

Build shared lib:

```bash
cd designs/examples/calculator
c++ -std=c++17 -O2 -shared -fPIC \
  -I../../runtime -I../../../.pycircuit_out/examples/calculator \
  -o libcalculator_sim.dylib calculator_capi.cpp
```

Run emulator:

```bash
cd /Users/zhoubot/pyCircuit
python3 designs/examples/calculator/emulate_calculator.py
```
