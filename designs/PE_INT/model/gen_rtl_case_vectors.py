from __future__ import annotations

import argparse
import random
from pathlib import Path

from pe_int_pycircuit_eval import PEIntL3Model
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

VEC_PER_CASE = 1000


def _hex(width: int, value: int) -> str:
    digits = (width + 3) // 4
    return f"{width}'h{(value & ((1 << width) - 1)):0{digits}x}"


def _sdec(width: int, value: int) -> str:
    if value < 0:
        return f"-{width}'sd{-value}"
    return f"{width}'sd{value}"


def _rand_s(width: int) -> int:
    lo = -(1 << (width - 1))
    hi = (1 << (width - 1)) - 1
    return random.randint(lo, hi)


def _rand_s_rng(rng: random.Random, width: int) -> int:
    lo = -(1 << (width - 1))
    hi = (1 << (width - 1)) - 1
    return rng.randint(lo, hi)


def _make_txn(mode: int):
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


def _write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    print(f"generated: {path}")


def _hex_word(width: int, value: int) -> str:
    digits = (width + 3) // 4
    return f"{(value & ((1 << width) - 1)):0{digits}x}"


def _write_mem(path: Path, width: int, values: list[int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(_hex_word(width, v) for v in values) + "\n", encoding="utf-8")
    print(f"generated: {path}")


def gen_sanity_case_headers(gen_dir: Path, seed: int) -> None:
    # Sanity vectors are deterministic for the same seed.
    rng = random.Random(seed)

    # 2b: 1000 vectors
    tx_2b = []
    for _ in range(VEC_PER_CASE):
        a = [_rand_s_rng(rng, 8) for _ in range(8)]
        b0 = [_rand_s_rng(rng, 4) for _ in range(8)]
        b1 = [_rand_s_rng(rng, 4) for _ in range(8)]
        a_w = pack_s8x8_to_laneword(a)
        b_w = pack_s4x8_to_40(b0) | (pack_s4x8_to_40(b1) << 40)
        r = compute_transaction(MODE_2B, a_w, b_w, 0, (0, 0), (0, 0), (0, 0))
        tx_2b.append((a_w, b_w, r.out0_19, r.out1_16))
    _write_mem(gen_dir / "tc_mode2b_sanity_tx_a.mem", 80, [x[0] for x in tx_2b])
    _write_mem(gen_dir / "tc_mode2b_sanity_tx_b.mem", 80, [x[1] for x in tx_2b])
    _write_mem(gen_dir / "tc_mode2b_sanity_exp_o0.mem", 19, [x[2] for x in tx_2b])
    _write_mem(gen_dir / "tc_mode2b_sanity_exp_o1.mem", 16, [x[3] for x in tx_2b])

    # 2a: each case is preload-2b + mode2a
    cases_2a = []
    for _ in range(VEC_PER_CASE):
        a_pre = [_rand_s_rng(rng, 8) for _ in range(8)]
        b0_pre = [_rand_s_rng(rng, 4) for _ in range(8)]
        b1_pre = [_rand_s_rng(rng, 4) for _ in range(8)]
        a_pre_w = pack_s8x8_to_laneword(a_pre)
        b_pre_w = pack_s4x8_to_40(b0_pre) | (pack_s4x8_to_40(b1_pre) << 40)
        r_pre = compute_transaction(MODE_2B, a_pre_w, b_pre_w, 0, (0, 0), (0, 0), (0, 0))

        a2 = [_rand_s_rng(rng, 8) for _ in range(8)]
        b2 = [_rand_s_rng(rng, 8) for _ in range(8)]
        a2_w = pack_s8x8_to_laneword(a2)
        b2_w = pack_s8x8_to_laneword(b2)
        r2 = compute_transaction(MODE_2A, a2_w, b2_w, 0, (0, 0), (0, 0), (0, 0))
        cases_2a.append((a_pre_w, b_pre_w, a2_w, b2_w, r_pre.out1_16, r2.out0_19))
    _write_mem(gen_dir / "tc_mode2a_sanity_pre_a.mem", 80, [x[0] for x in cases_2a])
    _write_mem(gen_dir / "tc_mode2a_sanity_pre_b.mem", 80, [x[1] for x in cases_2a])
    _write_mem(gen_dir / "tc_mode2a_sanity_a2a.mem", 80, [x[2] for x in cases_2a])
    _write_mem(gen_dir / "tc_mode2a_sanity_b2a.mem", 80, [x[3] for x in cases_2a])
    _write_mem(gen_dir / "tc_mode2a_sanity_exp_pre_o1.mem", 16, [x[4] for x in cases_2a])
    _write_mem(gen_dir / "tc_mode2a_sanity_exp_2a_o0.mem", 19, [x[5] for x in cases_2a])

    # 2d: 1000 vectors
    tx_2d = []
    for _ in range(VEC_PER_CASE):
        a = [_rand_s_rng(rng, 8) for _ in range(8)]
        b0 = [_rand_s_rng(rng, 5) for _ in range(8)]
        b1 = [_rand_s_rng(rng, 5) for _ in range(8)]
        a_w = pack_s8x8_to_laneword(a)
        b_w = pack_s5x8_to_40(b0) | (pack_s5x8_to_40(b1) << 40)
        r = compute_transaction(MODE_2D, a_w, b_w, 0, (0, 0), (0, 0), (0, 0))
        tx_2d.append((a_w, b_w, r.out0_19, r.out1_16))
    _write_mem(gen_dir / "tc_mode2d_sanity_tx_a.mem", 80, [x[0] for x in tx_2d])
    _write_mem(gen_dir / "tc_mode2d_sanity_tx_b.mem", 80, [x[1] for x in tx_2d])
    _write_mem(gen_dir / "tc_mode2d_sanity_exp_o0.mem", 19, [x[2] for x in tx_2d])
    _write_mem(gen_dir / "tc_mode2d_sanity_exp_o1.mem", 16, [x[3] for x in tx_2d])

    # 2c: 1000 vectors
    tx_2c = []
    for _ in range(VEC_PER_CASE):
        a = [_rand_s_rng(rng, 5) for _ in range(16)]
        b0 = [_rand_s_rng(rng, 5) for _ in range(16)]
        b1 = [_rand_s_rng(rng, 5) for _ in range(16)]
        e1a = (rng.randint(0, 1), rng.randint(0, 1))
        e1b0 = (rng.randint(0, 1), rng.randint(0, 1))
        e1b1 = (rng.randint(0, 1), rng.randint(0, 1))
        a_w = pack_s5x16_to_80(a)
        b0_w = pack_s5x16_to_80(b0)
        b1_w = pack_s5x16_to_80(b1)
        r = compute_transaction(MODE_2C, a_w, b0_w, b1_w, e1a, e1b0, e1b1)
        tx_2c.append((a_w, b0_w, b1_w, e1a, e1b0, e1b1, r.out0_19, r.out1_16))
    _write_mem(gen_dir / "tc_mode2c_sanity_tx_a.mem", 80, [x[0] for x in tx_2c])
    _write_mem(gen_dir / "tc_mode2c_sanity_tx_b0.mem", 80, [x[1] for x in tx_2c])
    _write_mem(gen_dir / "tc_mode2c_sanity_tx_b1.mem", 80, [x[2] for x in tx_2c])
    _write_mem(gen_dir / "tc_mode2c_sanity_tx_e1a.mem", 2, [x[3][0] | (x[3][1] << 1) for x in tx_2c])
    _write_mem(gen_dir / "tc_mode2c_sanity_tx_e1b0.mem", 2, [x[4][0] | (x[4][1] << 1) for x in tx_2c])
    _write_mem(gen_dir / "tc_mode2c_sanity_tx_e1b1.mem", 2, [x[5][0] | (x[5][1] << 1) for x in tx_2c])
    _write_mem(gen_dir / "tc_mode2c_sanity_exp_o0.mem", 19, [x[6] for x in tx_2c])
    _write_mem(gen_dir / "tc_mode2c_sanity_exp_o1.mem", 16, [x[7] for x in tx_2c])


def gen_mode_switch_random(gen_dir: Path, seed: int) -> None:
    random.seed(seed)
    modes = [MODE_2A, MODE_2B, MODE_2C, MODE_2D]
    tx = []
    for _ in range(VEC_PER_CASE):
        vld = random.randint(0, 1)
        mode = random.choice(modes)
        a, b, b1, e1a, e1b0, e1b1 = _make_txn(mode)
        tx.append((vld, mode, a, b, b1, e1a, e1b0, e1b1))

    dut = PEIntL3Model()
    for _ in range(3):
        dut.step(rst_n=0, vld=0, mode=0, a=0, b=0, b1=0, e1_a=(0, 0), e1_b0=(0, 0), e1_b1=(0, 0))
    for _ in range(3):
        dut.step(rst_n=1, vld=0, mode=0, a=0, b=0, b1=0, e1_a=(0, 0), e1_b0=(0, 0), e1_b1=(0, 0))

    exp = []
    for item in tx:
        r = dut.step(
            rst_n=1,
            vld=item[0],
            mode=item[1],
            a=item[2],
            b=item[3],
            b1=item[4],
            e1_a=item[5],
            e1_b0=item[6],
            e1_b1=item[7],
        )
        if r.vld_out:
            exp.append((r.out0, r.out1))
    for _ in range(30):
        r = dut.step(rst_n=1, vld=0, mode=0, a=0, b=0, b1=0, e1_a=(0, 0), e1_b0=(0, 0), e1_b1=(0, 0))
        if r.vld_out:
            exp.append((r.out0, r.out1))

    _write(
        gen_dir / "tc_mode_switch_random_sizes.vh",
        "\n".join(
            [
                "// Auto-generated by model/gen_rtl_case_vectors.py",
                f"// seed: {seed}",
                f"localparam integer N_TX = {len(tx)};",
                f"localparam integer N_EXP = {len(exp)};",
                "",
            ]
        ),
    )
    _write_mem(gen_dir / "tc_mode_switch_random_tx_vld.mem", 1, [x[0] for x in tx])
    _write_mem(gen_dir / "tc_mode_switch_random_tx_mode.mem", 2, [x[1] for x in tx])
    _write_mem(gen_dir / "tc_mode_switch_random_tx_a.mem", 80, [x[2] for x in tx])
    _write_mem(gen_dir / "tc_mode_switch_random_tx_b.mem", 80, [x[3] for x in tx])
    _write_mem(gen_dir / "tc_mode_switch_random_tx_b1.mem", 80, [x[4] for x in tx])
    _write_mem(gen_dir / "tc_mode_switch_random_tx_e1a.mem", 2, [x[5][0] | (x[5][1] << 1) for x in tx])
    _write_mem(gen_dir / "tc_mode_switch_random_tx_e1b0.mem", 2, [x[6][0] | (x[6][1] << 1) for x in tx])
    _write_mem(gen_dir / "tc_mode_switch_random_tx_e1b1.mem", 2, [x[7][0] | (x[7][1] << 1) for x in tx])
    exp_o0 = [x[0] for x in exp] + [0] * (VEC_PER_CASE - len(exp))
    exp_o1 = [x[1] for x in exp] + [0] * (VEC_PER_CASE - len(exp))
    _write_mem(gen_dir / "tc_mode_switch_random_exp_o0.mem", 19, exp_o0)
    _write_mem(gen_dir / "tc_mode_switch_random_exp_o1.mem", 16, exp_o1)
    _write_mem(gen_dir / "tc_mode_switch_random_meta.mem", 32, [len(exp)])


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate RTL testcase vectors from model.")
    parser.add_argument(
        "--seed",
        type=int,
        default=20260420,
        help="seed for random mode-switch testcase generation",
    )
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    gen_dir = repo / "tb_rtl" / "case" / "generated"
    gen_sanity_case_headers(gen_dir, args.seed)
    gen_mode_switch_random(gen_dir, args.seed)


if __name__ == "__main__":
    main()
