"""Unit test for ALU module — compile verification + testbench generation."""

from __future__ import annotations

import os
import sys

_root = os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", "..")
sys.path.insert(0, os.path.join(_root, "compiler", "frontend"))
sys.path.insert(0, _root)

from pycircuit import CycleAwareTb, compile_cycle_aware
from pycircuit.tb import Tb


def alu_test(m, domain) -> None:
    from designs.outerCube.davinci.backend.scalar_exu.alu import alu

    alu(m, domain, data_w=16, tag_w=4, prefix="alu0")


alu_test.__pycircuit_name__ = "test_alu"


def test_alu_compile():
    """ALU compiles to valid MLIR."""
    circuit = compile_cycle_aware(alu_test, name="test_alu", eager=True)
    mlir = circuit.emit_mlir()
    assert "func.func @test_alu" in mlir
    assert "alu0_result_data" in mlir
    assert "alu0_result_valid" in mlir


def test_alu_tb_add():
    """Generate ALU ADD testbench (5 + 3 = 8)."""
    compile_cycle_aware(alu_test, name="test_alu", eager=True)
    t = Tb()
    ct = CycleAwareTb(t)
    ct.clock("clk")
    ct.reset("rst", cycles_asserted=2, cycles_deasserted=1)
    ct.timeout(20)

    ct.drive("alu0_valid", 1)
    ct.drive("alu0_func", 0)  # ALU_ADD
    ct.drive("alu0_src1", 5)
    ct.drive("alu0_src2", 3)
    ct.drive("alu0_pdst", 1)
    ct.expect("alu0_result_data", 8, msg="ADD 5+3 should be 8")
    ct.expect("alu0_result_valid", 1)
    ct.next()
    ct.finish()

    assert len(t.drives) == 5
    assert len(t.expects) == 2
    assert t.expects[0].value == 8


def test_alu_tb_sub():
    """Generate ALU SUB testbench (10 - 3 = 7)."""
    compile_cycle_aware(alu_test, name="test_alu", eager=True)
    t = Tb()
    ct = CycleAwareTb(t)
    ct.clock("clk")
    ct.reset("rst", cycles_asserted=2, cycles_deasserted=1)
    ct.timeout(20)

    ct.drive("alu0_valid", 1)
    ct.drive("alu0_func", 1)  # ALU_SUB
    ct.drive("alu0_src1", 10)
    ct.drive("alu0_src2", 3)
    ct.drive("alu0_pdst", 2)
    ct.expect("alu0_result_data", 7, msg="SUB 10-3 should be 7")
    ct.next()
    ct.finish()

    assert t.expects[0].value == 7


def test_alu_tb_and():
    """Generate ALU AND testbench."""
    compile_cycle_aware(alu_test, name="test_alu", eager=True)
    t = Tb()
    ct = CycleAwareTb(t)
    ct.clock("clk")
    ct.reset("rst", cycles_asserted=2, cycles_deasserted=1)
    ct.timeout(20)

    ct.drive("alu0_valid", 1)
    ct.drive("alu0_func", 2)  # ALU_AND
    ct.drive("alu0_src1", 0xFF00)
    ct.drive("alu0_src2", 0x0F0F)
    ct.drive("alu0_pdst", 3)
    ct.expect("alu0_result_data", 0x0F00, msg="AND FF00 & 0F0F = 0F00")
    ct.next()
    ct.finish()

    assert t.expects[0].value == 0x0F00


if __name__ == "__main__":
    test_alu_compile()
    test_alu_tb_add()
    test_alu_tb_sub()
    test_alu_tb_and()
