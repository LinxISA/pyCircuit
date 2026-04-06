#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CSU 行为仿真 → Perfetto UI 波形文件（Chrome Trace Event JSON）。

用法（从仓库根目录）::

    PYTHONPATH=compiler/frontend python3 designs/CSU/sim_csu_perfetto.py

输出 ``designs/CSU/sim_out/csu_wave.json``；在 https://ui.perfetto.dev 中
点击 "Open trace file" 直接打开即可看到所有信号的波形。

模型精度
--------
本脚本对 ``csu.py`` / ``build_csu`` 的 MLIR 做一对一行为建模：

* 两条 ``_v5_bal_*`` 延迟寄存器（``tb_issue_req`` +1拍、``tb_txreq_seed`` +1拍）。
* ``pyc.sync_mem``（``f035_data_ram_stub``，深度 16×32b，同步读输出）。
* 所有 ``domain.signal`` / ``domain.cycle`` DFF 在同一时钟沿采样。
* 输出端口为纯组合（``mux(rst, 0, reg_q)``）。

信号组（Perfetto pid 区分）
---------------------------
| pid | 组 | 信号 |
|-----|---|----|
| 1 | 控制 | clk, rst |
| 2 | CHI/TB 输入（低 32b） | rxrsp, rxdat, rxsnp, rsp1_side, rxwkup, rxerr, rxfill, tb_seed, tb_issue, cfg_word |
| 3 | 内部状态（低 32b） | tracker, lat_req, pend, side_wkup, side_err, side_fill, snp_digest, mega_0..7(低32b), feat(低32b), lfsr, brq, pmu, rrip, bal_issue, bal_seed(低32b) |
| 4 | F-035 sync_mem | ram_hit_waddr, mem_rd |
| 5 | 输出端口（低 32b） | txreq, txrsp, txdat, txreq_pend |
"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path
from typing import Any

_THIS = Path(__file__).resolve().parent
_REPO = _THIS.parents[1]
sys.path.insert(0, str(_REPO / "compiler" / "frontend"))
sys.path.insert(0, str(_THIS))

from csu import LEGAL_REQ_OPCODE_VALUES, _MEGA_COEFF  # noqa: E402

# ---------------------------------------------------------------------------
# 位宽掩码
# ---------------------------------------------------------------------------
_M = {w: (1 << w) - 1 for w in (1, 2, 3, 4, 7, 8, 16, 18, 26, 30, 32, 42,
                                  64, 82, 97, 128, 584, 615)}

_LEGAL_SET = frozenset(LEGAL_REQ_OPCODE_VALUES)


def _mask(v: int, w: int) -> int:
    return int(v) & _M[w]


# ---------------------------------------------------------------------------
# 行为模型状态
# ---------------------------------------------------------------------------
class CsuState:
    __slots__ = [
        "tracker", "lat_req", "pend",
        "side_wkup", "side_err", "side_fill", "snp_digest",
        "mega", "feat", "lfsr", "brq", "pmu", "rrip",
        "txreq_q", "txrsp_q", "txdat_q", "pend_q",
        "bal_issue", "bal_seed",
        "ram", "mem_rd",
    ]

    def __init__(self) -> None:
        self.tracker: int = 0
        self.lat_req: int = 0
        self.pend: int = 0
        self.side_wkup: int = 0
        self.side_err: int = 0
        self.side_fill: int = 0
        self.snp_digest: int = 0
        self.mega: list[int] = [0] * 8
        self.feat: int = 0
        self.lfsr: int = 0
        self.brq: int = 0
        self.pmu: int = 0
        self.rrip: int = 0
        self.txreq_q: int = 0
        self.txrsp_q: int = 0
        self.txdat_q: int = 0
        self.pend_q: int = 0
        # _v5_bal_ balance registers
        self.bal_issue: int = 0   # i1: delayed tb_issue_req
        self.bal_seed: int = 0    # i97: delayed tb_txreq_seed
        # F-035 sync_mem
        self.ram: list[int] = [0] * 16
        self.mem_rd: int = 0      # registered read output, i32


