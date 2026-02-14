#!/usr/bin/env bash
set -euo pipefail

LLVM_PROJECT_DIR="${LLVM_PROJECT_DIR:-$HOME/llvm-project}"
LLVM_BUILD_DIR="${LLVM_BUILD_DIR:-$HOME/llvm-project/build-mlir}"
PYC_REPO_ROOT="${PYC_REPO_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
PYC_BUILD_DIR="${PYC_BUILD_DIR:-$PYC_REPO_ROOT/compiler/mlir/build}"

if [[ ! -d "$LLVM_PROJECT_DIR/llvm" ]]; then
  echo "error: LLVM_PROJECT_DIR does not look like llvm-project: $LLVM_PROJECT_DIR" >&2
  exit 1
fi

cmake -G Ninja -S "$LLVM_PROJECT_DIR/llvm" -B "$LLVM_BUILD_DIR" \
  -DLLVM_ENABLE_PROJECTS=mlir \
  -DLLVM_ENABLE_ASSERTIONS=ON \
  -DCMAKE_BUILD_TYPE=Release

ninja -C "$LLVM_BUILD_DIR" mlir-opt

cmake -G Ninja -S "$PYC_REPO_ROOT/compiler/mlir" -B "$PYC_BUILD_DIR" \
  -DMLIR_DIR="$LLVM_BUILD_DIR/lib/cmake/mlir" \
  -DLLVM_DIR="$LLVM_BUILD_DIR/lib/cmake/llvm"

ninja -C "$PYC_BUILD_DIR" pyc-opt pyc-compile

echo "Built:"
echo "  mlir-opt:    $LLVM_BUILD_DIR/bin/mlir-opt"
echo "  pyc-opt:     $PYC_BUILD_DIR/bin/pyc-opt"
echo "  pyc-compile: $PYC_BUILD_DIR/bin/pyc-compile"

