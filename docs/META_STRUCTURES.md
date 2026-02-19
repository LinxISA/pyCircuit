# Meta Structures (v3.2)

`pycircuit.meta` provides immutable compile-time structures used by `@template` code.

## Core types

- `FieldSpec(name, width, signed=False)`
- `BundleSpec(name, fields)`
- `InterfaceSpec(name, bundles)`
- `StagePipeSpec(name, payload, has_valid=True, has_ready=False, ...)`
- `ParamSpec(name, default, min_value=None, max_value=None, choices=...)`
- `ParamSet(values, name=None)`
- `ParamSpace(variants)`
- `DecodeRule(name, mask, match, updates, priority=0)`

All are immutable and export canonical template values via `__pyc_template_value__()`.

## Builders

- `meta.bundle("name").field(...).build()`
- `meta.iface("name").bundle(...).build()`
- `meta.stage_pipe(...)`
- `meta.params().add(...).build(...)`
- `meta.ruleset().rule(...).build()`

## Wiring helpers

- `meta.declare_inputs(m, spec, prefix=...)`
- `meta.declare_outputs(m, spec, values, prefix=...)`
- `meta.declare_state_regs(m, spec, clk=..., rst=..., ...)`
- `meta.bind(spec, value)` -> spec-checked binding token for `instance_bind`
- `meta.bind_instance_ports(m, spec_bindings)`
- `meta.connect_like(m, dst, src, when=...)`

`Circuit` wraps these as grammar-candy methods:
- `m.io_in`, `m.io_out`, `m.state_regs`, `m.pipe_regs`, `m.instance_bind`

## DSE helpers

- `meta.dse.product({...})`
- `meta.dse.grid({...})`
- `meta.dse.filter(space, pred)`
- `meta.dse.named_variant(name, **values)`

All DSE helpers keep deterministic ordering for stable artifact naming.