def step(s: CsuState, inp: dict[str, int]) -> dict[str, int]:
    """One clock-edge evaluation: compute outputs, compute next state.

    Returns output dict *before* the edge (i.e., the current-cycle outputs).
    Updates ``s`` in place with the next-cycle state.
    """
    rst_i = int(inp["rst"]) & 1    # active-high reset i1
    run = 1 - rst_i

    rxrsp = _mask(inp.get("rxrsp", 0), 42)
    rxdat = _mask(inp.get("rxdat", 0), 584)
    rxsnp = _mask(inp.get("rxsnp", 0), 82)
    rsp1  = _mask(inp.get("rsp1_side", 0), 4)
    rxwkup = _mask(inp.get("rxwkup", 0), 18)
    rxerr  = _mask(inp.get("rxerr", 0), 2)
    rxfill = _mask(inp.get("rxfill", 0), 3)
    seed  = _mask(inp.get("tb_txreq_seed", 0), 97)
    issue = _mask(inp.get("tb_issue_req", 0), 1)
    cfg   = _mask(inp.get("cfg_word", 0), 64)

    # ---- Outputs (combinational, use current Q) ----
    out = {
        "txreq":      0 if rst_i else s.txreq_q,
        "txrsp":      0 if rst_i else s.txrsp_q,
        "txdat":      0 if rst_i else s.txdat_q,
        "txreq_pend": 0 if rst_i else s.pend_q,
    }

    # ---- Occurrence-0 combinational ----
    digest32 = rxrsp & _M[32]
    snp_in   = rxsnp & _M[32]

    # ---- Occurrence-1: combinational using current Q ----
    # latched_txreq opcode (uses current Q, NOT next)
    opc     = (s.lat_req >> 21) & 0x7F
    legal   = int(opc in _LEGAL_SET)
    # inc: raw tb_issue_req (NOT bal_issue) & legal & run  [MLIR line 122]
    inc     = issue & legal & run

    # txreq_payload  [MLIR lines 110-116]
    tx_pay  = 0 if rst_i else (_mask(s.lat_req, 97) if legal else 0)

    # base_mix for mega (64b XOR aggregate of current inputs + current state regs)
    b0 = digest32
    b0 ^= snp_in
    b0 ^= rxdat & _M[32]
    b0 ^= rxrsp & _M[32]          # rxrsp[39:0] is 40b, but xor into 64b → use full rxrsp
    b0 ^= rxrsp                    # include upper bits too (rxrsp is 42b)
    b0 ^= rxwkup
    b0 ^= rxerr
    b0 ^= rxfill
    b0 ^= cfg
    b0 ^= s.lat_req & _M[32]      # lat_req[31:0]
    b0 ^= s.pend
    base_mix = b0 & _M[64]

    # sync_mem: F-035 read/write
    # write if inc=1 at waddr=lat_req[34:31], wdata=rxdat[31:0]
    # read registered: mem_rd is Q from previous edge
    mem_z = s.mem_rd & _M[64]     # 32b read data zero-extended to 64b

    # lfsr feedback bits (current Q)
    fb_lfsr = ((s.lfsr >> 15) & 1) ^ ((s.lfsr >> 12) & 1)

    # ---- Compute NEXT state (all D values) ----

    # bal registers (delay tb_issue_req and tb_txreq_seed by 1 cycle)
    n_bal_issue = issue
    n_bal_seed  = seed

    # tracker
    n_tracker = 0 if rst_i else digest32

    # latched_txreq: uses bal registers [MLIR line 37]
    n_lat_req = 0 if rst_i else (_mask(s.bal_seed, 97) if s.bal_issue else s.lat_req)

    # pend
    if rst_i:
        n_pend = 0
    elif inc:
        n_pend = _mask(s.pend + 1, 2)
    else:
        n_pend = s.pend

    # sideband
    n_side_wkup  = 0 if rst_i else rxwkup
    n_side_err   = 0 if rst_i else rxerr
    n_side_fill  = 0 if rst_i else rxfill
    n_snp_digest = 0 if rst_i else snp_in

    # mega features: mega[i]_D = rst ? 0 : (mega[i] XOR (base_mix XOR coeff[i] XOR mem_z))
    n_mega = []
    for i, coeff in enumerate(_MEGA_COEFF):
        delta = (base_mix ^ coeff ^ mem_z) & _M[64]
        n_mega.append(0 if rst_i else _mask(s.mega[i] ^ delta, 64))

    # lfsr: shift-register polynomial (F-060)
    lfsr_sh = _mask(s.lfsr << 1, 16)
    n_lfsr = 0 if rst_i else _mask(lfsr_sh | fb_lfsr, 16)

    # brq (F-042): increment when inc
    n_brq = 0 if rst_i else (_mask(s.brq + 1, 4) if inc else s.brq)

    # pmu (F-047): increment when inc
    n_pmu = 0 if rst_i else (_mask(s.pmu + 1, 16) if inc else s.pmu)

    # rrip (F-061): free-running counter
    n_rrip = 0 if rst_i else _mask(s.rrip + 1, 8)

    # feat: fold all mega + misc
    h01 = (_mask(s.mega[0], 64) << 64) | _mask(s.mega[1], 64)
    h23 = (_mask(s.mega[2], 64) << 64) | _mask(s.mega[3], 64)
    h45 = (_mask(s.mega[4], 64) << 64) | _mask(s.mega[5], 64)
    h67 = (_mask(s.mega[6], 64) << 64) | _mask(s.mega[7], 64)
    mix128 = (h01 ^ h23 ^ h45 ^ h67) & _M[128]
    mix128 ^= s.lfsr | s.brq | s.pmu | s.rrip | s.mem_rd  # all zext to 128b
    n_feat = 0 if rst_i else _mask(s.feat ^ mix128, 128)

    # F-035 sync_mem: update ram then read
    n_ram = list(s.ram)
    if inc:
        waddr = (s.lat_req >> 31) & 0xF
        n_ram[waddr] = rxdat & _M[32]
    raddr = rxdat & 0xF
    n_mem_rd = n_ram[raddr] if run else 0

    # cycle registers (D = values computed in occurrence 1/2)
    n_txreq_q = tx_pay                                          # txreq_q
    n_txrsp_q = _mask((rsp1 << 26) | (rxrsp & _M[26]), 30)     # txrsp_q
    n_txdat_q = _mask(rxdat, 615)                               # txdat_q (zero-ext)
    n_pend_q  = s.pend                                          # pend_q = current pend

    # ---- Commit next state ----
    s.tracker   = n_tracker
    s.lat_req   = n_lat_req
    s.pend      = n_pend
    s.side_wkup = n_side_wkup
    s.side_err  = n_side_err
    s.side_fill = n_side_fill
    s.snp_digest= n_snp_digest
    s.mega      = n_mega
    s.feat      = n_feat
    s.lfsr      = n_lfsr
    s.brq       = n_brq
    s.pmu       = n_pmu
    s.rrip      = n_rrip
    s.txreq_q   = n_txreq_q
    s.txrsp_q   = n_txrsp_q
    s.txdat_q   = n_txdat_q
    s.pend_q    = n_pend_q
    s.bal_issue = n_bal_issue
    s.bal_seed  = n_bal_seed
    s.ram       = n_ram
    s.mem_rd    = n_mem_rd

    return out


