import importlib.util
import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTRACT_EPOCH = "0.1"
LLVM_LOCK = {
    "release": "22.1.8",
    "upstream_commit": "ca7933e47d3a3451d81e72ac174dcb5aa28b59d1",
    "source_url": (
        "https://github.com/llvm/llvm-project/releases/download/llvmorg-22.1.8/"
        "llvm-project-22.1.8.src.tar.xz"
    ),
    "source_sha256": "922f1817a0df7b1489272d18134ee0087a8b068828f87ac63b9861b1a9965888",
    "local_prefix": "/opt/homebrew/opt/llvm",
    "supported_host_triples": ["arm64-apple-darwin", "x86_64-linux-gnu"],
    "package_version_policy": "exact",
}
GOVERNANCE_FILES = {
    "LICENSE",
    "CONTRIBUTING.md",
    "CODE_OF_CONDUCT.md",
    "SECURITY.md",
    "SUPPORT.md",
    "CHANGELOG.md",
}


def load_contract_checker():
    path = ROOT / "scripts/check-contracts.py"
    spec = importlib.util.spec_from_file_location("contract_checker", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def initialize_markdown_fixture(files):
    temporary_directory = tempfile.TemporaryDirectory()
    root = Path(temporary_directory.name)
    subprocess.run(["git", "init", "-q"], cwd=root, check=True)
    for relative_path, content in files.items():
        path = root / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    subprocess.run(["git", "add", "."], cwd=root, check=True)
    return temporary_directory, root


def assert_exact_ci_cache_commands(test_case, workflow):
    fingerprint_step = re.search(
        r"      - name: Compute LLVM build fingerprint.*?(?=\n      - name:)",
        workflow,
        re.DOTALL,
    ).group()
    verification_step = re.search(
        r"      - name: Verify exact LLVM build outputs.*?(?=\n      - name:)",
        workflow,
        re.DOTALL,
    ).group()

    for command in (
        "pwd -P",
        "command -v c++",
        "c++ --version",
        "cmake --version",
        "ninja --version",
        "uname -m",
        "ldd --version 2>&1 | head -n 1",
    ):
        test_case.assertRegex(
            fingerprint_step,
            rf"(?m)^\s+{re.escape(command)}$",
        )

    exact_verification_commands = (
        '.cache/llvm-build/bin/mlir-opt --version | grep -F "LLVM version ${LLVM_RELEASE}"',
        'grep -F "set(PACKAGE_VERSION \\"${LLVM_RELEASE}\\")" .cache/llvm-build/lib/cmake/llvm/LLVMConfigVersion.cmake',
        'grep -F "set(PACKAGE_VERSION \\"${LLVM_RELEASE}\\")" .cache/llvm-build/lib/cmake/mlir/MLIRConfigVersion.cmake',
        'grep -F "CMAKE_HOME_DIRECTORY:INTERNAL=${GITHUB_WORKSPACE}/.cache/llvm-project-${LLVM_RELEASE}.src/llvm" .cache/llvm-build/CMakeCache.txt',
        'grep -F "CMAKE_CACHEFILE_DIR:INTERNAL=${GITHUB_WORKSPACE}/.cache/llvm-build" .cache/llvm-build/CMakeCache.txt',
    )
    for command in exact_verification_commands:
        test_case.assertRegex(
            verification_step,
            rf"(?m)^\s+{re.escape(command)}$",
        )


class RepositoryContractsTest(unittest.TestCase):
    def test_ci_caches_verified_llvm_sources_and_exact_build_outputs(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text()
        assert_exact_ci_cache_commands(self, workflow)
        source_cache_step = re.search(
            r"      - name: Cache content-addressed LLVM source archive.*?(?=\n      - name:)",
            workflow,
            re.DOTALL,
        ).group()
        build_cache_step = re.search(
            r"      - name: Cache exact LLVM build outputs.*?(?=\n      - name:)",
            workflow,
            re.DOTALL,
        ).group()
        fingerprint_step = re.search(
            r"      - name: Compute LLVM build fingerprint.*?(?=\n      - name:)",
            workflow,
            re.DOTALL,
        ).group()
        verification_step = re.search(
            r"      - name: Fetch and verify locked LLVM source.*?(?=\n      - name:)",
            workflow,
            re.DOTALL,
        ).group()
        build_step = re.search(
            r"      - name: Build locked MLIR package.*?(?=\n      - name:)",
            workflow,
            re.DOTALL,
        ).group()

        self.assertRegex(
            source_cache_step,
            r"(?m)^\s+path: \.cache/llvm-project-22\.1\.8\.src\.tar\.xz$",
        )
        self.assertEqual(1, source_cache_step.count(".cache/"), source_cache_step)
        self.assertNotRegex(verification_step, r"(?m)^\s+if:")
        self.assertIn("sha256sum --check --strict", verification_step)
        self.assertIn("path: .cache/llvm-build", build_cache_step)
        expected_build_cache_key = (
            "key: llvm-build-${{ runner.os }}-${{ runner.arch }}-"
            "${{ env.LLVM_SOURCE_SHA256 }}-"
            "${{ hashFiles('toolchains/llvm.lock.json', "
            "'scripts/ci-build-llvm.sh') }}-"
            "${{ steps.llvm-build-fingerprint.outputs.value }}"
        )
        self.assertIn(expected_build_cache_key, build_cache_step)
        for fingerprint_input in (
            "pwd -P",
            "command -v c++",
            "c++ --version",
            "cmake --version",
            "ninja --version",
            "ldd --version",
        ):
            self.assertIn(fingerprint_input, fingerprint_step)
        self.assertIn("steps.llvm-build-cache.outputs.cache-hit != 'true'", build_step)

        native_dependencies = re.search(
            r"      - name: Install native test dependencies.*?(?=\n      - name:)",
            workflow,
            re.DOTALL,
        ).group()
        self.assertRegex(
            native_dependencies,
            r"sudo apt-get install --yes --no-install-recommends libgtest-dev",
        )

        build_verification = re.search(
            r"      - name: Verify exact LLVM build outputs.*?(?=\n      - name:)",
            workflow,
            re.DOTALL,
        ).group()
        self.assertNotRegex(build_verification, r"(?m)^\s+if:")
        self.assertIn("test -x .cache/llvm-build/bin/mlir-opt", build_verification)
        self.assertIn(
            "test -f .cache/llvm-build/lib/cmake/llvm/LLVMConfigVersion.cmake",
            build_verification,
        )
        self.assertIn(
            "test -f .cache/llvm-build/lib/cmake/mlir/MLIRConfigVersion.cmake",
            build_verification,
        )
        self.assertIn(
            'mlir-opt --version | grep -F "LLVM version ${LLVM_RELEASE}"',
            build_verification,
        )
        self.assertEqual(
            2,
            build_verification.count(
                'grep -F "set(PACKAGE_VERSION \\"${LLVM_RELEASE}\\")"'
            ),
            build_verification,
        )
        self.assertIn("CMAKE_HOME_DIRECTORY:INTERNAL=${GITHUB_WORKSPACE}", build_verification)
        self.assertIn("CMAKE_CACHEFILE_DIR:INTERNAL=${GITHUB_WORKSPACE}", build_verification)

    def test_ci_cache_contract_rejects_inert_or_misdirected_checks(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text()
        mutations = (
            ("            c++ --version", "            echo 'c++ --version'"),
            (
                'grep -F "set(PACKAGE_VERSION \\"${LLVM_RELEASE}\\")" .cache/llvm-build/lib/cmake/mlir/MLIRConfigVersion.cmake',
                'grep -F "set(PACKAGE_VERSION \\"${LLVM_RELEASE}\\")" .cache/llvm-build/lib/cmake/llvm/LLVMConfigVersion.cmake',
            ),
            (
                "CMAKE_HOME_DIRECTORY:INTERNAL=${GITHUB_WORKSPACE}/.cache/llvm-project-${LLVM_RELEASE}.src/llvm",
                "CMAKE_HOME_DIRECTORY:INTERNAL=${GITHUB_WORKSPACE}/wrong-source",
            ),
            (
                "CMAKE_CACHEFILE_DIR:INTERNAL=${GITHUB_WORKSPACE}/.cache/llvm-build",
                "CMAKE_CACHEFILE_DIR:INTERNAL=${GITHUB_WORKSPACE}/wrong-build",
            ),
        )
        for original, replacement in mutations:
            with self.subTest(original=original):
                self.assertIn(original, workflow)
                mutated = workflow.replace(original, replacement, 1)
                with self.assertRaises(AssertionError):
                    assert_exact_ci_cache_commands(self, mutated)

    def test_cmake_package_versions_require_exact_match(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        package_version = re.search(
            r"write_basic_package_version_file\(.*?\n\)", cmake, re.DOTALL
        ).group()
        self.assertIn("COMPATIBILITY ExactVersion", package_version)

    def test_governance_files_are_present_and_nonempty(self):
        missing = sorted(
            path for path in GOVERNANCE_FILES if not (ROOT / path).is_file()
        )
        empty = sorted(
            path
            for path in GOVERNANCE_FILES
            if (ROOT / path).is_file() and not (ROOT / path).read_text().strip()
        )
        self.assertEqual([], missing, f"missing governance files: {missing}")
        self.assertEqual([], empty, f"empty governance files: {empty}")

    def test_all_machine_contracts_use_the_exact_epoch(self):
        schema_epochs = {}
        for path in sorted((ROOT / "schemas").glob("*.schema.json")):
            document = json.loads(path.read_text())
            properties = document.get("properties", {})
            epoch = properties.get("contract_epoch", {}).get("const")
            schema_epochs[path.name] = epoch

        pyproject = (ROOT / "pyproject.toml").read_text()
        declared_epoch = re.search(
            r'^contract-epoch\s*=\s*"([^"]+)"\s*$', pyproject, re.MULTILINE
        )

        self.assertEqual(10, len(schema_epochs))
        self.assertEqual(
            {CONTRACT_EPOCH}, set(schema_epochs.values()), schema_epochs
        )
        self.assertIsNotNone(declared_epoch, "pyproject.toml lacks contract-epoch")
        self.assertEqual(CONTRACT_EPOCH, declared_epoch.group(1))

    def test_all_json_schemas_compile_as_draft_2020_12(self):
        self.assertIsNotNone(
            importlib.util.find_spec("jsonschema"),
            "install the locked development requirements before running contracts",
        )
        from jsonschema.validators import Draft202012Validator

        checked = []
        for path in sorted((ROOT / "schemas").glob("*.schema.json")):
            document = json.loads(path.read_text())
            self.assertEqual(
                "https://json-schema.org/draft/2020-12/schema",
                document.get("$schema"),
                path.name,
            )
            Draft202012Validator.check_schema(document)
            checked.append(path.name)
        self.assertEqual(10, len(checked), checked)

    def test_process_state_plan_schema_is_closed_and_accepts_exact_baseline(self):
        self.assertIsNotNone(importlib.util.find_spec("jsonschema"))
        from jsonschema import ValidationError
        from jsonschema.validators import Draft202012Validator

        schema = json.loads(
            (ROOT / "schemas/acir-process-state-plan.schema.json").read_text()
        )
        validator = Draft202012Validator(schema)
        occurrence = {
            "call_sites": [],
            "iteration_vector": [],
            "kind": "original",
            "operation_path": "@Top::@workload/r0/b0/o0",
        }
        descriptor = {
            "cpp": "acir::generated::impl_wake_next_delta_63cacba5c3eb82976464804b4aeaa17d43b445733efaddfad7c7bec1ab650269",
            "effect": "stateful",
            "fingerprint": "sha256:63cacba5c3eb82976464804b4aeaa17d43b445733efaddfad7c7bec1ab650269",
            "inputs": [],
            "kind": "implementation",
            "ordinal": 0,
            "payload": {"wake_kind": "next_delta", "wake_type": "@acir_wake_next_delta"},
            "results": ["@acir_wake_next_delta"],
            "role": "wake_next_delta",
            "source_paths": [],
            "symbol": "@acir_impl_wake_next_delta_63cacba5c3eb82976464804b4aeaa17d43b445733efaddfad7c7bec1ab650269",
        }
        fixture = {
            "callees": [descriptor],
            "contract_epoch": "0.1",
            "processes": [{
                "blocks": [{"actions": [], "cost": 2, "edge": {"kind": "suspend", "transition": 0}, "frames": [], "loads": [], "ordinal": 0, "path": "@Top::@workload/plan/pc/entry/b00000000", "pc": 0}],
                "captures": [], "definition_key": "@Top::@workload", "entry_pc": 0,
                "fairness_work": 2, "live_slots": [], "pc_bit_width": 1,
                "pcs": [{"blocks": [0], "entry_path": "@Top::@workload/plan/pc/entry/b00000000", "name": "entry", "ordinal": 0}],
                "transitions": [{"iteration_vector": [], "loads": [], "ordinal": 0, "source_pc": 0, "stores": [], "target_pc": 0, "wake": 0}],
                "wakes": [{"callee": 0, "iteration_vector": [], "kind": "next_delta", "occurrence": occurrence, "operation_path": "@Top::@workload/r0/b0/o0", "ordinal": 0, "sources": [], "target": "", "type_key": "@acir_wake_next_delta"}],
            }],
            "schema": "acir-process-state-plan-0.1",
            "value_types": [],
        }
        validator.validate(fixture)

        branch = schema["$defs"]["edge_branch"]
        self.assertEqual(
            {"condition", "false_bindings", "false_block", "kind", "true_bindings", "true_block"},
            set(branch["properties"]),
        )
        self.assertEqual(set(branch["properties"]), set(branch["required"]))

        object_definitions = [schema]
        object_definitions.extend(
            value for value in schema["$defs"].values()
            if isinstance(value, dict) and value.get("type") == "object"
        )
        for object_schema in object_definitions:
            self.assertIs(False, object_schema.get("additionalProperties"))

        def reject_unknown(path):
            mutated = json.loads(json.dumps(fixture))
            node = mutated
            for key in path:
                node = node[key]
            node["unknown"] = True
            with self.assertRaises(ValidationError):
                validator.validate(mutated)

        for path in (
            (), ("callees", 0), ("callees", 0, "payload"),
            ("processes", 0), ("processes", 0, "blocks", 0),
            ("processes", 0, "blocks", 0, "edge"),
            ("processes", 0, "pcs", 0),
            ("processes", 0, "transitions", 0),
            ("processes", 0, "wakes", 0),
            ("processes", 0, "wakes", 0, "occurrence"),
        ):
            reject_unknown(path)

    def test_acsim_binding_schema_is_closed_and_accepts_only_lock_records(self):
        self.assertIsNotNone(
            importlib.util.find_spec("jsonschema"),
            "install the locked development requirements before running contracts",
        )
        from jsonschema import ValidationError
        from jsonschema.validators import Draft202012Validator

        schema = json.loads(
            (ROOT / "schemas/acsim-binding.schema.json").read_text()
        )
        registry = json.loads(
            (ROOT / "test/Bindings/Inputs/leaf-fast.json").read_text()
        )
        candidate = registry["candidates"][0]
        record = candidate["record"]
        validator = Draft202012Validator(schema)
        validator.validate(record)
        unit_record = json.loads(json.dumps(record))
        unit_record["parameters"][0]["value"] = {"unit": "cycles", "value": 4}
        unit_record["construction"]["arguments"][0] = {
            "unit": "cycles",
            "value": 4,
        }
        validator.validate(unit_record)

        stateful_duplicate_name = json.loads(json.dumps(record))
        stateful_duplicate_name["effect"] = "stateful"
        stateful_duplicate_name["cpp"]["entry_points"].update(
            {"pure": "", "work": "gfsim::work", "xfer": "gfsim::xfer"}
        )
        stateful_duplicate_name["ownership"] = {
            "kind": "unique",
            "placement": "member_or_array",
        }
        stateful_duplicate_name["activation_sources"] = [
            {"kind": "ac.std.Clock", "name": "wake"},
            {"kind": "ac.std.Reset", "name": "wake"},
        ]
        # Draft 2020-12 cannot state unique-by-name. The schema accepts this
        # structurally; BindingRecord semantic validation rejects it.
        validator.validate(stateful_duplicate_name)
        identical_activation = json.loads(json.dumps(stateful_duplicate_name))
        identical_activation["activation_sources"][1] = dict(
            identical_activation["activation_sources"][0]
        )
        with self.assertRaises(ValidationError):
            validator.validate(identical_activation)

        def assert_closed_objects(fragment, path="#"):
            if isinstance(fragment, list):
                for index, value in enumerate(fragment):
                    assert_closed_objects(value, f"{path}/{index}")
                return
            if not isinstance(fragment, dict):
                return
            if fragment.get("type") == "object":
                self.assertIs(
                    False,
                    fragment.get("additionalProperties"),
                    f"open object schema at {path}",
                )
            for key, value in fragment.items():
                assert_closed_objects(value, f"{path}/{key}")

        assert_closed_objects(schema)

        mutations = []
        unknown = dict(record)
        unknown["emitter_callback"] = "emitLeaf"
        mutations.append(unknown)
        unavailable = dict(record)
        unavailable["availability"] = "unavailable"
        mutations.append(unavailable)
        wrong_epoch = dict(record)
        wrong_epoch["contract_epoch"] = "0.2"
        mutations.append(wrong_epoch)
        wrong_schema = dict(record)
        wrong_schema["binding_schema"] = "acsim-binding-0.2"
        mutations.append(wrong_schema)
        wrong_cardinality = dict(record)
        wrong_cardinality["ports"] = [dict(record["ports"][0])]
        wrong_cardinality["ports"][0]["cardinality"] = "many"
        mutations.append(wrong_cardinality)
        raw_parameter_type = json.loads(json.dumps(record))
        raw_parameter_type["parameters"][0]["cpp_type"] = "int; emit()"
        mutations.append(raw_parameter_type)
        invalid_static_key = json.loads(json.dumps(unit_record))
        invalid_static_key["parameters"][0]["value"] = {"not-valid": 4}
        mutations.append(invalid_static_key)
        for mutation in mutations:
            with self.assertRaises(ValidationError):
                validator.validate(mutation)

    def test_markdown_local_links_resolve(self):
        broken = []
        link_pattern = re.compile(r"(?<!!)\[[^\]]+\]\(([^)]+)\)")
        markdown_files = [ROOT / "README.md", *sorted((ROOT / "docs/specs").glob("*.md"))]
        for path in markdown_files:
            for target in link_pattern.findall(path.read_text()):
                destination = target.split("#", 1)[0]
                if not destination or re.match(r"^[a-z][a-z0-9+.-]*:", destination):
                    continue
                if not (path.parent / destination).resolve().exists():
                    broken.append(f"{path.relative_to(ROOT)} -> {target}")
        self.assertEqual([], broken, f"broken Markdown links: {broken}")

    def test_checker_scans_tracked_markdown_and_image_targets(self):
        temporary_directory, root = initialize_markdown_fixture(
            {
                "README.md": "# Fixture\n",
                "nested/guide.md": "# Guide\n\n![missing](missing.png)\n",
            }
        )
        self.addCleanup(temporary_directory.cleanup)
        checker = load_contract_checker()
        checker.ROOT = root
        errors = []

        checker.check_links(errors)

        self.assertTrue(errors, "tracked Markdown image target was not checked")
        self.assertIn("nested/guide.md -> missing.png", errors[0])

    def test_checker_rejects_missing_markdown_fragments(self):
        temporary_directory, root = initialize_markdown_fixture(
            {
                "README.md": "# Fixture\n\n[section](guide.md#missing-heading)\n",
                "guide.md": "# Existing Heading\n",
            }
        )
        self.addCleanup(temporary_directory.cleanup)
        checker = load_contract_checker()
        checker.ROOT = root
        errors = []

        checker.check_links(errors)

        self.assertTrue(errors, "missing Markdown fragment was not checked")
        self.assertIn("guide.md#missing-heading", errors[0])

    def test_checker_ignores_links_in_fenced_and_indented_code(self):
        temporary_directory, root = initialize_markdown_fixture(
            {
                "README.md": (
                    "# Fixture\n\n"
                    "```markdown\n"
                    "[fenced](missing-fenced.txt)\n"
                    "![fenced image](missing-fenced.png)\n"
                    "```\n\n"
                    "    [indented](missing-indented.txt)\n"
                ),
            }
        )
        self.addCleanup(temporary_directory.cleanup)
        checker = load_contract_checker()
        checker.ROOT = root
        errors = []

        checker.check_links(errors)

        self.assertEqual([], errors, f"code example links were checked: {errors}")

    def test_code_example_headings_do_not_satisfy_fragments(self):
        temporary_directory, root = initialize_markdown_fixture(
            {
                "README.md": "# Fixture\n\n[section](guide.md#example-heading)\n",
                "guide.md": (
                    "# Guide\n\n"
                    "```markdown\n"
                    "# Example Heading\n"
                    "```\n"
                ),
            }
        )
        self.addCleanup(temporary_directory.cleanup)
        checker = load_contract_checker()
        checker.ROOT = root
        errors = []

        checker.check_links(errors)

        self.assertTrue(errors, "code example heading satisfied a real fragment")
        self.assertIn("guide.md#example-heading", errors[0])

    def test_repository_has_no_placeholder_markers(self):
        offenders = []
        marker = re.compile(r"\b(?:TODO|TBD|FIXME|XXX)\b")
        checked = [ROOT / "README.md"]
        checked.extend(ROOT / path for path in GOVERNANCE_FILES)
        for path in checked:
            if path.is_file() and marker.search(path.read_text()):
                offenders.append(str(path.relative_to(ROOT)))
        readme = (ROOT / "README.md").read_text()
        if "proposed Python" in readme or "No implementation contract is approved yet" in readme:
            offenders.append("README.md (stale specification-phase placeholder)")
        self.assertEqual([], offenders, f"placeholder content: {offenders}")

    def test_llvm_lock_is_exact_and_complete(self):
        lock = json.loads((ROOT / "toolchains/llvm.lock.json").read_text())
        self.assertEqual(1, lock.get("lock_version"))
        self.assertEqual(LLVM_LOCK, lock.get("llvm"))

    def test_read_only_checker_accepts_the_repository(self):
        before = subprocess.run(
            ["git", "status", "--porcelain=v1"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        result = subprocess.run(
            [sys.executable, "scripts/check-contracts.py"],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        after = subprocess.run(
            ["git", "status", "--porcelain=v1"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertEqual(before, after, "contract checker modified the repository")


if __name__ == "__main__":
    unittest.main()
