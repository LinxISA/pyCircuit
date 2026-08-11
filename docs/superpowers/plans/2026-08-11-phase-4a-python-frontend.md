# Phase 4A Python Frontend and ACPy-to-ACIR Implementation Plan

**Status:** Complete — verified by
[`phase-4-audit.md`](../../implementation/phase-4-audit.md).

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement every specified Python construction API and deterministically lower the supported Python subset through verified ACPy into canonical ACIR, including a buildable process path.

**Architecture:** A dependency-free Python package captures trusted project definitions against their AST, validates a closed subset, evaluates static expressions with a closed interpreter, and builds immutable schema-shaped ACPy. Typed passes normalize SSA, resolve schema-driven calls, outline strong scopes, construct processes, and emit canonical ACIR; the native parser/verifier is the acceptance boundary, and the ACSim/code-generation path is extended to preserve the full frontend-emitted process CFG.

**Tech Stack:** Python 3.11+ standard library, immutable dataclasses, `ast`, `tomllib`, RFC 8785/I-JSON, JSON Schema fixtures, C++20, LLVM/MLIR 22.1.8, ACIR, ACSim, GoogleTest, LLVM lit/FileCheck, Python `unittest`, CMake/CTest.

## Global Constraints

- Global contract epoch is exactly `0.1`; ACPy identity is exactly `acpy@0.1`, and ACIR identity is exactly `acir@0.1`.
- Public Python requires Python `>=3.11` and has no runtime package dependencies.
- The exact public names are `system`, `module`, `extern_module`, `generated_module`, `struct`, `packet`, `transaction`, `protocol`, `interface`, `process`, `scope`, schema-generated component callables, `array`, `instances`, `view`, `queue`, `ResourceRef`, `address_space`, and `address_map`.
- AST capture is authoritative. Executed operator-overload traces alone are never accepted as source semantics.
- Static evaluation never calls Python `eval`, executes arbitrary callables, invokes user conversion hooks, or coerces symbolic values.
- ACPy has exactly the 16 entity kinds declared by `schemas/acpy.schema.json`; no private kind or legacy alias is accepted.
- Ordinary Python assignments and calls lower to Graph-region SSA and bindings; no public `ins`, `outs`, `connect`, or emitted `ac.connect` surface is added.
- Strong scopes outline real nested modules with minimal capture/escape signatures.
- Every collection shape, specialization value, instance name, loop bound, and static branch is resolved before topology construction.
- Every frontend-emitted process form must survive process-state planning, canonical ACSim, model-plan extraction, and typed enum-PC C++ generation.
- Generated process code contains no coroutine, descriptor interpreter, `std::function` frame, or Python dependency.
- Canonical artifacts contain no absolute workspace path, timestamp, random value, process ID, memory address, or hash-randomized ordering.
- Each task follows red-green-refactor, records the observed failing test, runs focused and broader affected suites, and ends in one reviewable commit.
- Preserve the unrelated untracked `phase-1-pr-description.md`; never stage or modify it.

---

## File and Responsibility Map

| File | Responsibility |
| --- | --- |
| `src/agentic_circuit/__init__.py` | Exact documented Python exports only. |
| `src/agentic_circuit/_types.py` | Static/symbolic annotations, roles, values, and prohibited coercions. |
| `src/agentic_circuit/_definitions.py` | Immutable decorator metadata and public decorator factories. |
| `src/agentic_circuit/_diagnostics.py` | Schema-shaped diagnostics, source spans, ordering, and bags. |
| `src/agentic_circuit/_source.py` | Workspace-relative loading, hashing, AST indexing, and definition lookup. |
| `src/agentic_circuit/_validate.py` | Context-sensitive supported-Python validation. |
| `src/agentic_circuit/_static_eval.py` | Closed static-expression evaluator. |
| `src/agentic_circuit/_canonical_json.py` | RFC 8785/I-JSON serialization and SHA-256 spelling. |
| `src/agentic_circuit/_acpy.py` | Frozen ACPy records, verification, allocation, and serialization. |
| `src/agentic_circuit/_schemas.py` | Component/protocol records and schema-generated callables. |
| `src/agentic_circuit/_normalize.py` | Assignment/ANF/SSA normalization and explicit results/uses. |
| `src/agentic_circuit/_resolve.py` | Call, port, result, protocol, role, and type inference. |
| `src/agentic_circuit/_naming.py` | Stable explicit/assignment/derived naming. |
| `src/agentic_circuit/_collections.py` | Static expansion and `array`/`instances`/`view` decisions. |
| `src/agentic_circuit/_scopes.py` | Strong-scope outlining, capture, and escape analysis. |
| `src/agentic_circuit/_resources.py` | Queue/resource/address semantic records and checks. |
| `src/agentic_circuit/_process.py` | Supported process CFG, effects, suspension, and lowering records. |
| `src/agentic_circuit/_lower_acir.py` | Verified-ACPy to deterministic textual ACIR. |
| `src/agentic_circuit/_frontend.py` | Library-level capture/check/elaborate composition. |
| `tests/python_frontend/` | Pure Python API, semantic, lowering, negative, and determinism tests. |
| `include/acir/CodeGen/ModelPlan.h` | CFG-preserving process block and branch plan types. |
| `lib/CodeGen/ModelPlanDetails.cpp` | Lossless canonical ACSim process CFG extraction. |
| `lib/CodeGen/ProcessGenerator.cpp` | Typed C++ local process control-flow emission. |
| `lib/Conversion/ACIRToACSim/ACIRToACSim.cpp` | Canonical process action and CFG preservation. |
| `docs/implementation/phase-4a-audit.md` | Frontend/lowering evidence and exit audit. |

---

### Task 1: Python package and exact public value categories

**Files:**
- Modify: `pyproject.toml`
- Create: `src/agentic_circuit/__init__.py`
- Create: `src/agentic_circuit/_types.py`
- Create: `src/agentic_circuit/_definitions.py`
- Create: `tests/python_frontend/__init__.py`
- Create: `tests/python_frontend/test_public_api.py`

**Interfaces:**
- Consumes: Python 3.11 typing and dataclasses only.
- Produces: `Static[T]`, `Flow[T, P]`, `Endpoint[I, R]`, `ResourceRef[T]`, `SymbolicValue`, `Definition`, and the exact public exports.

- [x] **Step 1: Write the failing public inventory and coercion tests**

