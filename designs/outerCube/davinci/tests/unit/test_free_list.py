"""Unit test for Free List — compile verification + testbench generation."""

from __future__ import annotations

import os
import sys

_root = os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", "..")
sys.path.insert(0, os.path.join(_root, "compiler", "frontend"))
sys.path.insert(0, _root)

from pycircuit import CycleAwareTb, compile_cycle_aware
from pycircuit.tb import Tb


def fl_test(m, domain) -> None:
    from designs.outerCube.davinci.common.free_list import free_list

    free_list(m, domain, depth=8, tag_w=4, deq_width=2, enq_width=2, prefix="fl")


fl_test.__pycircuit_name__ = "test_free_list"


def test_free_list_compile():
    """Free list compiles to valid MLIR."""
    circuit = compile_cycle_aware(fl_test, name="test_free_list", eager=True)
    mlir = circuit.emit_mlir()
    assert "func.func @test_free_list" in mlir
    assert "fl_stall" in mlir
    assert "fl_count" in mlir


def test_free_list_tb_initial():
    """Generate testbench: initial state should be full, no stall."""
    t = Tb()
    ct = CycleAwareTb(t)
    ct.clock("clk")
    ct.reset("rst", cycles_asserted=2, cycles_deasserted=1)
    ct.timeout(20)

    ct.drive("fl_alloc_req0", 0)
    ct.drive("fl_alloc_req1", 0)
    ct.drive("fl_free_valid0", 0)
    ct.drive("fl_free_valid1", 0)
    ct.drive("fl_free_tag0", 0)
    ct.drive("fl_free_tag1", 0)
    ct.drive("fl_restore", 0)
    ct.drive("fl_restore_head", 0)

    ct.expect("fl_stall", 0, msg="Should not stall when full")
    ct.expect("fl_count", 8, msg="Initial count should be 8")
    ct.next()
    ct.finish()

    assert len(t.drives) == 8
    assert len(t.expects) == 2


def test_free_list_tb_alloc():
    """Generate testbench: allocate 2 then verify count drops."""
    t = Tb()
    ct = CycleAwareTb(t)
    ct.clock("clk")
    ct.reset("rst", cycles_asserted=2, cycles_deasserted=1)
    ct.timeout(30)

    # Cycle 0: request 2 allocs
    ct.drive("fl_alloc_req0", 1)
    ct.drive("fl_alloc_req1", 1)
    ct.drive("fl_free_valid0", 0)
    ct.drive("fl_free_valid1", 0)
    ct.drive("fl_free_tag0", 0)
    ct.drive("fl_free_tag1", 0)
    ct.drive("fl_restore", 0)
    ct.drive("fl_restore_head", 0)
    ct.expect("fl_stall", 0, msg="Enough entries for 2 allocs")

    ct.next()

    # Cycle 1: stop allocs, check count
    ct.drive("fl_alloc_req0", 0)
    ct.drive("fl_alloc_req1", 0)
    ct.drive("fl_free_valid0", 0)
    ct.drive("fl_free_valid1", 0)
    ct.drive("fl_free_tag0", 0)
    ct.drive("fl_free_tag1", 0)
    ct.drive("fl_restore", 0)
    ct.drive("fl_restore_head", 0)
    ct.expect("fl_count", 6, msg="After 2 allocs count=6")

    ct.next()
    ct.finish()

    assert any(e.value == 6 for e in t.expects)


if __name__ == "__main__":
    test_free_list_compile()
    test_free_list_tb_initial()
    test_free_list_tb_alloc()
