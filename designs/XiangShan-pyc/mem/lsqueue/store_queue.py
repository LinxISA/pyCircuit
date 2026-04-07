"""Store Queue — circular buffer for in-flight stores in XiangShan-pyc.

Holds all in-flight store instructions from dispatch to commit.
Provides store-to-load forwarding: when a load arrives, the store queue
is searched for an older store to the same address; matching data is
forwarded.  On commit, entries are marked committed and eventually
drained to the Store Buffer (SBuffer).

Reference: XiangShan/src/main/scala/xiangshan/mem/lsqueue/StoreQueue.scala

Key features:
  M-SQ-001  Circular buffer with enqueue/dequeue/commit pointers
  M-SQ-002  Store-to-load forwarding via address comparison
  M-SQ-003  Enqueue on dispatch, data fill from store unit
  M-SQ-004  Commit marks entry ready for SBuffer drain
  M-SQ-005  Redirect/flush: roll back enqueue pointer
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


def store_queue(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    prefix: str = "stq",
    size: int = 56,
    addr_width: int = 36,
    data_width: int = XLEN,
    rob_idx_width: int = ROB_IDX_WIDTH,
    inputs: dict[str, CycleAwareSignal] | None = None,
) -> dict[str, CycleAwareSignal]:
    """Store Queue: circular buffer for in-flight stores with forwarding."""
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

    write_valid = (_in["write_valid"] if "write_valid" in _in else

        cas(domain, m.input(f"{prefix}_write_valid", width=1), cycle=0))
    write_idx = (_in["write_idx"] if "write_idx" in _in else
        cas(domain, m.input(f"{prefix}_write_idx", width=idx_w), cycle=0))
    write_addr = (_in["write_addr"] if "write_addr" in _in else
        cas(domain, m.input(f"{prefix}_write_addr", width=addr_width), cycle=0))
    write_data = (_in["write_data"] if "write_data" in _in else
        cas(domain, m.input(f"{prefix}_write_data", width=data_width), cycle=0))

    commit_valid = (_in["commit_valid"] if "commit_valid" in _in else

        cas(domain, m.input(f"{prefix}_commit_valid", width=1), cycle=0))

    # Forwarding lookup from load pipeline
    fwd_valid = (_in["fwd_valid"] if "fwd_valid" in _in else
        cas(domain, m.input(f"{prefix}_fwd_valid", width=1), cycle=0))
    fwd_addr = (_in["fwd_addr"] if "fwd_addr" in _in else
        cas(domain, m.input(f"{prefix}_fwd_addr", width=addr_width), cycle=0))

    # SBuffer drain handshake
    sbuf_ready = (_in["sbuf_ready"] if "sbuf_ready" in _in else
        cas(domain, m.input(f"{prefix}_sbuf_ready", width=1), cycle=0))

    redirect_valid = (_in["redirect_valid"] if "redirect_valid" in _in else

        cas(domain, m.input(f"{prefix}_redirect_valid", width=1), cycle=0))

    zero1 = cas(domain, m.const(0, width=1), cycle=0)
    one1 = cas(domain, m.const(1, width=1), cycle=0)
    zero_data = cas(domain, m.const(0, width=data_width), cycle=0)

    # ── Entry storage ─────────────────────────────────────────────────

    e_valid = [domain.signal(width=1, reset_value=0, name=f"{prefix}_sq_v_{i}") for i in range(size)]
    e_addr_valid = [domain.signal(width=1, reset_value=0, name=f"{prefix}_sq_av_{i}") for i in range(size)]
    e_committed = [domain.signal(width=1, reset_value=0, name=f"{prefix}_sq_cm_{i}") for i in range(size)]
    e_addr = [domain.signal(width=addr_width, reset_value=0, name=f"{prefix}_sq_a_{i}") for i in range(size)]
    e_data = [domain.signal(width=data_width, reset_value=0, name=f"{prefix}_sq_d_{i}") for i in range(size)]
    e_rob = [domain.signal(width=rob_idx_width, reset_value=0, name=f"{prefix}_sq_r_{i}") for i in range(size)]

    enq_ptr = domain.signal(width=ptr_w, reset_value=0, name=f"{prefix}_sq_enq")
    deq_ptr = domain.signal(width=ptr_w, reset_value=0, name=f"{prefix}_sq_deq")
    commit_ptr = domain.signal(width=ptr_w, reset_value=0, name=f"{prefix}_sq_cmt")

    enq_idx = enq_ptr[0:idx_w]
    deq_idx = deq_ptr[0:idx_w]
    commit_idx = commit_ptr[0:idx_w]

    count = cas(domain, (wire_of(enq_ptr) - wire_of(deq_ptr))[0:ptr_w], cycle=0)
    full = count == cas(domain, m.const(size, width=ptr_w), cycle=0)
    empty = count == cas(domain, m.const(0, width=ptr_w), cycle=0)

    can_enq = enq_valid & (~full) & (~flush)

    # ── Store-to-load forwarding ─────────────────────────────────────
    # Search committed entries for matching address; forward data

    line_bits = int(math.log2(CACHE_LINE_BYTES))
    fwd_tag = fwd_addr[line_bits:addr_width]
    tag_w = addr_width - line_bits

    fwd_hit = zero1
    fwd_data_out = zero_data
    for j in range(size):
        ev = e_valid[j]
        eav = e_addr_valid[j]
        ea = e_addr[j]
        ed = e_data[j]
        entry_tag = ea[line_bits:addr_width]
        tag_match = entry_tag == fwd_tag
        entry_hit = fwd_valid & ev & eav & tag_match
        fwd_hit = mux(entry_hit, one1, fwd_hit)
        fwd_data_out = mux(entry_hit, ed, fwd_data_out)

    # Drain: head committed entry → SBuffer
    head_valid = e_valid[0]
    head_committed = e_committed[0]

    drain_head_valid = zero1
    drain_head_addr = cas(domain, m.const(0, width=addr_width), cycle=0)
    drain_head_data = zero_data
    for j in range(size):
        j_const = cas(domain, m.const(j, width=idx_w), cycle=0)
        is_head = deq_idx == j_const
        ev = e_valid[j]
        ecm = e_committed[j]
        eav = e_addr_valid[j]
        can_drain = is_head & ev & ecm & eav
        drain_head_valid = mux(can_drain, one1, drain_head_valid)
        drain_head_addr = mux(can_drain, e_addr[j], drain_head_addr)
        drain_head_data = mux(can_drain, e_data[j], drain_head_data)

    drain_fire = drain_head_valid & sbuf_ready

    m.output(f"{prefix}_fwd_hit", wire_of(fwd_hit))
    _out["fwd_hit"] = fwd_hit
    m.output(f"{prefix}_fwd_data", wire_of(fwd_data_out))
    _out["fwd_data"] = fwd_data_out
    m.output(f"{prefix}_can_enqueue", wire_of(can_enq))
    _out["can_enqueue"] = can_enq
    m.output(f"{prefix}_enq_idx", wire_of(enq_idx))
    _out["enq_idx"] = enq_idx
    m.output(f"{prefix}_count", wire_of(count))
    _out["count"] = count
    m.output(f"{prefix}_sbuf_valid", wire_of(drain_head_valid))
    _out["sbuf_valid"] = drain_head_valid
    m.output(f"{prefix}_sbuf_addr", wire_of(drain_head_addr))
    _out["sbuf_addr"] = drain_head_addr
    m.output(f"{prefix}_sbuf_data", wire_of(drain_head_data))
    _out["sbuf_data"] = drain_head_data

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

    # Data/address fill from store unit
    for j in range(size):
        j_const = cas(domain, m.const(j, width=idx_w), cycle=0)
        is_write = write_valid & (write_idx == j_const)
        e_addr[j].assign(mux(is_write, write_addr, e_addr[j]), when=is_write)
        e_data[j].assign(mux(is_write, write_data, e_data[j]), when=is_write)
        e_addr_valid[j].assign(mux(is_write, one1, e_addr_valid[j]), when=is_write)

    # Commit
    for j in range(size):
        j_const = cas(domain, m.const(j, width=idx_w), cycle=0)
        is_cmt = commit_valid & (commit_idx == j_const)
        e_committed[j].assign(mux(is_cmt, one1, e_committed[j]), when=is_cmt)

    # Drain (dequeue committed head to SBuffer)
    for j in range(size):
        j_const = cas(domain, m.const(j, width=idx_w), cycle=0)
        is_deq = drain_fire & (deq_idx == j_const)
        e_valid[j].assign(mux(is_deq, zero1, e_valid[j]), when=is_deq)

    # Pointer updates
    next_enq = mux(can_enq,
                    cas(domain, (wire_of(enq_ptr) + u(ptr_w, 1))[0:ptr_w], cycle=0),
                    enq_ptr)
    next_enq = mux(redirect_valid | flush, commit_ptr, next_enq)
    enq_ptr <<= next_enq

    next_deq = mux(drain_fire,
                    cas(domain, (wire_of(deq_ptr) + u(ptr_w, 1))[0:ptr_w], cycle=0),
                    deq_ptr)
    deq_ptr <<= next_deq

    next_cmt = mux(commit_valid,
                    cas(domain, (wire_of(commit_ptr) + u(ptr_w, 1))[0:ptr_w], cycle=0),
                    commit_ptr)
    commit_ptr <<= next_cmt

    # Flush
    for j in range(size):
        e_valid[j].assign(zero1, when=flush)
    return _out


store_queue.__pycircuit_name__ = "store_queue"


if __name__ == "__main__":
    print(compile_cycle_aware(
        store_queue, name="store_queue", eager=True,
        size=8, addr_width=36,
    ).emit_mlir())