```python
PUBLIC = {
    "system", "module", "extern_module", "generated_module", "struct",
    "packet", "transaction", "protocol", "interface", "process", "scope",
    "array", "instances", "view", "queue", "ResourceRef",
    "address_space", "address_map", "Static", "Flow", "Endpoint",
}

class PublicApiTest(unittest.TestCase):
    def test_exact_public_inventory_is_importable(self) -> None:
        api = importlib.import_module("agentic_circuit")
        self.assertEqual(PUBLIC, set(api.__all__))

    def test_symbolic_values_reject_python_coercion(self) -> None:
        value = _test_symbolic("request", Flow[int, ReadyValid])
        for operation in (bool, int, hash, iter):
            with self.assertRaisesRegex(TypeError, "ACPY-STATIC-002"):
                operation(value)
```

- [x] **Step 2: Run the focused test and confirm the RED state**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_public_api -v`

Expected: FAIL because `agentic_circuit` does not exist.

- [x] **Step 3: Implement closed annotation and symbolic types**

```python
class Static(Generic[T]): pass
class Flow(Generic[T, P]): pass
class Endpoint(Generic[I, R]): pass
class ResourceRef(Generic[T]): pass

@dataclass(frozen=True, slots=True)
class SymbolicValue:
    stable_name: str
    annotation: object

    def _reject(self, operation: str) -> Never:
        raise TypeError(f"ACPY-STATIC-002: {self.stable_name!r} cannot be used for {operation}")

    def __bool__(self) -> Never: return self._reject("truthiness")
    def __int__(self) -> Never: return self._reject("integer conversion")
    def __hash__(self) -> Never: return self._reject("hashing")
    def __iter__(self) -> Never: return self._reject("iteration")
```

- [x] **Step 4: Implement immutable decorator metadata**

```python
DefinitionKind = Literal[
    "system", "module", "extern_module", "generated_module", "struct",
    "packet", "transaction", "protocol", "interface", "process",
]

@dataclass(frozen=True, slots=True)
class Definition:
    kind: DefinitionKind
    function: Callable[..., object]
    qualified_name: str
    explicit_options: tuple[tuple[str, object], ...]

def _decorate(kind: DefinitionKind, function: F | None = None, **options: object):
    def apply(target: F) -> Definition:
        return Definition(kind, target, target.__qualname__, tuple(sorted(options.items())))
    return apply(function) if function is not None else apply
```

Every public decorator delegates to `_decorate`; `Definition` never executes
the body as a graph-builder DSL. Export only the documented names.

- [x] **Step 5: Run focused tests and repository metadata checks**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_public_api -v && .venv/bin/python scripts/check-contracts.py`

Expected: PASS; `requires-python` remains `>=3.11` and dependencies remain `[]`.

- [x] **Step 6: Commit the public contract**

```bash
git add pyproject.toml src/agentic_circuit tests/python_frontend
git commit -m "feat(python): add exact frontend public types"
```

---

### Task 2: Source indexing and schema-shaped diagnostics

**Files:**
- Create: `src/agentic_circuit/_diagnostics.py`
- Create: `src/agentic_circuit/_source.py`
- Create: `src/agentic_circuit/_canonical_json.py`
- Create: `tests/python_frontend/test_source_capture.py`
- Create: `tests/python_frontend/fixtures/source/basic.py`
- Test fixture: `schemas/diagnostic.schema.json`

**Interfaces:**
- Consumes: `Definition` from Task 1.
- Produces: `JsonValue`, `SourceSpan`, `RelatedLocation`, `FixIt`, `Diagnostic`, `DiagnosticBag`, `SourceUnit`, `DefinitionSite`, and `load_source_unit`.

- [x] **Step 1: Write failing path, span, and ordering tests**

```python
class SourceCaptureTest(unittest.TestCase):
    def test_identity_is_workspace_relative_and_hashed(self) -> None:
        unit = load_source_unit(WORKSPACE / "architecture.py", WORKSPACE)
        self.assertEqual("architecture.py", unit.path)
        self.assertRegex(unit.sha256, r"^sha256:[0-9a-f]{64}$")

    def test_diagnostics_sort_by_source_then_code(self) -> None:
        bag = DiagnosticBag()
        bag.add(error("ACPY-TYPE-002", "second", span(line=8)))
        bag.add(error("ACPY-TYPE-001", "first", span(line=3)))
        self.assertEqual(["ACPY-TYPE-001", "ACPY-TYPE-002"],
                         [item.code for item in bag.freeze()])
```

- [x] **Step 2: Run the focused test and confirm the RED state**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_source_capture -v`

Expected: FAIL because source and diagnostic records are absent.

- [x] **Step 3: Implement immutable diagnostics matching the public schema**

```python
JsonScalar: TypeAlias = None | bool | int | float | str
JsonValue: TypeAlias = JsonScalar | list["JsonValue"] | dict[str, "JsonValue"]

def sha256_bytes(data: bytes) -> str:
    return "sha256:" + hashlib.sha256(data).hexdigest()

@dataclass(frozen=True, order=True, slots=True)
class SourceSpan:
    file: str
    start_line: int
    start_column: int
    end_line: int
    end_column: int

@dataclass(frozen=True, slots=True)
class RelatedLocation:
    message: str
    source: SourceSpan | None
    object_path: str | None

@dataclass(frozen=True, slots=True)
class FixIt:
    message: str

@dataclass(frozen=True, slots=True)
class Diagnostic:
    stage: str
    code: str
    severity: Literal["error", "warning", "note"]
    message: str
    source: SourceSpan | None = None
    object_path: str | None = None
    expected: JsonValue = None
    actual: JsonValue = None
    related: tuple[RelatedLocation, ...] = ()
    fixits: tuple[FixIt, ...] = ()
    schema: str = "agentic-circuit-diagnostic"
    version: str = "0.1"
    contract_epoch: str = "0.1"

    def sort_key(self) -> tuple[object, ...]:
        source = self.source
        return (source is None, source.file if source else "",
                source.start_line if source else 0,
                source.start_column if source else 0,
                self.object_path or "", self.code, self.message)
```

`RelatedLocation` contains exact `message`, optional source, and optional
`object_path`; `FixIt` contains exactly one non-empty `message`. Public JSON
projects an internal span to its starting `file`, `line`, and `column`, and
emits every required closed schema field even when its value is null or empty.

- [x] **Step 4: Implement normalized source loading and AST indexing**

```python
@dataclass(frozen=True, slots=True)
class SourceUnit:
    path: str
    sha256: str
    text: str
    tree: ast.Module
    definitions: tuple[DefinitionSite, ...]

def load_source_unit(entry: Path, workspace: Path) -> SourceUnit:
    root = workspace.resolve(strict=True)
    source = entry.resolve(strict=True)
    relative = source.relative_to(root).as_posix()
    raw = source.read_bytes()
    text = raw.decode("utf-8")
    tree = ast.parse(text, filename=relative, type_comments=True)
    return SourceUnit(relative, sha256_bytes(raw), text, tree,
                      index_definitions(tree, relative))
