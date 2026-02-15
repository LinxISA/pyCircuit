#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/../../.." && pwd)"
# shellcheck source=../../flows/scripts/lib.sh
source "${ROOT_DIR}/flows/scripts/lib.sh"
pyc_find_pyc_compile

OUT_ROOT="${ROOT_DIR}/.pycircuit_out/janus/janus_tmu_pyc"
mkdir -p "${OUT_ROOT}"

tmp_pyc="$(mktemp -t "pycircuit.janus.tmu.XXXXXX.pyc")"

PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="$(pyc_pythonpath):${ROOT_DIR}/designs/janus/pyc" \
  python3 -m pycircuit.cli emit "${ROOT_DIR}/designs/janus/pyc/janus/tmu/janus_tmu_pyc.py" -o "${tmp_pyc}"

"${PYC_COMPILE}" "${tmp_pyc}" --emit=verilog -o "${OUT_ROOT}/janus_tmu_pyc.v"
"${PYC_COMPILE}" "${tmp_pyc}" --emit=cpp -o "${OUT_ROOT}/janus_tmu_pyc.hpp"

mv -f "${OUT_ROOT}/janus_tmu_pyc.hpp" "${OUT_ROOT}/janus_tmu_pyc_gen.hpp"

pyc_log "ok: wrote TMU outputs under ${OUT_ROOT}"
