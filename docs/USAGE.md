# pyCircuit v3.4 Usage

## 1) Required contracts

- Top entrypoint must be `@module`.
- Helper logic must be `@function` or `@const`.
- Inter-module links must use connectors.
- Removed APIs fail at source/JIT compile stage.

## 2) Decorators

- `@module`: hierarchy-preserving boundary
- `@function`: inline hardware helper
- `@const`: compile-time pure helper; may not emit IR or mutate module state

## 3) Circuit API

Core connectors:
- `input_connector`, `output_connector`, `reg_connector`, `bundle_connector`, `as_connector`, `connect`

Spec-driven grammar:
- `inputs(spec, prefix=...)`
- `outputs(spec, values, prefix=...)`
- `state(spec, clk=..., rst=..., prefix=..., init=..., en=...)`
- `pipe(spec, src_values, clk=..., rst=..., en=..., flush=..., prefix=..., init=...)`

Instantiation:
- `new(fn, name=..., bind=..., params=..., module_name=...)`
- `array(fn_or_collection, name=..., bind=..., keys=..., per=..., params=..., module_name=...)`

## 4) Top-level compile API

Use:
- `from pycircuit import compile`
- `compile(build, name="Top", **jit_params)`

Removed:
- legacy compile entrypoint
- legacy template decorator alias

## 5) Compile-time helpers

### `ct`

Arithmetic helpers include:
- `clog2`, `flog2`, `div_ceil`, `align_up`, `pow2_ceil`, `bitmask`
- `is_pow2`, `pow2_floor`, `gcd`, `lcm`, `clamp`, `wrap_inc`, `wrap_dec`, `slice_width`, `bits_for_enum`, `onehot`, `decode_mask`

### `meta`

Types:
- `FieldSpec`, `BundleSpec`, `InterfaceSpec`, `StagePipeSpec`
- `StructFieldSpec`, `StructSpec`
- `ParamSpec`, `ParamSet`, `ParamSpace`, `DecodeRule`
- `ModuleFamilySpec`, `ModuleListSpec`, `ModuleVectorSpec`, `ModuleMapSpec`, `ModuleDictSpec`

Builders:
- `bundle(...)`, `iface(...)`, `struct(...)`, `stage_pipe(...)`, `params(...)`, `ruleset(...)`, `module_family(...)`, `valueclass`

Connect helpers:
- `meta.bind(...)`, `meta.ports(...)`
- `meta.inputs(...)`, `meta.outputs(...)`, `meta.state(...)`
- `meta.connect(...)`

## 6) Minimal example

```python
from pycircuit import Circuit, compile, meta, module, const

@const
def lane_spec(m: Circuit, width: int):
    _ = m
    return meta.struct("lane").field("data", width=width).field("valid", width=1).build()

@module
def build(m: Circuit, width: int = 32):
    spec = lane_spec(m, width)
    inp = m.inputs(spec, prefix="in_")
    m.outputs(spec, {"data": inp["data"], "valid": inp["valid"]}, prefix="out_")

print(compile(build, name="demo").emit_mlir())
```

## 7) API hygiene

Run strict source contract checks:

```bash
python3 /Users/zhoubot/pyCircuit/flows/tools/check_api_hygiene.py
```

For external projects:

```bash
python3 /Users/zhoubot/pyCircuit/flows/tools/check_api_hygiene.py \
  --scan-root /Users/zhoubot/LinxCore src
```
