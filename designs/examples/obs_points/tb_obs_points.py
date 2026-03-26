from __future__ import annotations

import sys
from pathlib import Path

from pycircuit import Tb, compile, testbench

_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from obs_points import build  # noqa: E402
from obs_points_config import DEFAULT_PARAMS, TB_PRESETS  # noqa: E402


@testbench
def tb(t: Tb) -> None:
    p = TB_PRESETS["smoke"]
    t.clock("clk")
    t.reset("rst", cycles_asserted=2, cycles_deasserted=0)
    t.timeout(int(p["timeout"]))

    # Default drives.
    t.drive("x", 0, at=0)

    # Cycle 0: comb changes visible at pre; state updates visible at post.
    t.drive("x", 10, at=0)
    t.expect("y", 11, at=0, phase="pre", msg="TICK-OBS: comb must reflect current drives")
    t.expect("q", 0, at=0, phase="pre", msg="TICK-OBS: state is pre-commit")
    t.expect("q", 11, at=0, phase="post", msg="XFER-OBS: state commit is visible")

    # Cycle 1: repeat with a new drive to validate both obs points again.
    t.drive("x", 20, at=1)
    t.expect("y", 21, at=1, phase="pre")
    t.expect("q", 11, at=1, phase="pre")
    t.expect("q", 21, at=1, phase="post")

    t.finish(at=int(p["finish"]))


if __name__ == "__main__":
    print(compile(build, name="tb_obs_points_top", **DEFAULT_PARAMS).emit_mlir())
