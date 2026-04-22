from __future__ import annotations

from pycircuit import cat, wire_of


def sext(value, bits: int):
    if hasattr(value, "sext"):
        return value.sext(width=bits)
    raw = wire_of(value)
    if hasattr(raw, "sext"):
        return raw.sext(width=bits)
    return raw._sext(width=bits)


def lane5(word, idx: int):
    lo = 5 * idx
    return wire_of(word)[lo : lo + 5]


def decode_s8_from_lane_word(word, idx: int):
    lo_lane = lane5(word, 2 * idx)
    hi_lane = lane5(word, 2 * idx + 1)
    lo4 = wire_of(lo_lane)[1:5]
    hi4 = wire_of(hi_lane)[1:5]
    raw8 = cat(hi4, lo4)
    return sext(raw8, 32)


def decode_s4_from_40(word40, idx: int):
    lane = wire_of(word40)[5 * idx : 5 * idx + 5]
    raw4 = wire_of(lane)[1:5]
    return sext(raw4, 32)


def decode_s4_hi_from_80(word80, idx: int):
    lane = wire_of(word80)[40 + 5 * idx : 40 + 5 * idx + 5]
    raw4 = wire_of(lane)[1:5]
    return sext(raw4, 32)


def decode_s5(word, idx: int):
    raw = wire_of(word)[5 * idx : 5 * idx + 5]
    return sext(raw, 32)


def decode_s5_hi_from_80(word80, idx: int):
    raw = wire_of(word80)[40 + 5 * idx : 40 + 5 * idx + 5]
    return sext(raw, 32)
