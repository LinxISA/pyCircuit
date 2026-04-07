"""Integration Test: Tile Pipeline (TILE.LD → VADD → TILE.ST).

Tests the full tile data path modules compile cleanly and can be composed:
  MTE RS → MTE Unit → TCB → Vector RS wakeup → Vector Unit → TCB

Also verifies a minimal CycleAwareTb scenario for the tile pipeline.
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
Scenario: TILE.LD T10 → TILE.LD T20 → VADD T30, T10, T20 → TILE.ST T30

Pipeline flow:
  TILE.LD enters MTE RS (scalar addr ready) → MTE Unit → TCB(PT200)
  VADD waits in Vec RS until TCB wakes PT200, PT201 → Vec Unit → TCB(PT202)
  TILE.ST waits in MTE RS until TCB wakes PT202 → MTE Unit → memory
"""


def test_mte_rs_compile():
    """MTE RS compiles to valid MLIR."""
    from designs.outerCube.davinci.backend.mte_rs.mte_rs import mte_rs

    circ = compile_cycle_aware(
        mte_rs,
        name="mte_rs",
        eager=True,
        n_entries=4,
        n_dispatch=2,
        n_cdb=2,
        n_tcb=2,
        stag_w=3,
        ttag_w=3,
        uop_w=4,
        age_w=3,
    )
    mlir = circ.emit_mlir()
    assert "mrs_issue_valid" in mlir
    assert "mrs_issue_op" in mlir


def test_vec_rs_compile():
    """Vec RS compiles to valid MLIR."""
    from designs.outerCube.davinci.backend.vec_rs.vec_rs import vec_rs

    circ = compile_cycle_aware(
        vec_rs,
        name="vec_rs",
        eager=True,
        n_entries=4,
        n_dispatch=2,
        n_tcb=2,
        n_tile_src=2,
        ttag_w=4,
        prefix="vrs",
    )
    mlir = circ.emit_mlir()
    assert "vrs_issue_valid" in mlir


def test_mte_unit_compile():
    """MTE Unit compiles to valid MLIR."""
    from designs.outerCube.davinci.backend.mte_unit.mte_unit import mte_unit

    circ = compile_cycle_aware(
        mte_unit,
        name="mte_unit",
        eager=True,
        ttag_w=3,
        stag_w=3,
        data_w=16,
        prefix="mte",
    )
    mlir = circ.emit_mlir()
    assert "mte_tcb_valid" in mlir


def test_vec_unit_compile():
    """Vector Unit compiles to valid MLIR."""
    from designs.outerCube.davinci.backend.vec_unit.vec_unit import vec_unit

    circ = compile_cycle_aware(
        vec_unit, name="vec_unit", eager=True, ttag_w=3, prefix="vu"
    )
    mlir = circ.emit_mlir()
    assert "vu_" in mlir


def test_tile_pipeline_tb():
    """Generate tile pipeline testbench description."""
    t = Tb()
    ct = CycleAwareTb(t)
    ct.clock("clk")
    ct.reset("rst", cycles_asserted=2, cycles_deasserted=1)
    ct.timeout(200)

    ct.drive("mrs_dv0", 1)
    ct.drive("mrs_dop0", 0x10)
    ct.drive("mrs_dps0", 5)
    ct.drive("mrs_dsr0", 1)
    ct.drive("mrs_dts0", 0)
    ct.drive("mrs_dtr0", 0)
    ct.drive("mrs_dtd0", 200)
    ct.drive("mrs_dpsd0", 0)

    ct.next()

    ct.drive("mrs_dv0", 1)
    ct.drive("mrs_dop0", 0x10)
    ct.drive("mrs_dps0", 6)
    ct.drive("mrs_dsr0", 1)
    ct.drive("mrs_dts0", 0)
    ct.drive("mrs_dtr0", 0)
    ct.drive("mrs_dtd0", 201)
    ct.drive("mrs_dpsd0", 0)

    ct.next()

    ct.drive("mrs_dv0", 0)
    ct.drive("mrs_tcb_v0", 1)
    ct.drive("mrs_tcb_t0", 200)
    ct.next()

    ct.drive("mrs_tcb_v0", 1)
    ct.drive("mrs_tcb_t0", 201)
    ct.next()

    ct.finish()

    assert len(t.drives) > 0


if __name__ == "__main__":
    test_mte_rs_compile()
    test_vec_rs_compile()
    test_mte_unit_compile()
    test_vec_unit_compile()
    test_tile_pipeline_tb()