```

Reject non-UTF-8 files, workspace escape, duplicate qualified definitions, and
unmatchable definition spans.

- [x] **Step 5: Run focused and schema tests**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_source_capture -v && .venv/bin/python -m unittest tests.contracts.test_contracts.RepositoryContractsTest.test_all_json_schemas_compile_as_draft_2020_12 -v`

Expected: PASS.

- [x] **Step 6: Commit source and diagnostics**

```bash
git add src/agentic_circuit tests/python_frontend
git commit -m "feat(frontend): index source and order diagnostics"
```

---

### Task 3: Supported-Python validation and closed static evaluation

**Files:**
- Create: `src/agentic_circuit/_validate.py`
- Create: `src/agentic_circuit/_static_eval.py`
- Create: `tests/python_frontend/test_static_eval.py`
- Create: `tests/python_frontend/fixtures/static/supported.py`
- Create: `tests/python_frontend/fixtures/static/unsupported.py`

**Interfaces:**
- Consumes: source/diagnostic records and static/symbolic types.
- Produces: `ValidationContext`, `validate_definition`, `StaticEnvironment`, `StaticValue`, and `evaluate_static`.

- [x] **Step 1: Write failing supported and forbidden tests**

```python
class StaticEvaluationTest(unittest.TestCase):
    def test_closed_expression_is_deterministic(self) -> None:
        expression = parse_expr("tuple(i * 2 for i in range(lanes))")
        self.assertEqual((0, 2, 4, 6),
                         evaluate_static(expression, StaticEnvironment({"lanes": 4})))

    def test_dynamic_truthiness_is_rejected_at_operand(self) -> None:
        item = one(validate_fixture("static/unsupported.py"), "ACPY-STATIC-002")
        self.assertEqual((8, 7), (item.source.start_line, item.source.start_column))
```

- [x] **Step 2: Run the focused test and confirm the RED state**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_static_eval -v`

Expected: FAIL because validation and evaluation are absent.

- [x] **Step 3: Implement context-sensitive validation**

```python
@dataclass(frozen=True, slots=True)
class ValidationContext:
    definition_kind: DefinitionKind
    static_names: frozenset[str]
    symbolic_names: frozenset[str]
    approved_helpers: frozenset[str]

def validate_definition(site: DefinitionSite, context: ValidationContext,
                        diagnostics: DiagnosticBag) -> None:
    validator = _Validator(site.span.file, context, diagnostics)
    for statement in site.node.body:
        validator.visit(statement)
```

Use explicit handlers for every admitted node. The fallback emits
`ACPY-SYNTAX-001`; reject async/yield/lambda, exception handling, mutable
global/nonlocal state, dynamic imports, reflection, arbitrary mutation,
unbounded loops, and unapproved calls.

- [x] **Step 4: Implement the closed evaluator**

```python
StaticScalar: TypeAlias = None | bool | int | float | str
StaticValue: TypeAlias = StaticScalar | tuple["StaticValue", ...] | FrozenMap

@dataclass(frozen=True, slots=True)
class StaticEnvironment:
    values: Mapping[str, StaticValue]
    helpers: Mapping[str, StaticHelper] = field(default_factory=dict)

def evaluate_static(node: ast.AST, environment: StaticEnvironment) -> StaticValue:
    value = _StaticEvaluator(environment).visit(node)
    validate_ijson_value(value)
    return value
```

Support only the specified literal/container/name/attribute/arithmetic/
comparison/subscript and bounded helper/comprehension forms. Check integer
ranges, finite floats, unique keys, positive loop steps, and maximum expansion.

- [x] **Step 5: Run focused and hash-seed tests**

Run: `for seed in 1 7 99; do PYTHONHASHSEED=$seed PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_static_eval -v || exit 1; done`

Expected: every run passes identically.

- [x] **Step 6: Commit validation and evaluation**

```bash
git add src/agentic_circuit tests/python_frontend
git commit -m "feat(frontend): validate and evaluate static Python"
```

---

### Task 4: Closed ACPy records and canonical JSON

**Files:**
- Modify: `src/agentic_circuit/_canonical_json.py`
- Create: `src/agentic_circuit/_acpy.py`
- Create: `tests/python_frontend/test_acpy.py`
- Create: `tests/python_frontend/fixtures/acpy/minimal.acpy.json`
- Modify: `tests/contracts/test_contracts.py`
- Test fixture: `schemas/acpy.schema.json`

**Interfaces:**
- Consumes: `SourceSpan`, `Diagnostic`, and `StaticValue`.
- Produces: `SourceFile`, `SchemaRef`, `Property`, `Entity`, `AcpyDocument`, `EntityAllocator`, `canonical_json_bytes`, and `sha256_bytes`.

- [x] **Step 1: Write failing schema and canonical-byte tests**

```python
class AcpyContractTest(unittest.TestCase):
    def test_minimal_document_matches_golden(self) -> None:
        self.assertEqual(fixture_bytes("acpy/minimal.acpy.json"),
                         minimal_document().canonical_bytes())

    def test_ids_are_dense_and_references_resolve(self) -> None:
        document = document_with_call_and_result()
        self.assertEqual([f"e{i}" for i in range(len(document.entities))],
                         [entity.id for entity in document.entities])
        self.assertEqual((), document.verify())
```

- [x] **Step 2: Run the focused test and confirm the RED state**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_acpy -v`

Expected: FAIL because ACPy is absent.

- [x] **Step 3: Implement the exact frozen model**

```python
EntityKind: TypeAlias = Literal[
    "system", "module", "scope", "arg", "call", "result", "get_result",
    "bind", "static_if", "static_for", "collection", "get_static",
    "return", "capture", "escape", "process",
]

@dataclass(frozen=True, slots=True)
class Entity:
    id: str
    kind: EntityKind
    source: SourceSpan | None
    parent: str | None
    scope: str
    type: str | None
    definition: str | None
    uses: tuple[str, ...]
    schema_ref: SchemaRef | None
    properties: tuple[Property, ...]

@dataclass(frozen=True, slots=True)
class AcpyDocument:
    entry: str
    sources: tuple[SourceFile, ...]
    entities: tuple[Entity, ...]
    schema: str = "agentic-circuit-acpy"
    version: str = "0.1"
    contract_epoch: str = "0.1"
```

`verify()` enforces constants, dense IDs, reference/source closure, unique
ordered uses, canonical property order, kind-specific properties, and I-JSON.

