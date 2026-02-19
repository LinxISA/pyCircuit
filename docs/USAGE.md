# pyCircuit v3.2 Usage

## 1) Required contracts

- Top entrypoint must be `@module`.
- Helper logic must be explicitly tagged as `@function` or `@template`.
- Inter-module links must use connectors.

## 2) Decorators

- `@module`: hierarchy-preserving boundary
- `@function`: inline hardware helper
- `@template`: compile-time pure helper; may not emit IR or mutate module interface state

## 3) Connector APIs

Core connector methods on `Circuit`:
- `input_connector`, `output_connector`, `reg_connector`, `bundle_connector`, `as_connector`, `connect`

Instance APIs:
- `instance(...)`
- `instance_handle(...)`
- `instance_bind(...)` (v3.2 grammar-candy)

## 4) Compile-time helpers

### `ct`

Arithmetic helpers include:
- `clog2`, `flog2`, `div_ceil`, `align_up`, `pow2_ceil`, `bitmask`
- `is_pow2`, `pow2_floor`, `gcd`, `lcm`, `clamp`, `wrap_inc`, `wrap_dec`, `slice_width`, `bits_for_enum`, `onehot`, `decode_mask`

### `meta`

`pycircuit.meta` includes immutable template data structures and builders:
- `FieldSpec`, `BundleSpec`, `InterfaceSpec`, `StagePipeSpec`
- `ParamSpec`, `ParamSet`, `ParamSpace`, `DecodeRule`
- Builders: `bundle(...)`, `iface(...)`, `stage_pipe(...)`, `params(...)`, `ruleset(...)`
- Wiring helpers: `declare_inputs`, `declare_outputs`, `declare_state_regs`, `bind`, `bind_instance_ports`, `connect_like`
- DSE helpers: `meta.dse.grid`, `meta.dse.product`, `meta.dse.filter`, `meta.dse.named_variant`

## 5) v3.2 grammar-candy methods

- `m.io_in(spec, prefix=...)`
- `m.io_out(spec, values, prefix=...)`
- `m.state_regs(spec, clk=..., rst=..., prefix=..., init=..., en=...)`
- `m.pipe_regs(stage_spec, in_bundle, clk=..., rst=..., en=..., flush=...)`
- `m.instance_bind(fn, name=..., spec_bindings=..., params=...)`
  - Use `meta.bind(spec, value)` in `spec_bindings` for strict key/width/signed validation.

## 6) Minimal example

```python
from pycircuit import Circuit, compile_design, meta, module, template

@template
def lane_spec(m: Circuit, width: int):
    _ = m
    return meta.bundle("lane").field("data", width=width).field("valid", width=1).build()

@module
def build(m: Circuit, width: int = 32):
    spec = lane_spec(m, width)
    inp = m.io_in(spec, prefix="in_")
    m.io_out(spec, {"data": inp["data"], "valid": inp["valid"]}, prefix="out_")

print(compile_design(build, name="demo").emit_mlir())
```

## 7) Fresh-start policy

v3+ is a hard break. Removed APIs (old inline helper decorator and public `compile`) are not supported.
Use `/Users/zhoubot/pyCircuit/flows/tools/check_api_hygiene.py` to enforce source/docs hygiene.
