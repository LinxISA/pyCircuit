from __future__ import annotations

import sys
from pathlib import Path

from pycircuit import Tb, compile, testbench

_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from sync_mem_init_zero import build  # noqa: E402
from sync_mem_init_zero_config import DEFAULT_PARAMS, TB_PRESETS  # noqa: E402


@testbench
def tb(t: Tb) -> None:
    p = TB_PRESETS["smoke"]
    t.clock("clk")
    t.reset("rst", cycles_asserted=2, cycles_deasserted=1)
    t.timeout(int(p["timeout"]))

    # Default drives (no writes).
    t.drive("wvalid", 0, at=0)
    t.drive("waddr", 0, at=0)
    t.drive("wdata", 0, at=0)
    t.drive("wstrb", 0, at=0)

    # Read from unwritten addresses: deterministic sim init must be 0.
    t.drive("ren", 1, at=0)
    t.drive("raddr", 1, at=0)
    t.expect("rdata", 0, at=0, phase="post", msg="sync_mem must initialize entries to 0 (deterministic sim)")

    t.drive("ren", 1, at=1)
    t.drive("raddr", 3, at=1)
    t.expect("rdata", 0, at=1, phase="post")

    t.finish(at=int(p["finish"]))


if __name__ == "__main__":
    print(compile(build, name="tb_sync_mem_init_zero_top", **DEFAULT_PARAMS).emit_mlir())

