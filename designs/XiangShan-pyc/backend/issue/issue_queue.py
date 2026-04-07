"""IssueQueue — Age-matrix based Issue Queue for XiangShan-pyc backend.

Enqueues dispatched micro-ops, tracks source operand readiness via a
writeback wakeup bus, and selects the oldest ready entry for issue using
an age-matrix priority scheme.

Reference: XiangShan/src/main/scala/xiangshan/backend/issue/

Pipeline:
  Cycle 0 — Enqueue dispatched uops, wakeup (mark operands ready),
            age-matrix selection of oldest-ready entry
  Cycle 1 — State updates: write enqueued entries, update age matrix,
            dequeue issued entry, update readiness bits

Key features:
  B-IQ-001  Multi-entry storage with per-entry valid / ready bits
  B-IQ-002  Source operand tracking: src1_ready, src2_ready per entry
  B-IQ-003  Wakeup: snoop writeback bus, compare pdest to entry psrc tags
  B-IQ-004  Age matrix: triangular bit-matrix for oldest-first selection
  B-IQ-005  Selection: pick oldest entry where valid & src1_ready & src2_ready
  B-IQ-006  Multi-enqueue (enq_ports) and multi-issue (issue_ports)
  B-IQ-007  Flush: clear all entries on redirect
"""

from __future__ import annotations

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
    mux,
    wire_of,
)
from top.parameters import (
    ISSUE_QUEUE_SIZE,
    PTAG_WIDTH_INT,
    ROB_IDX_WIDTH,
)

FU_TYPE_WIDTH = 3


