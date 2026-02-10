# Digital Real-Time Clock (pyCircuit)

A digital clock with hours, minutes, seconds display in BCD format,
implemented using the pyCircuit unified signal model (`domain.signal()` /
`.set()` / `domain.next()`).

## Features

- **24-hour time** (00:00:00 – 23:59:59)
- **BCD output** for 7-segment LED displays (8 bits per digit pair: `{tens[7:4], ones[3:0]}`)
- **Three buttons** with hardware debounce:
  - `btn_set`   — cycle modes: RUN → SET\_HOUR → SET\_MIN → SET\_SEC → RUN
  - `btn_plus`  — increment the field being set (wraps around)
  - `btn_minus` — decrement the field being set (wraps around)
- **Colon blink** output toggling at 1 Hz (for display `:` blinking)

## File structure

| File | Description |
|------|-------------|
| `digital_clock.py` | Main design — prescaler, clock logic, mode FSM |
| `debounce.py` | Counter-based button debouncer with rising-edge pulse output |
| `bcd.py` | Binary-to-BCD conversion (0–59, 0–23) using priority MUX chains |

## Architecture

Single-cycle design (no pipeline):

```
cycle 0:  read flop Q → debounce → prescaler → clock arithmetic → BCD
          domain.next()
cycle 1:  .set() D-inputs for all flops
```

### Debounce

Each button debouncer uses a counter that resets when the raw input
changes, and updates the stable output only when the counter saturates
(input has been stable for `DEBOUNCE_CYCLES` consecutive cycles).
A rising-edge detector on the stable signal produces a single-cycle pulse.

### BCD conversion

Binary values (0–59 for seconds/minutes, 0–23 for hours) are converted
to BCD via a priority MUX chain for the tens digit, followed by
arithmetic subtraction `ones = value − tens × 10`.

## Ports

| Port | Dir | Width | Description |
|------|-----|-------|-------------|
| `clk` | in | 1 | System clock |
| `rst` | in | 1 | Synchronous reset |
| `btn_set` | in | 1 | Set button (active high) |
| `btn_plus` | in | 1 | Plus button (active high) |
| `btn_minus` | in | 1 | Minus button (active high) |
| `hours_bcd` | out | 8 | Hours in BCD `{tens, ones}` |
| `minutes_bcd` | out | 8 | Minutes in BCD `{tens, ones}` |
| `seconds_bcd` | out | 8 | Seconds in BCD `{tens, ones}` |
| `setting_mode` | out | 2 | 0=RUN, 1=SET\_H, 2=SET\_M, 3=SET\_S |
| `colon_blink` | out | 1 | Toggles at 1 Hz |

## JIT parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `CLK_FREQ` | 50 000 000 | System clock frequency (Hz) |
| `DEBOUNCE_MS` | 20 | Debounce window (milliseconds) |

## Compile

```bash
# Emit MLIR
python -m examples.digital_clock.digital_clock

# Full flow: MLIR → Verilog + C++
python -m pycircuit.cli emit examples/digital_clock --name digital_clock

# Generate schematic
python tools/schematic_view.py examples/generated/digital_clock/digital_clock.v --stats
```
