#!/usr/bin/env bash
set -euo pipefail

# Run all folderized examples through emit + pycc (cpp) to sanity-check the
# compiler/codegen pipeline.

source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/lib.sh"
pyc_find_pycc

PYTHONPATH_VAL="$(pyc_pythonpath)"
EX_DIR="${PYC_ROOT_DIR}/designs/examples"
DISCOVER="${PYC_ROOT_DIR}/flows/tools/discover_examples.py"

if [[ ! -d "${EX_DIR}" ]]; then
  pyc_die "examples dir not found: ${EX_DIR}"
fi

pyc_log "using pycc: ${PYCC}"

pyc_log "running strict API hygiene gate"
python3 "${PYC_ROOT_DIR}/flows/tools/check_api_hygiene.py" \
  compiler/frontend/pycircuit \
  designs/examples \
  docs \
  README.md

fail=0
count=0

while IFS=$'\t' read -r bn design _tb _cfg _tier; do
  [[ -n "${bn}" ]] || continue

  count=$((count+1))
  out_root="$(pyc_out_root)/example-smoke/${bn}"
  rm -rf "${out_root}" >/dev/null 2>&1 || true
  mkdir -p "${out_root}"
  pyc_file="${out_root}/${bn}.pyc"
  cpp_dir="${out_root}/cpp"

  pyc_log "[${count}] emit ${bn}"
  if ! PYTHONPATH="${PYTHONPATH_VAL}" python3 -m pycircuit.cli emit "${design}" -o "${pyc_file}"; then
    pyc_warn "emit failed: ${bn}"
    fail=1
    continue
  fi

  if [[ "${bn}" == "bundle_probe_expand" ]]; then
    pyc_log "[${count}] check M4 hardened layout metadata ${bn}"
    if ! PYC_FILE="${pyc_file}" python3 - <<'PY'
import json
import os
import re
import sys

path = os.environ["PYC_FILE"]
text = open(path, encoding="utf-8").read()

m = re.search(
    r'func\.func @bundle_probe_expand\b[^\n]*?pyc\.hardened\s*=\s*"((?:\\.|[^"\\])*)"',
    text,
)
if not m:
    raise SystemExit("missing pyc.hardened attribute on @bundle_probe_expand")

hardened_json = json.loads('"' + m.group(1) + '"')
payload = json.loads(hardened_json)

exp_layout_id = "e86f8e1f062bb1e3"
layout_table = payload.get("layout_table", {})
if exp_layout_id not in layout_table:
    raise SystemExit(f"missing expected layout_id {exp_layout_id} in layout_table")

entry = layout_table[exp_layout_id]
if entry.get("kind") != "struct":
    raise SystemExit(f"expected kind=struct for layout_id {exp_layout_id}, got {entry.get('kind')!r}")

fmap = entry.get("field_map", {})
if fmap.get("a") != [1, 8]:
    raise SystemExit(f"field_map['a'] mismatch: got {fmap.get('a')!r} exp [1, 8]")
if fmap.get("b.c") != [0, 1]:
    raise SystemExit(f"field_map['b.c'] mismatch: got {fmap.get('b.c')!r} exp [0, 1]")

groups = payload.get("layout_groups", [])
matched = [
    g
    for g in groups
    if g.get("usage") == "inputs"
    and g.get("prefix") == "in_"
    and isinstance(g.get("spec"), dict)
    and g["spec"].get("layout_id") == exp_layout_id
]
if len(matched) != 1:
    raise SystemExit(f"expected exactly 1 matching layout_group, got {len(matched)}")

ports = matched[0].get("ports", {})
if ports.get("a") != "in_a":
    raise SystemExit(f"ports['a'] mismatch: got {ports.get('a')!r} exp 'in_a'")
if ports.get("b.c") != "in_b_c":
    raise SystemExit(f"ports['b.c'] mismatch: got {ports.get('b.c')!r} exp 'in_b_c'")

# Probe hardening (Decision 0132/0140): probe_table must include stable at/tags.
probe_table = payload.get("probe_table", {})
if not isinstance(probe_table, dict):
    raise SystemExit("missing probe_table in hardened payload")

def check_probe(port, exp_ty, exp_field):
    meta = probe_table.get(port)
    if not isinstance(meta, dict):
        raise SystemExit(f"missing probe_table entry for {port!r}")
    if meta.get("at") != "tick":
        raise SystemExit(f"{port}: expected at='tick', got {meta.get('at')!r}")
    if meta.get("ty") != exp_ty:
        raise SystemExit(f"{port}: expected ty={exp_ty!r}, got {meta.get('ty')!r}")
    tags = meta.get("tags", {})
    if not isinstance(tags, dict):
        raise SystemExit(f"{port}: missing tags dict")
    if tags.get("family") != "pv":
        raise SystemExit(f"{port}: expected tags.family='pv', got {tags.get('family')!r}")
    if tags.get("stage") != "ex":
        raise SystemExit(f"{port}: expected tags.stage='ex', got {tags.get('stage')!r}")
    if tags.get("lane") != 0:
        raise SystemExit(f"{port}: expected tags.lane=0, got {tags.get('lane')!r}")
    if tags.get("field") != exp_field:
        raise SystemExit(f"{port}: expected tags.field={exp_field!r}, got {tags.get('field')!r}")
    if tags.get("demo") != "bundle":
        raise SystemExit(f"{port}: expected tags.demo='bundle', got {tags.get('demo')!r}")

