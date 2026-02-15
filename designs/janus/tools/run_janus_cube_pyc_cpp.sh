#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/../../.." && pwd)"
# shellcheck source=../../flows/scripts/lib.sh
source "${ROOT_DIR}/flows/scripts/lib.sh"
pyc_find_pyc_compile

GEN_DIR="${ROOT_DIR}/.pycircuit_out/janus/janus_cube_pyc"
HDR="${GEN_DIR}/janus_cube_pyc_gen.hpp"
if [[ ! -f "${HDR}" ]]; then
  bash "${ROOT_DIR}/designs/janus/update_generated.sh"
fi

WORK_DIR="$(mktemp -d -t janus_cube_pyc_tb.XXXXXX)"
trap 'rm -rf "${WORK_DIR}"' EXIT

"${CXX:-clang++}" -std=c++17 -O2 \
  -I "${ROOT_DIR}/runtime" \
  -I "${GEN_DIR}" \
  -o "${WORK_DIR}/tb_janus_cube_pyc" \
  "${ROOT_DIR}/designs/janus/tb/tb_janus_cube_pyc.cpp"

if [[ $# -gt 0 ]]; then
  "${WORK_DIR}/tb_janus_cube_pyc" "$@"
else
  "${WORK_DIR}/tb_janus_cube_pyc"
fi
