from __future__ import annotations

from dataclasses import dataclass

from pycircuit import CycleAwareReg


@dataclass(frozen=True)
class CoreState:
    stage: CycleAwareReg
    pc: CycleAwareReg
    br_kind: CycleAwareReg
    br_base_pc: CycleAwareReg
    br_off: CycleAwareReg
    commit_cond: CycleAwareReg
    commit_tgt: CycleAwareReg
    cycles: CycleAwareReg
    halted: CycleAwareReg


@dataclass(frozen=True)
class IfIdRegs:
    window: CycleAwareReg
    pc: CycleAwareReg  # fetch address of instruction in IF/ID


@dataclass(frozen=True)
class IdExRegs:
    op: CycleAwareReg
    len_bytes: CycleAwareReg
    pc: CycleAwareReg  # PC of instruction in ID/EX
    regdst: CycleAwareReg
    srcl: CycleAwareReg
    srcr: CycleAwareReg
    srcp: CycleAwareReg
    imm: CycleAwareReg
    srcl_val: CycleAwareReg
    srcr_val: CycleAwareReg
    srcp_val: CycleAwareReg


@dataclass(frozen=True)
class ExMemRegs:
    op: CycleAwareReg
    len_bytes: CycleAwareReg
    pc: CycleAwareReg  # PC of instruction in EX/MEM
    regdst: CycleAwareReg
    alu: CycleAwareReg
    is_load: CycleAwareReg
    is_store: CycleAwareReg
    size: CycleAwareReg
    addr: CycleAwareReg
    wdata: CycleAwareReg


@dataclass(frozen=True)
class MemWbRegs:
    op: CycleAwareReg
    len_bytes: CycleAwareReg
    pc: CycleAwareReg  # PC of instruction in MEM/WB
    regdst: CycleAwareReg
    value: CycleAwareReg


@dataclass(frozen=True)
class RegFiles:
    gpr: list[CycleAwareReg]
    t: list[CycleAwareReg]
    u: list[CycleAwareReg]