check_probe("dbg__pv_ex_in.a_lane0_ex", "i8", "in.a")
check_probe("dbg__pv_ex_in.b.c_lane0_ex", "i1", "in.b.c")
print("ok: hardened layout metadata present and stable")
PY
    then
      pyc_warn "M4 hardened layout metadata check failed: ${bn}"
      fail=1
      continue
    fi
  fi

  pyc_log "[${count}] compile(cpp) ${bn}"
  if ! "${PYCC}" "${pyc_file}" \
      --emit=cpp \
      --cpp-split=module \
      --out-dir="${cpp_dir}" \
      --logic-depth="${PYC_EXAMPLE_LOGIC_DEPTH:-256}"; then
    pyc_warn "pycc failed: ${bn}"
    fail=1
    continue
  fi

  if [[ "${bn}" == "bundle_probe_expand" ]]; then
    pyc_log "[${count}] check M4 canonical DFX field paths ${bn}"
    h="${cpp_dir}/bundle_probe_expand.hpp"
    if [[ ! -f "${h}" ]]; then
      pyc_warn "missing generated header for canonical DFX check: ${h}"
      fail=1
      continue
    fi
    if ! grep -Fq "dbg__pv_ex_in.b.c_lane0_ex" "${h}"; then
      pyc_warn "missing dotted canonical field path in generated C++ DFX strings (Decision 0143): ${bn}"
      fail=1
      continue
    fi
  fi

  # Basic artifact check.
  if [[ ! -f "${cpp_dir}/cpp_compile_manifest.json" ]]; then
    pyc_warn "missing cpp_compile_manifest.json: ${bn}"
    fail=1
  fi

done < <(python3 "${DISCOVER}" --root "${EX_DIR}" --tier all --format tsv)

# Project build flow smoke checks (multi-.pyc + parallel pycc + CMake/Ninja).
for bex in counter huge_hierarchy_stress boundary_value_ports bundle_probe_expand trace_dsl_smoke; do
  ex="${EX_DIR}/${bex}/tb_${bex}.py"
  [[ -f "${ex}" ]] || continue
  count=$((count+1))
  out_root="$(pyc_out_root)/example-build-smoke/${bex}"
  rm -rf "${out_root}" >/dev/null 2>&1 || true
  mkdir -p "${out_root}"
  pyc_log "[${count}] build ${bex}"
  build_cmd=(python3 -m pycircuit.cli build
    "${ex}"
    --out-dir "${out_root}"
    --target cpp
    --jobs "${PYC_EXAMPLE_JOBS:-4}"
    --logic-depth "${PYC_EXAMPLE_LOGIC_DEPTH:-256}")
  trace_cfg=""
  if [[ "${bex}" == "trace_dsl_smoke" || "${bex}" == "bundle_probe_expand" ]]; then
    trace_cfg="${EX_DIR}/${bex}/${bex}_trace.json"
    build_cmd+=(--trace-config "${trace_cfg}")
  fi
  if ! PYTHONPATH="${PYTHONPATH_VAL}" "${build_cmd[@]}"; then
    pyc_warn "build failed: ${bex}"
    fail=1
    continue
  fi
  if [[ ! -f "${out_root}/project_manifest.json" ]]; then
    pyc_warn "missing project_manifest.json: ${bex}"
    fail=1
  fi

  if [[ "${bex}" == "trace_dsl_smoke" ]]; then
    pyc_log "[${count}] check M4 trace DSL manifest ${bex}"
    if [[ ! -f "${out_root}/trace_plan.json" ]]; then
      pyc_warn "missing trace_plan.json: ${bex}"
      fail=1
      continue
    fi
    if ! python3 - "${out_root}" <<'PY'
import json
import sys
from pathlib import Path

out_root = Path(sys.argv[1]).resolve()
plan = json.loads((out_root / "trace_plan.json").read_text(encoding="utf-8"))

sig = plan.get("enabled_signals", [])
if sig != [
    "dut.u0:dbg__pv_leaf_q_lane0_leaf",
    "dut.u0:out_y",
]:
    raise SystemExit(f"enabled_signals mismatch: got {sig!r}")

probe = json.loads((out_root / "probe_manifest.json").read_text(encoding="utf-8"))
paths = [p.get("canonical_path", "") for p in probe.get("probes", []) if isinstance(p, dict)]
for exp in sig:
    if exp not in paths:
        raise SystemExit(f"probe_manifest missing canonical_path: {exp!r}")

w = plan.get("window", {})
if w.get("begin_cycle") != 1 or w.get("end_cycle") != 3:
    raise SystemExit(f"window mismatch: got {w!r}")

tb_cpp = out_root / "tb" / "tb_trace_dsl_smoke.cpp"
txt = tb_cpp.read_text(encoding="utf-8")
if "pyc_trace_vcd" not in txt or "kEnabledSignals" not in txt or "setVcdWindow" not in txt:
    raise SystemExit("generated C++ TB missing expected trace DSL wiring")

tb_pyc = out_root / "tb" / "tb_trace_dsl_smoke.pyc"
mlir = tb_pyc.read_text(encoding="utf-8")
if "pyc.tb.payload" not in mlir:
    raise SystemExit("missing pyc.tb.payload in tb .pyc")
payload_json = None
for line in mlir.splitlines():
    if "pyc.tb.payload" in line:
        # Extract the string attribute literal: pyc.tb.payload = "<json>"
        s = line.split("pyc.tb.payload", 1)[1]
        i = s.find('"')
        j = s.rfind('"')
        if i >= 0 and j > i:
            payload_json = json.loads(s[i : j + 1])
            break
if payload_json is None:
    raise SystemExit("failed to extract pyc.tb.payload JSON string")
payload = json.loads(payload_json)
sv = payload.get("sv_text", "")
if "$dumpvars" not in sv or "$dumpoff" not in sv or "$dumpon" not in sv:
    raise SystemExit("generated SV TB missing expected $dumpvars/$dumpon/$dumpoff trace hooks")

print("ok: trace DSL plan + generated TB texts are stable")
PY
    then
      pyc_warn "M4 trace DSL manifest check failed: ${bex}"
      fail=1
      continue
    fi

    pyc_log "[${count}] check M5 incremental cache ${bex}"
    if ! PYTHONPATH="${PYTHONPATH_VAL}" python3 - "${ex}" "${out_root}" "${trace_cfg}" <<'PY'
import json
import os
import subprocess
import sys
from pathlib import Path

src = sys.argv[1]
out_root = Path(sys.argv[2]).resolve()
trace_cfg = sys.argv[3]

cmd = [
    sys.executable,
    "-m",
    "pycircuit.cli",
    "build",
    src,
    "--out-dir",
    str(out_root),
    "--target",
    "cpp",
    "--jobs",
    os.environ.get("PYC_EXAMPLE_JOBS", "4"),
    "--logic-depth",
    os.environ.get("PYC_EXAMPLE_LOGIC_DEPTH", "256"),
    "--trace-config",
    trace_cfg,
]
proc = subprocess.run(cmd, text=True, capture_output=True, env=os.environ.copy())
if proc.returncode != 0:
    raise SystemExit(f"second build failed:\n{proc.stdout}\n{proc.stderr}")
