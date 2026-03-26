#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PYC_ROOT="$(cd "${ROOT}/../.." && pwd)"

find_linx_root() {
  local cand
  if [[ -n "${LINX_ROOT:-}" && -d "${LINX_ROOT}/tools/bringup" ]]; then
    echo "${LINX_ROOT}"
    return 0
  fi
  for cand in \
    "$(cd "${ROOT}/../../../.." && pwd)" \
    "$(cd "${PYC_ROOT}/.." && pwd)" \
    "/Users/zhoubot/linx-isa"
  do
    if [[ -d "${cand}/tools/bringup" ]]; then
      echo "${cand}"
      return 0
    fi
  done
  return 1
}

LINX_ROOT="$(find_linx_root)" || {
  echo "error: unable to resolve linx-isa superproject root" >&2
  exit 2
}

find_linxcore_root() {
  local cand
  if [[ -n "${LINXCORE_ROOT:-}" && -d "${LINXCORE_ROOT}/generated/cpp/linxcore_top" ]]; then
    echo "${LINXCORE_ROOT}"
    return 0
  fi
  for cand in \
    "${LINX_ROOT}/rtl/LinxCore" \
    "/Users/zhoubot/LinxCore"
  do
    if [[ -d "${cand}/generated/cpp/linxcore_top" ]]; then
      echo "${cand}"
      return 0
    fi
  done
  return 1
}

LINXCORE_ROOT="$(find_linxcore_root)" || {
  echo "error: unable to resolve LinxCore root" >&2
  exit 2
}

DEFAULT_SRC=""
for cand in \
  "/Users/zhoubot/linx-isa/emulator/qemu/tests/linxisa/mcopy_mset_basic.s" \
  "$LINX_ROOT/emulator/qemu/tests/linxisa/mcopy_mset_basic.s" \
  "$ROOT/../qemu/tests/linxisa/mcopy_mset_basic.s"
do
  if [[ -f "$cand" ]]; then
    DEFAULT_SRC="$cand"
    break
  fi
done
SRC="${1:-$DEFAULT_SRC}"

LLVM_BUILD="${LLVM_BUILD:-${LINX_ROOT}/compiler/llvm/build-linxisa-clang}"
LLVM_MC="${LLVM_MC:-$LLVM_BUILD/bin/llvm-mc}"

QEMU_BIN="${QEMU_BIN:-}"
if [[ -z "$QEMU_BIN" ]]; then
  for cand in \
    "/Users/zhoubot/linx-isa/emulator/qemu/build/qemu-system-linx64" \
    "$LINX_ROOT/emulator/qemu/build/qemu-system-linx64"
  do
    if [[ -x "$cand" ]]; then
      QEMU_BIN="$cand"
      break
    fi
  done
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/linx-diff.XXXXXX")"
KEEP_WORK=0
cleanup() {
  if [[ "${KEEP_WORK}" == "0" ]]; then
    rm -rf "${WORK}"
  else
    echo "[artifact] kept work dir: ${WORK}" >&2
  fi
}
trap cleanup EXIT
FALLBACK_USED=0

