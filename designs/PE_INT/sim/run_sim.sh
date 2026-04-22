#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
export PE_INT="$ROOT"
GEN_DIR="${ROOT}/tb_rtl/case/generated"

export PATH="$HOME/.local/bin:$PATH"
PROMPT_TIMEOUT_SEC=30

CASES=(
  "tc_mode2a_sanity"
  "tc_mode2b_sanity"
  "tc_mode2c_sanity"
  "tc_mode2d_sanity"
  "tc_mode_switch_random"
)

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

DEFAULT_SEED="$(date +%s)"
if [[ ! "$DEFAULT_SEED" =~ ^[0-9]+$ ]]; then
  DEFAULT_SEED="20260420"
fi

timed_prompt() {
  local __out_var="$1"
  local __title="$2"
  local __default="$3"
  local __timeout="${4:-$PROMPT_TIMEOUT_SEC}"
  local __input=""
  local __remain="$__timeout"
  local __last_remain=-1
  local __start_ts
  local __now_ts

  echo
  echo "${__title} (default: ${__default}, timeout ${__timeout}s):"

  # Non-interactive mode (e.g. piped input): keep predictable timeout behavior.
  if [[ ! -t 0 || ! -t 1 ]]; then
    printf "[INFO] 倒數 %2ds（輸入完成請按 Enter）> " "$__timeout"
    if IFS= read -r -t "$__timeout" __input; then
      :
    else
      echo
      echo "[INFO] timeout reached, use default: ${__default}"
      __input="$__default"
    fi
    if [[ -z "$__input" ]]; then
      __input="$__default"
    fi
    printf -v "$__out_var" "%s" "$__input"
    return 0
  fi

  # Interactive mode: dynamic countdown line is updated independently,
  # while user input remains on the next line without being overwritten.
  __start_ts="$(date +%s)"
  echo "[INFO] time left: ${__timeout}s"
  printf "> "
  while (( __remain > 0 )); do
    __now_ts="$(date +%s)"
    __remain=$((__timeout - (__now_ts - __start_ts)))
    if (( __remain < 0 )); then
      __remain=0
    fi

    if (( __remain != __last_remain )); then
      printf "\033[s\033[1A\r\033[2K[INFO] time left: %2ds\033[u" "$__remain"
      __last_remain="$__remain"
    fi

    # Read a full line for up to 1 second.
    # Pressing Enter immediately submits (empty line allowed) and skips countdown.
    if IFS= read -r -t 1 __input; then
      break
    fi
  done

  if (( __remain == 0 )) && [[ -z "$__input" ]]; then
    echo
    echo "[INFO] timeout reached, use default: ${__default}"
    __input="$__default"
  elif [[ -z "$__input" ]]; then
    __input="$__default"
  fi
  printf -v "$__out_var" "%s" "$__input"
  return 0
}

timed_prompt SEED_IN "Random seed for testcase vectors" "$DEFAULT_SEED"
if [[ "$SEED_IN" =~ ^[0-9]+$ ]]; then
  BASE_SEED="$SEED_IN"
else
  echo "[WARN] Invalid seed '$SEED_IN', fallback to default: ${DEFAULT_SEED}"
  BASE_SEED="$DEFAULT_SEED"
fi

DEFAULT_SEED_RUNS=10
timed_prompt SEED_RUNS_IN "How many different seeds to run?" "$DEFAULT_SEED_RUNS"
if [[ "$SEED_RUNS_IN" =~ ^[0-9]+$ ]] && [[ "$SEED_RUNS_IN" -ge 1 ]]; then
  SEED_RUNS="$SEED_RUNS_IN"
else
  echo "[WARN] Invalid seed-run count '$SEED_RUNS_IN', fallback to default: ${DEFAULT_SEED_RUNS}"
  SEED_RUNS="$DEFAULT_SEED_RUNS"
fi

timed_prompt SIM "Select simulator iverilog(i)/verilator(v), enter [i/v]" "i"
SIM="$(echo "$SIM" | tr '[:upper:]' '[:lower:]')"
if [[ "$SIM" == "i" ]]; then
  SIM="iverilog"
elif [[ "$SIM" == "v" ]]; then
  SIM="verilator"
fi
if [[ "$SIM" != "iverilog" && "$SIM" != "verilator" ]]; then
  echo "[WARN] Unknown simulator '$SIM', fallback to default: iverilog"
  SIM="iverilog"
fi

timed_prompt WAVE_ANS "Generate waveform? [y/N]" "n"
WAVE_ANS="$(echo "$WAVE_ANS" | tr '[:upper:]' '[:lower:]')"
WAVE_ON=0
if [[ "$WAVE_ANS" == "y" || "$WAVE_ANS" == "yes" ]]; then
  WAVE_ON=1
fi

WAVE_FMT="vcd"
if [[ "$WAVE_ON" -eq 1 ]]; then
  timed_prompt WAVE_FMT_ANS "Wave format vcd(v)/fst(f), enter [v/f]" "v"
  WAVE_FMT_ANS="$(echo "$WAVE_FMT_ANS" | tr '[:upper:]' '[:lower:]')"
  if [[ "$WAVE_FMT_ANS" == "v" ]]; then
    WAVE_FMT="vcd"
  elif [[ "$WAVE_FMT_ANS" == "f" ]]; then
    WAVE_FMT="fst"
  elif [[ "$WAVE_FMT_ANS" == "fst" || "$WAVE_FMT_ANS" == "vcd" ]]; then
    WAVE_FMT="$WAVE_FMT_ANS"
  else
    echo "[WARN] Unknown wave format '$WAVE_FMT_ANS', fallback to default: vcd"
    WAVE_FMT="vcd"
  fi
