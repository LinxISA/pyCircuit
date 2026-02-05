#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
# shellcheck source=../scripts/lib.sh
source "${ROOT_DIR}/scripts/lib.sh"
pyc_find_pyc_compile

SEED="${SEED:-1}"
CYCLES="${CYCLES:-20000}"
PACKETS="${PACKETS:-60000}"
PARAMS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --seed)
      SEED="${2:?missing value for --seed}"
      shift 2
      ;;
    --cycles)
      CYCLES="${2:?missing value for --cycles}"
      shift 2
      ;;
    --packets)
      PACKETS="${2:?missing value for --packets}"
      shift 2
      ;;
    --param)
      PARAMS+=("${2:?missing value for --param}")
      shift 2
      ;;
    -h|--help)
      cat <<EOF
Usage:
  $0 [--seed N] [--cycles N] [--packets N] [--param name=value]...

Env vars:
  SEED, CYCLES, PACKETS

Tracing:
  PYC_TRACE=1        write a text log
  PYC_VCD=1          write a VCD
  PYC_TRACE_DIR=...  output directory (default: examples/generated/fastfwd_pyc)
EOF
      exit 0
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

WORK_DIR="$(mktemp -d -t fastfwd_pyc.XXXXXX)"
trap 'rm -rf "${WORK_DIR}"' EXIT

cd "${ROOT_DIR}"

EMIT_ARGS=()
if (( ${#PARAMS[@]} )); then
  for p in "${PARAMS[@]}"; do
    if [[ "${p}" == ENG_PER_LANE=* ]]; then
      echo "error: --param ENG_PER_LANE changes the module port list; tb_fastfwd_pyc.cpp assumes the default (ENG_PER_LANE=2)" >&2
      exit 2
    fi
    EMIT_ARGS+=(--param "${p}")
  done
fi

emit_cmd=(python3 -m pycircuit.cli emit examples/fastfwd_pyc/fastfwd_pyc.py)
if (( ${#EMIT_ARGS[@]} )); then
  emit_cmd+=("${EMIT_ARGS[@]}")
fi
emit_cmd+=(-o "${WORK_DIR}/fastfwd_pyc.pyc")

PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="$(pyc_pythonpath)" "${emit_cmd[@]}"

"${PYC_COMPILE}" "${WORK_DIR}/fastfwd_pyc.pyc" --emit=cpp -o "${WORK_DIR}/fastfwd_pyc_gen.hpp"

"${CXX:-clang++}" -std=c++17 -O2 \
  -I "${ROOT_DIR}/include" \
  -I "${WORK_DIR}" \
  -o "${WORK_DIR}/tb_fastfwd_pyc" \
  "${ROOT_DIR}/examples/fastfwd_pyc/tb_fastfwd_pyc.cpp"

"${WORK_DIR}/tb_fastfwd_pyc" --seed "${SEED}" --cycles "${CYCLES}" --packets "${PACKETS}"