- [x] **Step 4: Implement canonical serialization and hashes**

```python
def canonical_json_bytes(value: JsonValue) -> bytes:
    validate_ijson_value(value)
    return _encode_value(value).encode("utf-8")

def sha256_bytes(data: bytes) -> str:
    return "sha256:" + hashlib.sha256(data).hexdigest()
```

Use RFC 8785 string escaping, UTF-16 key ordering, finite binary64 number
spelling, and no insignificant whitespace. Validate the golden against the
checked-in schema in repository contracts.

- [x] **Step 5: Run ACPy and repository contracts**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_acpy -v && .venv/bin/python -m unittest tests.contracts.test_contracts -v && .venv/bin/python scripts/check-contracts.py`

Expected: PASS.

- [x] **Step 6: Commit ACPy**

```bash
git add src/agentic_circuit tests/python_frontend tests/contracts
git commit -m "feat(acpy): add closed semantic document contract"
```

---

### Task 5: Definition capture and schema-generated callables

**Files:**
- Create: `src/agentic_circuit/_schemas.py`
- Create: `src/agentic_circuit/_frontend.py`
- Modify: `src/agentic_circuit/_definitions.py`
- Modify: `src/agentic_circuit/__init__.py`
- Create: `tests/python_frontend/test_definitions.py`
- Create: `tests/python_frontend/fixtures/definitions/basic.py`
- Test fixture: `schemas/component.schema.json`
- Test fixture: `schemas/stdlib.catalog.json`

**Interfaces:**
- Consumes: source index, decorators, static evaluator, diagnostics, and ACPy records.
- Produces: `ComponentSchema`, `SchemaRegistry`, `ComponentCallable`, `CaptureRequest`, `FrontendResult`, and `capture_definitions`.

- [x] **Step 1: Write failing definition and signature tests**

```python
class DefinitionCaptureTest(unittest.TestCase):
    def test_definition_matches_ast_and_system_selection(self) -> None:
        result = capture_fixture("definitions/basic.py", system="main")
        self.assertEqual((), result.diagnostics)
        self.assertEqual("main", result.selected_system.qualified_name)

    def test_callable_rejects_undocumented_keyword(self) -> None:
        queue = registry().callable("ac.std.Queue")
        with self.assertRaisesRegex(TypeError, "ACPY-CALL-003"):
            queue(_test_symbolic("input", object()), depth=4, surprise=True)
```

- [x] **Step 2: Run the focused test and confirm the RED state**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_definitions -v`

Expected: FAIL because schema callables and capture composition are absent.

- [x] **Step 3: Implement closed schemas and generated callables**

```python
@dataclass(frozen=True, slots=True)
class ComponentSchema:
    identity: str
    fingerprint: str
    ports: tuple[PortSchema, ...]
    results: tuple[ResultSchema, ...]
    parameters: tuple[ParameterSchema, ...]
    availability: Literal["available", "declared_unavailable"]

class ComponentCallable:
    def __init__(self, schema: ComponentSchema) -> None:
        self.schema = schema
        self.__signature__ = signature_for(schema)

    def __call__(self, *args: object, **kwargs: object) -> PendingCall:
        bound = self.__signature__.bind(*args, **kwargs)
        return PendingCall(self.schema, tuple(bound.arguments.items()))
```

Load only exact epoch `0.1` entries, verify fingerprints/closed fields, preserve
declared port/result order, and refuse callables for `declared_unavailable`.

- [x] **Step 4: Implement deterministic definition capture**

```python
@dataclass(frozen=True, slots=True)
class CaptureRequest:
    entry: Path
    workspace: Path
    system: str
    static_arguments: tuple[tuple[str, StaticValue], ...] = ()

@dataclass(frozen=True, slots=True)
class FrontendResult:
    document: AcpyDocument | None
    acir: str | None
    diagnostics: tuple[Diagnostic, ...]

def capture_definitions(request: CaptureRequest,
                        namespace: Mapping[str, object],
                        registry: SchemaRegistry) -> CapturedProgram:
    unit = load_source_unit(request.entry, request.workspace)
    definitions = match_registered_definitions(namespace, unit.definitions)
    selected = select_exact_system(definitions, request.system)
    return CapturedProgram(unit, definitions, selected, registry)
```

Match by normalized module, qualified name, kind, and source line. Reject
source-less/dynamically fabricated definitions, decorator/static metadata that
cannot be reproduced from the indexed AST, invalid annotations/defaults, and
ambiguous systems. Enforce static-only `@system` parameters and the exact
annotated input/static/result signature rules for modules, external/generated
modules, records, protocols, interfaces, and processes.

- [x] **Step 5: Run focused and public API tests**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_definitions tests.python_frontend.test_public_api -v`

Expected: PASS.

- [x] **Step 6: Commit definition and schema capture**

```bash
git add src/agentic_circuit tests/python_frontend
git commit -m "feat(frontend): capture definitions and schema calls"
```

---

### Task 6: SSA normalization, stable naming, and call inference

**Files:**
- Create: `src/agentic_circuit/_normalize.py`
- Create: `src/agentic_circuit/_resolve.py`
- Create: `src/agentic_circuit/_naming.py`
- Create: `tests/python_frontend/test_normalization.py`
- Create: `tests/python_frontend/fixtures/normalize/calls.py`
- Create: `tests/python_frontend/fixtures/normalize/ambiguous.py`

**Interfaces:**
- Consumes: `CapturedProgram`, component schemas, static values, ACPy allocation, and diagnostics.
- Produces: `NormalizedProgram`, `ValueVersion`, `ResolvedCall`, `PortBinding`, `ResultBinding`, and `StableNameAllocator`.

- [x] **Step 1: Write failing SSA, mapping, and naming tests**

```python
class NormalizationTest(unittest.TestCase):
    def test_calls_become_explicit_ssa_and_results(self) -> None:
        program = normalize_fixture("normalize/calls.py")
        self.assertEqual(("decoded#0", "accepted#0"), program.value_names())
        self.assertEqual(("input",), tuple(x.port for x in program.calls[0].inputs))
        self.assertEqual(("decoded", "accepted"),
                         tuple(x.result for x in program.calls[0].results))

    def test_ambiguity_reports_attempted_binding(self) -> None:
        item = one(normalize_fixture("normalize/ambiguous.py").diagnostics,
                   "ACPY-CALL-006")
        self.assertIn("attempted", item.related[0].message)
```

- [x] **Step 2: Run the focused test and confirm the RED state**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_normalization -v`

Expected: FAIL because normalization and inference are absent.

- [x] **Step 3: Implement normalized values and calls**

