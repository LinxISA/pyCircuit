#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/../../.." && pwd)"
# shellcheck source=../../flows/scripts/lib.sh
source "${ROOT_DIR}/flows/scripts/lib.sh"
pyc_find_pyc_compile

# Fast-run default: traces are opt-in.
export PYC_KONATA="${PYC_KONATA:-0}"
PYC_BUILD_PROFILE="${PYC_BUILD_PROFILE:-release}"

GEN_DIR="$(pyc_out_root)/janus/janus_bcc_ooo_pyc"
HDR="${GEN_DIR}/janus_bcc_ooo_pyc_gen.hpp"

need_regen=0
if [[ ! -f "${HDR}" ]]; then
  need_regen=1
elif find "${ROOT_DIR}/designs/janus/pyc/janus" -name '*.py' -newer "${HDR}" | grep -q .; then
  need_regen=1
elif ! grep -q "dispatch_fire0" "${HDR}" || ! grep -q "dispatch_pc0" "${HDR}" || ! grep -q "dispatch_rob0" "${HDR}" || ! grep -q "dispatch_op0" "${HDR}" || \
     ! grep -q "issue_fire0" "${HDR}" || ! grep -q "issue_pc0" "${HDR}" || ! grep -q "issue_rob0" "${HDR}" || ! grep -q "issue_op0" "${HDR}"; then
  # Header is older than the current TB's trace hooks; regenerate.
  need_regen=1
fi

if [[ "${need_regen}" -ne 0 ]]; then
  bash "${ROOT_DIR}/designs/janus/update_generated.sh"
fi

TB_SRC="${ROOT_DIR}/designs/janus/tb/tb_janus_bcc_ooo_pyc.cpp"
TB_EXE="${GEN_DIR}/tb_janus_bcc_ooo_pyc_cpp_${PYC_BUILD_PROFILE}"

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

need_build=0
if [[ ! -x "${TB_EXE}" ]]; then
  need_build=1
elif [[ "${TB_SRC}" -nt "${TB_EXE}" || "${HDR}" -nt "${TB_EXE}" ]]; then
  need_build=1
elif find "${ROOT_DIR}/runtime/cpp" -name '*.hpp' -newer "${TB_EXE}" | grep -q .; then
  need_build=1
fi

if [[ "${need_build}" -ne 0 ]]; then
  mkdir -p "${GEN_DIR}"
  tmp_exe="${TB_EXE}.tmp.$$"
  "${CXX:-clang++}" "${CXXFLAGS[@]}" \
    -I "${ROOT_DIR}/runtime" \
    -I "${GEN_DIR}" \
    -o "${tmp_exe}" \
    "${TB_SRC}"
  mv -f "${tmp_exe}" "${TB_EXE}"
fi

if [[ $# -gt 0 ]]; then
  "${TB_EXE}" "$@"
else
  "${TB_EXE}"
fi
