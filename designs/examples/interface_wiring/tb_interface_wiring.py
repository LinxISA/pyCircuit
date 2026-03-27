from __future__ import annotations

import sys
from pathlib import Path

from pycircuit import CycleAwareTb, Tb, compile_cycle_aware, CycleAwareCircuit, CycleAwareDomain, testbench

_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from interface_wiring import build  # noqa: E402
from interface_wiring_config import DEFAULT_PARAMS, TB_PRESETS  # noqa: E402


@testbench
def tb(t: Tb) -> None:
    tb = CycleAwareTb(t)
    p = TB_PRESETS["smoke"]
    tb.timeout(int(p["timeout"]))

    # --- cycle 0 ---
    tb.drive("top_in_left", 1)
    tb.drive("top_in_rhs", 2)
    tb.expect("top_out_left", 1)
    tb.expect("top_out_rhs", 3)

    tb.finish(at=int(p["finish"]))


if __name__ == "__main__":
    print(compile_cycle_aware(build, name="tb_interface_wiring_top", **DEFAULT_PARAMS).emit_mlir())