out = (proc.stdout or "") + "\n" + (proc.stderr or "")
if "jit-cache: hit" not in out:
    raise SystemExit("expected 'jit-cache: hit' on second build output")

cache_path = out_root / ".build_cache.json"
cache = json.loads(cache_path.read_text(encoding="utf-8"))
jobs = cache.get("last_pycc_jobs")
if jobs != 0:
    raise SystemExit(f"expected last_pycc_jobs==0 on second build, got {jobs!r}")

print("ok: incremental build cache hit (jit + pycc)")
PY
    then
      pyc_warn "M5 incremental cache check failed: ${bex}"
      fail=1
      continue
    fi
  fi

  if [[ "${bex}" == "bundle_probe_expand" ]]; then
    pyc_log "[${count}] check M4 trace DSL preserves dotted field paths ${bex}"
    if [[ ! -f "${out_root}/trace_plan.json" ]]; then
      pyc_warn "missing trace_plan.json: ${bex}"
      fail=1
      continue
    fi
    if ! python3 - "${out_root}" <<'PY'
import json
import sys
from pathlib import Path

out_root = Path(sys.argv[1]).resolve()
plan = json.loads((out_root / "trace_plan.json").read_text(encoding="utf-8"))

sig = plan.get("enabled_signals", [])
if sig != [
    "dut:dbg__pv_ex_in.a_lane0_ex",
    "dut:dbg__pv_ex_in.b.c_lane0_ex",
]:
    raise SystemExit(f"enabled_signals mismatch: got {sig!r}")

probe = json.loads((out_root / "probe_manifest.json").read_text(encoding="utf-8"))
paths = [p.get("canonical_path", "") for p in probe.get("probes", []) if isinstance(p, dict)]
for exp in sig:
    if exp not in paths:
        raise SystemExit(f"probe_manifest missing canonical_path: {exp!r}")

tb_pyc = out_root / "tb" / "tb_bundle_probe_expand.pyc"
mlir = tb_pyc.read_text(encoding="utf-8")
if "pyc.tb.payload" not in mlir:
    raise SystemExit("missing pyc.tb.payload in tb .pyc")
payload_json = None
for line in mlir.splitlines():
    if "pyc.tb.payload" in line:
        s = line.split("pyc.tb.payload", 1)[1]
        i = s.find('"')
        j = s.rfind('"')
        if i >= 0 and j > i:
            payload_json = json.loads(s[i : j + 1])
            break
if payload_json is None:
    raise SystemExit("failed to extract pyc.tb.payload JSON string")
payload = json.loads(payload_json)
sv = payload.get("sv_text", "")
if "dut.dbg__pv_ex_in_a_lane0_ex" not in sv:
    raise SystemExit("missing sanitized SV trace for dbg__pv_ex_in.a_lane0_ex")
if "dut.dbg__pv_ex_in_b_c_lane0_ex" not in sv:
    raise SystemExit("missing sanitized SV trace for dbg__pv_ex_in.b.c_lane0_ex")

print("ok: trace DSL preserves dotted field paths and SV mapping is sanitized")
PY
    then
      pyc_warn "M4 trace DSL dotted-field-path check failed: ${bex}"
      fail=1
      continue
    fi
  fi
done

pyc_log "running M5 const/valueclass canonicalization positive checks"
pos_dir="$(pyc_out_root)/example-smoke/_pos_valueclass_params"
rm -rf "${pos_dir}" >/dev/null 2>&1 || true
mkdir -p "${pos_dir}"

pos_py="${pos_dir}/pos_valueclass_params.py"
cat > "${pos_py}" <<'PY'
from __future__ import annotations

from pycircuit import Circuit, module
from pycircuit.spec import valueclass


@valueclass
class Cfg:
    ways: int = 4
    mode: str = "a"


@module
def build(m: Circuit, cfg: Cfg = Cfg()) -> None:
    _ = cfg
    x = m.input("x", width=8)
    m.output("y", x)


build.__pycircuit_name__ = "pos_valueclass_params"
PY

pos_pyc="${pos_dir}/pos_valueclass_params.pyc"
if ! PYTHONPATH="${PYTHONPATH_VAL}" python3 -m pycircuit.cli emit "${pos_py}" -o "${pos_pyc}"; then
  pyc_warn "positive check failed: valueclass param emit failed"
  fail=1
else
  if ! PYC_FILE="${pos_pyc}" python3 - <<'PY'
import json
import os
import re

path = os.environ["PYC_FILE"]
text = open(path, encoding="utf-8").read()

m = re.search(
    r'func\.func @pos_valueclass_params\b[^\n]*?pyc\.params\s*=\s*"((?:\\.|[^"\\])*)"',
    text,
)
if not m:
    raise SystemExit("missing pyc.params attribute on @pos_valueclass_params")

params_json = json.loads('"' + m.group(1) + '"')
params = json.loads(params_json)
cfg = params.get("cfg", None)
if not isinstance(cfg, dict):
    raise SystemExit(f"missing cfg param in pyc.params: {params!r}")
if cfg.get("kind") != "valueclass":
    raise SystemExit(f"expected cfg.kind='valueclass', got {cfg.get('kind')!r}")
if not isinstance(cfg.get("type"), str) or not cfg["type"]:
    raise SystemExit(f"expected cfg.type string, got {cfg.get('type')!r}")
fields = cfg.get("fields", None)
if fields != {"mode": "a", "ways": 4}:
    raise SystemExit(f"cfg.fields mismatch: got {fields!r}")

print("ok: valueclass params are canonicalized into stable pyc.params JSON")
PY
  then
    pyc_warn "positive check failed: valueclass param canonicalization check"
    fail=1
  fi
fi

pyc_log "running boundary value-param negative checks"
neg_dir="$(pyc_out_root)/example-smoke/_negative_boundary_value_params"
rm -rf "${neg_dir}" >/dev/null 2>&1 || true
mkdir -p "${neg_dir}"

