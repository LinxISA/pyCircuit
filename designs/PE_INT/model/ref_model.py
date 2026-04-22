from __future__ import annotations

from dataclasses import dataclass
from typing import List, Sequence, Tuple

MODE_2A = 0
MODE_2B = 1
MODE_2C = 2
MODE_2D = 3


def _mask(width: int) -> int:
    return (1 << width) - 1


def to_signed(value: int, width: int) -> int:
    value &= _mask(width)
    sign = 1 << (width - 1)
    return value - (1 << width) if value & sign else value


def get_lane5(word80: int, lane_idx: int) -> int:
    return (word80 >> (5 * lane_idx)) & 0x1F


def decode_s8x8_from_laneword(word80: int) -> List[int]:
    vals: List[int] = []
    for i in range(8):
        lo = (get_lane5(word80, 2 * i) >> 1) & 0xF
        hi = (get_lane5(word80, 2 * i + 1) >> 1) & 0xF
        vals.append(to_signed((hi << 4) | lo, 8))
    return vals


def decode_s4x8_from_40(word40: int) -> List[int]:
    vals: List[int] = []
    for i in range(8):
        lane = (word40 >> (5 * i)) & 0x1F
        vals.append(to_signed((lane >> 1) & 0xF, 4))
    return vals


def decode_s5x8_from_40(word40: int) -> List[int]:
    return [to_signed((word40 >> (5 * i)) & 0x1F, 5) for i in range(8)]


def decode_s5x16_from_80(word80: int) -> List[int]:
    return [to_signed((word80 >> (5 * i)) & 0x1F, 5) for i in range(16)]


def _e1_shift(e1_pair_a: Sequence[int], e1_pair_b: Sequence[int]) -> Tuple[int, int]:
    sh_lo = int(e1_pair_a[0]) + int(e1_pair_b[0])
    sh_hi = int(e1_pair_a[1]) + int(e1_pair_b[1])
    return sh_lo, sh_hi


@dataclass(frozen=True)
class MacResult:
    out0_19: int
    out1_16: int


def compute_transaction(
    mode: int,
    a80: int,
    b80: int,
    b1_80: int,
    e1_a: Sequence[int],
    e1_b0: Sequence[int],
    e1_b1: Sequence[int],
) -> MacResult:
    mode &= 0x3
    a80 &= _mask(80)
    b80 &= _mask(80)
    b1_80 &= _mask(80)

    if mode == MODE_2A:
        a = decode_s8x8_from_laneword(a80)
        b = decode_s8x8_from_laneword(b80)
        sum0 = sum(x * y for x, y in zip(a, b))
        return MacResult(to_signed(sum0, 19), 0)

    if mode == MODE_2B:
        a = decode_s8x8_from_laneword(a80)
        b0 = decode_s4x8_from_40(b80 & _mask(40))
        b1 = decode_s4x8_from_40((b80 >> 40) & _mask(40))
        sum0 = sum(x * y for x, y in zip(a, b0))
        sum1 = sum(x * y for x, y in zip(a, b1))
        return MacResult(to_signed(sum0, 19), to_signed(sum1, 16))

    if mode == MODE_2D:
        a = decode_s8x8_from_laneword(a80)
        b0 = decode_s5x8_from_40(b80 & _mask(40))
        b1 = decode_s5x8_from_40((b80 >> 40) & _mask(40))
        sum0 = sum(x * y for x, y in zip(a, b0))
        sum1 = sum(x * y for x, y in zip(a, b1))
        return MacResult(to_signed(sum0, 19), to_signed(sum1, 16))

    # MODE_2C
    a = decode_s5x16_from_80(a80)
    b0 = decode_s5x16_from_80(b80)
    b1 = decode_s5x16_from_80(b1_80)
    lo0 = sum(a[i] * b0[i] for i in range(8))
    hi0 = sum(a[i] * b0[i] for i in range(8, 16))
    lo1 = sum(a[i] * b1[i] for i in range(8))
    hi1 = sum(a[i] * b1[i] for i in range(8, 16))

    sh0_lo, sh0_hi = _e1_shift(e1_a, e1_b0)
    sh1_lo, sh1_hi = _e1_shift(e1_a, e1_b1)
    sum0 = (lo0 << sh0_lo) + (hi0 << sh0_hi)
    sum1 = (lo1 << sh1_lo) + (hi1 << sh1_hi)
    return MacResult(to_signed(sum0, 19), to_signed(sum1, 16))


def pack_s8x8_to_laneword(values: Sequence[int]) -> int:
    if len(values) != 8:
        raise ValueError("need 8 S8 values")
    word = 0
    for i, val in enumerate(values):
        raw = val & 0xFF
        lane_lo = (raw & 0xF) << 1
        lane_hi = ((raw >> 4) & 0xF) << 1
        word |= lane_lo << (5 * (2 * i))
        word |= lane_hi << (5 * (2 * i + 1))
    return word


def pack_s4x8_to_40(values: Sequence[int]) -> int:
    if len(values) != 8:
        raise ValueError("need 8 S4 values")
    word = 0
    for i, val in enumerate(values):
        lane = ((val & 0xF) << 1) & 0x1F
        word |= lane << (5 * i)
    return word


def pack_s5x8_to_40(values: Sequence[int]) -> int:
    if len(values) != 8:
        raise ValueError("need 8 S5 values")
    word = 0
    for i, val in enumerate(values):
        word |= (val & 0x1F) << (5 * i)
    return word


def pack_s5x16_to_80(values: Sequence[int]) -> int:
    if len(values) != 16:
        raise ValueError("need 16 S5 values")
    word = 0
    for i, val in enumerate(values):
        word |= (val & 0x1F) << (5 * i)
    return word
