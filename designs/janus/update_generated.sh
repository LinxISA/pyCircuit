#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=../flows/scripts/lib.sh
source "${ROOT_DIR}/flows/scripts/lib.sh"
pyc_find_pyc_compile

OUT_ROOT="$(pyc_out_root)/janus"
mkdir -p "${OUT_ROOT}"

emit_one() {
  local name="$1"
  local src="$2"
  local logic_depth="${3:-}"
  local outdir="${OUT_ROOT}/${name}"

  mkdir -p "${outdir}"
  pyc_log "emit ${name}: ${src}"

  local tmp_pyc
  tmp_pyc="$(mktemp -t "pycircuit.janus.${name}.pyc")"

  PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="$(pyc_pythonpath):${ROOT_DIR}/designs/janus/pyc" \
    python3 -m pycircuit.cli emit "${src}" -o "${tmp_pyc}"

  if [[ -n "${logic_depth}" ]]; then
    "${PYC_COMPILE}" "${tmp_pyc}" --logic-depth "${logic_depth}" --emit=verilog -o "${outdir}/${name}.v"
    "${PYC_COMPILE}" "${tmp_pyc}" --logic-depth "${logic_depth}" --emit=cpp -o "${outdir}/${name}.hpp"
  else
    "${PYC_COMPILE}" "${tmp_pyc}" --emit=verilog -o "${outdir}/${name}.v"
    "${PYC_COMPILE}" "${tmp_pyc}" --emit=cpp -o "${outdir}/${name}.hpp"
  fi
}

emit_one janus_bcc_pyc "${ROOT_DIR}/designs/janus/pyc/janus/bcc/janus_bcc_pyc.py"
emit_one janus_bcc_ooo_pyc "${ROOT_DIR}/designs/janus/pyc/janus/bcc/janus_bcc_ooo_pyc.py"
emit_one janus_top_pyc "${ROOT_DIR}/designs/janus/pyc/janus/top.py"
emit_one janus_cube_pyc "${ROOT_DIR}/designs/janus/pyc/janus/cube/cube_v2_reuse.py" "${JANUS_CUBE_LOGIC_DEPTH:-96}"

mv -f "${OUT_ROOT}/janus_bcc_pyc/janus_bcc_pyc.hpp" "${OUT_ROOT}/janus_bcc_pyc/janus_bcc_pyc_gen.hpp"
mv -f "${OUT_ROOT}/janus_bcc_ooo_pyc/janus_bcc_ooo_pyc.hpp" "${OUT_ROOT}/janus_bcc_ooo_pyc/janus_bcc_ooo_pyc_gen.hpp"
mv -f "${OUT_ROOT}/janus_top_pyc/janus_top_pyc.hpp" "${OUT_ROOT}/janus_top_pyc/janus_top_pyc_gen.hpp"
mv -f "${OUT_ROOT}/janus_cube_pyc/janus_cube_pyc.hpp" "${OUT_ROOT}/janus_cube_pyc/janus_cube_pyc_gen.hpp"

pyc_log "ok: wrote outputs under ${OUT_ROOT}"