def issue_queue(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    prefix: str = "iq",
    entries: int = ISSUE_QUEUE_SIZE,
    enq_ports: int = 2,
    issue_ports: int = 2,
    wb_ports: int = 4,
    ptag_w: int = PTAG_WIDTH_INT,
    rob_idx_w: int = ROB_IDX_WIDTH,
    fu_type_width: int = FU_TYPE_WIDTH,
    inputs: dict[str, CycleAwareSignal] | None = None,
) -> dict[str, CycleAwareSignal]:
    """IssueQueue: age-matrix based issue queue with wakeup and selection."""
    _in = inputs or {}
    _out: dict[str, CycleAwareSignal] = {}

    idx_w = max(1, (entries - 1).bit_length())
    cnt_w = max(1, entries.bit_length())

    # ================================================================
    # Cycle 0 — Inputs
    # ================================================================

    flush = (
        _in["flush"]
        if "flush" in _in
        else cas(domain, m.input(f"{prefix}_flush", width=1), cycle=0)
    )

    # Enqueue interface (from dispatch)
    enq_valid = [
        cas(domain, m.input(f"{prefix}_enq_valid_{i}", width=1), cycle=0)
        for i in range(enq_ports)
    ]
    enq_pdest = [
        cas(domain, m.input(f"{prefix}_enq_pdest_{i}", width=ptag_w), cycle=0)
        for i in range(enq_ports)
    ]
    enq_psrc1 = [
        cas(domain, m.input(f"{prefix}_enq_psrc1_{i}", width=ptag_w), cycle=0)
        for i in range(enq_ports)
    ]
    enq_psrc2 = [
        cas(domain, m.input(f"{prefix}_enq_psrc2_{i}", width=ptag_w), cycle=0)
        for i in range(enq_ports)
    ]
    enq_src1_ready = [
        cas(domain, m.input(f"{prefix}_enq_src1_ready_{i}", width=1), cycle=0)
        for i in range(enq_ports)
    ]
    enq_src2_ready = [
        cas(domain, m.input(f"{prefix}_enq_src2_ready_{i}", width=1), cycle=0)
        for i in range(enq_ports)
    ]
    enq_rob_idx = [
        cas(domain, m.input(f"{prefix}_enq_rob_idx_{i}", width=rob_idx_w), cycle=0)
        for i in range(enq_ports)
    ]
    enq_fu_type = [
        cas(domain, m.input(f"{prefix}_enq_fu_type_{i}", width=fu_type_width), cycle=0)
        for i in range(enq_ports)
    ]

    # Writeback / wakeup bus (from execution units)
    wb_valid = [
        cas(domain, m.input(f"{prefix}_wb_valid_{i}", width=1), cycle=0)
        for i in range(wb_ports)
    ]
    wb_pdest = [
        cas(domain, m.input(f"{prefix}_wb_pdest_{i}", width=ptag_w), cycle=0)
        for i in range(wb_ports)
    ]

    # ── Constants ────────────────────────────────────────────────
    ZERO_1 = cas(domain, m.const(0, width=1), cycle=0)
    ONE_1 = cas(domain, m.const(1, width=1), cycle=0)
    ZERO_IDX = cas(domain, m.const(0, width=idx_w), cycle=0)
    ZERO_PTAG = cas(domain, m.const(0, width=ptag_w), cycle=0)

    # ── Entry storage (state registers) ──────────────────────────
    ent_valid = [
        domain.signal(width=1, reset_value=0, name=f"{prefix}_ev_{i}")
        for i in range(entries)
    ]
    ent_pdest = [
        domain.signal(width=ptag_w, reset_value=0, name=f"{prefix}_epd_{i}")
        for i in range(entries)
    ]
    ent_psrc1 = [
        domain.signal(width=ptag_w, reset_value=0, name=f"{prefix}_eps1_{i}")
        for i in range(entries)
    ]
    ent_psrc2 = [
        domain.signal(width=ptag_w, reset_value=0, name=f"{prefix}_eps2_{i}")
        for i in range(entries)
    ]
    ent_s1rdy = [
        domain.signal(width=1, reset_value=0, name=f"{prefix}_er1_{i}")
        for i in range(entries)
    ]
    ent_s2rdy = [
        domain.signal(width=1, reset_value=0, name=f"{prefix}_er2_{i}")
        for i in range(entries)
    ]
    ent_rob_idx = [
        domain.signal(width=rob_idx_w, reset_value=0, name=f"{prefix}_erob_{i}")
        for i in range(entries)
    ]
    ent_fu_type = [
        domain.signal(width=fu_type_width, reset_value=0, name=f"{prefix}_efu_{i}")
        for i in range(entries)
    ]

    # Age matrix: age[i][j] = 1 means entry i is older than entry j
    # Triangular — only store i < j pairs; age[i][j] for i >= j is implicit.
    age_matrix = [
        [
            domain.signal(width=1, reset_value=0, name=f"{prefix}_age_{i}_{j}")
            for j in range(entries)
        ]
        for i in range(entries)
    ]

    # ── Wakeup: check writeback bus against stored psrc tags ─────
    wk_s1 = [ZERO_1] * entries
    wk_s2 = [ZERO_1] * entries
    for i in range(entries):
        for w in range(wb_ports):
            s1_match = wb_valid[w] & (wb_pdest[w] == ent_psrc1[i])
            s2_match = wb_valid[w] & (wb_pdest[w] == ent_psrc2[i])
            wk_s1[i] = wk_s1[i] | s1_match
            wk_s2[i] = wk_s2[i] | s2_match

    # Effective readiness (already ready OR just woken up)
    eff_s1 = [ent_s1rdy[i] | wk_s1[i] for i in range(entries)]
    eff_s2 = [ent_s2rdy[i] | wk_s2[i] for i in range(entries)]

    # ── Entry "can issue" = valid & both sources ready ───────────
    can_issue = [ent_valid[i] & eff_s1[i] & eff_s2[i] for i in range(entries)]

    # ── Age-matrix based selection (oldest-ready-first) ──────────
    # Entry i is "oldest ready" if it can_issue AND for every other entry j
    # that can_issue, age[i][j] == 1 (i is older than j).
    # Simplified: entry i is selected if can_issue[i] and no older can_issue
    # entry exists.  "Older" = age[j][i] == 1 for some j != i that can issue.

    oldest_ready = []
    for i in range(entries):
        is_oldest = can_issue[i]
        for j in range(entries):
            if j == i:
                continue
            j_older = can_issue[j] & age_matrix[j][i]
            is_oldest = is_oldest & (~j_older)
        oldest_ready.append(is_oldest)

    # ── Issue port allocation (up to issue_ports per cycle) ──────
    # Greedy: port 0 gets the overall oldest-ready, port 1 gets next, etc.
    # For simplicity, port 0 picks from oldest_ready; port 1 picks from
    # remaining candidates after masking out port 0's selection.

    issued = [ZERO_1] * entries
    for p in range(issue_ports):
        # Candidate = oldest_ready AND not already issued by earlier port
        cand = [
            oldest_ready[i] & (~issued[i]) if p == 0 else can_issue[i] & (~issued[i])
            for i in range(entries)
        ]

        if p > 0:
            # Recompute "oldest among remaining"
            new_oldest = []
            for i in range(entries):
                is_old = cand[i]
                for j in range(entries):
                    if j == i:
                        continue
                    j_older = cand[j] & age_matrix[j][i]
                    is_old = is_old & (~j_older)
                new_oldest.append(is_old)
            cand = new_oldest

        # Priority encode to pick one (LSB priority as tie-breaker)
        sel_valid = ZERO_1
        sel_idx = ZERO_IDX
        sel_pdest = ZERO_PTAG
        sel_rob = cas(domain, m.const(0, width=rob_idx_w), cycle=0)
        sel_fu = cas(domain, m.const(0, width=fu_type_width), cycle=0)

        for i in reversed(range(entries)):
            sel_valid = mux(cand[i], ONE_1, sel_valid)
            sel_idx = mux(
                cand[i], cas(domain, m.const(i, width=idx_w), cycle=0), sel_idx
            )
            sel_pdest = mux(cand[i], ent_pdest[i], sel_pdest)
            sel_rob = mux(cand[i], ent_rob_idx[i], sel_rob)
            sel_fu = mux(cand[i], ent_fu_type[i], sel_fu)

        issue_valid = sel_valid & (~flush)
        m.output(f"{prefix}_issue_valid_{p}", wire_of(issue_valid))
        _out[f"issue_valid_{p}"] = issue_valid
        m.output(f"{prefix}_issue_pdest_{p}", wire_of(sel_pdest))
        _out[f"issue_pdest_{p}"] = sel_pdest
        m.output(f"{prefix}_issue_rob_idx_{p}", wire_of(sel_rob))
        _out[f"issue_rob_idx_{p}"] = sel_rob
        m.output(f"{prefix}_issue_fu_type_{p}", wire_of(sel_fu))
        _out[f"issue_fu_type_{p}"] = sel_fu

        # Mark as issued for next port's masking
        for i in range(entries):
            issued[i] = issued[i] | mux(cand[i], ONE_1, ZERO_1)

    # ── Enqueue: find free slots ─────────────────────────────────
    # Scan for first N free entries (enq_ports)
    allocated = [ZERO_1] * entries
    enq_slot_idx = []
    enq_slot_found = []

    for p in range(enq_ports):
        found = ZERO_1
        slot = ZERO_IDX
        for i in reversed(range(entries)):
            free = (~ent_valid[i]) & (~allocated[i])
            found = mux(free, ONE_1, found)
            slot = mux(free, cas(domain, m.const(i, width=idx_w), cycle=0), slot)
        enq_slot_idx.append(slot)
        enq_slot_found.append(found)
        # Mark this slot as allocated for subsequent ports
        for i in range(entries):
            hit = slot == cas(domain, m.const(i, width=idx_w), cycle=0)
            allocated[i] = allocated[i] | (found & hit)

    # ── Backpressure output ──────────────────────────────────────
    # Count free entries
    free_cnt = cas(domain, m.const(0, width=cnt_w), cycle=0)
    ONE_CNT = cas(domain, m.const(1, width=cnt_w), cycle=0)
    for i in range(entries):
        free_cnt = mux(
            ~ent_valid[i],
            cas(domain, (wire_of(free_cnt) + wire_of(ONE_CNT))[0:cnt_w], cycle=0),
            free_cnt,
        )

    enq_cnt_const = cas(domain, m.const(enq_ports, width=cnt_w), cycle=0)
    has_room = ~(free_cnt < enq_cnt_const)
    ready = has_room & (~flush)
    m.output(f"{prefix}_ready", wire_of(ready))
    _out["ready"] = ready
    m.output(f"{prefix}_free_count", wire_of(free_cnt))
    _out["free_count"] = free_cnt

    # ── domain.next() → Cycle 1: state updates ──────────────────
    domain.next()

    # ── Enqueue: write new entries ───────────────────────────────
    for p in range(enq_ports):
        do_enq = enq_valid[p] & enq_slot_found[p] & has_room & (~flush)
        for i in range(entries):
            hit = enq_slot_idx[p] == cas(domain, m.const(i, width=idx_w), cycle=0)
            we = do_enq & hit
            ent_valid[i].assign(ONE_1, when=we)
            ent_pdest[i].assign(enq_pdest[p], when=we)
            ent_psrc1[i].assign(enq_psrc1[p], when=we)
            ent_psrc2[i].assign(enq_psrc2[p], when=we)
            ent_s1rdy[i].assign(enq_src1_ready[p], when=we)
            ent_s2rdy[i].assign(enq_src2_ready[p], when=we)
            ent_rob_idx[i].assign(enq_rob_idx[p], when=we)
            ent_fu_type[i].assign(enq_fu_type[p], when=we)

            # Age matrix: new entry is younger than all existing valid entries
            for j in range(entries):
                if j == i:
                    continue
                age_matrix[j][i].assign(
                    mux(we & ent_valid[j], ONE_1, age_matrix[j][i]), when=we
                )
                age_matrix[i][j].assign(mux(we, ZERO_1, age_matrix[i][j]), when=we)

    # ── Wakeup: update readiness bits ────────────────────────────
    for i in range(entries):
        ent_s1rdy[i].assign(
            mux(wk_s1[i] & ent_valid[i], ONE_1, ent_s1rdy[i]), when=wk_s1[i]
        )
        ent_s2rdy[i].assign(
            mux(wk_s2[i] & ent_valid[i], ONE_1, ent_s2rdy[i]), when=wk_s2[i]
        )

    # ── Dequeue: invalidate issued entries ───────────────────────
    for i in range(entries):
        deq = issued[i] & (~flush)
        ent_valid[i].assign(ZERO_1, when=deq)
        # Clear age bits for dequeued entry
        for j in range(entries):
            if j == i:
                continue
            age_matrix[i][j].assign(ZERO_1, when=deq)
            age_matrix[j][i].assign(ZERO_1, when=deq)

    # ── Flush: clear all entries ─────────────────────────────────
    for i in range(entries):
        ent_valid[i].assign(ZERO_1, when=flush)
    return _out


issue_queue.__pycircuit_name__ = "issue_queue"


if __name__ == "__main__":
    pass
