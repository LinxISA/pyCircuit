"""Tests for the drive_when conditional drive API (tb.py + cli.py codegen).

This module tests:
1. Tb.drive_when() API — input validation and data-model construction
2. DriveWhen dataclass — correct normalization of drives / on_done
"""

from __future__ import annotations

import pytest

from pycircuit.tb import DriveWhen, Tb, TbError


# ──────────────────────────────────────────────────────────────────────
#  1. Basic API: successful construction
# ──────────────────────────────────────────────────────────────────────

class TestDriveWhenBasic:
    """Valid calls to Tb.drive_when should create DriveWhen entries."""

    def test_uniform_dict(self) -> None:
        """A single dict for `drives` is expanded to all firings."""
        tb = Tb()
        tb.drive_when(
            "rdy", 1,
            drives={"vld": 1, "data": 0xAB},
            tag="s0", start=5, repeat=3,
        )
        assert len(tb.drive_whens) == 1
        dw = tb.drive_whens[0]
        assert dw.tag == "s0"
        assert dw.condition_port == "rdy"
        assert dw.condition_value == 1
        assert dw.start == 5
        assert dw.repeat == 3
        # Uniform: all 3 firings should have the same drives
        assert len(dw.drives_sequence) == 3
        assert dw.drives_sequence[0] == dw.drives_sequence[1] == dw.drives_sequence[2]
        assert ("vld", 1) in dw.drives_sequence[0]
        assert ("data", 0xAB) in dw.drives_sequence[0]

    def test_per_firing_list(self) -> None:
        """A list of dicts provides per-firing drive values."""
        tb = Tb()
        seq = [
            {"vld": 1, "tgt": 0},
            {"vld": 1, "tgt": 1},
        ]
        tb.drive_when("rdy", 1, drives=seq, tag="s1", repeat=2)
        dw = tb.drive_whens[0]
        assert len(dw.drives_sequence) == 2
        assert ("tgt", 0) in dw.drives_sequence[0]
        assert ("tgt", 1) in dw.drives_sequence[1]

    def test_on_done(self) -> None:
        """on_done ports are recorded correctly."""
        tb = Tb()
        tb.drive_when(
            "rdy", 1,
            drives={"vld": 1},
            tag="s2", repeat=1,
            on_done={"vld": 0},
        )
        dw = tb.drive_whens[0]
        assert ("vld", 0) in dw.on_done

    def test_defaults(self) -> None:
        """Default start=0, repeat=1, on_done=()."""
        tb = Tb()
        tb.drive_when("rdy", 1, drives={"vld": 1}, tag="d")
        dw = tb.drive_whens[0]
        assert dw.start == 0
        assert dw.repeat == 1
        assert dw.on_done == ()

    def test_condition_value_bool(self) -> None:
        """Bool condition_value is accepted."""
        tb = Tb()
        tb.drive_when("rdy", True, drives={"vld": 1}, tag="b")
        assert tb.drive_whens[0].condition_value is True

    def test_multiple_drive_whens(self) -> None:
        """Multiple drive_when calls accumulate."""
        tb = Tb()
        tb.drive_when("rdy", 1, drives={"vld": 1}, tag="a")
        tb.drive_when("rdy", 1, drives={"vld": 1}, tag="b")
        assert len(tb.drive_whens) == 2
        assert tb.drive_whens[0].tag == "a"
        assert tb.drive_whens[1].tag == "b"


# ──────────────────────────────────────────────────────────────────────
#  2. Validation: error cases
# ──────────────────────────────────────────────────────────────────────

class TestDriveWhenValidation:
    """Invalid inputs should raise TbError."""

    def test_empty_condition_port(self) -> None:
        tb = Tb()
        with pytest.raises(TbError, match="condition_port must be non-empty"):
            tb.drive_when("", 1, drives={"vld": 1}, tag="x")

    def test_empty_tag(self) -> None:
        tb = Tb()
        with pytest.raises(TbError, match="tag must be non-empty"):
            tb.drive_when("rdy", 1, drives={"vld": 1}, tag="")

    def test_invalid_condition_value(self) -> None:
        tb = Tb()
        with pytest.raises(TbError, match="condition_value must be bool or int"):
            tb.drive_when("rdy", "high", drives={"vld": 1}, tag="x")  # type: ignore[arg-type]

    def test_repeat_zero(self) -> None:
        tb = Tb()
        with pytest.raises(TbError, match="repeat must be >= 1"):
            tb.drive_when("rdy", 1, drives={"vld": 1}, tag="x", repeat=0)

    def test_negative_start(self) -> None:
        tb = Tb()
        with pytest.raises(TbError, match="start must be >= 0"):
            tb.drive_when("rdy", 1, drives={"vld": 1}, tag="x", start=-1)

    def test_empty_drives_dict(self) -> None:
        tb = Tb()
        with pytest.raises(TbError, match="drives must be non-empty"):
            tb.drive_when("rdy", 1, drives={}, tag="x")

    def test_drives_list_length_mismatch(self) -> None:
        tb = Tb()
        with pytest.raises(TbError, match="drives list length"):
            tb.drive_when("rdy", 1, drives=[{"vld": 1}], tag="x", repeat=3)

    def test_drives_invalid_type(self) -> None:
        tb = Tb()
        with pytest.raises(TbError, match="drives must be a dict or list"):
            tb.drive_when("rdy", 1, drives="vld=1", tag="x")  # type: ignore[arg-type]


# ──────────────────────────────────────────────────────────────────────
#  3. DriveWhen dataclass properties
# ──────────────────────────────────────────────────────────────────────

class TestDriveWhenDataclass:
    """Direct DriveWhen dataclass tests."""

    def test_frozen(self) -> None:
        """DriveWhen should be immutable (frozen)."""
        dw = DriveWhen(
            tag="t", condition_port="rdy", condition_value=1,
            drives_sequence=((("vld", 1),),),
        )
        with pytest.raises(AttributeError):
            dw.tag = "new"  # type: ignore[misc]

    def test_equality(self) -> None:
        """Two identical DriveWhen instances should be equal."""
        a = DriveWhen(tag="t", condition_port="rdy", condition_value=1,
                      drives_sequence=((("vld", 1),),))
        b = DriveWhen(tag="t", condition_port="rdy", condition_value=1,
                      drives_sequence=((("vld", 1),),))
        assert a == b


# ──────────────────────────────────────────────────────────────────────
#  4. Port name sanitization
# ──────────────────────────────────────────────────────────────────────

class TestDriveWhenSanitization:
    """Port and tag names should be stripped of whitespace."""

    def test_strips_whitespace(self) -> None:
        tb = Tb()
        tb.drive_when(" rdy ", 1, drives={" vld ": 1}, tag=" s0 ")
        dw = tb.drive_whens[0]
        assert dw.condition_port == "rdy"
        assert dw.tag == "s0"
        assert ("vld", 1) in dw.drives_sequence[0]

    def test_on_done_strips_whitespace(self) -> None:
        tb = Tb()
        tb.drive_when("rdy", 1, drives={"vld": 1}, tag="s0",
                       on_done={" vld ": 0})
        dw = tb.drive_whens[0]
        assert ("vld", 0) in dw.on_done
