# Template Metaprogramming (v3.4)

`@const` is pyCircuit's explicit compile-time metaprogramming primitive.

## Contract

- `@const` executes during JIT elaboration.
- It must emit zero IR operations.
- It must not mutate module interface/build state.
- Violations raise `JitError` with source-located diagnostics.

## Allowed returns

- `None`, `bool`, `int`, `str`, `LiteralValue`
- containers (`list`, `tuple`, `dict`) of allowed values
- immutable meta objects exposing `__pyc_template_value__()`
- `@meta.valueclass` objects

## Disallowed returns

- `Wire`, `Reg`, `Signal`
- `Connector`, `ConnectorBundle`, `ConnectorStruct`
- mutable/opaque runtime objects without canonical template representation

## Purity checks

JIT snapshots and validates at least:
- `_lines`, `_next_tmp`, `_args`, `_results`
- `_finalizers`
- scope/debug state
- function attributes/indent state

If mutation occurs, state is restored and compile fails.

## Memoization

`@const` calls are memoized per compile by:
- function identity
- canonicalized args/kwargs
- canonicalized meta values (`__pyc_template_value__()`)

## `valueclass`

Use `@meta.valueclass` to make Python classes template-canonical:

```python
@meta.valueclass
class Cfg:
    ways: int
    sets: int
```

`valueclass` instances are valid as:
- template args
- template returns
- deterministic template cache key components

## Practical patterns

- Build immutable struct/module-collection specs in `@const`.
- Derive widths/masks/loop factors in `@const`.
- Keep hardware emission in `@module` / `@function` only.
- Use `array(...)` + module collection specs to elaborate fixed instance graphs.

See:
- `/Users/zhoubot/pyCircuit/docs/META_STRUCTURES.md`
- `/Users/zhoubot/pyCircuit/docs/META_COLLECTIONS.md`
- `/Users/zhoubot/pyCircuit/designs/examples/template_struct_transform_demo.py`
- `/Users/zhoubot/pyCircuit/designs/examples/template_module_collection_demo.py`
