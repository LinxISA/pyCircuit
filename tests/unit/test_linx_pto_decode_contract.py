from __future__ import annotations

import importlib.util
from pathlib import Path

import pytest


@pytest.mark.unit
def test_linx_pto_decode_contract() -> None:
    root = Path(__file__).resolve().parents[2]
    checker = root / "contrib/linx/flows/tools/check_pto_isa_v058_decode.py"
    spec = importlib.util.spec_from_file_location("linx_pto_decode_contract", checker)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    assert module.main() == 0
