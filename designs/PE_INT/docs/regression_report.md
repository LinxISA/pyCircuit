# PE_INT Regression Report

Date: 2026-05-07

Git HEAD at run start: `c88b634`

Working tree at run time included the public API cleanup for PE_INT
(`sext`/`zext`/`select`), the project-level `codereviewer` model configuration,
and the PE_INT scope-boundary rule.

Scope:

- Model unittest
- PyCircuit build and generated RTL sync
- Compiled PyCircuit testbench
- RTL testcase regeneration
- RTL regression with `iverilog`
- RTL regression with `verilator -Wall`

Known deferred item:

- Reset release mismatch is treated as a user-accepted PyCircuit framework
  limitation for this run.
- Explicit radix-4 Booth structural multiplier implementation for S8-involved
  products remains deferred pending synthesis/timing/area evidence.

## Commands

Model unittest:

```bash
wsl bash -lc 'cd /mnt/d/git-repo/pycircuit/designs/PE_INT && PYTHONPATH=/mnt/d/git-repo/pycircuit/compiler/frontend:/mnt/d/git-repo/pycircuit/designs/PE_INT/python python3 model/test_pe_int.py'
```

PyCircuit build and generated RTL sync:

```bash
wsl bash -lc 'cd /mnt/d/git-repo/pycircuit/designs/PE_INT && PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:$HOME/.local/bin PYTHONPATH=/mnt/d/git-repo/pycircuit/compiler/frontend:/mnt/d/git-repo/pycircuit/designs/PE_INT/python python3 python/build.py --target both --out-dir build/pe_int --jobs 8 --pyc-tb-vectors 8'
```

Compiled PyCircuit testbench:

```bash
wsl bash -lc 'cd /mnt/d/git-repo/pycircuit/designs/PE_INT && ./build/pe_int/cpp_build/build/pyc_tb'
```

Full RTL regression:

```bash
wsl bash -lc 'cd /mnt/d/git-repo/pycircuit/designs/PE_INT && PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:$HOME/.local/bin bash sim/run_all_wsl.sh'
```

## Results

| Step | Result | Evidence |
|---|---|---|
| Model unittest | PASS | `build/regression_logs/model_unittest.log`: 5 tests, OK |
| PyCircuit build | PASS | `build/regression_logs/pycircuit_build.log`: `pycircuit.cli build --target both`, `Synced generated RTL into .../rtl/build` |
| PyCircuit TB | PASS | `build/regression_logs/pycircuit_tb.log`: `OK` |
| RTL vector generation | PASS | `build/regression_logs/rtl_regression.log`: `Regenerating testcase vectors from model/ ...` |
| RTL iverilog regression | PASS | `build/regression_logs/rtl_regression.log`: all 9 cases passed before Verilator stage |
| RTL Verilator regression | PASS | `build/regression_logs/rtl_regression.log`: all 9 cases passed; final line reports all cases passed on `iverilog + verilator` |

RTL cases:

- `tc_mode2a_sanity`
- `tc_mode2a_sanity_rand_timing`
- `tc_mode2b_sanity`
- `tc_mode2b_sanity_rand_timing`
- `tc_mode2c_sanity`
- `tc_mode2c_sanity_rand_timing`
- `tc_mode2d_sanity`
- `tc_mode2d_sanity_rand_timing`
- `tc_mode_switch_random`

Warning summary:

- Verilator was run with `-Wall` and the project warning flags in
  `sim/run_all_wsl.sh`.
- `-Wno-UNUSEDSIGNAL` is not used.
- No `UNUSEDSIGNAL` warning was observed in the full RTL regression log.

