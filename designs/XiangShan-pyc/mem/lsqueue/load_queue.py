"""Load Queue — tracks in-flight loads for memory ordering in XiangShan-pyc.

Circular buffer that tracks all in-flight load instructions.
Used for memory ordering violation detection: when a store commits,
the load queue is searched to find younger loads to the same address
that may have executed too early.

Reference: XiangShan/src/main/scala/xiangshan/mem/lsqueue/LoadQueue.scala

Key features:
  M-LQ-001  Circular buffer with enqueue/dequeue pointers
  M-LQ-002  Enqueue on dispatch, dequeue on commit
  M-LQ-003  Address lookup for store-load ordering violation detection
  M-LQ-004  Redirect/flush: roll back enqueue pointer
  M-LQ-005  Per-entry valid/committed/address-valid tracking
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
    wire_of,
)
from top.parameters import *


def load_queue(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    prefix: str = "ldq",
    size: int = 72,
    addr_width: int = 36,
    data_width: int = XLEN,
    rob_idx_width: int = ROB_IDX_WIDTH,
    inputs: dict[str, CycleAwareSignal] | None = None,
) -> dict[str, CycleAwareSignal]:
    """Load Queue: circular buffer tracking in-flight loads."""
    _in = inputs or {}
    _out: dict[str, CycleAwareSignal] = {}


    idx_w = max(1, math.ceil(math.log2(size)))
    ptr_w = idx_w + 1

    # ── Cycle 0: Inputs ──────────────────────────────────────────────

    flush = (_in["flush"] if "flush" in _in else

        cas(domain, m.input(f"{prefix}_flush", width=1), cycle=0))

    enq_valid = (_in["enq_valid"] if "enq_valid" in _in else

        cas(domain, m.input(f"{prefix}_enq_valid", width=1), cycle=0))
    enq_rob_idx = (_in["enq_rob_idx"] if "enq_rob_idx" in _in else
        cas(domain, m.input(f"{prefix}_enq_rob_idx", width=rob_idx_width), cycle=0))

    addr_update_valid = (_in["addr_update_valid"] if "addr_update_valid" in _in else

        cas(domain, m.input(f"{prefix}_addr_update_valid", width=1), cycle=0))
    addr_update_idx = (_in["addr_update_idx"] if "addr_update_idx" in _in else
        cas(domain, m.input(f"{prefix}_addr_update_idx", width=idx_w), cycle=0))
    addr_update_addr = (_in["addr_update_addr"] if "addr_update_addr" in _in else
        cas(domain, m.input(f"{prefix}_addr_update_addr", width=addr_width), cycle=0))

    commit_valid = (_in["commit_valid"] if "commit_valid" in _in else

        cas(domain, m.input(f"{prefix}_commit_valid", width=1), cycle=0))

    lookup_valid = (_in["lookup_valid"] if "lookup_valid" in _in else

        cas(domain, m.input(f"{prefix}_lookup_valid", width=1), cycle=0))
    lookup_addr = (_in["lookup_addr"] if "lookup_addr" in _in else
        cas(domain, m.input(f"{prefix}_lookup_addr", width=addr_width), cycle=0))

    redirect_valid = (_in["redirect_valid"] if "redirect_valid" in _in else

        cas(domain, m.input(f"{prefix}_redirect_valid", width=1), cycle=0))
    redirect_rob_idx = (_in["redirect_rob_idx"] if "redirect_rob_idx" in _in else
        cas(domain, m.input(f"{prefix}_redirect_rob_idx", width=rob_idx_width), cycle=0))

    zero1 = cas(domain, m.const(0, width=1), cycle=0)
    one1 = cas(domain, m.const(1, width=1), cycle=0)

    # ── Entry storage ─────────────────────────────────────────────────

    e_valid = [domain.signal(width=1, reset_value=0, name=f"{prefix}_lq_v_{i}") for i in range(size)]
    e_addr_valid = [domain.signal(width=1, reset_value=0, name=f"{prefix}_lq_av_{i}") for i in range(size)]
    e_committed = [domain.signal(width=1, reset_value=0, name=f"{prefix}_lq_cm_{i}") for i in range(size)]
    e_addr = [domain.signal(width=addr_width, reset_value=0, name=f"{prefix}_lq_a_{i}") for i in range(size)]
    e_rob = [domain.signal(width=rob_idx_width, reset_value=0, name=f"{prefix}_lq_r_{i}") for i in range(size)]

    enq_ptr = domain.signal(width=ptr_w, reset_value=0, name=f"{prefix}_lq_enq")
    deq_ptr = domain.signal(width=ptr_w, reset_value=0, name=f"{prefix}_lq_deq")

    enq_idx = enq_ptr[0:idx_w]
    deq_idx = deq_ptr[0:idx_w]

    # Count for full/empty detection
    count = cas(domain, (wire_of(enq_ptr) - wire_of(deq_ptr))[0:ptr_w], cycle=0)
    full = count == cas(domain, m.const(size, width=ptr_w), cycle=0)
    empty = count == cas(domain, m.const(0, width=ptr_w), cycle=0)

    can_enq = enq_valid & (~full) & (~flush)

    # ── Lookup: detect ordering violation ────────────────────────────
    # Search for valid entries whose address matches the lookup address
    # (cache-line granularity comparison)

    line_bits = int(math.log2(CACHE_LINE_BYTES))
    lookup_tag = lookup_addr[line_bits:addr_width]
    tag_w = addr_width - line_bits

    violation_found = zero1
    for j in range(size):
        ev = e_valid[j]
        eav = e_addr_valid[j]
        ea = e_addr[j]
        entry_tag = ea[line_bits:addr_width]
        tag_match = entry_tag == lookup_tag
        entry_hit = lookup_valid & ev & eav & tag_match
        violation_found = mux(entry_hit, one1, violation_found)

    m.output(f"{prefix}_violation_found", wire_of(violation_found))
    _out["violation_found"] = violation_found
    m.output(f"{prefix}_can_enqueue", wire_of(can_enq))
    _out["can_enqueue"] = can_enq
    m.output(f"{prefix}_enq_idx", wire_of(enq_idx))
    _out["enq_idx"] = enq_idx
    m.output(f"{prefix}_count", wire_of(count))
    _out["count"] = count

    # ── domain.next() → Cycle 1: state updates ──────────────────────
    domain.next()

    # Enqueue
    for j in range(size):
        j_const = cas(domain, m.const(j, width=idx_w), cycle=0)
        is_enq_slot = enq_idx == j_const
        we_enq = can_enq & is_enq_slot
        e_valid[j].assign(mux(we_enq, one1, e_valid[j]), when=we_enq)
        e_addr_valid[j].assign(mux(we_enq, zero1, e_addr_valid[j]), when=we_enq)
        e_committed[j].assign(mux(we_enq, zero1, e_committed[j]), when=we_enq)
        e_rob[j].assign(mux(we_enq, enq_rob_idx, e_rob[j]), when=we_enq)

    # Address update (from load unit s1)
    for j in range(size):
        j_const = cas(domain, m.const(j, width=idx_w), cycle=0)
        is_upd = addr_update_valid & (addr_update_idx == j_const)
        e_addr[j].assign(mux(is_upd, addr_update_addr, e_addr[j]), when=is_upd)
        e_addr_valid[j].assign(mux(is_upd, one1, e_addr_valid[j]), when=is_upd)

    # Commit (dequeue head)
    for j in range(size):
        j_const = cas(domain, m.const(j, width=idx_w), cycle=0)
        is_deq = commit_valid & (~empty) & (deq_idx == j_const)
        e_committed[j].assign(mux(is_deq, one1, e_committed[j]), when=is_deq)
        e_valid[j].assign(mux(is_deq, zero1, e_valid[j]), when=is_deq)

    # Pointer updates
    next_enq = mux(can_enq,
                    cas(domain, (wire_of(enq_ptr) + u(ptr_w, 1))[0:ptr_w], cycle=0),
                    enq_ptr)
    next_enq = mux(redirect_valid, deq_ptr, next_enq)
    next_enq = mux(flush, deq_ptr, next_enq)
    enq_ptr <<= next_enq

    deq_commit = commit_valid & (~empty)
    next_deq = mux(deq_commit,
                    cas(domain, (wire_of(deq_ptr) + u(ptr_w, 1))[0:ptr_w], cycle=0),
                    deq_ptr)
    deq_ptr <<= next_deq

    # Flush: invalidate all entries
    for j in range(size):
        e_valid[j].assign(zero1, when=flush)
    return _out


load_queue.__pycircuit_name__ = "load_queue"


if __name__ == "__main__":
    print(compile_cycle_aware(
        load_queue, name="load_queue", eager=True,
        size=8, addr_width=36,
    ).emit_mlir())
