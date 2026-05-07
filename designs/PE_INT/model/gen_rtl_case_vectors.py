from __future__ import annotations
import argparse
import random
from pathlib import Path
from typing import Iterable, Sequence

from ref_model import (
    MODE_2A,
    MODE_2B,
    MODE_2C,
    MODE_2D,
    MacResult,
    compute_transaction,
    pack_s4x8_to_40,
    pack_s5x8_to_40,
    pack_s5x16_to_80,
    pack_s8x8_to_laneword,
)

VEC_PER_CASE = 1000


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


def _hex_word(width: int, value: int) -> str:
    digits = (width + 3) // 4
    return f"{(value & ((1 << width) - 1)):0{digits}x}"


def _write_mem(path: Path, width: int, values: Sequence[int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(_hex_word(width, item) for item in values) + "\n", encoding="utf-8")
    print(f"generated: {path}")


def _tx_result(mode: int, a: int, b: int, b1: int, e1_a, e1_b0, e1_b1) -> MacResult:
    return compute_transaction(mode, a, b, b1, e1_a, e1_b0, e1_b1)


def _expected_stream_by_accepted_tx(txs: Iterable[tuple[int, int, int, int, int, tuple[int, int], tuple[int, int], tuple[int, int]]]):
    exp_o0: list[int] = []
    exp_o1: list[int] = []
    out1_hold = 0
    accepted = 0
    for vld, mode, a, b, b1, e1_a, e1_b0, e1_b1 in txs:
        if not vld:
            continue
        accepted += 1
        mac = _tx_result(mode, a, b, b1, e1_a, e1_b0, e1_b1)
        exp_o0.append(mac.out0_19)
        if mode == MODE_2A:
            exp_o1.append(out1_hold)
        else:
            out1_hold = mac.out1_16
            exp_o1.append(out1_hold)
    return accepted, exp_o0, exp_o1


def gen_sanity_case_headers(gen_dir: Path, seed: int) -> None:
    rng = random.Random(seed)

    tx_2b = []
    for _ in range(VEC_PER_CASE):
        a, b, b1, e1a, e1b0, e1b1 = _make_txn_rng(MODE_2B, rng)
        tx_2b.append((1, MODE_2B, a, b, b1, e1a, e1b0, e1b1))
    _, exp2b_o0, exp2b_o1 = _expected_stream_by_accepted_tx(tx_2b)
    _write_mem(gen_dir / "tc_mode2b_sanity_tx_a.mem", 80, [item[2] for item in tx_2b])
    _write_mem(gen_dir / "tc_mode2b_sanity_tx_b.mem", 80, [item[3] for item in tx_2b])
    _write_mem(gen_dir / "tc_mode2b_sanity_exp_o0.mem", 19, exp2b_o0)
    _write_mem(gen_dir / "tc_mode2b_sanity_exp_o1.mem", 16, exp2b_o1)

    cases_2a = []
    tx_2a_stream = []
    for _ in range(VEC_PER_CASE):
        a_pre, b_pre, b1_pre, e1a_pre, e1b0_pre, e1b1_pre = _make_txn_rng(MODE_2B, rng)
        a_2a, b_2a, b1_2a, e1a_2a, e1b0_2a, e1b1_2a = _make_txn_rng(MODE_2A, rng)
        cases_2a.append((a_pre, b_pre, a_2a, b_2a))
        tx_2a_stream.append((1, MODE_2B, a_pre, b_pre, b1_pre, e1a_pre, e1b0_pre, e1b1_pre))
        tx_2a_stream.append((1, MODE_2A, a_2a, b_2a, b1_2a, e1a_2a, e1b0_2a, e1b1_2a))
    _, exp2a_o0, exp2a_o1 = _expected_stream_by_accepted_tx(tx_2a_stream)
    _write_mem(gen_dir / "tc_mode2a_sanity_pre_a.mem", 80, [item[0] for item in cases_2a])
    _write_mem(gen_dir / "tc_mode2a_sanity_pre_b.mem", 80, [item[1] for item in cases_2a])
    _write_mem(gen_dir / "tc_mode2a_sanity_a2a.mem", 80, [item[2] for item in cases_2a])
    _write_mem(gen_dir / "tc_mode2a_sanity_b2a.mem", 80, [item[3] for item in cases_2a])
    _write_mem(gen_dir / "tc_mode2a_sanity_exp_o0.mem", 19, exp2a_o0)
    _write_mem(gen_dir / "tc_mode2a_sanity_exp_o1.mem", 16, exp2a_o1)

    tx_2d = []
    for _ in range(VEC_PER_CASE):
        a, b, b1, e1a, e1b0, e1b1 = _make_txn_rng(MODE_2D, rng)
        tx_2d.append((1, MODE_2D, a, b, b1, e1a, e1b0, e1b1))
    _, exp2d_o0, exp2d_o1 = _expected_stream_by_accepted_tx(tx_2d)
    _write_mem(gen_dir / "tc_mode2d_sanity_tx_a.mem", 80, [item[2] for item in tx_2d])
    _write_mem(gen_dir / "tc_mode2d_sanity_tx_b.mem", 80, [item[3] for item in tx_2d])
    _write_mem(gen_dir / "tc_mode2d_sanity_exp_o0.mem", 19, exp2d_o0)
    _write_mem(gen_dir / "tc_mode2d_sanity_exp_o1.mem", 16, exp2d_o1)

    tx_2c = []
    for _ in range(VEC_PER_CASE):
        a, b0, b1, e1a, e1b0, e1b1 = _make_txn_rng(MODE_2C, rng)
        tx_2c.append((1, MODE_2C, a, b0, b1, e1a, e1b0, e1b1))
    _, exp2c_o0, exp2c_o1 = _expected_stream_by_accepted_tx(tx_2c)
    _write_mem(gen_dir / "tc_mode2c_sanity_tx_a.mem", 80, [item[2] for item in tx_2c])
    _write_mem(gen_dir / "tc_mode2c_sanity_tx_b0.mem", 80, [item[3] for item in tx_2c])
    _write_mem(gen_dir / "tc_mode2c_sanity_tx_b1.mem", 80, [item[4] for item in tx_2c])
    _write_mem(gen_dir / "tc_mode2c_sanity_tx_e1a.mem", 2, [item[5][0] | (item[5][1] << 1) for item in tx_2c])
    _write_mem(gen_dir / "tc_mode2c_sanity_tx_e1b0.mem", 2, [item[6][0] | (item[6][1] << 1) for item in tx_2c])
    _write_mem(gen_dir / "tc_mode2c_sanity_tx_e1b1.mem", 2, [item[7][0] | (item[7][1] << 1) for item in tx_2c])
    _write_mem(gen_dir / "tc_mode2c_sanity_exp_o0.mem", 19, exp2c_o0)
    _write_mem(gen_dir / "tc_mode2c_sanity_exp_o1.mem", 16, exp2c_o1)


def gen_mode_switch_random(gen_dir: Path, seed: int) -> None:
    rng = random.Random(seed)
    tx = []
    modes = [MODE_2A, MODE_2B, MODE_2C, MODE_2D]
    for _ in range(VEC_PER_CASE):
        vld = rng.randint(0, 1)
        mode = rng.choice(modes)
        a, b, b1, e1a, e1b0, e1b1 = _make_txn_rng(mode, rng)
        tx.append((vld, mode, a, b, b1, e1a, e1b0, e1b1))

    exp_count, exp_o0, exp_o1 = _expected_stream_by_accepted_tx(tx)
    _write_mem(gen_dir / "tc_mode_switch_random_tx_vld.mem", 1, [item[0] for item in tx])
    _write_mem(gen_dir / "tc_mode_switch_random_tx_mode.mem", 2, [item[1] for item in tx])
    _write_mem(gen_dir / "tc_mode_switch_random_tx_a.mem", 80, [item[2] for item in tx])
    _write_mem(gen_dir / "tc_mode_switch_random_tx_b.mem", 80, [item[3] for item in tx])
    _write_mem(gen_dir / "tc_mode_switch_random_tx_b1.mem", 80, [item[4] for item in tx])
    _write_mem(gen_dir / "tc_mode_switch_random_tx_e1a.mem", 2, [item[5][0] | (item[5][1] << 1) for item in tx])
    _write_mem(gen_dir / "tc_mode_switch_random_tx_e1b0.mem", 2, [item[6][0] | (item[6][1] << 1) for item in tx])
    _write_mem(gen_dir / "tc_mode_switch_random_tx_e1b1.mem", 2, [item[7][0] | (item[7][1] << 1) for item in tx])
    _write_mem(gen_dir / "tc_mode_switch_random_exp_o0.mem", 19, list(exp_o0) + [0] * (VEC_PER_CASE - exp_count))
    _write_mem(gen_dir / "tc_mode_switch_random_exp_o1.mem", 16, list(exp_o1) + [0] * (VEC_PER_CASE - exp_count))
    _write_mem(gen_dir / "tc_mode_switch_random_meta.mem", 32, [exp_count])


def gen_mode_fixed_random_timing(gen_dir: Path, seed: int) -> None:
    for case_name, mode in [("mode2a", MODE_2A), ("mode2b", MODE_2B), ("mode2c", MODE_2C), ("mode2d", MODE_2D)]:
        rng = random.Random(seed + mode * 101)
        tx = []
        for _ in range(VEC_PER_CASE):
            vld = rng.randint(0, 1)
            a, b, b1, e1a, e1b0, e1b1 = _make_txn_rng(mode, rng)
            tx.append((vld, mode, a, b, b1, e1a, e1b0, e1b1))
        exp_count, exp_o0, exp_o1 = _expected_stream_by_accepted_tx(tx)
        prefix = f"tc_{case_name}_sanity_rand_timing"
        _write_mem(gen_dir / f"{prefix}_tx_vld.mem", 1, [item[0] for item in tx])
        _write_mem(gen_dir / f"{prefix}_tx_a.mem", 80, [item[2] for item in tx])
        _write_mem(gen_dir / f"{prefix}_tx_b.mem", 80, [item[3] for item in tx])
        _write_mem(gen_dir / f"{prefix}_tx_b1.mem", 80, [item[4] for item in tx])
        _write_mem(gen_dir / f"{prefix}_tx_e1a.mem", 2, [item[5][0] | (item[5][1] << 1) for item in tx])
        _write_mem(gen_dir / f"{prefix}_tx_e1b0.mem", 2, [item[6][0] | (item[6][1] << 1) for item in tx])
        _write_mem(gen_dir / f"{prefix}_tx_e1b1.mem", 2, [item[7][0] | (item[7][1] << 1) for item in tx])
        _write_mem(gen_dir / f"{prefix}_exp_o0.mem", 19, list(exp_o0) + [0] * (VEC_PER_CASE - exp_count))
        _write_mem(gen_dir / f"{prefix}_exp_o1.mem", 16, list(exp_o1) + [0] * (VEC_PER_CASE - exp_count))
        _write_mem(gen_dir / f"{prefix}_meta.mem", 32, [exp_count])


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate RTL testcase vectors from ref model.")
    parser.add_argument("--seed", type=int, default=20260420, help="base seed for random vectors")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    gen_dir = repo / "tb_rtl" / "case" / "generated"
    gen_sanity_case_headers(gen_dir, args.seed)
    gen_mode_switch_random(gen_dir, args.seed)
    gen_mode_fixed_random_timing(gen_dir, args.seed)


if __name__ == "__main__":
    main()
