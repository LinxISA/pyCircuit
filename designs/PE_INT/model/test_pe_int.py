from __future__ import annotations

import random
import sys
import unittest
from pathlib import Path

MODEL_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(MODEL_DIR))

from pe_int_pycircuit_eval import DEFAULT_PIPELINE_L, PEIntL3Model
from ref_model import (
    MODE_2A,
    MODE_2B,
    MODE_2C,
    MODE_2D,
    compute_transaction,
    pack_s4x8_to_40,
    pack_s5x8_to_40,
    pack_s5x16_to_80,
    pack_s8x8_to_laneword,
)


def _rand_s(width: int) -> int:
    lo = -(1 << (width - 1))
    hi = (1 << (width - 1)) - 1
    return random.randint(lo, hi)


def _make_txn(mode: int) -> tuple[int, int, int, tuple[int, int], tuple[int, int], tuple[int, int]]:
    if mode == MODE_2A:
        a = [_rand_s(8) for _ in range(8)]
        b = [_rand_s(8) for _ in range(8)]
        return (
            pack_s8x8_to_laneword(a),
            pack_s8x8_to_laneword(b),
            0,
            (0, 0),
            (0, 0),
            (0, 0),
        )
    if mode == MODE_2B:
        a = [_rand_s(8) for _ in range(8)]
        b0 = [_rand_s(4) for _ in range(8)]
        b1 = [_rand_s(4) for _ in range(8)]
        b = pack_s4x8_to_40(b0) | (pack_s4x8_to_40(b1) << 40)
        return (pack_s8x8_to_laneword(a), b, 0, (0, 0), (0, 0), (0, 0))
    if mode == MODE_2D:
        a = [_rand_s(8) for _ in range(8)]
        b0 = [_rand_s(5) for _ in range(8)]
        b1 = [_rand_s(5) for _ in range(8)]
        b = pack_s5x8_to_40(b0) | (pack_s5x8_to_40(b1) << 40)
        return (pack_s8x8_to_laneword(a), b, 0, (0, 0), (0, 0), (0, 0))
    # MODE_2C
    a = [_rand_s(5) for _ in range(16)]
    b0 = [_rand_s(5) for _ in range(16)]
    b1 = [_rand_s(5) for _ in range(16)]
    e1a = (random.randint(0, 1), random.randint(0, 1))
    e1b0 = (random.randint(0, 1), random.randint(0, 1))
    e1b1 = (random.randint(0, 1), random.randint(0, 1))
    return (
        pack_s5x16_to_80(a),
        pack_s5x16_to_80(b0),
        pack_s5x16_to_80(b1),
        e1a,
        e1b0,
        e1b1,
    )


