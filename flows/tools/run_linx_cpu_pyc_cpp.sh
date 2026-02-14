#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
# shellcheck source=../flows/scripts/lib.sh
source "${ROOT_DIR}/flows/scripts/lib.sh"
pyc_find_pyc_compile

# Fast-run default: traces are opt-in.
export PYC_KONATA="${PYC_KONATA:-0}"

MEMH=""
ELF=""
EXPECTED=""
ELF_TEXT_BASE="0x10000"
ELF_DATA_BASE="0x20000"
ELF_PAGE_ALIGN="0x1000"
PYC_BUILD_PROFILE="${PYC_BUILD_PROFILE:-release}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --memh)
      MEMH="${2:?missing value for --memh}"
      shift 2
      ;;
    --elf)
      ELF="${2:?missing value for --elf}"
      shift 2
      ;;
    --expected)
      EXPECTED="${2:?missing value for --expected}"
      shift 2
      ;;
    --base)
      ELF_TEXT_BASE="${2:?missing value for --base}"
      shift 2
      ;;
    --text-base)
      ELF_TEXT_BASE="${2:?missing value for --text-base}"
      shift 2
      ;;
    --data-base)
      ELF_DATA_BASE="${2:?missing value for --data-base}"
      shift 2
      ;;
    --page-align)
      ELF_PAGE_ALIGN="${2:?missing value for --page-align}"
      shift 2
      ;;
    -h|--help)
      cat <<EOF
Usage:
  $0                     # run built-in regression memh tests
  $0 --memh <file> [--expected <hex>]   # run one memh program
  $0 --elf  <file> [--expected <hex>]   # convert ELF -> memh (apply relocs, load .data/.bss) and run

ELF options:
  --base <addr>       Alias for --text-base (default: 0x10000)
  --text-base <addr>  (default: 0x10000)
  --data-base <addr>  (default: 0x20000)
  --page-align <addr> (default: 0x1000)
EOF
      exit 0
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

TMP_DIR="$(mktemp -d -t linx_cpu_pyc.XXXXXX)"
trap 'rm -rf "${TMP_DIR}"' EXIT

cd "${ROOT_DIR}"

GEN_DIR="$(pyc_out_root)/examples/linx_cpu_pyc"
mkdir -p "${GEN_DIR}"
PYC_PATH="${GEN_DIR}/linx_cpu_pyc.pyc"
HDR="${GEN_DIR}/linx_cpu_pyc_gen.hpp"
KEY_FILE="${GEN_DIR}/linx_cpu_pyc.build_key"

EMIT_PARAMS=()

if [[ -n "${ELF}" ]]; then
  MEMH="${TMP_DIR}/program.memh"
  META="$(PYTHONDONTWRITEBYTECODE=1 python3 flows/tools/linxisa/elf_to_memh.py "${ELF}" --text-base "${ELF_TEXT_BASE}" --data-base "${ELF_DATA_BASE}" --page-align "${ELF_PAGE_ALIGN}" -o "${MEMH}" --print-start --print-max)"
  START_PC="$(printf "%s\n" "${META}" | sed -n '1p')"
  MAX_END="$(printf "%s\n" "${META}" | sed -n '2p')"
  if [[ -z "${PYC_BOOT_PC:-}" ]]; then
    export PYC_BOOT_PC="${START_PC}"
  fi
  if [[ -z "${PYC_MEM_BYTES:-}" ]]; then
    MEM_BYTES="$(
      PYTHONDONTWRITEBYTECODE=1 python3 - "${MAX_END}" <<'PY'
import sys

end = int(sys.argv[1], 0)
min_size = 1 << 20
size = min_size
while size < end:
    size <<= 1
print(size)
PY
    )"
    export PYC_MEM_BYTES="${MEM_BYTES}"
  fi
fi

if [[ -n "${MEMH}" ]]; then
  # If the user didn't provide a memory size, compute the minimum power-of-two
  # depth that covers the memh image.
  if [[ -z "${PYC_MEM_BYTES:-}" ]]; then
    MAX_END="$(
      PYTHONDONTWRITEBYTECODE=1 python3 - "${MEMH}" <<'PY'
import sys

path = sys.argv[1]
max_end = 0
addr = 0
with open(path, "r", encoding="utf-8") as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        if line[0] == "@":
            addr = int(line[1:], 16)
            if addr > max_end:
                max_end = addr
            continue
        # One byte token per line.
        addr += 1
        if addr > max_end:
            max_end = addr
