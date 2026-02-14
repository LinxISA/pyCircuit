#!/usr/bin/env bash
# Run Verilog simulation for janus_cube_pyc using Verilator
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/../../.." && pwd)"
# shellcheck source=../../flows/scripts/lib.sh
source "${ROOT_DIR}/flows/scripts/lib.sh"
pyc_find_pyc_compile

# Check for Verilator
VERILATOR="${VERILATOR:-$(command -v verilator || true)}"
if [[ -z "${VERILATOR}" ]]; then
  echo "error: missing verilator (install with: brew install verilator)" >&2
  exit 1
fi

# Paths
GEN_DIR="${ROOT_DIR}/.pycircuit_out/janus/janus_cube_pyc"
VLOG="${GEN_DIR}/janus_cube_pyc.v"
TB_SV="${ROOT_DIR}/designs/janus/tb/tb_janus_cube_pyc.sv"

# Regenerate if needed
if [[ ! -f "${VLOG}" ]]; then
  echo "[cube-verilator] Generating Verilog..."
  bash "${ROOT_DIR}/designs/janus/update_generated.sh"
fi

# Create work directory
WORK_DIR="$(mktemp -d -t janus_cube_pyc_verilator.XXXXXX)"
trap 'rm -rf "${WORK_DIR}"' EXIT

echo "[cube-verilator] Compiling with Verilator..."
"${VERILATOR}" --binary --timing \
  -I"${ROOT_DIR}/runtime/verilog" \
  -Wno-fatal \
  --top-module tb_janus_cube_pyc \
  -o "${WORK_DIR}/Vtb_janus_cube_pyc" \
  "${TB_SV}" \
  "${VLOG}"

echo "[cube-verilator] Running simulation..."
"${WORK_DIR}/Vtb_janus_cube_pyc" "$@"