# Negative 1: value-param used in compile-time range bound must fail during emit.
neg_py="${neg_dir}/neg_value_param_range.py"
cat > "${neg_py}" <<'PY'
from __future__ import annotations

from pycircuit import Circuit, module


@module(value_params={"n": "i4"})
def lane(m: Circuit, x, n):
    acc = x
    for _ in range(n):
        acc = acc + 1
    m.output("y", acc)


@module
def build(m: Circuit):
    x = m.input("x", width=32)
    y = lane(m, x=x, n=3)
    m.output("y", y)


build.__pycircuit_name__ = "neg_value_param_range"
PY

neg_pyc="${neg_dir}/neg_value_param_range.pyc"
if PYTHONPATH="${PYTHONPATH_VAL}" python3 -m pycircuit.cli emit "${neg_py}" -o "${neg_pyc}" >"${neg_dir}/neg_emit.stdout" 2>"${neg_dir}/neg_emit.stderr"; then
  pyc_warn "negative check failed: value-param range bound unexpectedly compiled"
  fail=1
else
  if ! grep -Eiq "value parameter|compile-time context|range bounds" "${neg_dir}/neg_emit.stderr"; then
    pyc_warn "negative check failed: missing expected value-param compile-time diagnostic"
    fail=1
  fi
fi

# Negative 2: malformed value-param metadata must fail frontend contract pass.
neg_mlir="${neg_dir}/neg_contract_missing_value_param_types.pyc"
cat > "${neg_mlir}" <<'MLIR'
module attributes {pyc.top = @neg_contract, pyc.frontend.contract = "pycircuit"} {
  func.func @neg_contract(%x: i1) -> (i1) attributes {arg_names = ["x"], result_names = ["y"], pyc.kind = "module", pyc.inline = "false", pyc.params = "{}", pyc.base = "neg_contract", pyc.value_params = ["vp"]} {
    return %x : i1
  }
}
MLIR

mkdir -p "${neg_dir}/neg_contract_cpp"
if "${PYCC}" "${neg_mlir}" --emit=cpp --out-dir "${neg_dir}/neg_contract_cpp" >"${neg_dir}/neg_contract.stdout" 2>"${neg_dir}/neg_contract.stderr"; then
  pyc_warn "negative check failed: malformed value-param metadata unexpectedly compiled"
  fail=1
else
  if ! grep -q "PYC913" "${neg_dir}/neg_contract.stderr"; then
    pyc_warn "negative check failed: expected PYC913 missing for malformed value-param metadata"
    fail=1
  fi
fi

pyc_log "running M0 C++ runtime ownership check (Decision 0012)"
m0_ptr_dir="$(pyc_out_root)/example-smoke/_m0_unique_ptr_children"
rm -rf "${m0_ptr_dir}" >/dev/null 2>&1 || true
mkdir -p "${m0_ptr_dir}"

pos_mlir="${m0_ptr_dir}/pos_unique_ptr_children.pyc"
cat > "${pos_mlir}" <<'MLIR'
module attributes {pyc.top = @top, pyc.frontend.contract = "pycircuit"} {
  func.func @leaf(%x: i1) -> (i1) attributes {arg_names = ["x"], result_names = ["y"], pyc.kind = "module", pyc.inline = "false", pyc.params = "{}", pyc.base = "leaf"} {
    return %x : i1
  }

  func.func @top(%x: i1) -> (i1) attributes {arg_names = ["x"], result_names = ["y"], pyc.kind = "module", pyc.inline = "false", pyc.params = "{}", pyc.base = "top"} {
    %y = pyc.instance %x {callee = @leaf, name = "u0"} : (i1) -> i1
    return %y : i1
  }
}
MLIR

mkdir -p "${m0_ptr_dir}/pos_cpp"
if ! "${PYCC}" "${pos_mlir}" --emit=cpp --cpp-split=module --out-dir "${m0_ptr_dir}/pos_cpp" \
    >"${m0_ptr_dir}/pos_cpp.stdout" 2>"${m0_ptr_dir}/pos_cpp.stderr"; then
  pyc_warn "positive check failed: Decision 0012 unique_ptr child ownership compile failed"
  fail=1
else
  if [[ ! -f "${m0_ptr_dir}/pos_cpp/top.hpp" ]]; then
    pyc_warn "positive check failed: missing top.hpp for Decision 0012 check"
    fail=1
  else
    if ! grep -q "std::unique_ptr<leaf>" "${m0_ptr_dir}/pos_cpp/top.hpp"; then
      pyc_warn "positive check failed: expected std::unique_ptr child ownership in generated C++ (Decision 0012)"
      fail=1
    fi
    if ! grep -q "std::make_unique<leaf>" "${m0_ptr_dir}/pos_cpp/top.hpp"; then
      pyc_warn "positive check failed: expected std::make_unique child construction in generated C++ (Decision 0012)"
      fail=1
    fi
  fi
fi

pyc_log "running M0 simulation phase API check (Decisions 0001/0026/0027)"
m0_phase_dir="$(pyc_out_root)/example-smoke/_m0_phase_api"
rm -rf "${m0_phase_dir}" >/dev/null 2>&1 || true
mkdir -p "${m0_phase_dir}"

pos_mlir="${m0_phase_dir}/pos_phase_api.pyc"
cat > "${pos_mlir}" <<'MLIR'
module attributes {pyc.top = @top, pyc.frontend.contract = "pycircuit"} {
  func.func @top(%x: i1) -> (i1) attributes {arg_names = ["x"], result_names = ["y"], pyc.kind = "module", pyc.inline = "false", pyc.params = "{}", pyc.base = "top"} {
    return %x : i1
  }
}
MLIR

mkdir -p "${m0_phase_dir}/pos_cpp"
if ! "${PYCC}" "${pos_mlir}" --emit=cpp --cpp-split=module --out-dir "${m0_phase_dir}/pos_cpp" \
    >"${m0_phase_dir}/pos_cpp.stdout" 2>"${m0_phase_dir}/pos_cpp.stderr"; then
  pyc_warn "positive check failed: phase API compile failed"
  fail=1
