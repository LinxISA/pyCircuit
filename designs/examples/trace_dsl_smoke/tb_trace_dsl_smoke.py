from __future__ import annotations

import sys
from pathlib import Path

from pycircuit import Tb, compile, testbench

_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from trace_dsl_smoke import build  # noqa: E402
from trace_dsl_smoke_config import DEFAULT_PARAMS, TB_PRESETS  # noqa: E402


@testbench
def tb(t: Tb) -> None:
    p = TB_PRESETS["smoke"]
    t.clock("clk")
    t.reset("rst", cycles_asserted=2, cycles_deasserted=0)
    t.timeout(int(p["timeout"]))

    # Cycle 0: pre is init, post sees commit.
    t.drive("in_x", 0x12, at=0)
    t.expect("y0", 0x00, at=0, phase="pre")
    t.expect("y1", 0x00, at=0, phase="pre")
    t.expect("y0", 0x12, at=0, phase="post")
    t.expect("y1", 0x12, at=0, phase="post")

    # Cycle 1: same behavior with a new drive.
    t.drive("in_x", 0x34, at=1)
    t.expect("y0", 0x12, at=1, phase="pre")
    t.expect("y1", 0x12, at=1, phase="pre")
    t.expect("y0", 0x34, at=1, phase="post")
    t.expect("y1", 0x34, at=1, phase="post")

    # Cycle 2: stable drive (committed output holds; trace still records Write intent; Decision 0053).
    t.drive("in_x", 0x34, at=2)
    t.expect("y0", 0x34, at=2, phase="pre")
    t.expect("y1", 0x34, at=2, phase="pre")
    t.expect("y0", 0x34, at=2, phase="post")
    t.expect("y1", 0x34, at=2, phase="post")

    t.finish(at=int(p["finish"]))


if __name__ == "__main__":
    print(compile(build, name="tb_trace_dsl_smoke_top", **DEFAULT_PARAMS).emit_mlir())