class TestPEIntModel(unittest.TestCase):
    def setUp(self) -> None:
        random.seed(11)

    def test_mode_math_direct(self) -> None:
        for mode in (MODE_2A, MODE_2B, MODE_2C, MODE_2D):
            for _ in range(40):
                a, b, b1, e1a, e1b0, e1b1 = _make_txn(mode)
                got = compute_transaction(mode, a, b, b1, e1a, e1b0, e1b1)
                # smoke checks on bounded output widths
                self.assertGreaterEqual(got.out0_19, -(1 << 18))
                self.assertLessEqual(got.out0_19, (1 << 18) - 1)
                self.assertGreaterEqual(got.out1_16, -(1 << 15))
                self.assertLessEqual(got.out1_16, (1 << 15) - 1)

    def test_pipeline_alignment_and_mode_switch(self) -> None:
        dut = PEIntL3Model()
        expected: list[tuple[int, int, int, int]] = []  # cycle, mode, out0, out1
        out_hits = 0

        for cycle in range(120):
            vld = random.randint(0, 1)
            mode = random.choice((MODE_2A, MODE_2B, MODE_2C, MODE_2D))
            a, b, b1, e1a, e1b0, e1b1 = _make_txn(mode)

            if vld:
                exp = compute_transaction(mode, a, b, b1, e1a, e1b0, e1b1)
                expected.append((cycle + DEFAULT_PIPELINE_L, mode, exp.out0_19, exp.out1_16))

            step = dut.step(
                rst_n=1,
                vld=vld,
                mode=mode,
                a=a,
                b=b,
                b1=b1,
                e1_a=e1a,
                e1_b0=e1b0,
                e1_b1=e1b1,
            )

            if step.vld_out:
                self.assertTrue(expected, "vld_out should map to one expected transaction")
                due_cycle, due_mode, exp0, exp1 = expected.pop(0)
                self.assertEqual(cycle, due_cycle)
                self.assertEqual(step.out0, exp0)
                if due_mode != MODE_2A:
                    self.assertEqual(step.out1, exp1)
                out_hits += 1

        # Drain pipeline
        for cycle in range(120, 130):
            step = dut.step(
                rst_n=1,
                vld=0,
                mode=MODE_2A,
                a=0,
                b=0,
                b1=0,
                e1_a=(0, 0),
                e1_b0=(0, 0),
                e1_b1=(0, 0),
            )
            if step.vld_out:
                due_cycle, due_mode, exp0, exp1 = expected.pop(0)
                self.assertEqual(cycle, due_cycle)
                self.assertEqual(step.out0, exp0)
                if due_mode != MODE_2A:
                    self.assertEqual(step.out1, exp1)
                out_hits += 1
        self.assertEqual(len(expected), 0)
        self.assertGreater(out_hits, 0)

    def test_mode2a_out1_no_toggle(self) -> None:
        dut = PEIntL3Model()

        # First, drive one non-2a sample to set out1 base.
        a, b, b1, e1a, e1b0, e1b1 = _make_txn(MODE_2B)
        dut.step(rst_n=1, vld=1, mode=MODE_2B, a=a, b=b, b1=b1, e1_a=e1a, e1_b0=e1b0, e1_b1=e1b1)
        for _ in range(DEFAULT_PIPELINE_L + 1):
            dut.step(rst_n=1, vld=0, mode=MODE_2A, a=0, b=0, b1=0, e1_a=(0, 0), e1_b0=(0, 0), e1_b1=(0, 0))
        base_out1 = dut.out1

        # Then continuous mode2a valid traffic; out1 should hold.
        for _ in range(30):
            a, b, b1, e1a, e1b0, e1b1 = _make_txn(MODE_2A)
            dut.step(rst_n=1, vld=1, mode=MODE_2A, a=a, b=b, b1=b1, e1_a=e1a, e1_b0=e1b0, e1_b1=e1b1)
            self.assertEqual(dut.out1, base_out1)

    def test_async_reset_clears_pipeline(self) -> None:
        dut = PEIntL3Model()
        # inject a valid sample
        a, b, b1, e1a, e1b0, e1b1 = _make_txn(MODE_2D)
        dut.step(rst_n=1, vld=1, mode=MODE_2D, a=a, b=b, b1=b1, e1_a=e1a, e1_b0=e1b0, e1_b1=e1b1)
        # asynchronous reset assertion
        res = dut.step(rst_n=0, vld=1, mode=MODE_2D, a=a, b=b, b1=b1, e1_a=e1a, e1_b0=e1b0, e1_b1=e1b1)
        self.assertEqual(res.vld_out, 0)
        self.assertEqual(res.out0, 0)
        self.assertEqual(res.out1, 0)
        # release reset and keep idle, no stale output should appear
        for _ in range(DEFAULT_PIPELINE_L + 2):
            res = dut.step(rst_n=1, vld=0, mode=MODE_2A, a=0, b=0, b1=0, e1_a=(0, 0), e1_b0=(0, 0), e1_b1=(0, 0))
            self.assertEqual(res.vld_out, 0)


if __name__ == "__main__":
    unittest.main()