else
  h="${m0_phase_dir}/pos_cpp/top.hpp"
  if [[ ! -f "${h}" ]]; then
    pyc_warn "positive check failed: missing top.hpp for phase API check"
    fail=1
  else
    if ! grep -q "void comb();" "${h}"; then
      pyc_warn "positive check failed: missing comb() API (Decision 0027)"
      fail=1
    fi
    if ! grep -q "void tick();" "${h}"; then
      pyc_warn "positive check failed: missing tick() API (Decision 0027)"
      fail=1
    fi
    if ! grep -q "void commit();" "${h}"; then
      pyc_warn "positive check failed: missing commit() API (Decision 0026/0027)"
      fail=1
    fi
    if ! grep -q "void transfer();" "${h}"; then
      pyc_warn "positive check failed: missing transfer() API (Decision 0001)"
      fail=1
    fi
    if ! grep -q "void step();" "${h}"; then
      pyc_warn "positive check failed: missing step() API (Decision 0027)"
      fail=1
    fi
  fi

  cpp="${m0_phase_dir}/pos_cpp/top.cpp"
  if [[ ! -f "${cpp}" ]]; then
    pyc_warn "positive check failed: missing top.cpp for phase API check"
    fail=1
  else
    if ! grep -q "void top::comb() { eval(); }" "${cpp}"; then
      pyc_warn "positive check failed: comb() should delegate to eval()"
      fail=1
    fi
    if ! grep -q "void top::tick() { tick_compute(); }" "${cpp}"; then
      pyc_warn "positive check failed: tick() should delegate to tick_compute()"
      fail=1
    fi
    if ! grep -q "void top::transfer() { tick_commit(); }" "${cpp}"; then
      pyc_warn "positive check failed: transfer() should delegate to tick_commit()"
      fail=1
    fi
    if ! grep -q "commit();" "${cpp}"; then
      pyc_warn "positive check failed: expected step() to call commit()"
      fail=1
    fi
  fi
fi

pyc_log "running M0 ProbeRegistry smoke (Decisions 0004/0018-0021)"
m0_probe_dir="$(pyc_out_root)/example-smoke/_m0_probe_registry"
rm -rf "${m0_probe_dir}" >/dev/null 2>&1 || true
mkdir -p "${m0_probe_dir}"

mlir="${m0_probe_dir}/pos_probe_registry.pyc"
cat > "${mlir}" <<'MLIR'
module attributes {pyc.top = @top, pyc.frontend.contract = "pycircuit"} {
  func.func @leaf(%x: i8) -> (i8) attributes {arg_names = ["x"], result_names = ["y"], pyc.kind = "module", pyc.inline = "false", pyc.params = "{}", pyc.base = "leaf"} {
    return %x : i8
  }

  func.func @top(%x: i8) -> (i8) attributes {arg_names = ["x"], result_names = ["y"], pyc.kind = "module", pyc.inline = "false", pyc.params = "{}", pyc.base = "top"} {
    %y = pyc.instance %x {callee = @leaf, name = "u0"} : (i8) -> i8
    return %y : i8
  }
}
MLIR

cpp_out="${m0_probe_dir}/cpp"
mkdir -p "${cpp_out}"
if ! "${PYCC}" "${mlir}" --emit=cpp --cpp-split=module --out-dir "${cpp_out}" \
    >"${m0_probe_dir}/pycc.stdout" 2>"${m0_probe_dir}/pycc.stderr"; then
  pyc_warn "positive check failed: ProbeRegistry pycc emit failed"
  fail=1
else
  cat > "${m0_probe_dir}/probe_registry_smoke.cpp" <<'CPP'
#include <cstdint>
#include <iostream>

#include <cpp/pyc_probe_registry.hpp>

#include "top.hpp"

int main() {
  pyc::gen::top dut;
  pyc::cpp::ProbeRegistry reg;
  dut.pyc_register_probes(reg, "dut");

  auto expect = [&](const char *path, std::uint32_t w) {
    const auto *e = reg.findByPath(path);
    if (!e) {
      std::cerr << "missing probe: " << path << "\n";
      return false;
    }
    if (e->kind != pyc::cpp::ProbeKind::Wire) {
      std::cerr << "unexpected kind for " << path << "\n";
      return false;
    }
    if (e->width_bits != w) {
      std::cerr << "unexpected width for " << path << ": got " << e->width_bits << " exp " << w << "\n";
      return false;
    }
    const std::uint64_t exp_id = pyc::cpp::ProbeRegistry::hash64ForPath(path);
    if (e->probe_id != exp_id) {
      std::cerr << "unexpected probe_id for " << path << "\n";
      return false;
    }
    return true;
  };

  if (!expect("dut:x", 8) || !expect("dut:y", 8) || !expect("dut.u0:x", 8) || !expect("dut.u0:y", 8)) {
    return 2;
  }

  const auto ys = reg.findByGlob("dut.**:y");
  if (ys.size() != 2) {
    std::cerr << "glob mismatch: expected 2 matches, got " << ys.size() << "\n";
    return 3;
  }

  const auto xs = reg.findByGlobAndKind("dut.**:x", pyc::cpp::ProbeKind::Wire);
  if (xs.size() != 2) {
    std::cerr << "glob+kind mismatch: expected 2 matches, got " << xs.size() << "\n";
    return 4;
  }

  std::cout << "ok: ProbeRegistry\n";
  return 0;
}
CPP

  cxx="${CXX:-c++}"
  if ! "${cxx}" -std=c++17 -O2 -I "${PYC_ROOT_DIR}/runtime" -I "${cpp_out}" \
      "${cpp_out}/leaf.cpp" "${cpp_out}/top.cpp" "${m0_probe_dir}/probe_registry_smoke.cpp" -o "${m0_probe_dir}/probe_registry_smoke" \
      >"${m0_probe_dir}/compile.stdout" 2>"${m0_probe_dir}/compile.stderr"; then
    pyc_warn "positive check failed: ProbeRegistry C++ compile failed"
    fail=1
  else
    if ! "${m0_probe_dir}/probe_registry_smoke" >"${m0_probe_dir}/run.stdout" 2>"${m0_probe_dir}/run.stderr"; then
      pyc_warn "positive check failed: ProbeRegistry C++ run failed"
      fail=1
    else
      if ! grep -q "ok: ProbeRegistry" "${m0_probe_dir}/run.stdout"; then
        pyc_warn "positive check failed: ProbeRegistry missing success marker"
        fail=1
      fi
    fi
  fi
