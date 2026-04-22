# RTL Testcases (`case`)

Each `.v` file in this directory is an independent testcase and can be compiled/simulated alone.

## Case List and Purpose

- `tc_mode2a_sanity.v`
  - Verifies mode 2a (`S8xS8`) math correctness.
  - Verifies `out1` is not unnecessarily updated in 2a mode (holds previous dual-path value).

- `tc_mode2b_sanity.v`
  - Verifies mode 2b (`S8xS4`) dual outputs `out0/out1`.

- `tc_mode2c_sanity.v`
  - Verifies mode 2c (`S5xS5 + E1`) dual outputs.

- `tc_mode2d_sanity.v`
  - Verifies mode 2d (`S8xS5`) dual outputs.

- `tc_mode_switch_random.v`
  - Verifies `vld -> vld_out` alignment/order under back-to-back mode switching.
  - Verifies output values under mixed traffic (including `vld=0` gaps).
  - Expected data must be traceable to reference models in `model/`.

## Naming Convention

- `tc_*`: directly usable as simulator top module names.
- Each case includes PASS/FAIL messages and `$fatal` for CI-friendly checks.

## Automatic Expected Generation

- Generator: `gen_rtl_case_vectors.py` resolved from `model/model.f`
- Output: `tb_rtl/case/generated/*.vh`
- Testcases use these generated files via `` `include ``; do not handwrite expected values.

Manual regenerate (resolved through filelist):

```bash
export PE_INT="$(pwd)"
sed "s|\$PE_INT|${PE_INT}|g" model/model.f > build/.model.resolved.f
python "$(awk '/gen_rtl_case_vectors\.py$/ {print; exit}' build/.model.resolved.f)"
```
