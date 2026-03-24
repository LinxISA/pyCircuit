"""Tests for LiteralValue module specialization (fix: constant-input dedup).

When a @module is instantiated multiple times with different LiteralValue
constants (e.g. u(5, 0) vs u(5, 4)), each distinct constant value must
produce a separate module specialization. Previously, LiteralValue was
classified as a hardware port and excluded from the specialization hash,
causing all instances to share a single (incorrect) module definition.

These tests verify:
1. _canon_param() correctly serializes LiteralValue
2. Different LiteralValue values produce different specialization hashes
3. Same LiteralValue values produce the same hash (dedup still works)
"""

from __future__ import annotations

import json

import pytest

from pycircuit.design import _canon_param, _params_hash8, canonical_params_json
from pycircuit.literals import LiteralValue, u, s


class TestCanonParamLiteral:
    """_canon_param should serialize LiteralValue deterministically."""

    def test_basic_literal(self) -> None:
        result = _canon_param(u(5, 4), path="test")
        assert result == {"kind": "literal", "value": 4, "width": 5, "signed": False}

    def test_signed_literal(self) -> None:
        result = _canon_param(s(8, -3), path="test")
        assert result == {"kind": "literal", "value": -3, "width": 8, "signed": True}

    def test_width_none(self) -> None:
        """LiteralValue with width=None (e.g. from U() / S())."""
        lit = LiteralValue(value=42, width=None, signed=None)
        result = _canon_param(lit, path="test")
        assert result == {"kind": "literal", "value": 42, "width": None, "signed": None}

    def test_literal_in_dict(self) -> None:
        """LiteralValue nested in a dict param."""
        result = _canon_param({"offset": u(8, 10)}, path="test")
        assert result == {"offset": {"kind": "literal", "value": 10, "width": 8, "signed": False}}

    def test_literal_in_list(self) -> None:
        """LiteralValue nested in a list param."""
        result = _canon_param([u(5, 0), u(5, 1)], path="test")
        assert len(result) == 2
        assert result[0]["value"] == 0
        assert result[1]["value"] == 1


class TestSpecializationHashDistinctness:
    """Different LiteralValue constants must produce different hashes."""

    def _hash_for(self, **params) -> str:
        """Compute the specialization hash for given params."""
        params_json = canonical_params_json(params)
        return _params_hash8(params_json)

    def test_different_values_different_hash(self) -> None:
        """u(5, 0) vs u(5, 4) must produce different specialization hashes."""
        h0 = self._hash_for(in_dir=u(5, 0))
        h4 = self._hash_for(in_dir=u(5, 4))
        assert h0 != h4, f"Different literal values must produce different hashes, got {h0}"

    def test_same_value_same_hash(self) -> None:
        """Two identical LiteralValues must produce the same hash (dedup works)."""
        h1 = self._hash_for(in_dir=u(5, 4))
        h2 = self._hash_for(in_dir=u(5, 4))
        assert h1 == h2

    def test_different_widths_different_hash(self) -> None:
        """Same value but different width should produce different hash."""
        h5 = self._hash_for(offset=u(5, 3))
        h8 = self._hash_for(offset=u(8, 3))
        assert h5 != h8

    def test_signed_vs_unsigned_different_hash(self) -> None:
        """Same value/width but different signedness should produce different hash."""
        hu = self._hash_for(val=u(8, 5))
        hs = self._hash_for(val=s(8, 5))
        assert hu != hs

    def test_five_directions_five_hashes(self) -> None:
        """Simulates the route_compute scenario: 5 different in_dir values."""
        hashes = set()
        for d in range(5):
            mask = 0 if d == 4 else (1 << d)
            h = self._hash_for(in_dir=u(5, mask))
            hashes.add(h)
        assert len(hashes) == 5, f"Expected 5 distinct hashes, got {len(hashes)}: {hashes}"

    def test_non_literal_params_still_work(self) -> None:
        """Regular int/str params continue to work correctly."""
        h1 = self._hash_for(depth=4, name="fifo")
        h2 = self._hash_for(depth=8, name="fifo")
        h3 = self._hash_for(depth=4, name="fifo")
        assert h1 != h2
        assert h1 == h3

    def test_mixed_literal_and_plain(self) -> None:
        """LiteralValue mixed with regular params."""
        h1 = self._hash_for(depth=4, offset=u(8, 0))
        h2 = self._hash_for(depth=4, offset=u(8, 10))
        h3 = self._hash_for(depth=8, offset=u(8, 0))
        assert h1 != h2  # different literal
        assert h1 != h3  # different plain param
        assert h2 != h3  # both different


class TestCanonParamJsonDeterminism:
    """canonical_params_json must be fully deterministic for LiteralValue."""

    def test_deterministic(self) -> None:
        """Multiple calls with same params produce identical JSON."""
        params = {"a": u(5, 1), "b": u(8, 42), "c": 7}
        j1 = canonical_params_json(params)
        j2 = canonical_params_json(params)
        assert j1 == j2

    def test_key_order_invariant(self) -> None:
        """Dict key order doesn't affect the output (sorted keys)."""
        j1 = canonical_params_json({"b": u(5, 1), "a": u(8, 2)})
        j2 = canonical_params_json({"a": u(8, 2), "b": u(5, 1)})
        assert j1 == j2
