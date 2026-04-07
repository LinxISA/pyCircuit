"""Unit test for Scalar RAT — compile verification + testbench generation."""

from __future__ import annotations

import os
import sys

_root = os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", "..")
sys.path.insert(0, os.path.join(_root, "compiler", "frontend"))
sys.path.insert(0, _root)

from pycircuit import CycleAwareTb, compile_cycle_aware
from pycircuit.tb import Tb


def srat_test(m, domain) -> None:
    from designs.outerCube.davinci.frontend.rename.scalar_rat import scalar_rat

    scalar_rat(m, domain, n_arch=8, arch_w=3, phys_w=4, width=2, prefix="srat")


srat_test.__pycircuit_name__ = "test_scalar_rat"


def test_scalar_rat_compile():
    """Scalar RAT compiles to valid MLIR."""
    circuit = compile_cycle_aware(srat_test, name="test_scalar_rat", eager=True)
    mlir = circuit.emit_mlir()
    assert "func.func @test_scalar_rat" in mlir
    assert "srat_src1_phys0" in mlir
    assert "srat_src1_rdy0" in mlir


def test_scalar_rat_tb_initial():
    """Generate testbench: at reset X[i]->P[i], all ready."""
    t = Tb()
    ct = CycleAwareTb(t)
    ct.clock("clk")
    ct.reset("rst", cycles_asserted=2, cycles_deasserted=1)
    ct.timeout(20)

    for i in range(2):
        ct.drive(f"srat_dst_valid{i}", 0)
        ct.drive(f"srat_dst_arch{i}", 0)
        ct.drive(f"srat_dst_phys{i}", 0)
    for i in range(6):
        ct.drive(f"srat_wb_valid{i}", 0)
        ct.drive(f"srat_wb_tag{i}", 0)
    ct.drive("srat_restore", 0)
    for i in range(8):
        ct.drive(f"srat_restore_map{i}", 0)
        ct.drive(f"srat_restore_rdy{i}", 0)

    ct.drive("srat_src1_arch0", 2)
    ct.drive("srat_src2_arch0", 3)
    ct.drive("srat_src1_arch1", 0)
    ct.drive("srat_src2_arch1", 0)

    ct.expect("srat_src1_phys0", 2, msg="X2->P2")
    ct.expect("srat_src1_rdy0", 1, msg="P2 ready")
    ct.expect("srat_src2_phys0", 3, msg="X3->P3")
    ct.next()
    ct.finish()

    assert t.expects[0].value == 2
    assert t.expects[1].value == 1


def test_scalar_rat_tb_bypass():
    """Generate testbench: rename X2->P10, slot 1 reads X2 should bypass."""
    t = Tb()
    ct = CycleAwareTb(t)
    ct.clock("clk")
    ct.reset("rst", cycles_asserted=2, cycles_deasserted=1)
    ct.timeout(20)

    for i in range(6):
        ct.drive(f"srat_wb_valid{i}", 0)
        ct.drive(f"srat_wb_tag{i}", 0)
    ct.drive("srat_restore", 0)
    for i in range(8):
        ct.drive(f"srat_restore_map{i}", 0)
        ct.drive(f"srat_restore_rdy{i}", 0)

    ct.drive("srat_dst_valid0", 1)
    ct.drive("srat_dst_arch0", 2)
    ct.drive("srat_dst_phys0", 10)
    ct.drive("srat_dst_valid1", 0)
    ct.drive("srat_dst_arch1", 0)

    ct.drive("srat_src1_arch0", 0)
    ct.drive("srat_src2_arch0", 0)
    ct.drive("srat_src1_arch1", 2)
    ct.drive("srat_src2_arch1", 0)

    ct.expect("srat_src1_phys1", 10, msg="Bypass to P10")
    ct.expect("srat_src1_rdy1", 0, msg="Not ready (just renamed)")
    ct.next()
    ct.finish()

    assert t.expects[0].value == 10
    assert t.expects[1].value == 0


if __name__ == "__main__":
    test_scalar_rat_compile()
    test_scalar_rat_tb_initial()
    test_scalar_rat_tb_bypass()
