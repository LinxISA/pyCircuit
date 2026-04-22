# sim (One-Command Simulation)

This directory provides an interactive one-command script:

- `run_sim.sh`

## Features

Before execution, the script prompts for options (5-second timeout per prompt, then default is used):

1. testcase vector seed (default: current timestamp)
2. simulator: `iverilog` / `verilator` (default: `iverilog`)
3. generate waveform or not (default: `No`)
4. waveform format: `vcd` / `fst` (default: `vcd`)

The script first resolves and runs the model vector generator from `model/model.f`:

```bash
python3 <resolved from model/model.f> --seed <seed>
```

Then it runs all testcases in `tb_rtl/case` via filelists (`filelist/pe_int.f` + `tb_rtl/tb.f`).

## Reproducibility

- The same seed always generates identical `tc_mode_switch_random` vectors and expected values.
- The same seed also generates identical sanity vectors (`tc_mode2a/2b/2c/2d_sanity`); different seeds produce different vectors.
- Both `iverilog` and `verilator` use the same seed generation flow, so seed behavior is consistent across simulators.
- Each testcase has an independent log, and the seed is recorded at the beginning for replay.
- Log path:
  - `sim/logs/<simulator>/<timestamp>_run<idx>/seed_<seed>_<case>.log`

## Run

```bash
bash sim/run_sim.sh
```

## Waveform Output

- When waveform output is enabled, files are written to:
  - `sim/waves/<simulator>/<case>/wave.<fmt>`

## Simulator Differences (Handled by Script)

- `iverilog`: runs through `iverilog + vvp`; waveform via `$dumpfile/$dumpvars` (commonly VCD).
- `verilator`: automatically adds:
  - `--trace`（VCD）
  - `--trace-fst`（FST）
