from __future__ import annotations

import pytest
from pycircuit.cli import _base_name_of, _collect_jit_params, _is_timed_domain_build

pytestmark = pytest.mark.unit


def timed_build(m, domain, *, width: int = 8, signed: bool = False) -> None:
    _ = (m, domain, width, signed)


timed_build.__pycircuit_name__ = "timed_smoke"


def structural_build(m, *, depth: int = 4) -> None:
    _ = (m, depth)


def test_collect_jit_params_skips_cycle_aware_domain_argument() -> None:
    assert _is_timed_domain_build(timed_build) is True
    assert _collect_jit_params(timed_build, overrides=[]) == {
        "signed": False,
        "width": 8,
    }


def test_collect_jit_params_keeps_structural_defaults() -> None:
    assert _is_timed_domain_build(structural_build) is False
    assert _collect_jit_params(structural_build, overrides=[]) == {"depth": 4}


def test_base_name_prefers_public_cycle_aware_symbol_override() -> None:
    assert _base_name_of(timed_build) == "timed_smoke"
    assert _base_name_of(structural_build) == "structural_build"
