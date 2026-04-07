"""Integration Test: Cube GEMM Pipeline.

Tests the cube execution path modules compile cleanly:
  Cube RS → Cube Unit → TCB

Also verifies MTE-to-Cube dataflow testbench generation.
"""

from __future__ import annotations

import os
import sys

_root = os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", "..")
sys.path.insert(0, os.path.join(_root, "compiler", "frontend"))
sys.path.insert(0, _root)

from pycircuit import CycleAwareTb, compile_cycle_aware
from pycircuit.tb import Tb

TEST_PROGRAM = """
Scenario: GEMM FP16, Mode A, M=8, K=64, N=64
  Phase 1: TILE.LD A + 8× TILE.LD B
  Phase 2: CUBE.OPA z0, Ta, Tb, Nb=8 → 26 cycles
  Phase 3: CUBE.DRAIN z0, Tc → 8 cycles
  Phase 4: TILE.ST Tc → memory

Performance: 4096 MACs/cycle × 8 steps = 32768 FP16 FMAs
"""


def test_cube_rs_compile():
    """Cube RS compiles to valid MLIR."""
    from designs.outerCube.davinci.backend.cube_rs.cube_rs import cube_rs

    circ = compile_cycle_aware(
        cube_rs,
        name="cube_rs",
        eager=True,
        n_entries=4,
        n_dispatch=2,
        n_tcb=2,
        n_tile_src=2,
        ttag_w=3,
        uop_w=4,
        age_w=3,
    )
    mlir = circ.emit_mlir()
    assert "crs_issue_valid" in mlir
    assert "crs_issue_op" in mlir


def test_cube_unit_compile():
    """Cube Unit compiles to valid MLIR."""
    from designs.outerCube.davinci.backend.cube_unit.cube_unit import cube_unit

    circ = compile_cycle_aware(
        cube_unit, name="cube_unit", eager=True, ttag_w=3, prefix="cu"
    )
    mlir = circ.emit_mlir()
    assert "cu_busy" in mlir
    assert "cu_complete_valid" in mlir


def test_cube_gemm_tb():
    """Generate cube GEMM testbench description."""
    t = Tb()
    ct = CycleAwareTb(t)
    ct.clock("clk")
    ct.reset("rst", cycles_asserted=2, cycles_deasserted=1)
    ct.timeout(300)

    ct.drive("crs_dv0", 1)
    ct.drive("crs_dop0", 0x20)
    ct.drive("crs_dts0_0", 100)
    ct.drive("crs_dtr0_0", 1)
    ct.drive("crs_dts1_0", 101)
    ct.drive("crs_dtr1_0", 1)
    ct.drive("crs_dtd0", 110)

    ct.next()

    ct.drive("crs_dv0", 0)

    for _ in range(25):
        ct.next()

    ct.drive("crs_tcb_v0", 1)
    ct.drive("crs_tcb_t0", 110)
    ct.expect("crs_issue_valid", 0, msg="Cube RS should be empty after issue")

    ct.next()
    ct.finish()

    assert len(t.drives) > 0


if __name__ == "__main__":
    test_cube_rs_compile()
    test_cube_unit_compile()
    test_cube_gemm_tb()
