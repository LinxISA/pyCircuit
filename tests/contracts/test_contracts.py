import importlib.util
import json
import re
import subprocess
import sys
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


class RepositoryContractsTest(unittest.TestCase):
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
