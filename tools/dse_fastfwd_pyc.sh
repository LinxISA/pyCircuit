#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

SEED="${SEED:-1}"
CYCLES="${CYCLES:-20000}"
PACKETS="${PACKETS:-60000}"

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
    -h|--help)
      cat <<EOF
Usage:
  $0 [--seed N] [--cycles N] [--packets N]

This runs a small design-space exploration sweep for the FastFwd example by
varying JIT parameters that do NOT change the module port list.

Note:
  ENG_PER_LANE changes the number of FE ports and is intentionally not swept
  by this script (tb_fastfwd_pyc.cpp assumes the default interface).
EOF
      exit 0
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

cd "${ROOT_DIR}"

configs=(
  "LANE_Q_DEPTH=16 ENG_Q_DEPTH=8 ROB_DEPTH=16"
  "LANE_Q_DEPTH=32 ENG_Q_DEPTH=8 ROB_DEPTH=16"
  "LANE_Q_DEPTH=64 ENG_Q_DEPTH=8 ROB_DEPTH=16"
  "LANE_Q_DEPTH=32 ENG_Q_DEPTH=8 ROB_DEPTH=32"
  "LANE_Q_DEPTH=32 ENG_Q_DEPTH=16 ROB_DEPTH=16"
)

printf "%-36s  %10s  %10s  %10s\n" "config" "sent" "thr(pkt/cyc)" "bkpr(%)"
printf "%-36s  %10s  %10s  %10s\n" "------------------------------------" "----------" "----------" "----------"

for cfg in "${configs[@]}"; do
  params=()
  for kv in ${cfg}; do
    params+=(--param "${kv}")
  done

  out="$("${ROOT_DIR}/tools/run_fastfwd_pyc_cpp.sh" --seed "${SEED}" --cycles "${CYCLES}" --packets "${PACKETS}" "${params[@]}")"
  sent="$(echo "${out}" | sed -n 's/.*sent=\([0-9][0-9]*\).*/\1/p')"
  thr="$(echo "${out}" | sed -n 's/.*throughput=\([0-9][0-9]*\.[0-9][0-9]*\).*/\1/p')"
  bkpr="$(echo "${out}" | sed -n 's/.*bkpr=\([0-9][0-9]*\.[0-9][0-9]*\)%.*/\1/p')"

  printf "%-36s  %10s  %10s  %10s\n" "${cfg}" "${sent:-?}" "${thr:-?}" "${bkpr:-?}"
done
