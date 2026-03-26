# API Reference (pyc4.0)

The pyc4.0 frontend is centered around `Circuit` authoring (`@module`) and the
testbench DSL (`@testbench`).

## Recommended imports

```python
from pycircuit import Circuit, Tb, compile, module, function, const, testbench
from pycircuit import ct, spec, wiring, logic, lib
```

## Key decorators

- `@module`: hierarchy boundary (materializes instances; maps 1:1 to SimObjects)
- `@function`: inline helper (inlined into the caller)
- `@const`: compile-time helper (pure; canonicalizable)
- `@testbench`: host-side test program lowered via a `.pyc` payload

## Core docs

- Frontend API: `docs/FRONTEND_API.md`
- Testbench: `docs/TESTBENCH.md`
- IR: `docs/IR_SPEC.md`
- Primitives: `docs/PRIMITIVES.md`
- Diagnostics: `docs/DIAGNOSTICS.md`