print(hex(max_end))
PY
    )"
    MEM_BYTES="$(
      PYTHONDONTWRITEBYTECODE=1 python3 - "${MAX_END}" <<'PY'
import sys

end = int(sys.argv[1], 0)
min_size = 1 << 20
size = min_size
while size < end:
    size <<= 1
print(size)
PY
    )"
    export PYC_MEM_BYTES="${MEM_BYTES}"
  fi

  # Default the boot SP to the top of the modeled RAM so stack doesn't overlap
  # large .bss reservations used by benchmarks.
  if [[ -z "${PYC_BOOT_SP:-}" ]]; then
    export PYC_BOOT_SP="$(
      PYTHONDONTWRITEBYTECODE=1 python3 - "${PYC_MEM_BYTES}" <<'PY'
import sys

mem = int(sys.argv[1], 0)
print(hex(max(0, mem - 0x100)))
PY
    )"
  fi

  # Benchmarks can run longer than tiny bring-up tests. Allow override.
  if [[ -z "${PYC_MAX_CYCLES:-}" ]]; then
    export PYC_MAX_CYCLES="10000000"
  fi

  EMIT_PARAMS+=(--param "mem_bytes=${PYC_MEM_BYTES}")
fi

PYC_LOGIC_DEPTH="${PYC_LOGIC_DEPTH:-1024}"
SIM_MODE="cpp-only"
BUILD_KEY="logic_depth=${PYC_LOGIC_DEPTH};sim_mode=${SIM_MODE};mem_bytes=${PYC_MEM_BYTES:-default}"

need_regen=0
if [[ ! -f "${HDR}" || ! -f "${KEY_FILE}" ]]; then
  need_regen=1
elif [[ "$(cat "${KEY_FILE}")" != "${BUILD_KEY}" ]]; then
  need_regen=1
elif find "${ROOT_DIR}/designs/examples/linx_cpu_pyc" -name '*.py' -newer "${HDR}" | grep -q .; then
  need_regen=1
fi

if [[ "${need_regen}" -ne 0 ]]; then
  PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="$(pyc_pythonpath)" \
    python3 -m pycircuit.cli emit ${EMIT_PARAMS[@]+"${EMIT_PARAMS[@]}"} \
    designs/examples/linx_cpu_pyc/linx_cpu_pyc.py -o "${PYC_PATH}"

  "${PYC_COMPILE}" "${PYC_PATH}" --emit=cpp --sim-mode="${SIM_MODE}" \
    --logic-depth "${PYC_LOGIC_DEPTH}" -o "${HDR}"
  printf '%s\n' "${BUILD_KEY}" > "${KEY_FILE}"
fi

CXXFLAGS=(-std=c++17)
case "${PYC_BUILD_PROFILE}" in
  dev)
    CXXFLAGS+=(-O1)
    ;;
  release)
    CXXFLAGS+=(-O2 -DNDEBUG)
    ;;
  *)
    echo "error: unknown PYC_BUILD_PROFILE=${PYC_BUILD_PROFILE} (expected: dev|release)" >&2
    exit 2
    ;;
esac

TB_SRC="${ROOT_DIR}/designs/examples/linx_cpu_pyc/tb_linx_cpu_pyc.cpp"
TB_EXE="${GEN_DIR}/tb_linx_cpu_pyc_cpp_${PYC_BUILD_PROFILE}"

need_build=0
if [[ ! -x "${TB_EXE}" ]]; then
  need_build=1
elif [[ "${TB_SRC}" -nt "${TB_EXE}" || "${HDR}" -nt "${TB_EXE}" ]]; then
  need_build=1
elif find "${ROOT_DIR}/runtime/cpp" -name '*.hpp' -newer "${TB_EXE}" | grep -q .; then
  need_build=1
fi

if [[ "${need_build}" -ne 0 ]]; then
  tmp_exe="${TB_EXE}.tmp.$$"
  "${CXX:-clang++}" "${CXXFLAGS[@]}" \
    -I "${ROOT_DIR}/runtime" \
    -I "${GEN_DIR}" \
    -o "${tmp_exe}" \
    "${TB_SRC}"
  mv -f "${tmp_exe}" "${TB_EXE}"
fi

if [[ -n "${MEMH}" ]]; then
  if [[ -n "${EXPECTED}" ]]; then
    "${TB_EXE}" "${MEMH}" "${EXPECTED}"
  else
    "${TB_EXE}" "${MEMH}"
  fi
else
  "${TB_EXE}"
fi
