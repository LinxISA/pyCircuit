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


class RepositoryContractsTest(unittest.TestCase):
    def test_ci_caches_verified_llvm_sources_and_exact_build_outputs(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text()
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
        self.assertIn("steps.llvm-build-fingerprint.outputs.value", build_cache_step)
        self.assertIn("env.LLVM_SOURCE_SHA256", build_cache_step)
        self.assertIn("hashFiles(", build_cache_step)
        self.assertIn("'toolchains/llvm.lock.json'", build_cache_step)
        self.assertIn("'scripts/ci-build-llvm.sh'", build_cache_step)
        self.assertIn("steps.llvm-build-cache.outputs.cache-hit != 'true'", build_step)

        native_dependencies = re.search(
            r"      - name: Install native test dependencies.*?(?=\n      - name:)",
            workflow,
            re.DOTALL,
        ).group()
        self.assertIn("libgtest-dev", native_dependencies)

        build_verification = re.search(
            r"      - name: Verify exact LLVM build outputs.*?(?=\n      - name:)",
            workflow,
            re.DOTALL,
        ).group()
        self.assertIn("mlir-opt --version", build_verification)
        self.assertIn("LLVMConfigVersion.cmake", build_verification)
        self.assertIn("MLIRConfigVersion.cmake", build_verification)

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

        self.assertEqual(8, len(schema_epochs))
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
        self.assertEqual(8, len(checked), checked)

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
