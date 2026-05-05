from __future__ import annotations

import random
import os
import sys
from pathlib import Path

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    CycleAwareTb,
    Tb,
    compile_cycle_aware,
    testbench,
)

_THIS_DIR = Path(__file__).resolve().parent
_PY_DIR = _THIS_DIR.parent / "python"
_MODEL_DIR = _THIS_DIR.parent / "model"
if str(_PY_DIR) not in sys.path:
    sys.path.insert(0, str(_PY_DIR))
if str(_MODEL_DIR) not in sys.path:
    sys.path.insert(0, str(_MODEL_DIR))

from pe_int_pycircuit import build  # noqa: E402
from pe_int_pycircuit_eval import PEIntL3Model  # noqa: E402
from ref_model import (  # noqa: E402
    MODE_2A,
    MODE_2B,
    MODE_2C,
    MODE_2D,
    pack_s4x8_to_40,
    pack_s5x8_to_40,
    pack_s5x16_to_80,
    pack_s8x8_to_laneword,
)

DEFAULT_PARAMS = {"latency": 4}
VEC_PER_CASE = int(os.environ.get("PE_INT_PYC_TB_VEC_PER_CASE", "8"))
BASE_SEED = 20260420


def _to_u(value: int, bits: int) -> int:
    return value & ((1 << bits) - 1)


def _rand_s_rng(rng: random.Random, width: int) -> int:
    lo = -(1 << (width - 1))
    hi = (1 << (width - 1)) - 1
    return rng.randint(lo, hi)


def _make_txn_rng(mode: int, rng: random.Random):
    if mode == MODE_2A:
        a = [_rand_s_rng(rng, 8) for _ in range(8)]
        b = [_rand_s_rng(rng, 8) for _ in range(8)]
        return (
            pack_s8x8_to_laneword(a),
            pack_s8x8_to_laneword(b),
            0,
            (0, 0),
            (0, 0),
            (0, 0),
        )
    if mode == MODE_2B:
        a = [_rand_s_rng(rng, 8) for _ in range(8)]
        b0 = [_rand_s_rng(rng, 4) for _ in range(8)]
        b1 = [_rand_s_rng(rng, 4) for _ in range(8)]
        b = pack_s4x8_to_40(b0) | (pack_s4x8_to_40(b1) << 40)
        return (pack_s8x8_to_laneword(a), b, 0, (0, 0), (0, 0), (0, 0))
    if mode == MODE_2D:
        a = [_rand_s_rng(rng, 8) for _ in range(8)]
        b0 = [_rand_s_rng(rng, 5) for _ in range(8)]
        b1 = [_rand_s_rng(rng, 5) for _ in range(8)]
        b = pack_s5x8_to_40(b0) | (pack_s5x8_to_40(b1) << 40)
        return (pack_s8x8_to_laneword(a), b, 0, (0, 0), (0, 0), (0, 0))
    a = [_rand_s_rng(rng, 5) for _ in range(16)]
    b0 = [_rand_s_rng(rng, 5) for _ in range(16)]
    b1 = [_rand_s_rng(rng, 5) for _ in range(16)]
    e1a = (rng.randint(0, 1), rng.randint(0, 1))
    e1b0 = (rng.randint(0, 1), rng.randint(0, 1))
    e1b1 = (rng.randint(0, 1), rng.randint(0, 1))
    return (
        pack_s5x16_to_80(a),
        pack_s5x16_to_80(b0),
        pack_s5x16_to_80(b1),
        e1a,
        e1b0,
        e1b1,
    )


def _tx(vld: int, mode: int, a: int, b: int, b1: int, e1a, e1b0, e1b1):
    return {
        "vld": int(vld),
        "mode": int(mode) & 0x3,
        "a": int(a),
        "b": int(b),
        "b1": int(b1),
        "e1_a": tuple(e1a),
        "e1_b0": tuple(e1b0),
        "e1_b1": tuple(e1b1),
    }


