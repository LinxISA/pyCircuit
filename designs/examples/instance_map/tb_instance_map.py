from __future__ import annotations

import sys
from pathlib import Path

from pycircuit import CycleAwareTb, Tb, compile_cycle_aware, CycleAwareCircuit, CycleAwareDomain, testbench

_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from instance_map import build  # noqa: E402
from instance_map_config import DEFAULT_PARAMS, TB_PRESETS  # noqa: E402


@testbench
def tb(t: Tb) -> None:
    tb = CycleAwareTb(t)
    p = TB_PRESETS["smoke"]
    tb.timeout(int(p["timeout"]))

    # --- cycle 0 ---
    tb.drive("in_alu", 0)
    tb.drive("in_branch", 0)
    tb.drive("in_lsu", 0)
    tb.expect("alu_y", 1)
    tb.expect("branch_y", 2)
    tb.expect("lsu_y", 3)
    tb.expect("acc", 6)

    tb.finish(at=int(p["finish"]))


if __name__ == "__main__":
    print(compile_cycle_aware(build, name="tb_instance_map_top", **DEFAULT_PARAMS).emit_mlir())
