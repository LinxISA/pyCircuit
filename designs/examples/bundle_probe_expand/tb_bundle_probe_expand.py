from __future__ import annotations

import sys
from pathlib import Path

from pycircuit import CycleAwareTb, Tb, TbProbes, compile_cycle_aware, CycleAwareCircuit, CycleAwareDomain, testbench

_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from bundle_probe_expand import build  # noqa: E402
from bundle_probe_expand_config import DEFAULT_PARAMS, TB_PRESETS  # noqa: E402


@testbench
def tb(t: Tb, probes: TbProbes) -> None:
    tb = CycleAwareTb(t)
    p = TB_PRESETS["smoke"]
    _ = probes["dut:probe.pv.in.a"]
    _ = probes["dut:probe.pv.in.b.c"]
    tb.clock("clk")
    tb.reset("rst", cycles_asserted=2, cycles_deasserted=0)
    tb.timeout(int(p["timeout"]))

    # --- cycle 0 ---
    tb.drive("in_a", 0)
    tb.drive("in_b_c", 0)

    tb.drive("in_a", 0x12)
    tb.drive("in_b_c", 1)
    tb.expect("in_a", 0x12, phase="pre")
    tb.expect("in_b_c", 1, phase="pre")

    tb.next()  # --- cycle 1 ---
    tb.drive("in_a", 0x34)
    tb.drive("in_b_c", 0)
    tb.expect("in_a", 0x34, phase="pre")
    tb.expect("in_b_c", 0, phase="pre")

    tb.finish(at=int(p["finish"]))


if __name__ == "__main__":
    print(compile_cycle_aware(build, name="tb_bundle_probe_expand_top", eager=True, **DEFAULT_PARAMS).emit_mlir())