# ---------------------------------------------------------------------------
# 仿真场景
# ---------------------------------------------------------------------------
def _build_scenario() -> list[dict[str, int]]:
    """返回每拍输入字典列表（共 80 拍）。"""
    scenario: list[dict[str, int]] = []
    base: dict[str, int] = {
        "rxrsp": 0, "rxdat": 0, "rxsnp": 0,
        "rsp1_side": 0, "rxwkup": 0, "rxerr": 0, "rxfill": 0,
        "tb_txreq_seed": 0, "tb_issue_req": 0, "cfg_word": 0,
    }

    def mk(**kw: int) -> dict[str, int]:
        r = dict(base)
        r.update(kw)
        return r

    # 拍 0-15: 复位（rst=1）
    for _ in range(16):
        scenario.append(mk(rst=1))

    # 拍 16-17: 复位释放后空转（rst=0）
    for _ in range(2):
        scenario.append(mk(rst=0))

    # 拍 18-22: 空闲，注入一些 rxrsp/rxsnp 数据
    for i in range(5):
        scenario.append(mk(rst=0,
                           rxrsp=0xAB_0000 + i,
                           rxsnp=0x1234_0000 + i * 3))

    # 拍 23-29: 发 3 次合法 REQ（opcode 0x02 = ReadShared，bits[27:21]）
    # opcode 0x02 → 放在 [27:21] 位置 = 0x02 << 21 = 0x004_0000
    for req_no in range(3):
        seed_val = (0x02 << 21) | (0xBEEF + req_no * 0x100)
        scenario.append(mk(rst=0,
                           rxrsp=0xCAFE_0000 + req_no,
                           rxdat=0x1111_0000 + req_no,
                           tb_txreq_seed=seed_val,
                           tb_issue_req=1,
                           cfg_word=0xFEED_DEAD_0000 + req_no))
        scenario.append(mk(rst=0,
                           rxrsp=0xCAFE_0000 + req_no,
                           tb_issue_req=0))

    # 拍 35: 发一个非法 opcode（0x01，不在白名单）→ F-003 滤掉
    bad_seed = (0x01 << 21) | 0xDEAD
    scenario.append(mk(rst=0, tb_txreq_seed=bad_seed, tb_issue_req=1))
    scenario.append(mk(rst=0, tb_issue_req=0))

    # 拍 37-44: 带 WKUP/ERR/FILL 侧带
    for i in range(8):
        scenario.append(mk(rst=0,
                           rxwkup=0x12 + i,
                           rxerr=i & 3,
                           rxfill=(i * 2) & 7,
                           rxsnp=0xAA00 + i))

    # 拍 45-49: 短暂复位再释放
    for _ in range(3):
        scenario.append(mk(rst=1))
    for _ in range(2):
        scenario.append(mk(rst=0))

    # 拍 50-59: 二次发请求（opcode 0x04 = ReadUnique）
    for req_no in range(5):
        seed_val = (0x04 << 21) | (0xF000 + req_no * 0x200)
        scenario.append(mk(rst=0,
                           tb_txreq_seed=seed_val,
                           tb_issue_req=1,
                           rxrsp=0xFFFF & (req_no * 0x1111),
                           rxdat=0xABCD0000 + req_no))
        scenario.append(mk(rst=0))

    # 拍 60-79: 观测期
    while len(scenario) < 80:
        scenario.append(mk(rst=0, rxrsp=0x5A5A & len(scenario)))

    return scenario[:80]


