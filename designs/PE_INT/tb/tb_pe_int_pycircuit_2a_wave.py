from __future__ import annotations

import os
import sys
from pathlib import Path

from pycircuit import CycleAwareTb, Tb, testbench

_THIS_DIR = Path(__file__).resolve().parent
_PY_DIR = _THIS_DIR.parent / "python"
_MODEL_DIR = _THIS_DIR.parent / "model"
if str(_PY_DIR) not in sys.path:
    sys.path.insert(0, str(_PY_DIR))
if str(_MODEL_DIR) not in sys.path:
    sys.path.insert(0, str(_MODEL_DIR))

from pe_int_pycircuit import build  # noqa: E402
from ref_model import MODE_2A, MODE_2B  # noqa: E402


def _read_mem(path: Path) -> list[int]:
    return [int(line.strip(), 16) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def _to_u(value: int, bits: int) -> int:
    return value & ((1 << bits) - 1)


def _drive(tb: CycleAwareTb, *, rst_n: int, vld: int, mode: int, a: int, b: int) -> None:
    tb.drive("rst_n", rst_n)
    tb.drive("vld", vld)
    tb.drive("mode", mode)
    tb.drive("a", _to_u(a, 80))
    tb.drive("b", _to_u(b, 80))
    tb.drive("b1", 0)
    tb.drive("e1_a", 0)
    tb.drive("e1_b0", 0)
    tb.drive("e1_b1", 0)


def _load_2a_sanity_tx() -> list[dict[str, int]]:
    repo = _THIS_DIR.parent
    gen_dir = Path(os.environ.get("PE_INT_WAVE_GEN_DIR", repo / "tb_rtl" / "case" / "generated"))
    tx_count = int(os.environ.get("PE_INT_WAVE_TX_COUNT", "10"))
    if tx_count < 1:
        raise ValueError("PE_INT_WAVE_TX_COUNT must be >= 1")

    pre_a = _read_mem(gen_dir / "tc_mode2a_sanity_pre_a.mem")
    pre_b = _read_mem(gen_dir / "tc_mode2a_sanity_pre_b.mem")
    a2a = _read_mem(gen_dir / "tc_mode2a_sanity_a2a.mem")
    b2a = _read_mem(gen_dir / "tc_mode2a_sanity_b2a.mem")

    txs: list[dict[str, int]] = []
    for idx in range(len(pre_a)):
        txs.append({"vld": 1, "mode": MODE_2B, "a": pre_a[idx], "b": pre_b[idx]})
        txs.append({"vld": 1, "mode": MODE_2A, "a": a2a[idx], "b": b2a[idx]})
        if len(txs) >= tx_count:
            return txs[:tx_count]
    return txs


@testbench
def tb(t: Tb) -> None:
    tb = CycleAwareTb(t)
    tb.clock("clk")
    tb.timeout(200)

    timeline: list[dict[str, int]] = []
    idle = {"vld": 0, "mode": MODE_2A, "a": 0, "b": 0}
    for _ in range(3):
        timeline.append(idle | {"rst_n": 0})
    for _ in range(3):
        timeline.append(idle | {"rst_n": 1})
    for item in _load_2a_sanity_tx():
        timeline.append(item | {"rst_n": 1})
    for _ in range(12):
        timeline.append(idle | {"rst_n": 1})

    first = timeline[0]
    _drive(tb, rst_n=first["rst_n"], vld=first["vld"], mode=first["mode"], a=first["a"], b=first["b"])
    for idx in range(len(timeline)):
        tb.next()
        if idx + 1 < len(timeline):
            nxt = timeline[idx + 1]
            _drive(tb, rst_n=nxt["rst_n"], vld=nxt["vld"], mode=nxt["mode"], a=nxt["a"], b=nxt["b"])

    tb.finish()
