#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
PYC_COMPILE="${ROOT_DIR}/pyc/mlir/build/bin/pyc-compile"

if [[ ! -x "${PYC_COMPILE}" ]]; then
  echo "error: missing ${PYC_COMPILE}" >&2
  echo "Build MLIR tools first (see ${ROOT_DIR}/README.md)." >&2
  exit 1
fi

OUT_ROOT="${ROOT_DIR}/examples/generated"
mkdir -p "${OUT_ROOT}"

emit_one() {
  local name="$1"
  local src="$2"
  local outdir="${OUT_ROOT}/${name}"

  mkdir -p "${outdir}"
  echo "[emit] ${name}: ${src}"

  # Emit MLIR to a temp file (keep repo-clean: only check in .sv/.hpp outputs).
  local tmp_pyc
  tmp_pyc="$(mktemp -t "pycircuit.${name}.pyc")"

  PYTHONPATH="${ROOT_DIR}/binding/python" python3 -m pycircuit.cli emit "${src}" -o "${tmp_pyc}"
  "${PYC_COMPILE}" "${tmp_pyc}" --emit=verilog -o "${outdir}/${name}.sv"
  "${PYC_COMPILE}" "${tmp_pyc}" --emit=cpp -o "${outdir}/${name}.hpp"
}

# Python examples.
emit_one counter "${ROOT_DIR}/examples/counter.py"
emit_one fifo_loopback "${ROOT_DIR}/examples/fifo_loopback.py"
emit_one issue_queue_2picker "${ROOT_DIR}/examples/issue_queue_2picker.py"
emit_one multiclock_regs "${ROOT_DIR}/examples/multiclock_regs.py"
emit_one wire_ops "${ROOT_DIR}/examples/wire_ops.py"
emit_one jit_control_flow "${ROOT_DIR}/examples/jit_control_flow.py"
emit_one jit_pipeline_vec "${ROOT_DIR}/examples/jit_pipeline_vec.py"
emit_one jit_cache "${ROOT_DIR}/examples/jit_cache.py"

# LinxISA CPU (pyCircuit).
emit_one linx_cpu_pyc "${ROOT_DIR}/examples/linx_cpu_pyc/linx_cpu_pyc.py"
mv -f "${OUT_ROOT}/linx_cpu_pyc/linx_cpu_pyc.hpp" "${OUT_ROOT}/linx_cpu_pyc/linx_cpu_pyc_gen.hpp"

echo "OK: wrote outputs under ${OUT_ROOT}"