# ---------------------------------------------------------------------------
# Perfetto JSON 生成
# ---------------------------------------------------------------------------
class PerfettoWriter:
    """Chrome Trace Event Format (JSON) for Perfetto UI."""

    # ts 单位：微秒（Chrome Trace Event Format 基准）
    # 每时钟周期 = 1 µs（便于在 Perfetto UI 中缩放查看单拍细节）
    CLK_PERIOD_US = 1

    def __init__(self) -> None:
        self._events: list[dict[str, Any]] = []
        self._pid_names: dict[int, str] = {}
        self._tid_counters: dict[tuple[int, str], int] = {}
        self._next_tid: dict[int, int] = {}

    def _tid(self, pid: int, name: str) -> int:
        key = (pid, name)
        if key not in self._tid_counters:
            self._tid_counters[key] = self._next_tid.get(pid, 100)
            self._next_tid[pid] = self._tid_counters[key] + 1
        return self._tid_counters[key]

    def declare_pid(self, pid: int, name: str) -> None:
        self._pid_names[pid] = name
        self._events.append({
            "ph": "M", "pid": pid, "tid": 0,
            "name": "process_name",
            "args": {"name": name},
        })

    def declare_track(self, pid: int, name: str) -> None:
        tid = self._tid(pid, name)
        self._events.append({
            "ph": "M", "pid": pid, "tid": tid,
            "name": "thread_name",
            "args": {"name": name},
        })

    def counter(self, cycle: int, pid: int, name: str, value: int) -> None:
        ts_us = cycle * self.CLK_PERIOD_US
        self._events.append({
            "ph": "C",
            "ts": float(ts_us),
            "pid": pid,
            "tid": self._tid(pid, name),
            "name": name,
            "args": {"value": int(value)},
        })

    def dumps(self) -> str:
        return json.dumps(self._events, separators=(",", ":"))


