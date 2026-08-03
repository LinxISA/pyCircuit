import os

import lit.formats


config.name = "AgenticCircuit"
config.test_format = lit.formats.ShTest(execute_external=True)
config.suffixes = [".mlir"]
config.test_source_root = os.path.dirname(__file__)

configured_paths = {
    "ACIR_TEST_EXEC_ROOT": getattr(config, "acir_test_exec_root", None)
    or os.environ.get("ACIR_TEST_EXEC_ROOT"),
    "ACIR_TOOLS_DIR": getattr(config, "acir_tools_dir", None)
    or os.environ.get("ACIR_TOOLS_DIR"),
    "LLVM_TOOLS_DIR": getattr(config, "llvm_tools_dir", None)
    or os.environ.get("LLVM_TOOLS_DIR"),
}
missing_paths = [name for name, value in configured_paths.items() if not value]
if missing_paths:
    lit_config.fatal(
        "standalone lit requires explicit paths: " + ", ".join(missing_paths)
    )

config.test_exec_root = configured_paths["ACIR_TEST_EXEC_ROOT"]
tools_dir = configured_paths["ACIR_TOOLS_DIR"]
llvm_tools_dir = configured_paths["LLVM_TOOLS_DIR"]

config.substitutions.append(("%acir_opt_public", os.path.join(tools_dir, "acir-opt")))
config.substitutions.append(("%acir_opt", os.path.join(tools_dir, "acir-opt-internal")))
config.substitutions.append(("%FileCheck", os.path.join(llvm_tools_dir, "FileCheck")))
config.substitutions.append(("%split_file", os.path.join(llvm_tools_dir, "split-file")))
config.substitutions.append(("%not", os.path.join(llvm_tools_dir, "not")))
