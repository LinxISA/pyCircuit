# Meta Structures (v3.4)

`pycircuit.meta` provides immutable compile-time structures consumed by `@const` and hardened during JIT elaboration.

## Core types

- `FieldSpec(name, width, signed=False)`
- `BundleSpec(name, fields)`
- `InterfaceSpec(name, bundles)`
- `StagePipeSpec(name, payload, has_valid=True, has_ready=False, ...)`
- `StructFieldSpec(name, width=..., signed=...)` or nested `StructFieldSpec(name, struct=...)`
- `StructSpec(name, fields)`
- `ParamSpec(name, default, min_value=None, max_value=None, choices=...)`
- `ParamSet(values, name=None)`
- `ParamSpace(variants)`
- `DecodeRule(name, mask, match, updates, priority=0)`

All types are immutable and canonicalizable via `__pyc_template_value__()`.

## Struct builder and transforms

Builder:
- `meta.struct("name").field("a.b", width=...).field("x", width=...).build()`

Transforms (immutable):
- `add_field(path, ...)`
- `remove_field(path)`
- `rename_field(path, new_name)`
- `select_fields(paths)`
- `drop_fields(paths)`
- `merge(other)`
- `with_prefix(prefix)`
- `with_suffix(suffix)`

Paths use dot notation (`payload.word`, `ctrl.valid`).

## Wiring helpers

Meta-level helpers:
- `meta.inputs(m, spec, prefix=...)`
- `meta.outputs(m, spec, values, prefix=...)`
- `meta.state(m, spec, clk=..., rst=..., init=..., en=..., prefix=...)`
- `meta.bind(spec, value)`
- `meta.ports(m, bind)`
- `meta.connect(m, dst, src, when=...)`

Circuit wrappers:
- `m.inputs(...)`, `m.outputs(...)`, `m.state(...)`, `m.pipe(...)`
- `m.new(...)`, `m.array(...)`

## DSE helpers

- `meta.dse.product({...})`
- `meta.dse.grid({...})`
- `meta.dse.filter(space, pred)`
- `meta.dse.named_variant(name, **values)`

All DSE helpers preserve deterministic ordering for stable artifact naming.
