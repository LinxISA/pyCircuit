from __future__ import annotations

from pycircuit import CycleAwareCircuit, CycleAwareSignal, mux

from ..pipeline import ExMemRegs, MemWbRegs


def mem_stage_logic(
    m: CycleAwareCircuit,
    ex_out: dict,
    mem_rdata: CycleAwareSignal,
) -> dict:
    """纯组合：EX 级输出 + 读出的 mem_rdata -> MEM/WB 级 value（无 flop）。"""
    op = ex_out["op"]
    len_bytes = ex_out["len_bytes"]
    pc = ex_out["pc"]
    regdst = ex_out["regdst"]
    alu = ex_out["alu"]
    is_load = ex_out["is_load"]
    is_store = ex_out["is_store"]
    load32 = mem_rdata.trunc(width=32)
    load64 = load32.sext(width=64)
    mem_val = alu
    mem_val = mux(is_load, load64, mem_val)
    mem_val = mux(is_store, m.ca_const(0, width=64), mem_val)
    return {"op": op, "len_bytes": len_bytes, "pc": pc, "regdst": regdst, "value": mem_val}


def build_mem_stage(
    m: CycleAwareCircuit,
    *,
    do_mem: CycleAwareSignal,
    exmem: ExMemRegs,
    memwb: MemWbRegs,
    mem_rdata: CycleAwareSignal,
) -> None:
    # Stage inputs.
    op = exmem.op.out()
    len_bytes = exmem.len_bytes.out()
    pc = exmem.pc.out()
    regdst = exmem.regdst.out()
    alu = exmem.alu.out()
    is_load = exmem.is_load.out()
    is_store = exmem.is_store.out()

    # Combinational.
    load32 = mem_rdata.trunc(width=32)
    load64 = load32.sext(width=64)
    
    # mem_val = alu by default, load64 if is_load, 0 if is_store
    mem_val = alu
    mem_val = mux(is_load, load64, mem_val)
    mem_val = mux(is_store, m.ca_const(0, width=64), mem_val)

    # Pipeline regs: MEM/WB.
    memwb.op.set(op, when=do_mem)
    memwb.len_bytes.set(len_bytes, when=do_mem)
    memwb.pc.set(pc, when=do_mem)
    memwb.regdst.set(regdst, when=do_mem)
    memwb.value.set(mem_val, when=do_mem)
