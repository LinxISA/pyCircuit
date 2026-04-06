# -*- coding: utf-8 -*-
"""LinxCore950 CSU — pyCircuit V5 cycle-aware RTL (incremental, CHI shell).

Port widths from ``designs/CSU/docs/port_list.md`` (SRC-01 XLSX).

Normative Markdown digests: ``designs/CSU/docs/converted/`` (regenerate via
``python3 designs/CSU/scripts/export_specs_to_md.py``).

REQ opcode allowlist for ``txreq`` gating: SRC-02 sheet ``CHI CSU_SoC_Opcode``,
column **P HC supports（950）** = ``Yes``.

**F-001 / F-014:** ``domain.create_reset()`` uses ``pyc.reset_active`` (``!pyc.reset`` → ``i1``)
so outputs and state **D** inputs are masked while reset is asserted (TB active-high).

**Feature coverage:** 主表 **71** 条 F-ID（F-001–F-075 除去 F-041、F-063、F-064、F-070）均在 RTL 中有可综合占位：
CHI 主路径 + 8×64b ``mega_feature_*`` 槽（按 ``FEATURE_SLOT_BY_FID`` 文档映射）+ ``feat_shaft_stub`` 折叠 +
F-035 ``sync_mem`` + F-042/F-047/F-060/F-061 专用寄存器。TB 口 ``cfg_word`` 默认 0；非 DOCX 级协议完备签核。

Verification hooks:
  * ``tb_txreq_seed`` (97b), ``tb_issue_req`` (1b) — directed REQ injection.

Cycle / MLIR contract: ``INC0_*`` constants + ``assert_inc0_mlir_cycle_contract`` — update when
pipeline ``next()`` calls / register count change.
"""

from __future__ import annotations

from pycircuit import CycleAwareCircuit, CycleAwareDomain, cas, compile_cycle_aware, mux

W_TXREQ = 97
W_TXREQ_PEND = 2
W_TXRSP = 30
W_RXRSP = 42
W_TXDAT = 615
W_RXDAT = 584
W_RXWKUP = 18
W_RXERR = 2
W_RXFILL = 3
W_RXSNP = 82
W_RSP1_SIDE = 4

_OPCODE_HI = 27
_OPCODE_LO = 21
_OPCODE_W = _OPCODE_HI - _OPCODE_LO + 1

# SRC-02: REQ opcodes marked "Yes" under P HC supports（950）
LEGAL_REQ_OPCODE_VALUES: tuple[int, ...] = (
    0x00,
    0x02,
    0x03,
    0x04,
    0x07,
    0x08,
    0x09,
    0x0C,
    0x0D,
    0x14,
    0x15,
    0x17,
    0x19,
    0x1B,
    0x1C,
    0x1D,
    0x26,
    0x27,
    0x41,
    0x42,
    0x43,
    0x44,
    0x4E,
    0x59,
    0x5C,
)

W_TRACKER = 32
W_SNP_DIGEST = 32

# 主表 71 条 F-ID：F-001 仅复位门控；其余 70 条 → ``mega_feature_{w}[hi:lo]`` 各 7b（见 ``FEATURE_SLOT_BY_FID``）
# 主表 71 项 = F-001 +（F-002…F-075 去掉 F-041、F-063、F-064、F-070）共 70 项
_FEATURE_IDS_WITH_MEGA_SLOT: tuple[int, ...] = tuple(
    i for i in range(2, 76) if i not in (41, 63, 64, 70)
)
assert len(_FEATURE_IDS_WITH_MEGA_SLOT) == 70
FEATURE_SLOT_BY_FID: dict[int, tuple[int, int, int]] = {}
for _k, _fid in enumerate(_FEATURE_IDS_WITH_MEGA_SLOT):
    _w = _k // 9
    _sub = _k % 9
    _lo = _sub * 7
    _hi = _lo + 6
    FEATURE_SLOT_BY_FID[_fid] = (_w, _lo, _hi)

# Golden coefficients for per-mega XOR (stable across runs)
_MEGA_COEFF: tuple[int, ...] = tuple(
    (0x9E3779B9 * (i + 1) ^ (i * 0xDEADBEEF)) & ((1 << 64) - 1) for i in range(8)
)

# Cycle budget — sync ``docs/cycle_budget.md`` §2 when editing ``build_csu``.
INC0_DOMAIN_NEXT_COUNT = 3
INC0_OCCURRENCE_STAGES = INC0_DOMAIN_NEXT_COUNT + 1
# tracker, latched_txreq, pend, 3×side, snp + 8×mega + feat + lfsr + brq + pmu + rrip
INC0_EXPLICIT_STATE_REGS = 7 + 8 + 1 + 4
INC0_EXPLICIT_CYCLE_REGS = 4
# Golden counts from ``emit_csu_mlir()`` after edits:
INC0_EXPECTED_PYC_REG_COUNT = 26
INC0_EXPECTED_V5_BAL_MARKERS = 4