fi

pyc_log "running M2 comb-cycle + logic-depth negative checks"
m2_neg_dir="$(pyc_out_root)/example-smoke/_negative_m2_combdepgraph"
rm -rf "${m2_neg_dir}" >/dev/null 2>&1 || true
mkdir -p "${m2_neg_dir}"

# Negative 3: module-local combinational loop must fail comb-cycle verifier.
neg_mlir="${m2_neg_dir}/neg_comb_cycle_local.pyc"
cat > "${neg_mlir}" <<'MLIR'
module attributes {pyc.top = @neg_comb_cycle_local, pyc.frontend.contract = "pycircuit"} {
  func.func @neg_comb_cycle_local(%x: i1) -> (i1) attributes {arg_names = ["x"], result_names = ["y"], pyc.kind = "module", pyc.inline = "false", pyc.params = "{}", pyc.base = "neg_comb_cycle_local"} {
    %w0 = pyc.wire {pyc.name = "w0"} : i1
    %w1 = pyc.wire {pyc.name = "w1"} : i1
    pyc.assign %w0, %w1 : i1
    pyc.assign %w1, %w0 : i1
    return %w0 : i1
  }
}
MLIR

mkdir -p "${m2_neg_dir}/neg_comb_cycle_local_cpp"
if "${PYCC}" "${neg_mlir}" --emit=cpp --out-dir "${m2_neg_dir}/neg_comb_cycle_local_cpp" \
    >"${m2_neg_dir}/neg_comb_cycle_local.stdout" 2>"${m2_neg_dir}/neg_comb_cycle_local.stderr"; then
  pyc_warn "negative check failed: module-local comb loop unexpectedly compiled"
  fail=1
else
  if ! grep -Eiq "combinational cycle detected" "${m2_neg_dir}/neg_comb_cycle_local.stderr"; then
    pyc_warn "negative check failed: missing expected comb-cycle diagnostic (local)"
    fail=1
  fi
fi

# Negative 4: cross-instance combinational loop must fail comb-cycle verifier.
neg_mlir="${m2_neg_dir}/neg_comb_cycle_instance.pyc"
cat > "${neg_mlir}" <<'MLIR'
module attributes {pyc.top = @top, pyc.frontend.contract = "pycircuit"} {
  func.func @id(%x: i1) -> (i1) attributes {arg_names = ["x"], result_names = ["y"], pyc.kind = "module", pyc.inline = "false", pyc.params = "{}", pyc.base = "id"} {
    return %x : i1
  }

  func.func @top(%x: i1) -> (i1) attributes {arg_names = ["x"], result_names = ["y"], pyc.kind = "module", pyc.inline = "false", pyc.params = "{}", pyc.base = "top"} {
    %w = pyc.wire {pyc.name = "w"} : i1
    %y = pyc.instance %w {callee = @id, name = "u0"} : (i1) -> i1
    pyc.assign %w, %y : i1
    return %w : i1
  }
}
MLIR

mkdir -p "${m2_neg_dir}/neg_comb_cycle_instance_cpp"
if "${PYCC}" "${neg_mlir}" --emit=cpp --out-dir "${m2_neg_dir}/neg_comb_cycle_instance_cpp" \
    >"${m2_neg_dir}/neg_comb_cycle_instance.stdout" 2>"${m2_neg_dir}/neg_comb_cycle_instance.stderr"; then
  pyc_warn "negative check failed: cross-instance comb loop unexpectedly compiled"
  fail=1
else
  if ! grep -Eiq "combinational cycle detected" "${m2_neg_dir}/neg_comb_cycle_instance.stderr"; then
    pyc_warn "negative check failed: missing expected comb-cycle diagnostic (cross-instance)"
    fail=1
  fi
fi

# Negative 5: cross-instance logic depth must propagate through instances.
neg_mlir="${m2_neg_dir}/neg_logic_depth_instance.pyc"
cat > "${neg_mlir}" <<'MLIR'
module attributes {pyc.top = @top, pyc.frontend.contract = "pycircuit"} {
  func.func @chain4(%x: i8) -> (i8) attributes {arg_names = ["x"], result_names = ["y"], pyc.kind = "module", pyc.inline = "false", pyc.params = "{}", pyc.base = "chain4"} {
    %c1 = pyc.constant 13 : i8
    %c2 = pyc.constant 37 : i8
    %c3 = pyc.constant 240 : i8
    %c4 = pyc.constant 85 : i8
    %t0 = pyc.add %x, %c1 : i8
    %t1 = pyc.xor %t0, %c2 : i8
    %t2 = pyc.and %t1, %c3 : i8
    %t3 = pyc.or %t2, %c4 : i8
    return %t3 : i8
  }

  func.func @top(%x: i8) -> (i8) attributes {arg_names = ["x"], result_names = ["y"], pyc.kind = "module", pyc.inline = "false", pyc.params = "{}", pyc.base = "top"} {
    %y1 = pyc.instance %x {callee = @chain4, name = "u0"} : (i8) -> i8
    %y2 = pyc.instance %y1 {callee = @chain4, name = "u1"} : (i8) -> i8
    return %y2 : i8
  }
}
MLIR

mkdir -p "${m2_neg_dir}/neg_logic_depth_instance_cpp"
if "${PYCC}" "${neg_mlir}" --emit=cpp --out-dir "${m2_neg_dir}/neg_logic_depth_instance_cpp" --logic-depth=6 \
    >"${m2_neg_dir}/neg_logic_depth_instance.stdout" 2>"${m2_neg_dir}/neg_logic_depth_instance.stderr"; then
  pyc_warn "negative check failed: cross-instance logic-depth unexpectedly compiled"
  fail=1
else
  if ! grep -Eiq "logic depth exceeds limit" "${m2_neg_dir}/neg_logic_depth_instance.stderr"; then
    pyc_warn "negative check failed: missing expected logic-depth diagnostic (cross-instance)"
    fail=1
  fi
fi