```python
@dataclass(frozen=True, slots=True)
class ValueVersion:
    source_name: str
    version: int
    category: Literal["static", "flow", "endpoint", "resource", "result"]
    type_key: str
    producer: str | None

@dataclass(frozen=True, slots=True)
class ResolvedCall:
    entity_key: str
    schema: ComponentSchema
    instance_name: str
    static_arguments: tuple[tuple[str, StaticValue], ...]
    inputs: tuple[PortBinding, ...]
    results: tuple[ResultBinding, ...]
    source: SourceSpan
```

Normalize statements to ANF, allocate monotonically increasing versions per
source name, materialize `result`/`get_result`, and record every use before
dense ACPy IDs are assigned. Materialize `static_if`, `static_for`, and
`get_static` entities before specialization, retain selected-branch or
unrolled-iteration evidence, then exclude rejected static paths from ACIR.

- [x] **Step 4: Implement exact inference and naming precedence**

```python
class StableNameAllocator:
    def allocate(self, schema_base: str, assignment: str | None,
                 explicit: str | None, source_key: tuple[int, int]) -> str:
        candidate = explicit or assignment or normalize_schema_base(schema_base)
        validate_instance_segment(candidate)
        return self._reserve_with_source_order_suffix(candidate, source_key)

def resolve_call(call: NormalizedCall, schema: ComponentSchema,
                 values: ValueTable) -> ResolvedCall:
    inputs = resolve_arguments_to_ports(call.arguments, schema.ports, values)
    results = resolve_results(call.targets, schema.results)
    verify_protocol_roles(inputs)
    return ResolvedCall(call.key, schema, call.instance_name,
                        call.static_arguments, inputs, results, call.source)
```

Resolve by exact declared order/names, cardinality, payload/interface identity,
protocol identity, and complementary role. Never choose by unordered scoring.

- [x] **Step 5: Run normalization and hash-seed tests**

Run: `for seed in 3 11 41; do PYTHONHASHSEED=$seed PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_normalization tests.python_frontend.test_acpy -v || exit 1; done`

Expected: identical names, mappings, and entity order.

- [x] **Step 6: Commit normalization and inference**

```bash
git add src/agentic_circuit tests/python_frontend
git commit -m "feat(frontend): normalize SSA and resolve component calls"
```

---

### Task 7: Static collections and strong-scope outlining

**Files:**
- Create: `src/agentic_circuit/_collections.py`
- Create: `src/agentic_circuit/_scopes.py`
- Modify: `src/agentic_circuit/_normalize.py`
- Create: `tests/python_frontend/test_scopes_collections.py`
- Create: `tests/python_frontend/fixtures/scopes/nested.py`
- Create: `tests/python_frontend/fixtures/scopes/invalid_escape.py`

**Interfaces:**
- Consumes: normalized program, static evaluation, naming, and ACPy allocation.
- Produces: `CollectionPlan`, `OutlinedScope`, `CaptureBinding`, `EscapeBinding`, `expand_collections`, and `outline_scopes`.

- [x] **Step 1: Write failing collection and scope tests**

```python
class ScopeCollectionTest(unittest.TestCase):
    def test_rectangular_homogeneous_collection_selects_array(self) -> None:
        document = elaborate_fixture("scopes/nested.py").document
        collection = one_kind(document, "collection")
        self.assertEqual("array", property_value(collection, "lowering"))
        self.assertEqual([2, 4], property_value(collection, "shape"))

    def test_scope_signature_is_minimal_and_ordered(self) -> None:
        scope = one_kind(elaborate_fixture("scopes/nested.py").document, "scope")
        self.assertEqual(["requests", "memory"], property_value(scope, "captures"))
        self.assertEqual(["stored"], property_value(scope, "escapes"))
```

- [x] **Step 2: Run the focused test and confirm the RED state**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_scopes_collections -v`

Expected: FAIL because collection and scope planning are absent.

- [x] **Step 3: Implement canonical collection expansion**

```python
@dataclass(frozen=True, slots=True)
class CollectionPlan:
    kind: Literal["array", "instances"]
    shape: tuple[int, ...]
    elements: tuple[str, ...]
    element_schema: str | None

def classify_collection(elements: Sequence[ResolvedCall]) -> CollectionPlan:
    shape = exact_rectangular_shape(elements)
    flat = tuple(flatten(elements))
    schemas = tuple(item.schema.identity for item in flat)
    homogeneous = len(set(schemas)) == 1
    return CollectionPlan("array" if shape and homogeneous else "instances",
                          shape or (len(flat),), tuple(x.entity_key for x in flat),
                          schemas[0] if homogeneous else None)
```

Expand only statically bounded loops/comprehensions, preserve index order,
reject ragged arrays, and record every `view` projection exactly.

- [x] **Step 4: Implement minimal capture and escape signatures**

```python
@dataclass(frozen=True, slots=True)
class OutlinedScope:
    key: str
    name: str
    captures: tuple[CaptureBinding, ...]
    escapes: tuple[EscapeBinding, ...]
    body: tuple[NormalizedOperation, ...]

def outline_scopes(definition: NormalizedDefinition) -> tuple[OutlinedScope, ...]:
    uses = build_ordered_use_index(definition)
    return tuple(OutlinedScope(region.key, region.name,
                               ordered_free_symbolic_values(region, uses),
                               ordered_values_used_after_region(region, uses),
                               region.operations)
                 for region in definition.strong_scopes_source_order())
```

Static captures become specialization inputs; symbolic values become exact
ports/results. Reject ownership escape, hidden fan-out, and invalid roles.

- [x] **Step 5: Run scope, collection, normalization, and ACPy tests**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_scopes_collections tests.python_frontend.test_normalization tests.python_frontend.test_acpy -v`

Expected: PASS.

- [x] **Step 6: Commit scopes and collections**

```bash
git add src/agentic_circuit tests/python_frontend
git commit -m "feat(frontend): outline scopes and canonicalize collections"
```

---

### Task 8: Protocol, queue, resource, and address semantics

**Files:**
- Create: `src/agentic_circuit/_resources.py`
- Modify: `src/agentic_circuit/_resolve.py`
- Modify: `src/agentic_circuit/_definitions.py`
- Modify: `src/agentic_circuit/__init__.py`
- Create: `tests/python_frontend/test_resources.py`
- Create: `tests/python_frontend/fixtures/resources/valid.py`
- Create: `tests/python_frontend/fixtures/resources/invalid.py`

**Interfaces:**
- Consumes: schema registry, normalized values, static evaluator, and diagnostics.
- Produces: `ProtocolContract`, `QueueSpec`, `AddressSpaceSpec`, `AddressMapEntry`, and public queue/address helpers.

