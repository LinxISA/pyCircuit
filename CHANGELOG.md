# Changelog

This project is still in an early prototype stage; APIs and generated outputs may change frequently.

## Unreleased

- Add `pyc.concat` lowering for readable `{a, b, c}` packed concatenations in generated Verilog and C++.
- Improve generated identifier readability and traceability (scope + file/line name mangling).
- C++ emitter: add default-on hierarchical instance input-change cache to skip redundant submodule `eval()` calls; add `PYC_DISABLE_INSTANCE_EVAL_CACHE` override for A/B checks.