def _build_sequences():
    seqs = []
    rng = random.Random(BASE_SEED)

    tx_2a = []
    for _ in range(VEC_PER_CASE):
        a_pre, b_pre, _, e1a, e1b0, e1b1 = _make_txn_rng(MODE_2B, rng)
        tx_2a.append(_tx(1, MODE_2B, a_pre, b_pre, 0, e1a, e1b0, e1b1))
        a2, b2, b12, e1a2, e1b02, e1b12 = _make_txn_rng(MODE_2A, rng)
        tx_2a.append(_tx(1, MODE_2A, a2, b2, b12, e1a2, e1b02, e1b12))
    seqs.append(("mode2a_sanity", tx_2a))

    for name, mode in [("mode2b_sanity", MODE_2B), ("mode2c_sanity", MODE_2C), ("mode2d_sanity", MODE_2D)]:
        txs = []
        for _ in range(VEC_PER_CASE):
            a, b, b1, e1a, e1b0, e1b1 = _make_txn_rng(mode, rng)
            txs.append(_tx(1, mode, a, b, b1, e1a, e1b0, e1b1))
        seqs.append((name, txs))

    tx_switch = []
    for _ in range(VEC_PER_CASE):
        vld = rng.randint(0, 1)
        mode = rng.choice([MODE_2A, MODE_2B, MODE_2C, MODE_2D])
        a, b, b1, e1a, e1b0, e1b1 = _make_txn_rng(mode, rng)
        tx_switch.append(_tx(vld, mode, a, b, b1, e1a, e1b0, e1b1))
    seqs.append(("mode_switch_random", tx_switch))

    for name, mode in [
        ("mode2a_sanity_rand_timing", MODE_2A),
        ("mode2b_sanity_rand_timing", MODE_2B),
        ("mode2c_sanity_rand_timing", MODE_2C),
        ("mode2d_sanity_rand_timing", MODE_2D),
    ]:
        rr = random.Random(BASE_SEED + mode * 101)
        txs = []
        for _ in range(VEC_PER_CASE):
            vld = rr.randint(0, 1)
            a, b, b1, e1a, e1b0, e1b1 = _make_txn_rng(mode, rr)
            txs.append(_tx(vld, mode, a, b, b1, e1a, e1b0, e1b1))
        seqs.append((name, txs))

    return seqs


def _idle_tx(rst_n: int):
    return _tx(0, MODE_2A, 0, 0, 0, (0, 0), (0, 0), (0, 0)) | {"rst_n": rst_n}


def _run_one_sequence(tb: CycleAwareTb, name: str, txs) -> None:
    model = PEIntL3Model()
    timeline = []

    for _ in range(3):
        timeline.append(_idle_tx(0))
    for _ in range(3):
        timeline.append(_idle_tx(1))
    for item in txs:
        timeline.append(item | {"rst_n": 1})
    for _ in range(30):
        timeline.append(_idle_tx(1))

    exp = []
    for item in timeline:
        step = model.step(
            rst_n=item["rst_n"],
            vld=item["vld"],
            mode=item["mode"],
            a=item["a"],
            b=item["b"],
            b1=item["b1"],
            e1_a=item["e1_a"],
            e1_b0=item["e1_b0"],
            e1_b1=item["e1_b1"],
        )
        exp.append(step)

    first = timeline[0]
    tb.drive("rst_n", first["rst_n"])
    tb.drive("vld", first["vld"])
    tb.drive("mode", first["mode"])
    tb.drive("a", _to_u(first["a"], 80))
    tb.drive("b", _to_u(first["b"], 80))
    tb.drive("b1", _to_u(first["b1"], 80))
    tb.drive("e1_a", _to_u(first["e1_a"][0] | (first["e1_a"][1] << 1), 2))
    tb.drive("e1_b0", _to_u(first["e1_b0"][0] | (first["e1_b0"][1] << 1), 2))
    tb.drive("e1_b1", _to_u(first["e1_b1"][0] | (first["e1_b1"][1] << 1), 2))

    for idx in range(len(timeline)):
        tb.next()
        tb.expect("vld_out", exp[idx].vld_out)
        if exp[idx].vld_out:
            tb.expect("out0", _to_u(exp[idx].out0, 19))
            tb.expect("out1", _to_u(exp[idx].out1, 16))

        if idx + 1 < len(timeline):
            nxt = timeline[idx + 1]
            tb.drive("rst_n", nxt["rst_n"])
            tb.drive("vld", nxt["vld"])
            tb.drive("mode", nxt["mode"])
            tb.drive("a", _to_u(nxt["a"], 80))
            tb.drive("b", _to_u(nxt["b"], 80))
            tb.drive("b1", _to_u(nxt["b1"], 80))
            tb.drive("e1_a", _to_u(nxt["e1_a"][0] | (nxt["e1_a"][1] << 1), 2))
            tb.drive("e1_b0", _to_u(nxt["e1_b0"][0] | (nxt["e1_b0"][1] << 1), 2))
            tb.drive("e1_b1", _to_u(nxt["e1_b1"][0] | (nxt["e1_b1"][1] << 1), 2))


@testbench
def tb(t: Tb) -> None:
    tb = CycleAwareTb(t)
    tb.clock("clk")
    tb.timeout(25000)

    for name, seq in _build_sequences():
        print(f"[pyc-tb] run {name}, tx_count={len(seq)}")
        _run_one_sequence(tb, name, seq)

    tb.finish()


if __name__ == "__main__":
    print(compile_cycle_aware(build, name="tb_pe_int_top", eager=True, **DEFAULT_PARAMS).emit_mlir())
