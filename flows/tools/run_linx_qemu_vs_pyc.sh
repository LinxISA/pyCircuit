#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LINX_ROOT="$(cd "${ROOT}/../.." && pwd)"

DEFAULT_SRC="$LINX_ROOT/emulator/qemu/tests/linxisa/mcopy_mset_basic.s"
if [[ ! -f "$DEFAULT_SRC" ]]; then
  DEFAULT_SRC="$ROOT/../qemu/tests/linxisa/mcopy_mset_basic.s"
fi
SRC="${1:-$DEFAULT_SRC}"

LLVM_BUILD="${LLVM_BUILD:-$HOME/llvm-project/build-linxisa-clang}"
LLVM_MC="${LLVM_MC:-$LLVM_BUILD/bin/llvm-mc}"

QEMU_BIN="${QEMU_BIN:-/Users/zhoubot/qemu/build/qemu-system-linx64}"

WORK="$(mktemp -d "${TMPDIR:-/tmp}/linx-diff.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

OBJ="$WORK/test.o"
QEMU_TRACE="$WORK/qemu.jsonl"
PYC_TRACE="$WORK/pyc.jsonl"
TRACE_SCHEMA_VERSION="${LINX_TRACE_SCHEMA_VERSION:-1.0}"

if [[ ! -x "$LLVM_MC" ]]; then
  echo "error: llvm-mc not found: $LLVM_MC" >&2
  exit 2
fi
if [[ ! -x "$QEMU_BIN" ]]; then
  echo "error: qemu-system-linx64 not found: $QEMU_BIN" >&2
  exit 2
fi
if [[ ! -f "$SRC" ]]; then
  echo "error: missing source: $SRC" >&2
  exit 2
fi

echo "[llvm-mc] $SRC"
"$LLVM_MC" -triple=linx64 -filetype=obj "$SRC" -o "$OBJ"

echo "[qemu] commit trace: $QEMU_TRACE"
LINX_COMMIT_TRACE="$QEMU_TRACE" "$QEMU_BIN" -nographic -monitor none -machine virt -kernel "$OBJ" >/dev/null

echo "[pyc] commit trace: $PYC_TRACE"
PYC_KONATA=0 PYC_EXPECT_EXIT=0 PYC_BOOT_PC=0x10000 PYC_COMMIT_TRACE="$PYC_TRACE" \
  bash "$ROOT/flows/tools/run_linx_cpu_pyc_cpp.sh" --elf "$OBJ" >/dev/null

if [[ -f "$LINX_ROOT/tools/bringup/validate_trace_schema.py" ]]; then
  echo "[schema] qemu trace"
  python3 "$LINX_ROOT/tools/bringup/validate_trace_schema.py" \
    --trace "$QEMU_TRACE" \
    --expected-version "$TRACE_SCHEMA_VERSION" \
    --assume-trace-version "${LINX_TRACE_ASSUMED_VERSION:-1.0}"
  echo "[schema] pyc trace"
  python3 "$LINX_ROOT/tools/bringup/validate_trace_schema.py" \
    --trace "$PYC_TRACE" \
    --expected-version "$TRACE_SCHEMA_VERSION" \
    --assume-trace-version "${LINX_TRACE_ASSUMED_VERSION:-1.0}"
fi

echo "[diff]"
python3 "$ROOT/flows/tools/linx_trace_diff.py" "$QEMU_TRACE" "$PYC_TRACE" --ignore cycle
