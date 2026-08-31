from __future__ import annotations

import importlib.util
from pathlib import Path

import pytest

REPOSITORY = Path(__file__).resolve().parents[2]
TOOL = (
    REPOSITORY
    / "contrib"
    / "linx"
    / "flows"
    / "tools"
    / "normalize_legacy_commit_trace.py"
)
DIFF_TOOL = REPOSITORY / "contrib" / "linx" / "flows" / "tools" / "linx_trace_diff.py"


def _load_tool():
    spec = importlib.util.spec_from_file_location("normalize_legacy_commit_trace", TOOL)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _load_diff_tool():
    spec = importlib.util.spec_from_file_location("linx_trace_diff", DIFF_TOOL)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.mark.unit
def test_normalize_legacy_trace_fills_current_schema_and_group_length() -> None:
    tool = _load_tool()
    rows = [
        {
            "cycle": 0,
            "pc": 0x10000,
            "insn": 0x20000031,
            "next_pc": 0x10000,
            "wb_valid": 0,
            "wb_rd": 0,
            "wb_data": 0,
            "mem_valid": 1,
            "mem_addr": 0x20000,
            "mem_wdata": 1,
            "mem_rdata": 0,
            "mem_size": 8,
            "trap_valid": 0,
            "trap_cause": 0,
        },
        {
            "cycle": 1,
            "pc": 0x10000,
            "insn": 0x20000031,
            "next_pc": 0x10004,
            "wb_valid": 1,
            "wb_rd": 3,
            "wb_data": 9,
            "mem_valid": 0,
            "mem_addr": 0,
            "mem_wdata": 0,
            "mem_rdata": 0,
            "mem_size": 0,
            "trap_valid": 0,
            "trap_cause": 0,
        },
    ]

    normalized = tool.normalize(rows)

    assert [row["len"] for row in normalized] == [4, 4]
    assert normalized[0]["src0_valid"] == 0
    assert normalized[1]["dst_valid"] == 1
    assert normalized[1]["dst_reg"] == 3
    assert normalized[1]["dst_data"] == 9
    assert normalized[0]["mem_is_store"] == 0
    assert normalized[0]["traparg0"] == 0


@pytest.mark.unit
def test_boundary_collapse_removes_micro_event_memory_group() -> None:
    tool = _load_diff_tool()
    rows = [
        tool.TraceRec(
            {
                "pc": 0x10000,
                "insn": 0x20000031,
                "next_pc": 0x10000,
                "mem_valid": 1,
                "mem_addr": 0x20000,
                "mem_wdata": 1,
                "mem_rdata": 0,
                "mem_size": 8,
            }
        ),
        tool.TraceRec(
            {
                "pc": 0x10000,
                "insn": 0x20000031,
                "next_pc": 0x10004,
                "mem_valid": 1,
                "mem_addr": 0x20008,
                "mem_wdata": 1,
                "mem_rdata": 0,
                "mem_size": 8,
            }
        ),
    ]

    collapsed = tool._collapse_boundary_selfloops(rows)

    assert len(collapsed) == 1
    assert collapsed[0].raw["next_pc"] == 0x10004
    assert collapsed[0].raw["mem_valid"] == 0
    assert collapsed[0].raw["mem_addr"] == 0
