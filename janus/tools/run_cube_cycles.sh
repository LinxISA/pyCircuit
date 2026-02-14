#!/usr/bin/env bash
# Run Verilog simulation for cube cycle count test using Verilator
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

# Check for Verilator
VERILATOR="${VERILATOR:-$(command -v verilator || true)}"
if [[ -z "${VERILATOR}" ]]; then
  echo "error: missing verilator (install with: brew install verilator)" >&2
  exit 1
fi

# Paths
GEN_DIR="${ROOT_DIR}/janus/generated/janus_cube_pyc"
VLOG="${GEN_DIR}/janus_cube_pyc.v"
TB_SV="${ROOT_DIR}/janus/tb/tb_cube_cycles.sv"

# Regenerate if needed
if [[ ! -f "${VLOG}" ]]; then
  echo "[cube-cycles] Generating Verilog..."
  bash "${ROOT_DIR}/janus/update_generated.sh"
fi

# Create work directory
WORK_DIR="$(mktemp -d -t cube_cycles_verilator.XXXXXX)"
trap 'rm -rf "${WORK_DIR}"' EXIT

echo "[cube-cycles] Compiling with Verilator..."
"${VERILATOR}" --binary --timing \
  -I"${ROOT_DIR}/include/pyc/verilog" \
  -Wno-fatal \
  --top-module tb_cube_cycles \
  -o "${WORK_DIR}/Vtb_cube_cycles" \
  "${TB_SV}" \
  "${VLOG}"

echo "[cube-cycles] Running simulation..."
"${WORK_DIR}/Vtb_cube_cycles" "$@"
