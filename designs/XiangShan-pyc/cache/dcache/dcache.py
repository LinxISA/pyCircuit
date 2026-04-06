"""DCache — Data Cache for XiangShan-pyc.

VIPT (Virtual Index Physical Tag) set-associative data cache.
4-stage pipeline: s0 (request + SRAM read), s1 (tag compare),
s2 (data select / miss handling), s3 (response).

Reference: XiangShan/src/main/scala/xiangshan/cache/DCacheWrapper.scala

Pipeline:
  s0  Accept load/store request, compute set index, read Tag/Data SRAMs
  s1  Tag compare: match physical tag, generate per-way hit vector
  s2  Load: data mux on hit; Store: schedule writeback; MSHR on miss
  s3  Drive response (valid, data, hit)

Key parameters (from XiangShan KunMingHu defaults):
  nSets=256, nWays=8, blockBytes=64  →  128 KB, VIPT

Simplified vs full XiangShan:
  - Single MSHR entry (XiangShan has 16 MSHRs)
  - Single-entry store writeback buffer
  - No probe queue / writeback queue (no coherence protocol)
  - No ECC / SECDED
  - No atomics unit
  - No TileLink coherence (Probe/Release)
  - Unified load/store pipeline (XiangShan has separate load pipe + main pipe)
"""
from __future__ import annotations

import math
import sys
from pathlib import Path

_XS_ROOT = Path(__file__).resolve().parent.parent.parent
if str(_XS_ROOT) not in sys.path:
    sys.path.insert(0, str(_XS_ROOT))

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    CycleAwareSignal,
    cas,
    compile_cycle_aware,
    mux,
    u,
)
from top.parameters import *