OBJ="$WORK/test.o"
QEMU_TRACE="$WORK/qemu.jsonl"
PYC_TRACE="$WORK/pyc.jsonl"
TRACE_SCHEMA_VERSION="${LINX_TRACE_SCHEMA_VERSION:-1.0}"
COMMIT_SCHEMA_ID="${LINX_COMMIT_SCHEMA_ID:-LC-COMMIT-BUNDLE-V2}"
DFX_DUMP_DIR="${LINX_DIFF_DFX_DUMP_DIR:-$WORK/dfx_dump}"
DFX_PRE="${LINX_DIFF_DFX_PRE:-8}"
DFX_POST="${LINX_DIFF_DFX_POST:-16}"
REQUIRE_SCHEMA_ID="${LINX_REQUIRE_COMMIT_SCHEMA_ID:-0}"

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
if [[ ! -s "$PYC_TRACE" ]]; then
  echo "[pyc] primary flow did not emit commit trace; trying LinxCore fallback"
  FALLBACK_USED=1
  MEMH="$WORK/test.memh"
  python3 "$ROOT/flows/tools/linxisa/elf_to_memh.py" \
    "$OBJ" \
    --text-base 0x10000 \
    --data-base 0x20000 \
    --page-align 0x1000 \
    -o "$MEMH" >/dev/null
  LINXCORE_TB="${LINXCORE_TB:-$LINXCORE_ROOT/generated/cpp/linxcore_top/tb_linxcore_top_cpp}"
  if [[ -x "$LINXCORE_TB" ]]; then
    PYC_KONATA=0 \
    PYC_EXPECT_EXIT=0 \
    PYC_BOOT_PC=0x10000 \
    PYC_BOOT_SP=0x0000000007fefff0 \
    PYC_MAX_CYCLES="${LINXCORE_FALLBACK_MAX_CYCLES:-5000}" \
    PYC_COMMIT_TRACE="$PYC_TRACE" \
      "$LINXCORE_TB" "$MEMH" >/dev/null 2>/dev/null || true
  fi
fi
if [[ ! -s "$PYC_TRACE" ]]; then
  echo "error: pyc trace was not produced: $PYC_TRACE" >&2
  exit 2
fi

echo "[schema] validate qemu trace"
python3 "$LINX_ROOT/tools/bringup/validate_trace_schema.py" \
  --trace "$QEMU_TRACE" \
  --expected-version "${TRACE_SCHEMA_VERSION}" \
  --assume-trace-version "${TRACE_SCHEMA_VERSION}" >/dev/null

echo "[schema] validate pyc trace"
python3 "$LINX_ROOT/tools/bringup/validate_trace_schema.py" \
  --trace "$PYC_TRACE" \
  --expected-version "${TRACE_SCHEMA_VERSION}" \
  --assume-trace-version "${TRACE_SCHEMA_VERSION}" >/dev/null

echo "[diff]"
DIFF_ARGS=(
  --ignore cycle
  --assume-schema-id "${COMMIT_SCHEMA_ID}"
  --expected-schema-id "${COMMIT_SCHEMA_ID}"
  --dump-dir "${DFX_DUMP_DIR}"
  --dump-pre "${DFX_PRE}"
  --dump-post "${DFX_POST}"
)
if [[ "${REQUIRE_SCHEMA_ID}" != "0" ]]; then
  DIFF_ARGS+=(--require-schema-id)
fi
if [[ "$FALLBACK_USED" == "1" ]]; then
  PREFIX_LIMIT="${LINX_QEMU_VS_PYC_FALLBACK_PREFIX:-6}"
  echo "[diff] using fallback-prefix mode (limit=${PREFIX_LIMIT})"
  DIFF_ARGS+=(--limit "$PREFIX_LIMIT")
fi
set +e
python3 "$ROOT/flows/tools/linx_trace_diff.py" "$QEMU_TRACE" "$PYC_TRACE" "${DIFF_ARGS[@]}"
rc=$?
set -e
if [[ "${rc}" -ne 0 ]]; then
  KEEP_WORK=1
  PYC_ROOT="$(cd "${ROOT}/../.." && pwd)"
  OUT_BASE="${PYC_ROOT}/.pycircuit_out/linx_diff"
  RUN_ID="$(date +%Y%m%d_%H%M%S)_$$"
  OUT_DIR="${OUT_BASE}/${RUN_ID}"
  mkdir -p "${OUT_DIR}"
  cp -f "${QEMU_TRACE}" "${OUT_DIR}/qemu.jsonl" 2>/dev/null || true
  cp -f "${PYC_TRACE}" "${OUT_DIR}/pyc.jsonl" 2>/dev/null || true
  if [[ -d "${DFX_DUMP_DIR}" ]]; then
    cp -R "${DFX_DUMP_DIR}" "${OUT_DIR}/dfx_dump" 2>/dev/null || true
  fi
  echo "[diff] mismatch artifacts: ${OUT_DIR}" >&2
  exit "${rc}"
fi