def assert_inc0_mlir_cycle_contract(mlir: str) -> None:
    n = mlir.count("pyc.reg")
    if n != INC0_EXPECTED_PYC_REG_COUNT:
        raise AssertionError(f"CSU MLIR: expected {INC0_EXPECTED_PYC_REG_COUNT} pyc.reg, got {n}")
    bal = mlir.count("_v5_bal_")
    if bal != INC0_EXPECTED_V5_BAL_MARKERS:
        raise AssertionError(f"CSU MLIR: expected {INC0_EXPECTED_V5_BAL_MARKERS} _v5_bal_ markers, got {bal}")
    if "pyc.reset_active" not in mlir:
        raise AssertionError("CSU MLIR: expected at least one pyc.reset_active (rst→i1)")


def _zero(m: CycleAwareCircuit, w: int):
    return m.const(0, width=int(w))


def _req_opcode_supported(m: CycleAwareCircuit, opc) -> object:
    parts = [opc == m.const(int(v), width=_OPCODE_W) for v in LEGAL_REQ_OPCODE_VALUES]
    acc = parts[0].wire
    for p in parts[1:]:
        acc = acc | p.wire
    return acc


def build_csu(m: CycleAwareCircuit, domain: CycleAwareDomain) -> None:
    rst = domain.create_reset()
    run = ~rst

    # --- Occurrence 0: RX + TB + sideband capture sources ---
    rxrsp = cas(domain, m.input("rxrsp", width=W_RXRSP), cycle=0)
    rxdat = cas(domain, m.input("rxdat", width=W_RXDAT), cycle=0)
    rxsnp = cas(domain, m.input("rxsnp", width=W_RXSNP), cycle=0)
    rsp1 = cas(domain, m.input("rsp1_side", width=W_RSP1_SIDE), cycle=0)
    rxwkup = cas(domain, m.input("rxwkup", width=W_RXWKUP), cycle=0)
    rxerr = cas(domain, m.input("rxerr", width=W_RXERR), cycle=0)
    rxfill = cas(domain, m.input("rxfill", width=W_RXFILL), cycle=0)

    tb_txreq_seed = cas(domain, m.input("tb_txreq_seed", width=W_TXREQ), cycle=0)
    tb_issue_req = cas(domain, m.input("tb_issue_req", width=1), cycle=0)
    cfg_word = cas(domain, m.input("cfg_word", width=64), cycle=0)

    _ = rsp1  # F-008: used next stage; keeps port live

    digest32 = rxrsp.slice(31, 0).wire
    if digest32.width < W_TRACKER:
        digest32 = digest32.zext(width=W_TRACKER)

    snp_in = rxsnp.slice(31, 0).wire
    if snp_in.width < W_SNP_DIGEST:
        snp_in = snp_in.zext(width=W_SNP_DIGEST)

    domain.next()

    # --- Occurrence 1: sequential state (masked on rst) ---
    z_tr = _zero(m, W_TRACKER)
    z_req = _zero(m, W_TXREQ)
    z_pend = _zero(m, W_TXREQ_PEND)
    z_wk = _zero(m, W_RXWKUP)
    z_er = _zero(m, W_RXERR)
    z_fl = _zero(m, W_RXFILL)
    z_snp = _zero(m, W_SNP_DIGEST)

    tracker = domain.signal(width=W_TRACKER, reset_value=0, name="txn_tracker_stub")
    tracker <<= mux(rst, z_tr, digest32)

    latched_txreq = domain.signal(width=W_TXREQ, reset_value=0, name="latched_txreq")
    next_seed = mux(tb_issue_req, tb_txreq_seed, latched_txreq)
    latched_txreq <<= mux(rst, z_req, next_seed)

    opc = latched_txreq.slice(_OPCODE_HI, _OPCODE_LO)
    legal = _req_opcode_supported(m, opc)
    # F-002: payload is full latched flit; F-003 clears illegal opcodes
    txreq_payload = mux(rst, z_req, mux(legal, latched_txreq, z_req))

    pend = domain.signal(width=W_TXREQ_PEND, reset_value=0, name="txreq_pend_stub")
    inc = tb_issue_req.wire & legal & run
    pend_next = pend + m.const(1, width=W_TXREQ_PEND)
    pend <<= mux(rst, z_pend, mux(inc, pend_next, pend))

    # F-009 / F-010 / F-011: sideband absorption registers (placeholder)
    side_wkup = domain.signal(width=W_RXWKUP, reset_value=0, name="side_rxwkup_hold")
    side_wkup <<= mux(rst, z_wk, rxwkup.wire)
    side_err = domain.signal(width=W_RXERR, reset_value=0, name="side_rxerr_hold")
    side_err <<= mux(rst, z_er, rxerr.wire)
    side_fill = domain.signal(width=W_RXFILL, reset_value=0, name="side_rxfill_hold")
    side_fill <<= mux(rst, z_fl, rxfill.wire)

    # F-008: snoop digest (rsp1 used in occ2 combine for txrsp stub)
    snp_digest = domain.signal(width=W_SNP_DIGEST, reset_value=0, name="snoop_digest_stub")
    snp_digest <<= mux(rst, z_snp, snp_in)

    cd = domain.clock_domain
    z64 = _zero(m, 64)
    z128 = _zero(m, 128)
    z16 = _zero(m, 16)
    z4 = _zero(m, 4)
    z8 = _zero(m, 8)

    megas = [
        domain.signal(width=64, reset_value=0, name=f"mega_feature_{i}") for i in range(8)
    ]
    feat = domain.signal(width=128, reset_value=0, name="feat_shaft_stub")
    lfsr = domain.signal(width=16, reset_value=0, name="f060_lfsr_stub")
    brq = domain.signal(width=4, reset_value=0, name="f042_brq_stub")
    pmu = domain.signal(width=16, reset_value=0, name="f047_pmu_stub")
    rrip = domain.signal(width=8, reset_value=0, name="f061_rrip_stub")

    base_mix = (
        digest32.zext(width=64)
        ^ snp_in.zext(width=64)
        ^ rxdat.slice(31, 0).wire.zext(width=64)
        ^ rxrsp.slice(39, 0).wire.zext(width=64)
        ^ rxwkup.wire.zext(width=64)
        ^ rxerr.wire.zext(width=64)
        ^ rxfill.wire.zext(width=64)
        ^ cfg_word.wire.zext(width=64)
        ^ latched_txreq.slice(31, 0).zext(width=64)
        ^ pend.zext(width=64)
    )

    raddr4 = rxdat.slice(3, 0).wire
    waddr4 = latched_txreq.slice(34, 31)
    wd32 = rxdat.slice(31, 0).wire
    wstrb4 = m.const(15, width=4)
    mem_rd = m.sync_mem(
        cd.clk,
        cd.rst,
        ren=run,
        raddr=raddr4,
        wvalid=inc,
        waddr=waddr4,
        wdata=wd32,
        wstrb=wstrb4,
        depth=16,
        name="f035_data_ram_stub",
    )
    mem_z = mem_rd.zext(width=64)

    for i, mega in enumerate(megas):
        delta = base_mix ^ m.const(_MEGA_COEFF[i], width=64) ^ mem_z
        mega <<= mux(rst, z64, mega ^ delta)

    fb = lfsr[15] ^ lfsr[12]
    lfsr_sh = (lfsr << 1).trunc(width=16)
    lfsr_next = lfsr_sh | fb.zext(width=16)
    lfsr <<= mux(rst, z16, lfsr_next)

    brq_next = brq + m.const(1, width=4)
    brq <<= mux(rst, z4, mux(inc, brq_next, brq))

    pmu_next = pmu + m.const(1, width=16)
    pmu <<= mux(rst, z16, mux(inc, pmu_next, pmu))

    rrip_next = rrip + m.const(1, width=8)
    rrip <<= mux(rst, z8, rrip_next)

    h01 = m.cat(megas[0], megas[1])
    h23 = m.cat(megas[2], megas[3])
    h45 = m.cat(megas[4], megas[5])
    h67 = m.cat(megas[6], megas[7])
    mix_128 = h01 ^ h23 ^ h45 ^ h67
    mix_128 = (
        mix_128
        ^ lfsr.zext(width=128)
        ^ brq.zext(width=128)
        ^ pmu.zext(width=128)
        ^ rrip.zext(width=128)
        ^ mem_rd.zext(width=128)
    )
    feat <<= mux(rst, z128, feat ^ mix_128)

    domain.next()

    # --- Occurrence 2: output registers ---
    txreq_q = domain.cycle(txreq_payload, name="txreq_q")
    # F-004 / F-008: mix RXRSP low 26b with rsp1 (4b) in MSBs — structural hook only
    txrsp_lo = rxrsp.slice(25, 0).wire
    rsp_z = rsp1.wire.zext(width=W_TXRSP)
    txrsp_src = (rsp_z << 26) | txrsp_lo.zext(width=W_TXRSP)
    txrsp_q = domain.cycle(txrsp_src, name="txrsp_q")
    txdat_wide = rxdat.wire.zext(width=W_TXDAT)
    txdat_q = domain.cycle(txdat_wide, name="txdat_q")
    pend_q = domain.cycle(pend, name="txreq_pend_q")

    domain.next()

    # F-001: combinational idle on egress during reset
    z_txrsp = _zero(m, W_TXRSP)
    z_txdat = _zero(m, W_TXDAT)
    m.output("txreq", mux(rst, z_req, txreq_q))
    m.output("txrsp", mux(rst, z_txrsp, txrsp_q))
    m.output("txdat", mux(rst, z_txdat, txdat_q))
    m.output("txreq_pend", mux(rst, z_pend, pend_q))


build_csu.__pycircuit_name__ = "csu"


def emit_csu_mlir() -> str:
    m = compile_cycle_aware(build_csu, name="csu", eager=True)
    mlir = m.emit_mlir()
    assert_inc0_mlir_cycle_contract(mlir)
    return mlir


if __name__ == "__main__":
    mlir = emit_csu_mlir()
    print(mlir[:2000])
    print("\n... pyc.reg", mlir.count("pyc.reg"), "chars", len(mlir))