# ---------------------------------------------------------------------------
# 主程序
# ---------------------------------------------------------------------------
def main() -> None:
    out_dir = _THIS / "sim_out"
    out_dir.mkdir(exist_ok=True)

    pw = PerfettoWriter()

    # 进程组声明
    pw.declare_pid(1, "1 Control (clk/rst)")
    pw.declare_pid(2, "2 CHI/TB Inputs")
    pw.declare_pid(3, "3 Internal State")
    pw.declare_pid(4, "4 F-035 sync_mem")
    pw.declare_pid(5, "5 Outputs")

    # 轨道声明（确保 Perfetto 按声明顺序排列）
    for sig in ("clk", "rst"):
        pw.declare_track(1, sig)
    for sig in ("rxrsp[31:0]", "rxdat[31:0]", "rxsnp[31:0]", "rsp1_side",
                "rxwkup", "rxerr", "rxfill",
                "tb_txreq_seed[31:0]", "tb_issue_req", "cfg_word[31:0]"):
        pw.declare_track(2, sig)
    for sig in (
        "tracker", "lat_req[31:0]", "pend", "side_wkup", "side_err", "side_fill",
        "snp_digest",
        "mega_0[31:0]", "mega_1[31:0]", "mega_2[31:0]", "mega_3[31:0]",
        "mega_4[31:0]", "mega_5[31:0]", "mega_6[31:0]", "mega_7[31:0]",
        "feat[31:0]", "lfsr", "brq", "pmu", "rrip",
        "bal_issue", "bal_seed[31:0]",
    ):
        pw.declare_track(3, sig)
    for sig in ("f035_mem_rd", "f035_ram_waddr"):
        pw.declare_track(4, sig)
    for sig in ("txreq[31:0]", "txrsp", "txdat[31:0]", "txreq_pend"):
        pw.declare_track(5, sig)

    # 仿真
    state = CsuState()
    scenario = _build_scenario()

    prev_clk = 0
    for cyc, inp in enumerate(scenario):
        # clk 计数器（每拍 0→1→0 不体现；这里只画 clk 拍号）
        clk_val = 1 - prev_clk
        pw.counter(cyc, 1, "clk", clk_val)
        prev_clk = clk_val

        pw.counter(cyc, 1, "rst", inp["rst"])

        # 输入
        pw.counter(cyc, 2, "rxrsp[31:0]",          inp.get("rxrsp", 0) & 0xFFFF_FFFF)
        pw.counter(cyc, 2, "rxdat[31:0]",           inp.get("rxdat", 0) & 0xFFFF_FFFF)
        pw.counter(cyc, 2, "rxsnp[31:0]",           inp.get("rxsnp", 0) & 0xFFFF_FFFF)
        pw.counter(cyc, 2, "rsp1_side",             inp.get("rsp1_side", 0))
        pw.counter(cyc, 2, "rxwkup",                inp.get("rxwkup", 0))
        pw.counter(cyc, 2, "rxerr",                 inp.get("rxerr", 0))
        pw.counter(cyc, 2, "rxfill",                inp.get("rxfill", 0))
        pw.counter(cyc, 2, "tb_txreq_seed[31:0]",   inp.get("tb_txreq_seed", 0) & 0xFFFF_FFFF)
        pw.counter(cyc, 2, "tb_issue_req",           inp.get("tb_issue_req", 0))
        pw.counter(cyc, 2, "cfg_word[31:0]",         inp.get("cfg_word", 0) & 0xFFFF_FFFF)

        # 在 step() 之前记录当前状态
        pw.counter(cyc, 3, "tracker",               state.tracker)
        pw.counter(cyc, 3, "lat_req[31:0]",          state.lat_req & 0xFFFF_FFFF)
        pw.counter(cyc, 3, "pend",                   state.pend)
        pw.counter(cyc, 3, "side_wkup",              state.side_wkup)
        pw.counter(cyc, 3, "side_err",               state.side_err)
        pw.counter(cyc, 3, "side_fill",              state.side_fill)
        pw.counter(cyc, 3, "snp_digest",             state.snp_digest)
        for mi in range(8):
            pw.counter(cyc, 3, f"mega_{mi}[31:0]",  state.mega[mi] & 0xFFFF_FFFF)
        pw.counter(cyc, 3, "feat[31:0]",             state.feat & 0xFFFF_FFFF)
        pw.counter(cyc, 3, "lfsr",                   state.lfsr)
        pw.counter(cyc, 3, "brq",                    state.brq)
        pw.counter(cyc, 3, "pmu",                    state.pmu)
        pw.counter(cyc, 3, "rrip",                   state.rrip)
        pw.counter(cyc, 3, "bal_issue",              state.bal_issue)
        pw.counter(cyc, 3, "bal_seed[31:0]",         state.bal_seed & 0xFFFF_FFFF)

        pw.counter(cyc, 4, "f035_mem_rd",            state.mem_rd)
        # record write address when inc would be triggered
        opc_cur = (state.lat_req >> 21) & 0x7F
        legal_cur = int(opc_cur in _LEGAL_SET)
        inc_cur = inp.get("tb_issue_req", 0) & legal_cur & (1 - inp["rst"])
        pw.counter(cyc, 4, "f035_ram_waddr", (state.lat_req >> 31) & 0xF if inc_cur else 0)

        # step
        out = step(state, inp)

        # 输出
        pw.counter(cyc, 5, "txreq[31:0]",    out["txreq"] & 0xFFFF_FFFF)
        pw.counter(cyc, 5, "txrsp",          out["txrsp"])
        pw.counter(cyc, 5, "txdat[31:0]",    out["txdat"] & 0xFFFF_FFFF)
        pw.counter(cyc, 5, "txreq_pend",     out["txreq_pend"])

    json_path = out_dir / "csu_wave.json"
    json_path.write_text(pw.dumps(), encoding="utf-8")
    size_kb = json_path.stat().st_size // 1024
    print(f"[sim_csu_perfetto] 完成 {len(scenario)} 拍 → {json_path}  ({size_kb} KB)")
    print("  在 https://ui.perfetto.dev 点击 'Open trace file' 打开")


if __name__ == "__main__":
    main()
