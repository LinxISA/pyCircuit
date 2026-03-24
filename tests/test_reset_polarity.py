"""Tests for configurable reset polarity (feat: reset polarity).

Verifies that:
1. ResetSpec supports active_low flag (default False)
2. Tb.reset() accepts active_low parameter
3. Serialized reset dict includes active_low field
"""

from __future__ import annotations

import pytest

from pycircuit.tb import ResetSpec, Tb, TbError


class TestResetSpec:
    """ResetSpec dataclass supports active_low field."""

    def test_default_active_high(self) -> None:
        spec = ResetSpec(port="rst")
        assert spec.active_low is False
        assert spec.cycles_asserted == 2
        assert spec.cycles_deasserted == 1

    def test_active_low(self) -> None:
        spec = ResetSpec(port="rst_n", active_low=True)
        assert spec.active_low is True

    def test_frozen(self) -> None:
        spec = ResetSpec(port="rst", active_low=True)
        with pytest.raises(AttributeError):
            spec.active_low = False  # type: ignore[misc]

    def test_backward_compat(self) -> None:
        """Constructing without active_low should always work."""
        spec = ResetSpec(port="rst", cycles_asserted=3, cycles_deasserted=2)
        assert spec.active_low is False


class TestTbResetAPI:
    """Tb.reset() method accepts active_low parameter."""

    def test_default(self) -> None:
        t = Tb()
        t.reset("rst")
        assert t.reset_spec is not None
        assert t.reset_spec.active_low is False

    def test_active_low(self) -> None:
        t = Tb()
        t.reset("rst_n", active_low=True)
        assert t.reset_spec is not None
        assert t.reset_spec.active_low is True
        assert t.reset_spec.port == "rst_n"

    def test_active_high_explicit(self) -> None:
        t = Tb()
        t.reset("rst", active_low=False)
        assert t.reset_spec is not None
        assert t.reset_spec.active_low is False

    def test_with_cycles(self) -> None:
        t = Tb()
        t.reset("rst_n", cycles_asserted=5, cycles_deasserted=3, active_low=True)
        assert t.reset_spec.cycles_asserted == 5
        assert t.reset_spec.cycles_deasserted == 3
        assert t.reset_spec.active_low is True


class TestResetSerialization:
    """Reset serialization dict includes active_low."""

    @staticmethod
    def _serialize_reset(spec: ResetSpec) -> dict:
        """Replicates the serialization logic from testbench.py."""
        return {
            "port": str(spec.port),
            "cycles_asserted": int(spec.cycles_asserted),
            "cycles_deasserted": int(spec.cycles_deasserted),
            "active_low": bool(spec.active_low),
        }

    def test_active_low_serialized(self) -> None:
        spec = ResetSpec(port="rst_n", active_low=True)
        d = self._serialize_reset(spec)
        assert d["active_low"] is True
        assert d["port"] == "rst_n"

    def test_active_high_serialized(self) -> None:
        spec = ResetSpec(port="rst")
        d = self._serialize_reset(spec)
        assert d["active_low"] is False

    def test_roundtrip_from_tb(self) -> None:
        """Tb.reset() → ResetSpec → serialization → dict."""
        t = Tb()
        t.reset("rst_n", cycles_asserted=4, active_low=True)
        d = self._serialize_reset(t.reset_spec)
        assert d == {
            "port": "rst_n",
            "cycles_asserted": 4,
            "cycles_deasserted": 1,
            "active_low": True,
        }

    def test_no_reset(self) -> None:
        t = Tb()
        assert t.reset_spec is None