fi

if [[ "$SIM" == "iverilog" && "$WAVE_ON" -eq 1 && "$WAVE_FMT" == "fst" ]]; then
  echo "[WARN] Icarus Verilog workflow commonly uses VCD. Fallback wave format: vcd"
  WAVE_FMT="vcd"
fi

cleanup_pass_logs() {
  local sim_log_root="sim/logs/${SIM}"
  local deleted=0
  local log_file
  if [[ ! -d "$sim_log_root" ]]; then
    return
  fi
  shopt -s globstar nullglob
  for log_file in "$sim_log_root"/**/*.log; do
    if ! grep -Eq "\[ERR\]|\[FAIL\]|FATAL" "$log_file"; then
      rm -f "$log_file"
      deleted=$((deleted + 1))
    fi
  done
  shopt -u globstar nullglob
  if [[ "$deleted" -gt 0 ]]; then
    echo "[INFO] Removed ${deleted} historical pass logs under ${sim_log_root}."
  fi
}

echo
echo "[INFO] simulator=$SIM wave=$WAVE_ON format=$WAVE_FMT base_seed=$BASE_SEED seed_runs=$SEED_RUNS"

WAVE_ROOT="sim/waves/${SIM}"
if [[ "$WAVE_ON" -eq 1 ]]; then
  mkdir -p "$WAVE_ROOT"
fi

cleanup_pass_logs

for ((seed_idx = 0; seed_idx < SEED_RUNS; seed_idx++)); do
  SEED=$((BASE_SEED + seed_idx))
  echo
  echo "[INFO] ===== seed run $((seed_idx + 1))/${SEED_RUNS}: seed=${SEED} ====="
  echo "[INFO] Re-generate testcase vectors from model/ (seed=${SEED})"
  python3 "$MODEL_GEN_SCRIPT" --seed "$SEED"

  RUN_TAG="$(date +%Y%m%d_%H%M%S)_run$((seed_idx + 1))"
  LOG_ROOT="sim/logs/${SIM}/${RUN_TAG}"
  mkdir -p "$LOG_ROOT"
  echo "[INFO] testcase logs are under ${LOG_ROOT}"

  if [[ "$SIM" == "iverilog" ]]; then
    for c in "${CASES[@]}"; do
      CASE_LOG="${LOG_ROOT}/seed_${SEED}_${c}.log"
      {
        echo "[INFO] simulator=iverilog case=${c} seed=${SEED}"
        iverilog -g2012 -s "$c" -f "$RTL_FILELIST" -f "$TB_FILELIST" -o "build/${c}.iv.out"
        if [[ "$WAVE_ON" -eq 1 ]]; then
          CASE_WAVE_DIR="${WAVE_ROOT}/${c}"
          mkdir -p "$CASE_WAVE_DIR"
          (
            cd "$CASE_WAVE_DIR"
            vvp "${ROOT}/build/${c}.iv.out" +GEN_DIR="${GEN_DIR}" +WAVE=1 +WAVE_FST=0
          )
        else
          vvp "build/${c}.iv.out" +GEN_DIR="${GEN_DIR}"
        fi
      } 2>&1 | tee "$CASE_LOG"
    done
  else
    for c in "${CASES[@]}"; do
      CASE_LOG="${LOG_ROOT}/seed_${SEED}_${c}.log"
      {
        echo "[INFO] simulator=verilator case=${c} seed=${SEED}"
        if [[ "$seed_idx" -eq 0 ]]; then
          if [[ "$WAVE_ON" -eq 1 && "$WAVE_FMT" == "fst" ]]; then
            verilator --binary --timing -Wall -Wno-fatal --trace-fst -f "$RTL_FILELIST" -f "$TB_FILELIST" --top-module "$c" -o "${c}.vlt.out"
          elif [[ "$WAVE_ON" -eq 1 ]]; then
            verilator --binary --timing -Wall -Wno-fatal --trace -f "$RTL_FILELIST" -f "$TB_FILELIST" --top-module "$c" -o "${c}.vlt.out"
          else
            verilator --binary --timing -Wall -Wno-fatal -f "$RTL_FILELIST" -f "$TB_FILELIST" --top-module "$c" -o "${c}.vlt.out"
          fi
        else
          echo "[INFO] reuse existing verilator binary for ${c}"
        fi

        if [[ "$WAVE_ON" -eq 1 ]]; then
          CASE_WAVE_DIR="${WAVE_ROOT}/${c}"
          mkdir -p "$CASE_WAVE_DIR"
          (
            cd "$CASE_WAVE_DIR"
            if [[ "$WAVE_FMT" == "fst" ]]; then
              "${ROOT}/obj_dir/${c}.vlt.out" +GEN_DIR="${GEN_DIR}" +WAVE=1 +WAVE_FST=1
            else
              "${ROOT}/obj_dir/${c}.vlt.out" +GEN_DIR="${GEN_DIR}" +WAVE=1 +WAVE_FST=0
            fi
          )
        else
          "./obj_dir/${c}.vlt.out" +GEN_DIR="${GEN_DIR}"
        fi
      } 2>&1 | tee "$CASE_LOG"
    done
  fi
done

echo "[PASS] all cases passed with simulator=$SIM seed_runs=$SEED_RUNS (base_seed=$BASE_SEED)."
if [[ "$WAVE_ON" -eq 1 ]]; then
  echo "[INFO] waveform files are under ${WAVE_ROOT}/<case>/wave.${WAVE_FMT}"
fi
