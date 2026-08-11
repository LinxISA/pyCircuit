from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from jsonschema.validators import Draft202012Validator


REPOSITORY = Path(__file__).parents[2]
BUILD_PYTHON = REPOSITORY / "build" / "dev-llvm22" / "python"


def run_cli(*arguments: str, cwd: Path) -> subprocess.CompletedProcess[str]:
    environment = os.environ.copy()
    environment["PYTHONPATH"] = os.pathsep.join(
        (str(REPOSITORY / "src"), str(BUILD_PYTHON))
    )
    return subprocess.run(
        [sys.executable, "-m", "agentic_circuit._cli", *arguments],
        cwd=cwd,
        env=environment,
        text=True,
        capture_output=True,
        check=False,
    )


def snapshot_tree(root: Path) -> tuple[tuple[str, str], ...]:
    return tuple(
        (
            path.relative_to(root).as_posix(),
            hashlib.sha256(path.read_bytes()).hexdigest(),
        )
        for path in sorted(root.rglob("*"))
        if path.is_file()
    )


class DiscoveryCommandTest(unittest.TestCase):
    def test_capabilities_match_schema_without_importing_project(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            hostile = Path(temporary)
            hostile.joinpath("architecture.py").write_text(
                "from pathlib import Path\nPath('imported.marker').write_text('bad')\n"
            )

            result = run_cli("schema", "capabilities", "--json", cwd=hostile)
            document = json.loads(result.stdout)

            schema = json.loads(
                (REPOSITORY / "schemas" / "capabilities.schema.json").read_text()
            )
            Draft202012Validator(schema).validate(document)
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertFalse(hostile.joinpath("imported.marker").exists())
            self.assertEqual(
                sorted((item["kind"], item["name"]) for item in document["items"]),
                [(item["kind"], item["name"]) for item in document["items"]],
            )
            self.assertIn(
                "declared_unavailable",
                {item["availability"] for item in document["items"]},
            )
            for item in document["items"]:
                if item["availability"] == "available":
                    self.assertIsNotNone(item["implementation_fingerprint"])
                else:
                    self.assertIsNone(item["implementation_fingerprint"])

    def test_component_protocol_and_list_queries_are_exact(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            component = run_cli("schema", "component", "Queue", "--json", cwd=root)
            protocol = run_cli(
                "schema", "protocol", "ready_valid", "--json", cwd=root
            )
            listing = run_cli("schema", "component", "--json", cwd=root)

        self.assertEqual("ac.std.Queue", json.loads(component.stdout)["canonical_name"])
        self.assertEqual(
            "ac.std.ready_valid", json.loads(protocol.stdout)["canonical_name"]
        )
        names = json.loads(listing.stdout)["items"]
        self.assertEqual(sorted(names), names)

    def test_unknown_schema_name_is_a_structured_user_error(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            result = run_cli(
                "schema", "component", "Missing", "--json", cwd=Path(temporary)
            )

        self.assertEqual(2, result.returncode)
        self.assertEqual("ACPY-SCHEMA-001", json.loads(result.stdout)["code"])

    def test_explain_and_doctor_are_read_only(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            workspace = Path(temporary)
            workspace.joinpath("sentinel.txt").write_text("unchanged\n")
            before = snapshot_tree(workspace)

            explained = run_cli(
                "explain", "ACIR-PROTOCOL-004", "--json", cwd=workspace
            )
            doctor = run_cli("doctor", "--json", cwd=workspace)

            self.assertEqual(before, snapshot_tree(workspace))

        self.assertEqual(0, explained.returncode, explained.stderr)
        self.assertEqual("ACIR-PROTOCOL-004", json.loads(explained.stdout)["code"])
        self.assertEqual(0, doctor.returncode, doctor.stderr)
        checks = json.loads(doctor.stdout)["checks"]
        self.assertTrue(
            all(check["status"] == "passed" for check in checks)
        )


if __name__ == "__main__":
    unittest.main()