def build_dcache(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    prefix: str = "dc",
    n_sets: int = DCACHE_SETS,
    n_ways: int = DCACHE_WAYS,
    block_bytes: int = DCACHE_BLOCK_BYTES,
    paddr_width: int = 36,
    inputs: dict[str, CycleAwareSignal] | None = None,
) -> dict[str, CycleAwareSignal]:
    """DCache: VIPT set-associative data cache with 4-stage pipeline."""
    _in = inputs or {}
    _out: dict[str, CycleAwareSignal] = {}


    block_bits = block_bytes * 8
    offset_bits = int(math.log2(block_bytes))
    index_bits = int(math.log2(n_sets))
    tag_bits = paddr_width - index_bits - offset_bits
    way_bits = max(1, int(math.log2(n_ways)))
    tag_strobe_w = (tag_bits + 7) // 8
    data_strobe_w = block_bytes

    cd = domain.clock_domain

    # ================================================================
    # s0 — Request: accept load/store, decompose address, read SRAMs
    # ================================================================

    flush = (_in["flush"] if "flush" in _in else

        cas(domain, m.input(f"{prefix}_flush", width=1), cycle=0))

    load_valid = (_in["load_valid"] if "load_valid" in _in else

        cas(domain, m.input(f"{prefix}_load_valid", width=1), cycle=0))
    load_vaddr = (_in["load_vaddr"] if "load_vaddr" in _in else
        cas(domain, m.input(f"{prefix}_load_vaddr", width=paddr_width), cycle=0))
    load_ptag = (_in["load_ptag"] if "load_ptag" in _in else
        cas(domain, m.input(f"{prefix}_load_ptag", width=tag_bits), cycle=0))

    store_valid = (_in["store_valid"] if "store_valid" in _in else

        cas(domain, m.input(f"{prefix}_store_valid", width=1), cycle=0))
    store_vaddr = (_in["store_vaddr"] if "store_vaddr" in _in else
        cas(domain, m.input(f"{prefix}_store_vaddr", width=paddr_width), cycle=0))
    store_ptag = (_in["store_ptag"] if "store_ptag" in _in else
        cas(domain, m.input(f"{prefix}_store_ptag", width=tag_bits), cycle=0))
    store_wdata = (_in["store_wdata"] if "store_wdata" in _in else
        cas(domain, m.input(f"{prefix}_store_wdata", width=block_bits), cycle=0))
    store_wmask = (_in["store_wmask"] if "store_wmask" in _in else
        cas(domain, m.input(f"{prefix}_store_wmask", width=block_bytes), cycle=0))

    refill_valid = (_in["refill_valid"] if "refill_valid" in _in else

        cas(domain, m.input(f"{prefix}_refill_valid", width=1), cycle=0))
    refill_set = (_in["refill_set"] if "refill_set" in _in else
        cas(domain, m.input(f"{prefix}_refill_set", width=index_bits), cycle=0))
    refill_tag = (_in["refill_tag"] if "refill_tag" in _in else
        cas(domain, m.input(f"{prefix}_refill_tag", width=tag_bits), cycle=0))
    refill_way = (_in["refill_way"] if "refill_way" in _in else
        cas(domain, m.input(f"{prefix}_refill_way", width=way_bits), cycle=0))
    refill_data = (_in["refill_data"] if "refill_data" in _in else
        cas(domain, m.input(f"{prefix}_refill_data", width=block_bits), cycle=0))

    # Load has priority; store accepted only when no load
    req_valid = load_valid | store_valid
    req_is_store = store_valid & (~load_valid)
    req_vaddr = mux(load_valid, load_vaddr, store_vaddr)
    req_ptag = mux(load_valid, load_ptag, store_ptag)

    s0_set_idx = req_vaddr[offset_bits:offset_bits + index_bits]
    s0_fire = req_valid & (~flush)

    # ── Feedback state ────────────────────────────────────────────

    valid_regs = [
        domain.signal(width=n_sets, reset_value=0, name=f"{prefix}_vld{w}")
        for w in range(n_ways)
    ]
    dirty_regs = [
        domain.signal(width=n_sets, reset_value=0, name=f"{prefix}_drt{w}")
        for w in range(n_ways)
    ]

    mshr_valid = domain.signal(width=1, reset_value=0, name=f"{prefix}_mshr_v")
    mshr_set = domain.signal(width=index_bits, reset_value=0, name=f"{prefix}_mshr_set")
    mshr_tag = domain.signal(width=tag_bits, reset_value=0, name=f"{prefix}_mshr_tag")

    swb_valid = domain.signal(width=1, reset_value=0, name=f"{prefix}_swb_v")
    swb_set = domain.signal(width=index_bits, reset_value=0, name=f"{prefix}_swb_set")
    swb_way = domain.signal(width=way_bits, reset_value=0, name=f"{prefix}_swb_way")
    swb_data = domain.signal(width=block_bits, reset_value=0, name=f"{prefix}_swb_data")
    swb_mask = domain.signal(width=block_bytes, reset_value=0, name=f"{prefix}_swb_mask")

    # ── Data SRAM write mux: refill has priority over store writeback ─

    swb_v_c0 = swb_valid
    swb_set_c0 = swb_set
    swb_way_c0 = swb_way
    swb_data_c0 = swb_data
    swb_mask_c0 = swb_mask

    data_wr_valid = refill_valid | swb_v_c0
    data_wr_set = mux(refill_valid, refill_set, swb_set_c0)
    data_wr_way = mux(refill_valid, refill_way, swb_way_c0)
    data_wr_data = mux(refill_valid, refill_data, swb_data_c0)
    full_mask = cas(
        domain,
        m.const((1 << block_bytes) - 1, width=block_bytes),
        cycle=0,
    )
    data_wr_mask = mux(refill_valid, full_mask, swb_mask_c0)

    # ── Per-way Tag & Data SRAMs (synchronous read) ──────────────

    tag_rd = []
    data_rd = []
    for w in range(n_ways):
        w_const = cas(domain, m.const(w, width=way_bits), cycle=0)

        tag_wr_en = (refill_valid & (refill_way == w_const)).wire
        data_wr_en = (data_wr_valid & (data_wr_way == w_const)).wire

        tag_rd.append(m.sync_mem(
            cd.clk, cd.rst,
            ren=s0_fire.wire, raddr=s0_set_idx.wire,
            wvalid=tag_wr_en, waddr=refill_set.wire,
            wdata=refill_tag.wire,
            wstrb=m.const((1 << tag_strobe_w) - 1, width=tag_strobe_w),
            depth=n_sets, name=f"{prefix}_tag_w{w}",
        ))

        data_rd.append(m.sync_mem(
            cd.clk, cd.rst,
            ren=s0_fire.wire, raddr=s0_set_idx.wire,
            wvalid=data_wr_en, waddr=data_wr_set.wire,
            wdata=data_wr_data.wire,
            wstrb=data_wr_mask.wire,
            depth=n_sets, name=f"{prefix}_data_w{w}",
        ))

    # ── Pipeline registers s0 → s1 ───────────────────────────────

    s1_valid_w = domain.cycle(s0_fire.wire, name=f"{prefix}_s1_v")
    s1_set_idx_w = domain.cycle(s0_set_idx.wire, name=f"{prefix}_s1_set")
    s1_ptag_w = domain.cycle(req_ptag.wire, name=f"{prefix}_s1_ptag")
    s1_is_store_w = domain.cycle(req_is_store.wire, name=f"{prefix}_s1_st")
    s1_store_data_w = domain.cycle(store_wdata.wire, name=f"{prefix}_s1_wdata")
    s1_store_mask_w = domain.cycle(store_wmask.wire, name=f"{prefix}_s1_wmask")

    domain.next()  # ─────────────── s0 → s1 boundary ──────────────

    # ================================================================
    # s1 — Tag Compare: match physical tag against each way
    # ================================================================

    s1_way_hit = []
    for w in range(n_ways):
        vld_bit = valid_regs[w].wire.lshr(amount=s1_set_idx_w)[0:1]
        tag_eq = (tag_rd[w][0:tag_bits] == s1_ptag_w)
        s1_way_hit.append(vld_bit & tag_eq)

    s1_any_hit = s1_way_hit[0]
    for w in range(1, n_ways):
        s1_any_hit = s1_any_hit | s1_way_hit[w]

    s1_miss = s1_valid_w & (~s1_any_hit)

    s1_hit_data = data_rd[0]
    for w in range(1, n_ways):
        s1_hit_data = s1_way_hit[w].select(data_rd[w], s1_hit_data)

    # Encode hit way index (for store writeback targeting)
    s1_hit_way = m.const(0, width=way_bits)
    for w in range(n_ways):
        s1_hit_way = s1_way_hit[w].select(m.const(w, width=way_bits), s1_hit_way)

    # ── Pipeline registers s1 → s2 ───────────────────────────────

    s2_valid_w = domain.cycle(s1_valid_w, name=f"{prefix}_s2_v")
    s2_hit_w = domain.cycle(s1_any_hit, name=f"{prefix}_s2_hit")
    s2_miss_w = domain.cycle(s1_miss, name=f"{prefix}_s2_miss")
    s2_data_w = domain.cycle(s1_hit_data, name=f"{prefix}_s2_data")
    s2_set_idx_w = domain.cycle(s1_set_idx_w, name=f"{prefix}_s2_set")
    s2_ptag_w = domain.cycle(s1_ptag_w, name=f"{prefix}_s2_ptag")
    s2_is_store_w = domain.cycle(s1_is_store_w, name=f"{prefix}_s2_st")
    s2_hit_way_w = domain.cycle(s1_hit_way, name=f"{prefix}_s2_hway")
    s2_store_data_w = domain.cycle(s1_store_data_w, name=f"{prefix}_s2_wdata")
    s2_store_mask_w = domain.cycle(s1_store_mask_w, name=f"{prefix}_s2_wmask")

    domain.next()  # ─────────────── s1 → s2 boundary ──────────────

    # ================================================================
    # s2 — Data Select / Miss Handling / Store Writeback Scheduling
    # ================================================================

    mshr_free = ~mshr_valid.wire
    mshr_alloc = s2_miss_w & s2_valid_w & mshr_free

    # Refill bypass: if refill arrives for the same line, forward it
    s2_refill_match = (
        refill_valid.wire
        & (refill_set.wire == s2_set_idx_w)
        & (refill_tag.wire == s2_ptag_w)
    )

    s2_resp_hit = s2_hit_w | s2_refill_match
    s2_resp_data = s2_refill_match.select(refill_data.wire, s2_data_w)

    # Store hit → will buffer in SWB for next-cycle SRAM writeback
    s2_store_hit = s2_valid_w & s2_is_store_w & s2_hit_w

    # Pre-compute s3 output signals
    s2_load_resp_valid = s2_valid_w & (~s2_is_store_w) & s2_resp_hit
    s2_load_hit = s2_resp_hit & (~s2_is_store_w)
    s2_store_resp_valid = s2_valid_w & s2_is_store_w

    # ── Pipeline registers s2 → s3 ───────────────────────────────

    s3_valid_w = domain.cycle(s2_valid_w, name=f"{prefix}_s3_v")
    s3_hit_w = domain.cycle(s2_resp_hit, name=f"{prefix}_s3_hit")
    s3_data_w = domain.cycle(s2_resp_data, name=f"{prefix}_s3_data")
    s3_store_hit_w = domain.cycle(s2_store_hit, name=f"{prefix}_s3_st_hit")
    s3_load_resp_v_w = domain.cycle(s2_load_resp_valid, name=f"{prefix}_s3_lrv")
    s3_load_hit_w = domain.cycle(s2_load_hit, name=f"{prefix}_s3_lhit")
    s3_store_resp_v_w = domain.cycle(s2_store_resp_valid, name=f"{prefix}_s3_srv")

    domain.next()  # ─────────────── s2 → s3 boundary ──────────────

    # ================================================================
    # s3 — Response
    # ================================================================

    domain.next()  # ─────────────── s3 → state-update boundary ────

    # ================================================================
    # State Updates
    # ================================================================

    # Valid bits: set way-valid on refill
    for w in range(n_ways):
        wr_way = refill_valid.wire & (refill_way.wire == m.const(w, width=way_bits))
        one_hot = m.const(1, width=n_sets).shl(amount=refill_set.wire)
        new_vld = valid_regs[w].wire | one_hot
        valid_regs[w] <<= wr_way.select(new_vld, valid_regs[w].wire)

    # Dirty bits: set on store hit
    for w in range(n_ways):
        st_way = s2_store_hit & (s2_hit_way_w == m.const(w, width=way_bits))
        one_hot_d = m.const(1, width=n_sets).shl(amount=s2_set_idx_w)
        new_drt = dirty_regs[w].wire | one_hot_d
        dirty_regs[w] <<= st_way.select(new_drt, dirty_regs[w].wire)

    # Store writeback buffer: capture on store hit, drain to SRAM next cycle
    swb_drain = swb_valid.wire & (~refill_valid.wire)
    new_swb_v = s2_store_hit.select(
        m.const(1, width=1),
        swb_drain.select(m.const(0, width=1), swb_valid.wire),
    )
    new_swb_v = flush.wire.select(m.const(0, width=1), new_swb_v)
    swb_valid <<= new_swb_v

    swb_set <<= s2_store_hit.select(s2_set_idx_w, swb_set.wire)
    swb_way <<= s2_store_hit.select(s2_hit_way_w, swb_way.wire)
    swb_data <<= s2_store_hit.select(s2_store_data_w, swb_data.wire)
    swb_mask <<= s2_store_hit.select(s2_store_mask_w, swb_mask.wire)

    # MSHR: allocate on miss, clear on refill, clear on flush
    mshr_clear = refill_valid.wire & mshr_valid.wire
    nv = mshr_clear.select(
        m.const(0, width=1),
        mshr_alloc.select(m.const(1, width=1), mshr_valid.wire),
    )
    nv = flush.wire.select(m.const(0, width=1), nv)
    mshr_valid <<= nv

    mshr_set <<= mshr_alloc.select(s2_set_idx_w, mshr_set.wire)
    mshr_tag <<= mshr_alloc.select(s2_ptag_w, mshr_tag.wire)

    # ================================================================
    # Output ports
    # ================================================================

    m.output(f"{prefix}_load_resp_valid", s3_load_resp_v_w)
    _out["load_resp_valid"] = cas(domain, s3_load_resp_v_w, cycle=domain.cycle_index)
    m.output(f"{prefix}_load_resp_data", s3_data_w)
    _out["load_resp_data"] = cas(domain, s3_data_w, cycle=domain.cycle_index)
    m.output(f"{prefix}_load_resp_hit", s3_load_hit_w)
    _out["load_resp_hit"] = cas(domain, s3_load_hit_w, cycle=domain.cycle_index)

    m.output(f"{prefix}_store_resp_valid", s3_store_resp_v_w)
    _out["store_resp_valid"] = cas(domain, s3_store_resp_v_w, cycle=domain.cycle_index)
    m.output(f"{prefix}_store_resp_hit", s3_store_hit_w)
    _out["store_resp_hit"] = cas(domain, s3_store_hit_w, cycle=domain.cycle_index)

    m.output(f"{prefix}_miss_valid", mshr_alloc)
    _out["miss_valid"] = cas(domain, mshr_alloc, cycle=domain.cycle_index)
    m.output(f"{prefix}_miss_addr", m.cat(
        s2_ptag_w,
        s2_set_idx_w,
        m.const(0, width=offset_bits),
    ))
    return _out


build_dcache.__pycircuit_name__ = "dcache"


if __name__ == "__main__":
    print(compile_cycle_aware(
        build_dcache, name="dcache", eager=True,
        n_sets=DCACHE_SETS, n_ways=DCACHE_WAYS,
        block_bytes=DCACHE_BLOCK_BYTES, paddr_width=36,
    ).emit_mlir())
