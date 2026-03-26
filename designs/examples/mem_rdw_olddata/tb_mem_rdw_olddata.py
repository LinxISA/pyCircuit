from __future__ import annotations

import sys
from pathlib import Path

from pycircuit import Tb, compile, testbench

_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from mem_rdw_olddata import build  # noqa: E402
from mem_rdw_olddata_config import DEFAULT_PARAMS, TB_PRESETS  # noqa: E402


@testbench
def tb(t: Tb) -> None:
    p = TB_PRESETS["smoke"]
    t.clock("clk")
    t.reset("rst", cycles_asserted=2, cycles_deasserted=1)
    t.timeout(int(p["timeout"]))

    # Default drives.
    t.drive("ren", 0, at=0)
    t.drive("raddr", 0, at=0)
    t.drive("wvalid", 0, at=0)
    t.drive("waddr", 0, at=0)
    t.drive("wdata", 0, at=0)
    t.drive("wstrb", 0, at=0)

    # Cycle 0: write old value.
    t.drive("wvalid", 1, at=0)
    t.drive("waddr", 0, at=0)
    t.drive("wdata", 0x11111111, at=0)
    t.drive("wstrb", 0xF, at=0)

    # Cycle 1: read+write same address -> expect old-data.
    t.drive("ren", 1, at=1)
    t.drive("raddr", 0, at=1)
    t.drive("wvalid", 1, at=1)
    t.drive("waddr", 0, at=1)
    t.drive("wdata", 0x22222222, at=1)
    t.drive("wstrb", 0xF, at=1)
    t.expect("rdata", 0x11111111, at=1, phase="post", msg="RDW must return old-data")

    # Cycle 2: read again -> expect new data.
    t.drive("wvalid", 0, at=2)
    t.drive("wstrb", 0, at=2)
    t.drive("ren", 1, at=2)
    t.drive("raddr", 0, at=2)
    t.expect("rdata", 0x22222222, at=2, phase="post")

    t.finish(at=int(p["finish"]))


if __name__ == "__main__":
    print(compile(build, name="tb_mem_rdw_olddata_top", **DEFAULT_PARAMS).emit_mlir())

