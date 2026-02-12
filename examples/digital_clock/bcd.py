# -*- coding: utf-8 -*-
"""Binary-to-BCD conversion for small ranges (0-59, 0-23).

Each converter creates a combinational wire ``tens`` via ``domain.signal()``
+ ``.set()`` priority-MUX chain, then computes ``ones = value − tens × 10``
arithmetically.  The result is an 8-bit ``ca_cat(tens, ones)`` BCD value
with tens in the high nibble.
"""
from __future__ import annotations

from pycircuit import CycleAwareDomain, CycleAwareSignal, ca_cat


def bin_to_bcd_60(
    domain: CycleAwareDomain,
    value: CycleAwareSignal,
    name: str,
) -> CycleAwareSignal:
    """Convert a 6-bit binary value (0–59) to 8-bit BCD ``{tens[3:0], ones[3:0]}``."""
    c = lambda v, w: domain.const(v, width=w)

    # Tens digit via priority MUX (last-write-wins → highest match wins)
    tens = domain.signal(f"{name}_tens", width=4)
    tens.set(c(0, 4))
    tens.set(c(1, 4), when=value.ge(c(10, 6)))
    tens.set(c(2, 4), when=value.ge(c(20, 6)))
    tens.set(c(3, 4), when=value.ge(c(30, 6)))
    tens.set(c(4, 4), when=value.ge(c(40, 6)))
    tens.set(c(5, 4), when=value.ge(c(50, 6)))

    # Ones digit = value − tens × 10
    # tens × 10 = (tens << 3) + (tens << 1)  (all in 6-bit arithmetic)
    tens_6 = tens.zext(width=6)
    tens_x10 = (tens_6 << 3) + (tens_6 << 1)
    ones = (value - tens_x10).trunc(width=4)

    return ca_cat(tens, ones)


def bin_to_bcd_24(
    domain: CycleAwareDomain,
    value: CycleAwareSignal,
    name: str,
) -> CycleAwareSignal:
    """Convert a 5-bit binary value (0–23) to 8-bit BCD ``{tens[3:0], ones[3:0]}``."""
    c = lambda v, w: domain.const(v, width=w)

    tens = domain.signal(f"{name}_tens", width=4)
    tens.set(c(0, 4))
    tens.set(c(1, 4), when=value.ge(c(10, 5)))
    tens.set(c(2, 4), when=value.ge(c(20, 5)))

    tens_5 = tens.zext(width=5)
    tens_x10 = (tens_5 << 3) + (tens_5 << 1)
    ones = (value - tens_x10).trunc(width=4)

    return ca_cat(tens, ones)
