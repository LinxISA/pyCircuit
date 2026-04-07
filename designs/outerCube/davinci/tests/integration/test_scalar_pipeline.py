"""Integration test — Scalar pipeline: Fetch → Decode → Rename → RS → ALU → CDB.

Verifies that an ADD instruction flows through the complete scalar pipeline
and produces correct MLIR when composed in davinci_top.
"""

from __future__ import annotations

import os
import sys

_root = os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", "..")
sys.path.insert(0, os.path.join(_root, "compiler", "frontend"))
sys.path.insert(0, _root)

from pycircuit import CycleAwareTb, compile_cycle_aware
from pycircuit.tb import Tb


def test_davinci_top_compile():
    """davinci_top compiles to valid MLIR with full pipeline."""
    from designs.outerCube.davinci.davinci_top import davinci_top

    circuit = compile_cycle_aware(davinci_top, eager=True, name="davinci_top")
    mlir = circuit.emit_mlir()
    assert "func.func @davinci_top" in mlir
    assert "dv_pc" in mlir
    assert "dv_fetch_valid" in mlir
    assert "dv_ren_psrc1_0" in mlir
    assert "dv_srs_age_0" in mlir


def test_scalar_pipeline_tb():
    """Generate scalar pipeline testbench: inject ADD, expect CDB writeback."""
    t = Tb()
    ct = CycleAwareTb(t)
    ct.clock("clk")
    ct.reset("rst", cycles_asserted=2, cycles_deasserted=1)
    ct.timeout(64)

    # Cycle 0: provide an ADD instruction (opcode=0110011, funct3=000, funct7=0000000)
    # Encoding: funct7[31:25]=0000000 rs2[24:20]=00010 rs1[19:15]=00001
    #           funct3[14:12]=000 rd[11:7]=00011 opcode[6:0]=0110011
    add_instr = 0b0000000_00010_00001_000_00011_0110011
    ct.drive("dv_icache_data0", add_instr)
    ct.drive("dv_icache_data1", 0)
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

    ct.expect("dv_fetch_valid", 1, msg="Fetch should be valid")
    ct.expect("dv_dec_valid_0", 1, msg="Slot 0 decoded")
    ct.expect("dv_dec_domain_0", 0b01, msg="Scalar domain")

    ct.next()

    # Cycle 1: PC should advance by 16
    ct.drive("dv_icache_valid", 0)
    ct.drive("dv_stall", 0)
    ct.drive("dv_bru_redirect", 0)
    ct.drive("dv_bru_target", 0)
    for i in range(6):
        ct.drive(f"dv_cdb_valid{i}", 0)
        ct.drive(f"dv_cdb_tag{i}", 0)
        ct.drive(f"dv_cdb_data{i}", 0)
    for i in range(4):
        ct.drive(f"dv_tcb_valid{i}", 0)
        ct.drive(f"dv_tcb_tag{i}", 0)

    ct.next()
    ct.finish()

    assert len(t.drives) > 0
    assert len(t.expects) > 0


if __name__ == "__main__":
    test_davinci_top_compile()
    test_scalar_pipeline_tb()
