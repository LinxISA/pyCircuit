import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


class BuildConfigurationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.repo_root = Path(__file__).resolve().parents[1]

    def test_production_configure_does_not_require_lit(self):
        mlir_dir = os.environ.get("MLIR_DIR")
        if not mlir_dir:
            self.skipTest("MLIR_DIR is required for the configure regression test")

        cmake = shutil.which("cmake")
        ninja = shutil.which("ninja")
        self.assertIsNotNone(cmake)
        self.assertIsNotNone(ninja)

        ignored_lit_dirs = {str(self.repo_root / ".venv" / "bin")}
        for executable in ("lit", "llvm-lit"):
            executable_path = shutil.which(executable)
            if executable_path:
                ignored_lit_dirs.add(str(Path(executable_path).resolve().parent))

        with tempfile.TemporaryDirectory() as temp_dir:
            result = subprocess.run(
                [
                    cmake,
                    "-S",
                    str(self.repo_root),
                    "-B",
                    str(Path(temp_dir) / "build"),
                    "-G",
                    "Ninja",
                    f"-DCMAKE_MAKE_PROGRAM={ninja}",
                    f"-DMLIR_DIR={mlir_dir}",
                    "-DBUILD_TESTING=OFF",
                    f"-DCMAKE_IGNORE_PATH={';'.join(sorted(ignored_lit_dirs))}",
                ],
                capture_output=True,
                env=os.environ,
                text=True,
            )

        self.assertEqual(
            result.returncode,
            0,
            msg=f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}",
        )

    def test_standalone_lit_requires_explicit_tool_paths(self):
        lit = shutil.which("lit") or str(Path(sys.executable).with_name("lit"))
        self.assertTrue(Path(lit).is_file(), f"lit executable not found: {lit}")

        environment = os.environ.copy()
        environment.pop("ACIR_TEST_EXEC_ROOT", None)
        environment.pop("ACIR_TOOLS_DIR", None)
        environment.pop("LLVM_TOOLS_DIR", None)
        result = subprocess.run(
            [lit, "-sv", str(self.repo_root / "test" / "ACIR" / "dialect-smoke.mlir")],
            capture_output=True,
            env=environment,
            text=True,
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ACIR_TEST_EXEC_ROOT", result.stderr)
        self.assertIn("ACIR_TOOLS_DIR", result.stderr)
        self.assertIn("LLVM_TOOLS_DIR", result.stderr)


if __name__ == "__main__":
    unittest.main()
