# Meta Collections (v3.4)

`pycircuit.meta` supports compile-time module collections that elaborate into fixed hardware instance graphs.

## Types

- `ModuleFamilySpec(name, module, params=None)`
- `ModuleListSpec`
- `ModuleVectorSpec`
- `ModuleMapSpec`
- `ModuleDictSpec`

v3.4 policy: collections are homogeneous (single module signature per collection).

## Builder shape

```python
family = meta.module_family("lane", module=build_lane, params={"width": 32})
lanes  = family.list(8)
vec    = family.vector(8)
mset   = family.map(["alu", "bru", "lsu"])
dct    = family.dict({"alu": {"gain": 1}, "bru": {"gain": 2}})
```

## Elaboration APIs

Use `Circuit.array(...)` directly.

All return `ModuleCollectionHandle` with:
- `instances[key] -> ModuleInstanceHandle`
- `outputs[key] -> Connector | ConnectorBundle | ConnectorStruct`

## Determinism

- Collection key ordering is canonicalized.
- Per-instance names are deterministic:
  - list/vector: `{base}_{index}`
  - map/dict: `{base}_{sanitized_key}`

## Strict binding policy

Collection binding is strict exact-match:
- missing keys: error
- extra keys: error
- width/signed mismatch: error
- cross-owner connector mismatch: error

No relaxed/default binding mode in v3.4.

## Example

See `/Users/zhoubot/pyCircuit/designs/examples/template_module_collection_demo.py` and `/Users/zhoubot/pyCircuit/designs/examples/template_array_demo.py`.
