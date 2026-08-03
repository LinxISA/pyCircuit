#!/usr/bin/env python3
import importlib.util
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EPOCH = "0.1"
GOVERNANCE_FILES = (
    "LICENSE",
    "CONTRIBUTING.md",
    "CODE_OF_CONDUCT.md",
    "SECURITY.md",
    "SUPPORT.md",
    "CHANGELOG.md",
)
EXPECTED_LLVM = {
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


def check_governance(errors):
    for name in GOVERNANCE_FILES:
        path = ROOT / name
        if not path.is_file() or not path.read_text().strip():
            errors.append(f"missing or empty governance file: {name}")


def check_epochs(errors):
    schemas = sorted((ROOT / "schemas").glob("*.schema.json"))
    if len(schemas) != 8:
        errors.append(f"expected 8 JSON schemas, found {len(schemas)}")
    for path in schemas:
        document = json.loads(path.read_text())
        actual = document.get("properties", {}).get("contract_epoch", {}).get("const")
        if actual != EPOCH:
            errors.append(f"{path.relative_to(ROOT)} declares epoch {actual!r}")
    pyproject = (ROOT / "pyproject.toml").read_text()
    match = re.search(r'^contract-epoch\s*=\s*"([^"]+)"\s*$', pyproject, re.MULTILINE)
    if match is None or match.group(1) != EPOCH:
        errors.append("pyproject.toml must declare contract-epoch = \"0.1\"")


def check_schemas(errors):
    if importlib.util.find_spec("jsonschema") is None:
        errors.append("jsonschema is unavailable; install requirements-dev.lock")
        return
    from jsonschema.exceptions import SchemaError
    from jsonschema.validators import Draft202012Validator

    for path in sorted((ROOT / "schemas").glob("*.schema.json")):
        document = json.loads(path.read_text())
        if document.get("$schema") != "https://json-schema.org/draft/2020-12/schema":
            errors.append(f"{path.relative_to(ROOT)} is not draft 2020-12")
            continue
        try:
            Draft202012Validator.check_schema(document)
        except SchemaError as exc:
            errors.append(f"{path.relative_to(ROOT)} does not compile: {exc.message}")


def check_links(errors):
    pattern = re.compile(r"(?<!!)\[[^\]]+\]\(([^)]+)\)")
    markdown = [ROOT / "README.md", *sorted((ROOT / "docs/specs").glob("*.md"))]
    for path in markdown:
        for target in pattern.findall(path.read_text()):
            destination = target.split("#", 1)[0]
            if not destination or re.match(r"^[a-z][a-z0-9+.-]*:", destination):
                continue
            if not (path.parent / destination).resolve().exists():
                errors.append(f"broken link: {path.relative_to(ROOT)} -> {target}")


def check_placeholders(errors):
    marker = re.compile(r"\b(?:TODO|TBD|FIXME|XXX)\b")
    for name in ("README.md", *GOVERNANCE_FILES):
        path = ROOT / name
        if path.is_file() and marker.search(path.read_text()):
            errors.append(f"placeholder marker in {name}")
    readme = (ROOT / "README.md").read_text()
    if "proposed Python" in readme or "No implementation contract is approved yet" in readme:
        errors.append("README.md still describes a specification-only placeholder")


def check_llvm_lock(errors):
    lock = json.loads((ROOT / "toolchains/llvm.lock.json").read_text())
    if lock.get("lock_version") != 1:
        errors.append("LLVM lock_version must equal 1")
    if lock.get("llvm") != EXPECTED_LLVM:
        errors.append("LLVM lock does not match the approved 22.1.8 toolchain")


def main():
    errors = []
    check_governance(errors)
    check_epochs(errors)
    check_schemas(errors)
    check_links(errors)
    check_placeholders(errors)
    check_llvm_lock(errors)
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1
    print("repository contracts: OK (8 schemas, epoch 0.1, LLVM 22.1.8)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