pyc_log "running M2 multi-clock/CDC legality negative checks"
cdc_neg_dir="$(pyc_out_root)/example-smoke/_negative_m2_clock_domains"
rm -rf "${cdc_neg_dir}" >/dev/null 2>&1 || true
mkdir -p "${cdc_neg_dir}"

# Negative 6: cross-clock combinational path into a sequential element must fail.
neg_mlir="${cdc_neg_dir}/neg_cross_clock_path.pyc"
cat > "${neg_mlir}" <<'MLIR'
module attributes {pyc.top = @neg_cross_clock_path, pyc.frontend.contract = "pycircuit"} {
  func.func @neg_cross_clock_path(%clk_a: !pyc.clock, %rst_a: !pyc.reset, %clk_b: !pyc.clock, %rst_b: !pyc.reset) -> (i8)
      attributes {arg_names = ["clk_a", "rst_a", "clk_b", "rst_b"], result_names = ["y"], pyc.kind = "module", pyc.inline = "false", pyc.params = "{}", pyc.base = "neg_cross_clock_path"} {
    %en = pyc.constant 1 : i1
    %init = pyc.constant 0 : i8
    %c0 = pyc.constant 0 : i8
    %qa = pyc.reg %clk_a, %rst_a, %en, %c0, %init : i8
    %qb = pyc.reg %clk_b, %rst_b, %en, %qa, %init : i8
    return %qb : i8
  }
}
MLIR

mkdir -p "${cdc_neg_dir}/neg_cross_clock_path_cpp"
if "${PYCC}" "${neg_mlir}" --emit=cpp --out-dir "${cdc_neg_dir}/neg_cross_clock_path_cpp" \
    >"${cdc_neg_dir}/neg_cross_clock_path.stdout" 2>"${cdc_neg_dir}/neg_cross_clock_path.stderr"; then
  pyc_warn "negative check failed: cross-clock path unexpectedly compiled"
  fail=1
else
  if ! grep -Eiq "clock-domain violation" "${cdc_neg_dir}/neg_cross_clock_path.stderr"; then
    pyc_warn "negative check failed: missing expected clock-domain diagnostic"
    fail=1
  fi
fi

pyc_log "running M5 cosim commit schema + mismatch dump checks"
cosim_dir="$(pyc_out_root)/example-smoke/_m5_cosim_commit_schema"
rm -rf "${cosim_dir}" >/dev/null 2>&1 || true
mkdir -p "${cosim_dir}"
if ! python3 - "${PYC_ROOT_DIR}" "${cosim_dir}" <<'PY'
import json
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
tool = root / "contrib" / "linx" / "flows" / "tools" / "linx_trace_diff.py"
schema = "LC-COMMIT-BUNDLE-V1"

ref = out / "ref.jsonl"
dut = out / "dut.jsonl"

ref_lines = [
    {"type": "start", "commit_schema_id": schema},
    {"cycle": 0, "pc": 0x100, "insn": 0x1, "len": 4, "wb_valid": 0, "mem_valid": 0, "trap_valid": 0, "next_pc": 0x104},
    # Validity gating: wb fields are don't-care when wb_valid==0 (Decision 0146).
    {"cycle": 1, "pc": 0x104, "insn": 0x2, "len": 4, "wb_valid": 0, "wb_rd": 3, "wb_data": 0xDEAD, "mem_valid": 0, "trap_valid": 0, "next_pc": 0x108},
]
dut_lines = [
    {"type": "start", "commit_schema_id": schema},
    # Unknown fields ignored (Decision 0146).
    {"cycle": 0, "pc": 0x100, "insn": 0x1, "len": 4, "wb_valid": 0, "mem_valid": 0, "trap_valid": 0, "next_pc": 0x104, "uop_uid": "0x123"},
    {"cycle": 1, "pc": 0x104, "insn": 0x2, "len": 4, "wb_valid": 0, "wb_rd": 4, "wb_data": 0xBEEF, "mem_valid": 0, "trap_valid": 0, "next_pc": 0x108},
]

ref.write_text("\n".join(json.dumps(x) for x in ref_lines) + "\n", encoding="utf-8")
dut.write_text("\n".join(json.dumps(x) for x in dut_lines) + "\n", encoding="utf-8")

cmd = [
    sys.executable,
    str(tool),
    "--require-schema-id",
    "--assume-schema-id",
    schema,
    "--expected-schema-id",
    schema,
    str(ref),
    str(dut),
]
proc = subprocess.run(cmd, text=True, capture_output=True)
if proc.returncode != 0:
    raise SystemExit(f"cosim schema/gating match test failed:\n{proc.stdout}\n{proc.stderr}")

# Mismatch must emit dump artifacts (Decision 0142).
ref2 = out / "ref_mismatch.jsonl"
dut2 = out / "dut_mismatch.jsonl"
dump_dir = out / "dump"

ref2_lines = [
    {"type": "start", "commit_schema_id": schema},
    {"cycle": 0, "pc": 0x100, "insn": 0x1, "len": 4, "wb_valid": 1, "wb_rd": 1, "wb_data": 123, "mem_valid": 0, "trap_valid": 0, "next_pc": 0x104},
]
dut2_lines = [
    {"type": "start", "commit_schema_id": schema},
    {"cycle": 0, "pc": 0x100, "insn": 0x1, "len": 4, "wb_valid": 1, "wb_rd": 1, "wb_data": 124, "mem_valid": 0, "trap_valid": 0, "next_pc": 0x104},
]
ref2.write_text("\n".join(json.dumps(x) for x in ref2_lines) + "\n", encoding="utf-8")
dut2.write_text("\n".join(json.dumps(x) for x in dut2_lines) + "\n", encoding="utf-8")

if dump_dir.exists():
    for p in dump_dir.iterdir():
        if p.is_file():
            p.unlink()

cmd2 = [
    sys.executable,
    str(tool),
    "--assume-schema-id",
    schema,
    "--expected-schema-id",
    schema,
    "--dump-dir",
    str(dump_dir),
    str(ref2),
    str(dut2),
]
proc2 = subprocess.run(cmd2, text=True, capture_output=True)
if proc2.returncode == 0:
    raise SystemExit("expected mismatch but traces matched")