- [x] **Step 1: Write failing protocol/resource tests**

```python
class ResourceFrontendTest(unittest.TestCase):
    def test_queue_and_address_map_are_static_records(self) -> None:
        result = elaborate_fixture("resources/valid.py")
        self.assertEqual((), result.diagnostics)
        self.assertEqual("ready_valid", result.program.queues[0].protocol)
        self.assertEqual((0x1000, 0x2000), result.program.address_map[0].range)

    def test_invalid_roles_overlap_and_dynamic_address_are_rejected(self) -> None:
        codes = codes_for_fixture("resources/invalid.py")
        self.assertGreaterEqual(codes, {
            "ACPY-PROTOCOL-004", "ACPY-ADDRESS-003", "ACPY-STATIC-002",
        })
```

- [x] **Step 2: Run the focused test and confirm the RED state**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_resources -v`

Expected: FAIL because resource semantics are absent.

- [x] **Step 3: Implement protocol and queue checks**

```python
@dataclass(frozen=True, slots=True)
class ProtocolContract:
    identity: str
    producer_role: str
    consumer_role: str
    payload_type: str
    time_domain: str

@dataclass(frozen=True, slots=True)
class QueueSpec:
    name: str
    payload_type: str
    protocol: str
    depth: int
    time_domain: str
```

Verify complementary roles, exact payload/protocol identities, cardinality,
time-domain compatibility, and positive static queue depth.

- [x] **Step 4: Implement closed address maps**

```python
@dataclass(frozen=True, slots=True)
class AddressMapEntry:
    start: int
    end: int
    target: ResourceRef[object]
    priority: int

def verify_address_map(entries: Sequence[AddressMapEntry]) -> tuple[AddressMapEntry, ...]:
    ordered = tuple(sorted(entries, key=lambda x: (x.start, x.end, x.priority,
                                                    x.target.stable_name)))
    for left, right in pairwise(ordered):
        if ranges_overlap(left, right) and left.priority == right.priority:
            raise FrontendRuleError("ACPY-ADDRESS-003", "ambiguous address overlap")
    return ordered
```

Require finite non-negative static ranges, declared spaces, exact resource
kinds, and deterministic selector priority.

- [x] **Step 5: Run resource and inference tests**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_resources tests.python_frontend.test_normalization -v`

Expected: PASS.

- [x] **Step 6: Commit resource semantics**

```bash
git add src/agentic_circuit tests/python_frontend
git commit -m "feat(frontend): add protocol resource and address semantics"
```

---

### Task 9: Supported `@process` construction

**Files:**
- Create: `src/agentic_circuit/_process.py`
- Modify: `src/agentic_circuit/_validate.py`
- Modify: `src/agentic_circuit/_frontend.py`
- Create: `tests/python_frontend/test_process.py`
- Create: `tests/python_frontend/fixtures/process/suspended.py`
- Create: `tests/python_frontend/fixtures/process/invalid.py`

**Interfaces:**
- Consumes: captured process definitions, normalized values, static evaluation, schema effects, and diagnostics.
- Produces: `ProcessProgram`, `ProcessBlock`, `ProcessAction`, `ProcessEdge`, `ProcessEffect`, and `construct_process`.

- [x] **Step 1: Write failing CFG, suspension, and rejection tests**

```python
class ProcessFrontendTest(unittest.TestCase):
    def test_nested_control_and_suspension_build_closed_cfg(self) -> None:
        process = construct_fixture_process("process/suspended.py")
        self.assertEqual("entry", process.entry)
        self.assertEqual(["entry", "then", "else", "resume", "done"],
                         [block.name for block in process.blocks])
        self.assertTrue(any(edge.kind == "suspend" for edge in process.edges))

    def test_busy_wait_coroutine_and_undeclared_effect_are_rejected(self) -> None:
        codes = codes_for_fixture("process/invalid.py")
        self.assertGreaterEqual(codes, {
            "ACPY-PROCESS-002", "ACPY-PROCESS-006", "ACPY-EFFECT-003",
        })
```

- [x] **Step 2: Run the focused test and confirm the RED state**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_process -v`

Expected: FAIL because process construction is absent.

- [x] **Step 3: Implement immutable process CFG records**

```python
@dataclass(frozen=True, slots=True)
class ProcessBlock:
    name: str
    arguments: tuple[ValueVersion, ...]
    actions: tuple[ProcessAction, ...]
    edge: ProcessEdge

@dataclass(frozen=True, slots=True)
class ProcessProgram:
    name: str
    entry: str
    captures: tuple[ValueVersion, ...]
    blocks: tuple[ProcessBlock, ...]
    effects: tuple[ProcessEffect, ...]
```

Normalize nested runtime `if`, bounded `for`, and progress-proven `while` into
source-ordered blocks; make every suspension/resume edge, capture, and value
live across suspension explicit.

- [x] **Step 4: Implement legality and effect verification**

```python
def construct_process(definition: NormalizedDefinition,
                      effects: EffectRegistry) -> ProcessProgram:
    cfg = _ProcessBuilder(definition, effects).build()
    verify_reachable_blocks(cfg)
    verify_progress_or_suspension(cfg)
    verify_declared_effects(cfg, effects)
    verify_linear_trace_cursors(cfg)
    verify_bounded_runtime_loops(cfg)
    return cfg
```

Reject Python generators/coroutines, polling, unsupported exception/finally
suspension, cursor fork/merge, illegal effects, and unbounded runtime loops.

- [x] **Step 5: Run process and complete frontend tests**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_process -v && PYTHONPATH=src .venv/bin/python -m unittest discover -s tests/python_frontend -v`

Expected: PASS.

- [x] **Step 6: Commit process construction**

```bash
git add src/agentic_circuit tests/python_frontend
git commit -m "feat(frontend): construct supported process CFGs"
```

---

### Task 10: Deterministic ACPy-to-ACIR lowering

**Files:**
- Create: `src/agentic_circuit/_lower_acir.py`
- Modify: `src/agentic_circuit/_frontend.py`
- Create: `tests/python_frontend/test_lower_acir.py`
- Create: `tests/python_frontend/fixtures/lowering/hierarchy.py`
- Create: `tests/python_frontend/fixtures/lowering/hierarchy.acpy.json`
- Create: `tests/python_frontend/fixtures/lowering/hierarchy.ac.mlir`
- Create: `tests/python_frontend/fixtures/lowering/process.py`
- Create: `tests/python_frontend/fixtures/lowering/process.ac.mlir`
- Create: `test/Python/frontend-lowering.mlir`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: verified ACPy, normalized program, outlined scopes, resources, and process CFGs.
- Produces: `AcirArtifact`, `lower_to_acir`, and `elaborate_frontend`.

