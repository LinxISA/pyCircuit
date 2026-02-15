#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/../../.." && pwd)"
# shellcheck source=../../flows/scripts/lib.sh
source "${ROOT_DIR}/flows/scripts/lib.sh"
pyc_find_pyc_compile

IVERILOG="${IVERILOG:-$(command -v iverilog || true)}"
VVP="${VVP:-$(command -v vvp || true)}"
if [[ -z "${IVERILOG}" ]]; then
  echo "error: missing iverilog (install with: brew install icarus-verilog)" >&2
  exit 1
fi
if [[ -z "${VVP}" ]]; then
  echo "error: missing vvp (install with: brew install icarus-verilog)" >&2
  exit 1
fi

GEN_DIR="${ROOT_DIR}/.pycircuit_out/janus/janus_bcc_ooo_pyc"
VLOG="${GEN_DIR}/janus_bcc_ooo_pyc.v"
if [[ ! -f "${VLOG}" ]]; then
  bash "${ROOT_DIR}/designs/janus/update_generated.sh"
fi

TB_SV="${ROOT_DIR}/designs/janus/tb/tb_janus_bcc_ooo_pyc.sv"
WORK_DIR="$(mktemp -d -t janus_bcc_ooo_pyc_sv.XXXXXX)"
trap 'rm -rf "${WORK_DIR}"' EXIT

"${IVERILOG}" -g2012 \
  -I "${ROOT_DIR}/runtime/verilog" \
  -o "${WORK_DIR}/tb_janus_bcc_ooo_pyc.vvp" \
  "${TB_SV}" \
  "${VLOG}"

run_case() {
  local name="$1"
  shift
  echo "[janus-sv] ${name}"
  "${VVP}" "${WORK_DIR}/tb_janus_bcc_ooo_pyc.vvp" "$@"
}

if [[ $# -gt 0 ]]; then
  run_case "program" "$@"
  exit 0
fi

run_case "test_csel_fixed" \
  +memh="${ROOT_DIR}/designs/janus/programs/test_csel_fixed.memh" \
  +expected_mem100=64

run_case "test_or" \
  +memh="${ROOT_DIR}/designs/janus/programs/test_or.memh" \
  +expected_mem100=0000ff00

run_case "test_store100_llvm" \
  +memh="${ROOT_DIR}/designs/janus/programs/test_store100_llvm.memh" \
  +expected_mem100=64

run_case "test_branch2" \
  +memh="${ROOT_DIR}/designs/janus/programs/test_branch2.memh" \
  +boot_pc=000000000001000a \
  +expected_a0=8

run_case "test_call_simple" \
  +memh="${ROOT_DIR}/designs/janus/programs/test_call_simple.memh" \
  +boot_pc=000000000001001c \
  +expected_a0=2a

run_case "test_jump" \
  +memh="${ROOT_DIR}/designs/janus/programs/test_jump.memh" \
  +expected_a0=2a

run_case "test_pcrel" \
  +memh="${ROOT_DIR}/designs/janus/programs/test_pcrel.memh" \
  +expected_a0=2b
