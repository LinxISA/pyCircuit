from __future__ import annotations

import sys
from pathlib import Path

from pycircuit import Tb, compile, testbench

_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from reset_invalidate_order_smoke import build  # noqa: E402
from reset_invalidate_order_smoke_config import DEFAULT_PARAMS, TB_PRESETS  # noqa: E402


@testbench
def tb(t: Tb) -> None:
    p = TB_PRESETS["smoke"]
    t.clock("clk")
    t.reset("rst", cycles_asserted=2, cycles_deasserted=0)
    t.timeout(int(p["timeout"]))

    t.drive("en", 1, at=0)
    t.expect("y", 0, at=0, phase="pre")
    t.expect("y", 1, at=0, phase="post")

    t.drive("en", 1, at=1)
    t.expect("y", 1, at=1, phase="pre")
    t.expect("y", 2, at=1, phase="post")

    t.finish(at=int(p["finish"]))


if __name__ == "__main__":
    print(compile(build, name="tb_reset_invalidate_order_smoke_top", **DEFAULT_PARAMS).emit_mlir())
