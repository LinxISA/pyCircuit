#!/usr/bin/env bash
set -euo pipefail

PYC_ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"

pyc_log() {
  echo "[pyc] $*"
}

pyc_warn() {
  echo "[pyc][warn] $*" >&2
}

pyc_die() {
  echo "[pyc][error] $*" >&2
  exit 1
}

pyc_find_pycc() {
  if [[ -n "${PYCC:-}" && -x "${PYCC}" ]]; then
    return 0
  fi

  local exe_suffix=""
  case "$(uname -s 2>/dev/null || true)" in
    MINGW*|MSYS*|CYGWIN*) exe_suffix=".exe";;
  esac

  local candidates=(
    # Preferred: current MLIR build dir.
    "${PYC_ROOT_DIR}/compiler/mlir/build2/bin/pycc${exe_suffix}"
    # Alternate build dirs still used in some local workflows.
    "${PYC_ROOT_DIR}/build/bin/pycc${exe_suffix}"
    "${PYC_ROOT_DIR}/compiler/mlir/build/bin/pycc${exe_suffix}"
    "${PYC_ROOT_DIR}/build-top/bin/pycc${exe_suffix}"
    # Also allow non-suffixed name in case the environment provides it.
    "${PYC_ROOT_DIR}/compiler/mlir/build2/bin/pycc"
    "${PYC_ROOT_DIR}/build/bin/pycc"
    "${PYC_ROOT_DIR}/compiler/mlir/build/bin/pycc"
    "${PYC_ROOT_DIR}/build-top/bin/pycc"
  )

  # Pick the newest executable among the common build locations. This avoids
  # accidentally grabbing an older `pycc` from a stale build directory.
  local best=""
  local best_mtime=0
  for c in "${candidates[@]}"; do
    if [[ -x "${c}" ]]; then
      local mtime=0
      if mtime="$(stat -f %m "${c}" 2>/dev/null)"; then
        :
      elif mtime="$(stat -c %Y "${c}" 2>/dev/null)"; then
        :
      else
        mtime=0
      fi
      if (( mtime > best_mtime )); then
        best="${c}"
        best_mtime="${mtime}"
      fi
    fi
  done
  if [[ -n "${best}" ]]; then
    export PYCC="${best}"
    return 0
  fi

  if command -v pycc >/dev/null 2>&1; then
    export PYCC
    PYCC="$(command -v pycc)"
    return 0
  fi

  if command -v pycc.exe >/dev/null 2>&1; then
    export PYCC
    PYCC="$(command -v pycc.exe)"
    return 0
  fi

  pyc_die "missing pycc (set PYCC=... or build it with: flows/scripts/pyc build)"
}

pyc_pythonpath() {
  # Prefer editable install (`pip install -e .`), but fall back to PYTHONPATH for
  # repo-local runs.
  echo "${PYC_ROOT_DIR}/compiler/frontend:${PYC_ROOT_DIR}/designs"
}

pyc_out_root() {
  echo "${PYC_ROOT_DIR}/.pycircuit_out"
}
