#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
export PE_INT="$ROOT"

export PATH="$HOME/.local/bin:$PATH"

mkdir -p build

RTL_FILELIST_SRC="$ROOT/filelist/pe_int.f"
TB_FILELIST_SRC="$ROOT/tb_rtl/tb.f"
MODEL_FILELIST_SRC="$ROOT/model/model.f"

RTL_FILELIST="build/.pe_int.resolved.f"
TB_FILELIST="build/.tb.resolved.f"
MODEL_FILELIST="build/.model.resolved.f"

sed "s|\$PE_INT|${PE_INT}|g" "$RTL_FILELIST_SRC" > "$RTL_FILELIST"
sed "s|\$PE_INT|${PE_INT}|g" "$TB_FILELIST_SRC" > "$TB_FILELIST"
sed "s|\$PE_INT|${PE_INT}|g" "$MODEL_FILELIST_SRC" > "$MODEL_FILELIST"

MODEL_GEN_SCRIPT="$(awk '{ gsub(/\r$/, "", $0); if ($0 ~ /gen_rtl_case_vectors\.py$/) { print; exit } }' "$MODEL_FILELIST")"
if [[ -z "${MODEL_GEN_SCRIPT}" ]]; then
  echo "[ERR] Could not find gen_rtl_case_vectors.py in model/model.f"
  exit 1
fi

echo "[INFO] Regenerating testcase vectors from model/ ..."
python3 "$MODEL_GEN_SCRIPT"

CASES=(
  "tc_mode2a_sanity"
  "tc_mode2b_sanity"
  "tc_mode2c_sanity"
  "tc_mode2d_sanity"
  "tc_mode_switch_random"
)

echo "[INFO] Running RTL cases with iverilog..."
for c in "${CASES[@]}"; do
  iverilog -g2012 -s "$c" -f "$RTL_FILELIST" -f "$TB_FILELIST" -o "build/${c}.iv.out"
  vvp "build/${c}.iv.out"
done

echo "[INFO] Running RTL cases with verilator..."
for c in "${CASES[@]}"; do
  verilator --binary --timing -Wall -Wno-fatal -f "$RTL_FILELIST" -f "$TB_FILELIST" --top-module "$c" -o "${c}.vlt.out"
  "./obj_dir/${c}.vlt.out"
done

echo "[PASS] all tb_rtl cases passed on iverilog + verilator."
