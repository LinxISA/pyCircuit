# PE_INT

Fixed-point / integer vector MAC unit.

Implementation flow:

`docs/spec.md` -> `python/pe_int_pycircuit.py` -> `pycircuit.cli build` -> `rtl/` + `tb/`

## Baseline

- Spec source: `docs/spec.md`
- Current baseline: `v2.0.5` (see the header in `docs/spec.md`)

## Directory Overview

- `docs/`: formal specifications
- `python/`: PyCircuit design sources and build scripts
- `model/`: golden models and model regressions
- `tb/`: PyCircuit testbench flow (not RTL simulator flow)
- `tb_rtl/`: dedicated RTL test environment (Verilog testbench + cases)
- `sim/`: one-command simulation entrypoints (iverilog / verilator + wave options)
- `rtl/`: generated RTL deliverables only

## Key Files

- `model/ref_model.py`: reference math and pack/unpack model for all four modes
- `model/pe_int_pycircuit_eval.py`: cycle-accurate pipeline golden model (`L=3`)
- `model/test_pe_int.py`: model-level random regressions
- `model/gen_rtl_case_vectors.py`: regenerates expected vectors for `tb_rtl/case`
- `python/pe_int_pycircuit.py`: PyCircuit frontend top entry (RTL source of truth)
- `python/build.py`: unified build entry (`python -m pycircuit.cli build`)
- `tb/tb_pe_int_pycircuit.py`: native PyCircuit testbench for `pycc`/sim flow
- `tb_rtl/case/*.v`: RTL testcases (single-mode sanity + mode-switch random)
- `sim/run_all_wsl.sh`: one-command RTL regression (`iverilog` + `verilator`)
- `sim/run_sim.sh`: interactive one-command simulation (seed + simulator + wave)
- `filelist/pe_int.f`: RTL filelist (prefixed with absolute `$PE_INT`)
- `tb_rtl/tb.f`: testbench/case filelist (prefixed with absolute `$PE_INT`)
- `model/model.f`: model filelist (prefixed with absolute `$PE_INT`)

## Quick Start

1) Run golden model tests:

```bash
python model/test_pe_int.py
```

2) Build RTL/simulation artifacts (requires a ready PyCircuit environment):

```bash
python python/build.py --target both --out-dir build/pe_int
```

3) Run dedicated RTL regressions (WSL):

```bash
bash sim/run_all_wsl.sh
```

Or use interactive one-command simulation (WSL):

```bash
bash sim/run_sim.sh
```

## Process Constraints

- Do not handwrite `rtl/*.v`.
- `vld_out` must align with `out0`/`out1`; under mode `2a`, `out1` uses hold policy to avoid unnecessary toggles.
- On a new machine without existing profiles, bootstrap the environment from `LinxISA/pyCircuit` first.
