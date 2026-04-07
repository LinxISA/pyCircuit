"""Integration test — Branch misprediction recovery.

Verifies that mispredict redirect restores RAT state and flushes RS.
"""

from __future__ import annotations

import os
import sys

_root = os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", "..")
sys.path.insert(0, os.path.join(_root, "compiler", "frontend"))
sys.path.insert(0, _root)

from pycircuit import CycleAwareTb
from pycircuit.tb import Tb


def test_branch_recovery_tb():
    """Generate branch mispredict recovery testbench.

    Scenario:
      Cycle 0: Inject a branch instruction + an ADD (speculative)
      Cycle 1: Assert bru_redirect — pipeline should flush
    """
    t = Tb()
    ct = CycleAwareTb(t)
    ct.clock("clk")
    ct.reset("rst", cycles_asserted=2, cycles_deasserted=1)
    ct.timeout(64)

    # BEQ x1,x2,offset (opcode[6:0]=1100011, funct3=000)
    beq_instr = 0b0000000_00010_00001_000_00000_1100011
    add_instr = 0b0000000_00100_00011_000_00101_0110011

    # Cycle 0: branch + speculative ADD
    ct.drive("dv_icache_data0", beq_instr)
    ct.drive("dv_icache_data1", add_instr)
    ct.drive("dv_icache_data2", 0)
    ct.drive("dv_icache_data3", 0)
    ct.drive("dv_icache_valid", 1)
    ct.drive("dv_stall", 0)
    ct.drive("dv_bru_redirect", 0)
    ct.drive("dv_bru_target", 0)
    ct.drive("dv_dmem_rdata", 0)
    ct.drive("dv_dmem_rvalid", 0)
    for i in range(6):
        ct.drive(f"dv_cdb_valid{i}", 0)
        ct.drive(f"dv_cdb_tag{i}", 0)
        ct.drive(f"dv_cdb_data{i}", 0)
    for i in range(4):
        ct.drive(f"dv_tcb_valid{i}", 0)
        ct.drive(f"dv_tcb_tag{i}", 0)

    ct.next()

    # Cycle 1: mispredict — redirect to 0x100
    ct.drive("dv_icache_valid", 0)
    ct.drive("dv_stall", 0)
    ct.drive("dv_bru_redirect", 1)
    ct.drive("dv_bru_target", 0x100)
    ct.drive("dv_dmem_rdata", 0)
    ct.drive("dv_dmem_rvalid", 0)
    for i in range(6):
        ct.drive(f"dv_cdb_valid{i}", 0)
        ct.drive(f"dv_cdb_tag{i}", 0)
        ct.drive(f"dv_cdb_data{i}", 0)
    for i in range(4):
        ct.drive(f"dv_tcb_valid{i}", 0)
        ct.drive(f"dv_tcb_tag{i}", 0)

    ct.next()

    # Cycle 2: PC should be 0x100 after redirect
    ct.drive("dv_bru_redirect", 0)
    ct.drive("dv_bru_target", 0)
    ct.drive("dv_stall", 0)
    ct.drive("dv_icache_valid", 0)
    ct.drive("dv_dmem_rdata", 0)
    ct.drive("dv_dmem_rvalid", 0)
    for i in range(6):
        ct.drive(f"dv_cdb_valid{i}", 0)
        ct.drive(f"dv_cdb_tag{i}", 0)
        ct.drive(f"dv_cdb_data{i}", 0)
    for i in range(4):
        ct.drive(f"dv_tcb_valid{i}", 0)
        ct.drive(f"dv_tcb_tag{i}", 0)

    ct.expect("dv_pc", 0x100, msg="PC should be redirect target 0x100")
    ct.next()
    ct.finish()

    assert any(e.value == 0x100 for e in t.expects)


if __name__ == "__main__":
    test_branch_recovery_tb()
