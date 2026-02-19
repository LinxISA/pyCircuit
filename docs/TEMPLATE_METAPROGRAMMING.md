# Template Metaprogramming (v3.2)

`@template` is the explicit compile-time metaprogramming primitive.

## Contract

- Templates execute during JIT elaboration.
- Template execution must emit **zero** IR operations.
- Template execution must not mutate module interface state.
- Violations raise `JitError` with mutation diagnostics.

## Allowed template returns

- `None`, `bool`, `int`, `str`, `LiteralValue`
- Containers (`list`, `tuple`, `dict`) of allowed values
- Immutable meta objects exposing `__pyc_template_value__()`

## Disallowed template returns

- `Wire`, `Reg`, `Signal`
- `Connector`, `ConnectorBundle`
- Mutable/opaque runtime objects without template canonicalization

## Purity checks

JIT snapshots and verifies at least:
- `_lines`, `_next_tmp`, `_args`, `_results`
- `_finalizers`
- scope/debug state
- function attributes/indent state

Any mutation is reverted and reported as an error.

## Memoization

Templates are memoized per compile invocation by:
- function identity
- canonicalized args/kwargs
- canonicalized meta values from `__pyc_template_value__()`

## Practical patterns

- Use templates to build immutable interface/pipe/param specs.
- Use templates to derive widths, masks, and unroll counts.
- Use chained compare expressions and subscripted meta objects naturally in template/JIT code (`lo <= x <= hi`, `cfg["sets"]`).
- Keep all hardware emission in `@module` / `@function` code.

See also:
- `/Users/zhoubot/pyCircuit/docs/META_STRUCTURES.md`
- `/Users/zhoubot/pyCircuit/designs/examples/template_interface_wiring_demo.py`
- `/Users/zhoubot/pyCircuit/designs/examples/template_pipeline_builder_demo.py`
