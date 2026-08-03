import os

import lit.formats


config.name = "AgenticCircuit"
config.test_format = lit.formats.ShTest(execute_external=True)
config.suffixes = [".mlir"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = getattr(
    config,
    "acir_test_exec_root",
    os.path.join(
        os.path.dirname(config.test_source_root), "build", "dev-llvm22", "test"
    ),
)

tools_dir = getattr(
    config,
    "acir_tools_dir",
    os.path.join(os.path.dirname(config.test_source_root), "build", "dev-llvm22", "bin"),
)
llvm_tools_dir = getattr(config, "llvm_tools_dir", "/opt/homebrew/opt/llvm/bin")

config.substitutions.append(("%acir_opt", os.path.join(tools_dir, "acir-opt")))
config.substitutions.append(("%FileCheck", os.path.join(llvm_tools_dir, "FileCheck")))
config.substitutions.append(("%split_file", os.path.join(llvm_tools_dir, "split-file")))
config.substitutions.append(("%not", os.path.join(llvm_tools_dir, "not")))