need = ["mismatch.json", "trace_config.json", "ref.context.jsonl", "dut.context.jsonl"]
missing = [n for n in need if not (dump_dir / n).is_file()]
if missing:
    raise SystemExit(f"missing mismatch dump artifacts: {missing!r}\n{proc2.stdout}\n{proc2.stderr}")

m = json.loads((dump_dir / "mismatch.json").read_text(encoding="utf-8"))
if m.get("mismatch", {}).get("field") != "wb_data":
    raise SystemExit(f"mismatch.json unexpected field: {m!r}")

print("ok: cosim commit schema + mismatch dump gates")
PY
then
  pyc_warn "M5 cosim commit schema/mismatch dump checks failed"
  fail=1
fi

pyc_log "running Decision 0006 mem hash/watch/dump smoke"
mem_obs_dir="$(pyc_out_root)/example-smoke/_d0006_mem_observability"
rm -rf "${mem_obs_dir}" >/dev/null 2>&1 || true
mkdir -p "${mem_obs_dir}"

cat > "${mem_obs_dir}/mem_observe.cpp" <<'CPP'
#include <cstdint>
#include <iostream>

#include <cpp/pyc_byte_mem.hpp>
#include <cpp/pyc_sync_mem.hpp>

int main() {
  using namespace pyc::cpp;

  // ---- sync_mem: entry-addressed ----
  Wire<1> clk(0);
  Wire<1> rst(0);
  Wire<1> ren(0);
  Wire<8> raddr(0);
  Wire<32> rdata(0);
  Wire<1> wvalid(0);
  Wire<8> waddr(0);
  Wire<32> wdata(0);
  Wire<4> wstrb(0);

  pyc_sync_mem<8, 32, 16> sm(clk, rst, ren, raddr, rdata, wvalid, waddr, wdata, wstrb);
  sm.pokeEntry(3, 0x11223344u);

  const std::uint64_t h0 = sm.mem_hash();
  sm.mem_watch(0, 15);

  // Read @3.
  ren = Wire<1>(1);
  raddr = Wire<8>(3);
  clk = Wire<1>(1);
  sm.tick_compute();
  sm.tick_commit();
  clk = Wire<1>(0);
  sm.tick_compute();
  sm.tick_commit();

  // Write @3.
  ren = Wire<1>(0);
  wvalid = Wire<1>(1);
  waddr = Wire<8>(3);
  wdata = Wire<32>(0x55667788u);
  wstrb = Wire<4>(0xFu);
  clk = Wire<1>(1);
  sm.tick_compute();
  sm.tick_commit();
  clk = Wire<1>(0);
  sm.tick_compute();
  sm.tick_commit();

  const bool has_sync_read = [&]() {
    for (const auto &ev : sm.mem_watch_events()) {
      if (ev.kind == pyc_sync_mem<8, 32, 16>::MemWatchEvent::Kind::Read)
        return true;
    }
    return false;
  }();
  const bool has_sync_write = [&]() {
    for (const auto &ev : sm.mem_watch_events()) {
      if (ev.kind == pyc_sync_mem<8, 32, 16>::MemWatchEvent::Kind::Write)
        return true;
    }
    return false;
  }();
  if (!has_sync_read || !has_sync_write) {
    std::cerr << "missing sync_mem watch events (read/write)\n";
    return 2;
  }
  if (sm.peekEntry(3) != 0x55667788u) {
    std::cerr << "sync_mem write did not commit\n";
    return 3;
  }
  if (sm.mem_hash() == h0) {
    std::cerr << "sync_mem hash did not change after write\n";
    return 4;
  }

  // Dump just the written entry.
  sm.mem_dump(std::cout, 3, 3);

  // ---- byte_mem: byte-addressed ----
  Wire<8> braddr(0);
  Wire<32> brdata(0);
  Wire<1> bwvalid(0);
  Wire<8> bwaddr(0);
  Wire<32> bwdata(0);
  Wire<4> bwstrb(0);

  pyc_byte_mem<8, 32, 64> bm(clk, rst, braddr, brdata, bwvalid, bwaddr, bwdata, bwstrb);
  bm.mem_watch(0, 63);

  // Force a read recompute after enabling watch.
  braddr = Wire<8>(4);
  bm.eval();

  // Write at byte address 0 (4 bytes).
  bwvalid = Wire<1>(1);
  bwaddr = Wire<8>(0);
  bwdata = Wire<32>(0xAABBCCDDu);
  bwstrb = Wire<4>(0xFu);
  clk = Wire<1>(1);
  bm.tick_compute();
  bm.tick_commit();

  const bool has_byte_read = [&]() {
    for (const auto &ev : bm.mem_watch_events()) {
      if (ev.kind == pyc_byte_mem<8, 32, 64>::MemWatchEvent::Kind::Read)
        return true;
    }
    return false;
  }();
  const bool has_byte_write = [&]() {
    for (const auto &ev : bm.mem_watch_events()) {
      if (ev.kind == pyc_byte_mem<8, 32, 64>::MemWatchEvent::Kind::Write)
        return true;
    }
    return false;
  }();
  if (!has_byte_read || !has_byte_write) {
    std::cerr << "missing byte_mem watch events (read/write)\n";
    return 5;
  }

  std::cout << "ok: mem observability\n";
  return 0;
}
CPP

cxx="${CXX:-c++}"
if ! "${cxx}" -std=c++17 -O2 -I "${PYC_ROOT_DIR}/runtime" "${mem_obs_dir}/mem_observe.cpp" -o "${mem_obs_dir}/mem_observe" \
    >"${mem_obs_dir}/compile.stdout" 2>"${mem_obs_dir}/compile.stderr"; then
  pyc_warn "Decision 0006 mem observability C++ compile failed"
  fail=1
else
  if ! "${mem_obs_dir}/mem_observe" >"${mem_obs_dir}/run.stdout" 2>"${mem_obs_dir}/run.stderr"; then
    pyc_warn "Decision 0006 mem observability C++ run failed"
    fail=1
  else
    if ! grep -q "ok: mem observability" "${mem_obs_dir}/run.stdout"; then
      pyc_warn "Decision 0006 mem observability missing expected success marker"
      fail=1
    fi
  fi
fi

if [[ "${fail}" -ne 0 ]]; then
  pyc_die "one or more examples failed"
fi

pyc_log "all examples passed"
