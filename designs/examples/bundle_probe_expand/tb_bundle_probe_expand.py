from __future__ import annotations

import sys
from pathlib import Path

from pycircuit import Tb, TbProbes, compile, testbench

_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from bundle_probe_expand import build  # noqa: E402
from bundle_probe_expand_config import DEFAULT_PARAMS, TB_PRESETS  # noqa: E402


@testbench
def tb(t: Tb, probes: TbProbes) -> None:
    p = TB_PRESETS["smoke"]
    _ = probes["dut:probe.pv.in.a"]
    _ = probes["dut:probe.pv.in.b.c"]
    t.clock("clk")
    t.reset("rst", cycles_asserted=2, cycles_deasserted=0)
    t.timeout(int(p["timeout"]))

    t.drive("in_a", 0, at=0)
    t.drive("in_b_c", 0, at=0)

    t.drive("in_a", 0x12, at=0)
    t.drive("in_b_c", 1, at=0)
    t.expect("in_a", 0x12, at=0, phase="pre")
    t.expect("in_b_c", 1, at=0, phase="pre")

    t.drive("in_a", 0x34, at=1)
    t.drive("in_b_c", 0, at=1)
    t.expect("in_a", 0x34, at=1, phase="pre")
    t.expect("in_b_c", 0, at=1, phase="pre")

    t.finish(at=int(p["finish"]))


if __name__ == "__main__":
    print(compile(build, name="tb_bundle_probe_expand_top", **DEFAULT_PARAMS).emit_mlir())