- [x] **Step 1: Write failing golden and cross-root tests**

```python
class AcirLoweringTest(unittest.TestCase):
    def test_hierarchy_matches_goldens(self) -> None:
        result = elaborate_fixture("lowering/hierarchy.py")
        self.assertEqual(fixture_bytes("lowering/hierarchy.acpy.json"),
                         result.document.canonical_bytes())
        self.assertEqual(fixture_text("lowering/hierarchy.ac.mlir"), result.acir)
        self.assertNotIn("ac.connect", result.acir)

    def test_equivalent_roots_emit_identical_bytes(self) -> None:
        first = elaborate_copied_fixture(ROOT_A, "lowering/hierarchy.py")
        second = elaborate_copied_fixture(ROOT_B, "lowering/hierarchy.py")
        self.assertEqual(first.document.canonical_bytes(),
                         second.document.canonical_bytes())
        self.assertEqual(first.acir, second.acir)
```

- [x] **Step 2: Run the focused tests and confirm the RED state**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_lower_acir -v`

Expected: FAIL because ACIR lowering is absent.

- [x] **Step 3: Implement typed deterministic emission**

```python
@dataclass(frozen=True, slots=True)
class AcirArtifact:
    text: str
    sha256: str
    source_map: tuple[tuple[str, SourceSpan], ...]

def lower_to_acir(program: NormalizedProgram,
                  document: AcpyDocument) -> AcirArtifact:
    if document.verify():
        raise ValueError("ACPY-VERIFY-001: lowering requires verified ACPy")
    writer = _AcirWriter()
    writer.file_header(contract_epoch="0.1")
    emit_declarations(writer, program)
    emit_modules_and_graph_regions(writer, program)
    emit_systems(writer, program)
    text = writer.finish()
    return AcirArtifact(text, sha256_bytes(text.encode("utf-8")),
                        writer.source_map())
```

Emit declarations in identity order and bodies in semantic order. Lower scopes
to modules/instances, flows to Graph SSA/`ac.bind`, collections to exact ACIR
forms, resources to exact declarations, and processes to structured ACIR.

- [x] **Step 4: Compose the frontend result atomically**

```python
def elaborate_frontend(request: CaptureRequest,
                       namespace: Mapping[str, object],
                       schemas: SchemaRegistry) -> FrontendResult:
    captured = capture_definitions(request, namespace, schemas)
    diagnostics = validate_and_normalize(captured)
    if diagnostics.has_errors():
        return FrontendResult(None, None, diagnostics.freeze())
    program, document = build_verified_acpy(captured)
    artifact = lower_to_acir(program, document)
    return FrontendResult(document, artifact.text, diagnostics.freeze())
```

- [x] **Step 5: Parse and verify every emitted ACIR golden**

Configure `test/Python/frontend-lowering.mlir` to generate each artifact, check
its golden, and pipe it through `acir-opt --normalize-ac-file --verify-ac-file`.

Run: `cmake --build --preset dev-llvm22 --target acir-opt check-acir && PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_lower_acir -v`

Expected: all Python goldens and native ACIR verifiers pass.

- [x] **Step 6: Commit lowering**

```bash
git add src/agentic_circuit tests/python_frontend test/Python test/CMakeLists.txt
git commit -m "feat(frontend): lower verified ACPy to canonical ACIR"
```

---

### Task 11: Lossless multi-block process extraction and C++ generation

**Files:**
- Modify: `include/acir/CodeGen/ModelPlan.h`
- Modify: `lib/CodeGen/ModelPlanDetails.cpp`
- Modify: `lib/CodeGen/ProcessGenerator.cpp`
- Modify: `lib/Conversion/ACIRToACSim/ACIRToACSim.cpp`
- Modify: `unittests/CodeGen/ModelPlanTest.cpp`
- Modify: `unittests/CodeGen/GeneratorTest.cpp`
- Modify: `unittests/CodeGen/GeneratedModelCompileTest.cpp`
- Create: `test/CodeGen/process-control-flow.mlir`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: canonical `acsim.process` regions with `cf.br`, `cf.cond_br`, scalar operations, live operations, calls, suspension, and termination.
- Produces: `PcBlockPlan`, `BlockArgumentPlan`, `BranchPlan`, `ConditionalBranchPlan`, CFG-preserving `ProcessPlan`, and typed local block dispatch.

- [x] **Step 1: Add a failing CFG extraction test**

```cpp
TEST(ModelPlanTest, PreservesEveryBlockAndBranchOperandInProcessStates) {
  OwningOpRef<ModuleOp> model = parseCanonicalACSim(processControlFlowFixture());
  auto plan = buildModelPlan(*model);
  ASSERT_TRUE(static_cast<bool>(plan));
  const ProcessPlan &process = plan->modules.front().processes.front();
  ASSERT_EQ(process.states[0].blocks.size(), 3u);
  EXPECT_TRUE(std::holds_alternative<ConditionalBranchPlan>(
      process.states[0].blocks[0].terminator));
  EXPECT_EQ(process.states[0].blocks[1].arguments.front().type, "i32");
}
```

- [x] **Step 2: Run the focused test and confirm the RED state**

Run: `cmake --build --preset dev-llvm22 --target CodeGenTests && build/dev-llvm22/bin/CodeGenTests --gtest_filter='ModelPlanTest.PreservesEveryBlockAndBranchOperandInProcessStates'`

Expected: compilation fails because the block/branch plan types do not exist.

- [x] **Step 3: Define CFG-preserving plan types**

```cpp
struct BlockArgumentPlan { std::string name; std::string type; };
struct BranchPlan { uint32_t targetBlock; std::vector<std::string> arguments; };
struct ConditionalBranchPlan {
  std::string condition;
  uint32_t trueBlock;
  std::vector<std::string> trueArguments;
  uint32_t falseBlock;
  std::vector<std::string> falseArguments;
};
using BlockTerminatorPlan = std::variant<BranchPlan, ConditionalBranchPlan,
    ContinuePlan, SuspendPlan, TerminatePlan>;
