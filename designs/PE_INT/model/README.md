# model (Models and Model Tests)

This directory centralizes all model assets used for verification and comparison.

## Contents

- `ref_model.py`: reference math and pack/unpack model for all four modes
- `pe_int_pycircuit_eval.py`: cycle-accurate pipeline model (`L=3`)
- `test_pe_int.py`: model-layer regression tests (without direct RTL simulator invocation)
- `gen_rtl_case_vectors.py`: generates testcase vectors and expected values for `tb_rtl/case`
- `model.f`: model filelist (prefixed with absolute `$PE_INT`)

## Principles

- Any verification needing golden/model comparison must use this directory as source of truth.
- If `tb/` or `tb_rtl/` uses expected values, they must be traceable back to `model/`.

## Run

```bash
python model/test_pe_int.py
```
