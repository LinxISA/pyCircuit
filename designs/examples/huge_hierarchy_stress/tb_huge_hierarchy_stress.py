from __future__ import annotations

import sys
from pathlib import Path

from pycircuit import CycleAwareTb, Tb, compile_cycle_aware, CycleAwareCircuit, CycleAwareDomain, testbench

_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from huge_hierarchy_stress import build  # noqa: E402
from huge_hierarchy_stress_config import DEFAULT_PARAMS, TB_PRESETS  # noqa: E402


@testbench
def tb(t: Tb) -> None:
    tb = CycleAwareTb(t)
    p = TB_PRESETS["smoke"]
    tb.clock("clk")
    tb.reset("rst", cycles_asserted=2, cycles_deasserted=1)
    tb.timeout(int(p["timeout"]))
    # --- cycle 0 ---
    tb.drive("seed", 0x1234)
    tb.finish(at=int(p["finish"]))


if __name__ == "__main__":
    print(compile_cycle_aware(build, name="tb_huge_hierarchy_stress_top", **DEFAULT_PARAMS).emit_mlir())