struct PcBlockPlan {
  uint32_t ordinal;
  std::vector<BlockArgumentPlan> arguments;
  std::vector<ProcessOperationPlan> operations;
  BlockTerminatorPlan terminator;
};
```

Make `PcStatePlan` own `std::vector<PcBlockPlan>`. Validate dense/reachable
blocks, legal local targets, exact successor arity/types, and value dominance.

- [x] **Step 4: Extract ACSim branches without flattening**

```cpp
if (auto branch = dyn_cast<cf::BranchOp>(operation)) {
  block.terminator = BranchPlan{
      blockOrdinal.lookup(branch.getDest()),
      valueNames(values, branch.getDestOperands(), nextValue)};
} else if (auto branch = dyn_cast<cf::CondBranchOp>(operation)) {
  block.terminator = ConditionalBranchPlan{
      valueName(values, branch.getCondition(), nextValue),
      blockOrdinal.lookup(branch.getTrueDest()),
      valueNames(values, branch.getTrueDestOperands(), nextValue),
      blockOrdinal.lookup(branch.getFalseDest()),
      valueNames(values, branch.getFalseDestOperands(), nextValue)};
}
```

Use one value namespace per PC and reject unknown operations, missing
terminators, cross-PC branch targets, or incomplete helper realization.

- [x] **Step 5: Emit typed local block dispatch**

Generate a closed block enum per multi-block PC, typed `std::optional<T>` only
for cross-block values, and a local loop/switch. Branches assign successor
arguments and select the next block; PC transitions use existing `ProcessStep`.

```cpp
for (;;) {
  switch (block_entry) {
  case Block_entry::b0:
    if (condition) { b1_arg0 = value0; block_entry = Block_entry::b1; }
    else { b2_arg0 = value1; block_entry = Block_entry::b2; }
    continue;
  case Block_entry::b1: return gfsim::ProcessStep::suspendAt(...);
  case Block_entry::b2: return gfsim::ProcessStep::continueAt(...);
  }
}
```

- [x] **Step 6: Add compile/run and forbidden-dependency tests**

Run: `cmake --build --preset dev-llvm22 --target CodeGenTests acir-cxxgen && build/dev-llvm22/bin/CodeGenTests --gtest_filter='ModelPlanTest.*Process*:GeneratorTest.*Process*:GeneratedModelCompileTest.*Process*' && lit -v build/dev-llvm22/test/CodeGen`

Expected: branched processes extract, compile, run both paths, and contain no
opaque process blob, operation-name interpreter, Python, coroutine, or
`std::function`.

- [x] **Step 7: Run all affected native suites**

Run: `ctest --test-dir build/dev-llvm22 -R '^(AnalysisTests|ConversionTests|CodeGenTests)$' --output-on-failure && cmake --build --preset dev-llvm22 --target check-acir`

Expected: PASS.

- [x] **Step 8: Commit process CFG closure**

```bash
git add include/acir/CodeGen lib/CodeGen lib/Conversion unittests/CodeGen test/CodeGen test/CMakeLists.txt
git commit -m "feat(codegen): preserve frontend process control flow"
```

---

### Task 12: Phase 4A integration and audit

**Files:**
- Modify: `.github/workflows/ci.yml`
- Modify: `tests/contracts/test_contracts.py`
- Create: `tests/python_frontend/test_determinism.py`
- Create: `docs/implementation/phase-4a-audit.md`
- Modify: `docs/superpowers/plans/2026-08-11-phase-4a-python-frontend.md`

**Interfaces:**
- Consumes: all Phase 4A Python and native process surfaces.
- Produces: CI gates, complete coverage/determinism evidence, and the audit.

- [x] **Step 1: Add failing complete-surface tests**

```python
class FrontendDeterminismTest(unittest.TestCase):
    def test_corpus_is_identical_across_roots_and_hash_seeds(self) -> None:
        first = elaborate_corpus(ROOT_A, hash_seed=1)
        second = elaborate_corpus(ROOT_B, hash_seed=99)
        self.assertEqual(first.acpy_files, second.acpy_files)
        self.assertEqual(first.acir_files, second.acir_files)

    def test_every_public_name_has_positive_and_negative_coverage(self) -> None:
        ledger = frontend_test_ledger()
        self.assertEqual(EXACT_PUBLIC_API, set(ledger))
        self.assertTrue(all(row.positive and row.negative for row in ledger.values()))
```

- [x] **Step 2: Run the new test and confirm the RED state**

Run: `PYTHONPATH=src .venv/bin/python -m unittest tests.python_frontend.test_determinism -v`

Expected: FAIL because the ledger/corpus runner is absent.

- [x] **Step 3: Add Python 3.11, 3.12, and 3.13 CI coverage**

```yaml
- name: Run Python frontend contracts
  env:
    PYTHONPATH: src
  run: |
    python -m unittest discover -s tests/python_frontend -v
    PYTHONHASHSEED=1 python -m unittest tests.python_frontend.test_determinism -v
    PYTHONHASHSEED=99 python -m unittest tests.python_frontend.test_determinism -v
```

Retain all existing repository, native build, sanitizer, and lit gates.

- [x] **Step 4: Run the complete local Phase 4A gate**

Run: `PYTHONPATH=src .venv/bin/python -m unittest discover -s tests/python_frontend -v && .venv/bin/python -m unittest tests.contracts.test_contracts -v && .venv/bin/python scripts/check-contracts.py && cmake --build --preset dev-llvm22 && ctest --test-dir build/dev-llvm22 --output-on-failure && cmake --build --preset dev-llvm22 --target check-acir && git diff --check`

Expected: PASS.

- [x] **Step 5: Write the Phase 4A audit**

Record environment, commit range, exact counts, public API/entity coverage,
syntax coverage, golden hashes, cross-root/hash-seed determinism, process CFG
compile/run evidence, forbidden dependency scan, and residual risk. Mark plan
checkboxes only from observed commit/test evidence.

- [x] **Step 6: Commit the audit**

```bash
git add .github/workflows/ci.yml tests docs/implementation/phase-4a-audit.md docs/superpowers/plans/2026-08-11-phase-4a-python-frontend.md
git commit -m "docs(audit): complete phase 4a frontend verification"
```

---

## Phase 4A Final Verification

```bash
PYTHONPATH=src .venv/bin/python -m unittest discover -s tests/python_frontend -v
.venv/bin/python -m unittest tests.contracts.test_contracts -v
.venv/bin/python scripts/check-contracts.py
cmake --build --preset dev-llvm22
ctest --test-dir build/dev-llvm22 --output-on-failure
cmake --build --preset dev-llvm22 --target check-acir
cmake --build --preset release-llvm22
ctest --test-dir build/release-llvm22 --output-on-failure
cmake --build --preset release-llvm22 --target check-acir
git diff --check
```

Expected: every command passes. Generated ACPy/ACIR contain normalized relative
locations and no host tokens; generated multi-block process code uses typed
local control flow and contains no Python or descriptor interpreter.
