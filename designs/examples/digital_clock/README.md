# Digital Clock Example

24-hour clock with BCD outputs and debounced set/plus/minus controls.

## Files

- `digital_clock.py`: top-level design
- `debounce.py`: button debouncer
- `bcd.py`: binary-to-BCD helpers
- `digital_clock_capi.cpp` + `emulate_digital_clock.py`: C++ sim wrapper + terminal emulator

## Build + run

Emit + compile model:

```bash
PYTHONPATH=compiler/frontend python3 -m pycircuit.cli emit \
  designs/examples/digital_clock/digital_clock.py \
  -o .pycircuit_out/examples/digital_clock/digital_clock.pyc

build/bin/pyc-compile \
  .pycircuit_out/examples/digital_clock/digital_clock.pyc \
  --emit=cpp \
  -o .pycircuit_out/examples/digital_clock/digital_clock_gen.hpp
```

Build shared lib:

```bash
cd designs/examples/digital_clock
c++ -std=c++17 -O2 -shared -fPIC \
  -I../../runtime -I../../../.pycircuit_out/examples/digital_clock \
  -o libdigital_clock_sim.dylib digital_clock_capi.cpp
```

Run emulator:

```bash
cd /Users/zhoubot/pyCircuit
python3 designs/examples/digital_clock/emulate_digital_clock.py
```
