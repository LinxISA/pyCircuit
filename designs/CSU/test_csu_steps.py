# -*- coding: utf-8 -*-
"""CSU methodology steps 1–10: verification hooks (pytest).

Run from repo root::

    PYTHONPATH=compiler/frontend python3 -m pytest designs/CSU/test_csu_steps.py -v

Or per-step::

    python3 -m pytest designs/CSU/test_csu_steps.py -m step1 -v
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

try:
    import pytest
except ModuleNotFoundError:  # stdlib-only runner (run_csu_verification.py)
    pytest = None  # type: ignore[misc,assignment]

_REPO_ROOT = Path(__file__).resolve().parents[2]
_DOCS = Path(__file__).resolve().parent / "docs"
_CSU_DIR = Path(__file__).resolve().parent

if str(_REPO_ROOT / "compiler" / "frontend") not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT / "compiler" / "frontend"))
if str(_CSU_DIR) not in sys.path:
    sys.path.insert(0, str(_CSU_DIR))

import csu as csu_mod  # noqa: E402

from csu import (  # noqa: E402
    INC0_DOMAIN_NEXT_COUNT,
    INC0_OCCURRENCE_STAGES,
    W_RXDAT,
    W_RXRSP,
    W_TXDAT,
    W_TXREQ,
    W_TXRSP,
    emit_csu_mlir,
)


def _mark(name: str):
    if pytest is None:
        return lambda f: f
    return getattr(pytest.mark, name)


@_mark("step1")
def test_step01_style_docs_exist():
    """Step 1: canonical pyCircuit / V5 docs present."""
    root_docs = _REPO_ROOT / "docs"
    for name in (
        "PyCircuit_V5_Spec.md",
        "pycircuit_implementation_method.md",
        "FRONTEND_API.md",
        "TESTBENCH.md",
    ):
        p = root_docs / name
        assert p.is_file(), f"missing {p}"


@_mark("step2")
def test_step02_requirement_sources_and_specs():
    """Step 2: CSU docs folder contains specs + Markdown digests in converted/."""
    assert (_DOCS / "requirement_sources.md").is_file()
    xlsx = list(_DOCS.glob("*Protocol*.xlsx"))
    assert xlsx, "expected CSU protocol xlsx under docs/"
    assert list(_DOCS.glob("*.pdf")), "expected at least one PDF spec under docs/"
    conv = _DOCS / "converted"
    assert conv.is_dir(), "run: python3 designs/CSU/scripts/export_specs_to_md.py"
    assert (conv / "README.md").is_file()
    assert (conv / "SRC-07_linxcore950_csu_design_spec.md").is_file()
    assert list(conv.glob("SRC-01_xlsx_*.md")), "missing XLSX-derived markdown"


@_mark("step3")
def test_step03_port_and_feature_lists():
    """Step 3: port_list.md, feature_list.md, optional finer workflow exist."""
    assert (_DOCS / "port_list.md").is_file()
    assert (_DOCS / "feature_list.md").is_file()
    assert (_DOCS / "workflow_substeps.md").is_file()
    fl = (_DOCS / "feature_list.md").read_text(encoding="utf-8")
    assert "SRC-07 digest heading checklist (full)" in fl
    assert "| F-075 |" in fl or "F-075" in fl


def _feature_table_ids(md_path: Path) -> set[int]:
    """Unique F-xxx IDs from Markdown table rows ``| F-NNN |`` (first column)."""
    text = md_path.read_text(encoding="utf-8")
    return {int(m.group(1)) for m in re.finditer(r"^\| F-(\d{3}) \|", text, re.MULTILINE)}


@_mark("step3")
def test_feature_implementation_registry_matches_feature_list():
    """Every F-xxx in feature_list main tables has exactly one row in implementation status."""
    st = _DOCS / "feature_implementation_status.md"
    assert st.is_file(), "missing docs/feature_implementation_status.md"
    fl_ids = _feature_table_ids(_DOCS / "feature_list.md")
    st_ids = _feature_table_ids(st)
    assert fl_ids == st_ids, (
        f"feature_implementation_status.md F-ID set must match feature_list.md; "
        f"only in feature_list={sorted(fl_ids - st_ids)}; only in status={sorted(st_ids - fl_ids)}"
    )


@_mark("step3")
def test_feature_implementation_status_all_full():
    """Implementation status table: every data row uses status ``full`` (no none/partial/shell)."""
    st = _DOCS / "feature_implementation_status.md"
    text = st.read_text(encoding="utf-8")
    bad: list[str] = []
    for m in re.finditer(r"^\| (F-\d{3}) \| ([^|]+) \|", text, re.MULTILINE):
        status = m.group(2).strip()
        if status != "full":
            bad.append(f"{m.group(1)}: {status!r}")
    assert not bad, "feature_implementation_status.md rows must be full: " + "; ".join(bad[:12])


@_mark("step4")
def test_step04_function_list():
    """Step 4: algorithmic decomposition documented."""
    text = (_DOCS / "function_list.md").read_text(encoding="utf-8")
    assert "csu_main_cycle" in text or "call graph" in text.lower()


@_mark("step5")
def test_step05_pipeline_doc():
    """Step 5: pipeline / domain.next mapping + explicit occurrence budget."""
    text = (_DOCS / "step5.md").read_text(encoding="utf-8")
    assert "domain.next" in text
    assert (_DOCS / "cycle_budget.md").is_file()
    cb = (_DOCS / "cycle_budget.md").read_text(encoding="utf-8")
    assert "Inc-0" in cb and "domain.next()" in cb


@_mark("step6")
def test_step06_emit_mlir_cycle_aware_shell():
    """Step 6: V5 shell + Inc-0 MLIR matches ``cycle_budget.md`` (via emit hook)."""
    mlir = emit_csu_mlir()
    assert "func.func @csu" in mlir
    assert "txreq" in mlir and "i97" in mlir
    assert "txdat" in mlir and "i615" in mlir
    assert "latched_txreq" in mlir
    src = (_CSU_DIR / "csu.py").read_text(encoding="utf-8")
    assert src.count("domain.next()") == INC0_DOMAIN_NEXT_COUNT
    assert INC0_OCCURRENCE_STAGES == INC0_DOMAIN_NEXT_COUNT + 1


@_mark("step7")
def test_inc1_all_pyc_reg_use_domain_rst():
    """Inc-1 (F-001): every emitted ``pyc.reg`` is clocked with the top ``rst`` reset port."""
    mlir = emit_csu_mlir()
    reg_lines = [ln for ln in mlir.splitlines() if "pyc.reg" in ln and "pyc.regfile" not in ln]
    assert len(reg_lines) == csu_mod.INC0_EXPECTED_PYC_REG_COUNT, (
        f"expected {csu_mod.INC0_EXPECTED_PYC_REG_COUNT} pyc.reg lines, got {len(reg_lines)}"
    )
    bad = [ln for ln in reg_lines if ", %rst, " not in ln]
    assert not bad, f"pyc.reg line(s) missing ', %rst, ' reset operand: {bad[:3]}"


@_mark("step7")
def test_tb_csu_present():
    """T-001 harness file exists (SV/C++ TB generation via ``pycircuit build``)."""
    p = _CSU_DIR / "tb_csu.py"
    assert p.is_file()
    txt = p.read_text(encoding="utf-8")
    assert "@testbench" in txt and "def tb_csu" in txt


@_mark("step7")
def test_step07_traceability_matrix():
    """Step 7: traceability file links ports, features, tests."""
    text = (_DOCS / "traceability.md").read_text(encoding="utf-8")
    assert "F-001" in text and "T-001" in text


@_mark("step8")
def test_step08_test_list_covers_ports():
    """Step 8: test_list enumerates directed + system tests."""
    text = (_DOCS / "test_list.md").read_text(encoding="utf-8")
    assert "T-001" in text and "SYS-01" in text


@_mark("step9")
def test_step09_incremental_plan_and_impl_log_or_stub():
    """Step 9: incremental plan exists; IMPLEMENTATION_LOG optional until first PR."""
    assert (_DOCS / "incremental_plan.md").is_file()
    _log = _CSU_DIR / "IMPLEMENTATION_LOG.md"
    # Optional: create IMPLEMENTATION_LOG.md when Inc-0 lands; do not fail stdlib runner.
    if _log.is_file() and _log.stat().st_size > 0:
        assert _log.read_text(encoding="utf-8").strip()


@_mark("step10")
def test_step10_system_test_spec():
    """Step 10: system test specification present."""
    assert (_DOCS / "system_test_spec.md").is_file()


@_mark("system")
def test_system_compile_and_port_width_constants():
    """System-level sanity: constants match port_list.md §3."""
    assert W_TXREQ == 97
    assert W_TXRSP == 30
    assert W_RXRSP == 42
    assert W_TXDAT == 615
    assert W_RXDAT == 584
    mlir = emit_csu_mlir()
    assert mlir.count("pyc.reg") == csu_mod.INC0_EXPECTED_PYC_REG_COUNT
    assert csu_mod._OPCODE_LO == 21  # REQFLIT opcode LSB per SRC-01 Sheet1
    assert len(csu_mod.LEGAL_REQ_OPCODE_VALUES) == 25  # SRC-02 Yes-count (export sync)


def test_all_steps_regression():
    """Run all step markers in one shot (quick regression)."""
    test_feature_implementation_registry_matches_feature_list()
    test_feature_implementation_status_all_full()
    test_inc1_all_pyc_reg_use_domain_rst()
    mlir = emit_csu_mlir()
    assert len(mlir) > 2000
