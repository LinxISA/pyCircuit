from __future__ import annotations

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
from ref_model import MODE_2A, MODE_2B, compute_transaction, pack_s4x8_to_40, pack_s8x8_to_laneword  # noqa: E402

DEFAULT_PARAMS = {"latency": 3}


def _to_u(value: int, bits: int) -> int:
    return value & ((1 << bits) - 1)


def _mode2b_vector():
    a = [1, -2, 3, -4, 5, -6, 7, -8]
    b0 = [1, 2, -3, 4, -5, 6, -7, -8]
    b1 = [-1, 2, 3, -4, 5, -6, 7, 0]
    a80 = pack_s8x8_to_laneword(a)
    b80 = pack_s4x8_to_40(b0) | (pack_s4x8_to_40(b1) << 40)
    exp = compute_transaction(MODE_2B, a80, b80, 0, (0, 0), (0, 0), (0, 0))
    return a80, b80, exp


@testbench
def tb(t: Tb) -> None:
    tb = CycleAwareTb(t)
    tb.clock("clk")
    tb.reset("rst_n", cycles_asserted=2, cycles_deasserted=1)
    tb.timeout(200)

    a80, b80, exp2b = _mode2b_vector()
    exp2a = compute_transaction(
        MODE_2A,
        pack_s8x8_to_laneword([1, 2, 3, 4, 5, 6, 7, 8]),
        pack_s8x8_to_laneword([-1, -1, -1, -1, -1, -1, -1, -1]),
        0,
        (0, 0),
        (0, 0),
        (0, 0),
    )

    tb.drive("vld", 0)
    tb.drive("mode", MODE_2A)
    tb.drive("a", 0)
    tb.drive("b", 0)
    tb.drive("b1", 0)
    tb.drive("e1_a", 0)
    tb.drive("e1_b0", 0)
    tb.drive("e1_b1", 0)
    tb.expect("vld_out", 0)

    # cycle 0: inject 2b
    tb.next()
    tb.drive("vld", 1)
    tb.drive("mode", MODE_2B)
    tb.drive("a", _to_u(a80, 80))
    tb.drive("b", _to_u(b80, 80))
    tb.drive("b1", 0)

    # cycle 1: inject 2a
    tb.next()
    tb.drive("vld", 1)
    tb.drive("mode", MODE_2A)
    tb.drive("a", _to_u(pack_s8x8_to_laneword([1, 2, 3, 4, 5, 6, 7, 8]), 80))
    tb.drive("b", _to_u(pack_s8x8_to_laneword([-1, -1, -1, -1, -1, -1, -1, -1]), 80))
    tb.drive("b1", 0)

    # drain and check outputs on target cycles.
    for _ in range(20):
        tb.next()
        tb.drive("vld", 0)
        if tb.cycle == 5:
            tb.expect("vld_out", 1)
            tb.expect("out0", _to_u(exp2b.out0_19, 19))
            tb.expect("out1", _to_u(exp2b.out1_16, 16))
        if tb.cycle == 6:
            tb.expect("vld_out", 1)
            tb.expect("out0", _to_u(exp2a.out0_19, 19))

    tb.finish()


if __name__ == "__main__":
    print(compile_cycle_aware(build, name="tb_pe_int_top", eager=True, **DEFAULT_PARAMS).emit_mlir())
