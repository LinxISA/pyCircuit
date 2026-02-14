"""Cube v2 Issue Queue Implementation.

The Issue Queue holds 16 uops and supports out-of-order execution.
A uop can be issued when its L0A and L0B data are ready and the ACC entry is available.
"""
from __future__ import annotations
from pycircuit import Circuit, Wire, jit_inline
from janus.cube.cube_v2_consts import ACC_ENTRIES, ACC_IDX_WIDTH, ISSUE_QUEUE_SIZE, L0A_ENTRIES, L0B_ENTRIES, L0_IDX_WIDTH, QUEUE_IDX_WIDTH
from janus.cube.cube_v2_types import IssueQueueEntry, IssueResult, Uop, UopRegs
from janus.cube.util import Consts

def _make_issue_queue_entry(m: Circuit, clk: Wire, rst: Wire, consts: Consts, idx: int) -> IssueQueueEntry:
    """Create registers for a single issue queue entry."""
    with m.scope(f'iq_entry_{idx}'):
        uop = UopRegs(l0a_idx=m.out('l0a_idx', clk=clk, rst=rst, width=L0_IDX_WIDTH, init=0, en=consts.one1), l0b_idx=m.out('l0b_idx', clk=clk, rst=rst, width=L0_IDX_WIDTH, init=0, en=consts.one1), acc_idx=m.out('acc_idx', clk=clk, rst=rst, width=ACC_IDX_WIDTH, init=0, en=consts.one1), is_first=m.out('is_first', clk=clk, rst=rst, width=1, init=0, en=consts.one1), is_last=m.out('is_last', clk=clk, rst=rst, width=1, init=0, en=consts.one1))
        return IssueQueueEntry(valid=m.out('valid', clk=clk, rst=rst, width=1, init=0, en=consts.one1), uop=uop, l0a_ready=m.out('l0a_ready', clk=clk, rst=rst, width=1, init=0, en=consts.one1), l0b_ready=m.out('l0b_ready', clk=clk, rst=rst, width=1, init=0, en=consts.one1), acc_ready=m.out('acc_ready', clk=clk, rst=rst, width=1, init=0, en=consts.one1), issued=m.out('issued', clk=clk, rst=rst, width=1, init=0, en=consts.one1))

@jit_inline
def build_issue_queue(m: Circuit, *, clk: Wire, rst: Wire, consts: Consts, enqueue_valid: Wire, enqueue_l0a_idx: Wire, enqueue_l0b_idx: Wire, enqueue_acc_idx: Wire, enqueue_is_first: Wire, enqueue_is_last: Wire, l0a_valid_bitmap: Wire, l0b_valid_bitmap: Wire, acc_available_bitmap: Wire, issue_ack: Wire, flush: Wire) -> tuple[list[IssueQueueEntry], IssueResult, Wire, Wire, Wire]:
    """Build the issue queue.

    Returns:
        (entries, issue_result, queue_full, queue_empty, entries_used)
    """
    c = m.const
    with m.scope('ISSUE_QUEUE'):
        entries = []
        for i in range(ISSUE_QUEUE_SIZE):
            entry = _make_issue_queue_entry(m, clk, rst, consts, i)
            entries.append(entry)
        with m.scope('PTRS'):
            head = m.out('head', clk=clk, rst=rst, width=QUEUE_IDX_WIDTH, init=0, en=consts.one1)
            tail = m.out('tail', clk=clk, rst=rst, width=QUEUE_IDX_WIDTH, init=0, en=consts.one1)
            count = m.out('count', clk=clk, rst=rst, width=QUEUE_IDX_WIDTH + 1, init=0, en=consts.one1)
        queue_full = count.out() == c(ISSUE_QUEUE_SIZE, width=QUEUE_IDX_WIDTH + 1)
        queue_empty = count.out() == c(0, width=QUEUE_IDX_WIDTH + 1)
        with m.scope('ENQUEUE'):
            can_enqueue = enqueue_valid & ~queue_full & ~flush
            for i in range(ISSUE_QUEUE_SIZE):
                tail_match = tail.out() == c(i, width=QUEUE_IDX_WIDTH)
                enqueue_this = can_enqueue & tail_match
                entries[i].uop.l0a_idx.set(enqueue_l0a_idx, when=enqueue_this)
                entries[i].uop.l0b_idx.set(enqueue_l0b_idx, when=enqueue_this)
                entries[i].uop.acc_idx.set(enqueue_acc_idx, when=enqueue_this)
                entries[i].uop.is_first.set(enqueue_is_first, when=enqueue_this)
                entries[i].uop.is_last.set(enqueue_is_last, when=enqueue_this)
                entries[i].valid.set(consts.one1, when=enqueue_this)
                entries[i].issued.set(consts.zero1, when=enqueue_this)
            next_tail = tail.out() + consts.one8[0:QUEUE_IDX_WIDTH] & c(ISSUE_QUEUE_SIZE - 1, width=QUEUE_IDX_WIDTH)
            tail.set(next_tail, when=can_enqueue)
        with m.scope('READY_UPDATE'):
            for i in range(ISSUE_QUEUE_SIZE):
                entry_valid = entries[i].valid.out()
                l0a_idx = entries[i].uop.l0a_idx.out()
                l0a_ready = consts.zero1
                for j in range(L0A_ENTRIES):
                    idx_match = l0a_idx == c(j, width=L0_IDX_WIDTH)
                    bit_val = l0a_valid_bitmap[j]
                    l0a_ready = bit_val if idx_match else l0a_ready
                entries[i].l0a_ready.set(l0a_ready, when=entry_valid)
                l0b_idx = entries[i].uop.l0b_idx.out()
                l0b_ready = consts.zero1
                for j in range(L0B_ENTRIES):
                    idx_match = l0b_idx == c(j, width=L0_IDX_WIDTH)
                    bit_val = l0b_valid_bitmap[j]
                    l0b_ready = bit_val if idx_match else l0b_ready
                entries[i].l0b_ready.set(l0b_ready, when=entry_valid)
                acc_idx = entries[i].uop.acc_idx.out()
                acc_ready = consts.zero1
                for j in range(ACC_ENTRIES):
                    idx_match = acc_idx == c(j, width=ACC_IDX_WIDTH)
                    bit_val = acc_available_bitmap[j]
                    acc_ready = bit_val if idx_match else acc_ready
                entries[i].acc_ready.set(acc_ready, when=entry_valid)
        with m.scope('ISSUE'):
            issue_valid = consts.zero1
            issue_idx = c(0, width=QUEUE_IDX_WIDTH)
            issue_l0a_idx = c(0, width=L0_IDX_WIDTH)
            issue_l0b_idx = c(0, width=L0_IDX_WIDTH)
            issue_acc_idx = c(0, width=ACC_IDX_WIDTH)
            issue_is_first = consts.zero1
            issue_is_last = consts.zero1
            found = consts.zero1
            for i in range(ISSUE_QUEUE_SIZE):
                entry = entries[i]
                is_ready = entry.valid.out() & ~entry.issued.out() & entry.l0a_ready.out() & entry.l0b_ready.out() & entry.acc_ready.out()
                select_this = is_ready & ~found
                issue_valid = consts.one1 if select_this else issue_valid
                issue_idx = c(i, width=QUEUE_IDX_WIDTH) if select_this else issue_idx
                issue_l0a_idx = entry.uop.l0a_idx.out() if select_this else issue_l0a_idx
                issue_l0b_idx = entry.uop.l0b_idx.out() if select_this else issue_l0b_idx
                issue_acc_idx = entry.uop.acc_idx.out() if select_this else issue_acc_idx
                issue_is_first = entry.uop.is_first.out() if select_this else issue_is_first
                issue_is_last = entry.uop.is_last.out() if select_this else issue_is_last
                found = found | is_ready
            issue_and_ack = issue_valid & issue_ack
            for i in range(ISSUE_QUEUE_SIZE):
                idx_match = issue_idx == c(i, width=QUEUE_IDX_WIDTH)
                mark_issued = issue_and_ack & idx_match
                entries[i].issued.set(consts.one1, when=mark_issued)
            issued_uop = Uop(l0a_idx=issue_l0a_idx, l0b_idx=issue_l0b_idx, acc_idx=issue_acc_idx, is_first=issue_is_first, is_last=issue_is_last)
            issue_result = IssueResult(issue_valid=issue_valid, uop=issued_uop)
        with m.scope('RETIRE'):
            for i in range(ISSUE_QUEUE_SIZE):
                head_match = head.out() == c(i, width=QUEUE_IDX_WIDTH)
                can_retire = head_match & entries[i].valid.out() & entries[i].issued.out()
                entries[i].valid.set(consts.zero1, when=can_retire)
            head_entry_issued = consts.zero1
            for i in range(ISSUE_QUEUE_SIZE):
                head_match = head.out() == c(i, width=QUEUE_IDX_WIDTH)
                head_entry_issued = entries[i].valid.out() & entries[i].issued.out() if head_match else head_entry_issued
            next_head = head.out() + consts.one8[0:QUEUE_IDX_WIDTH] & c(ISSUE_QUEUE_SIZE - 1, width=QUEUE_IDX_WIDTH)
            head.set(next_head, when=head_entry_issued)
        with m.scope('COUNT'):
            enqueued = can_enqueue
            retired = head_entry_issued
            next_count = count.out()
            next_count = next_count + c(1, width=QUEUE_IDX_WIDTH + 1) if enqueued else next_count
            next_count = next_count - c(1, width=QUEUE_IDX_WIDTH + 1) if retired else next_count
            count.set(next_count, when=enqueued | retired)
        with m.scope('FLUSH'):
            for i in range(ISSUE_QUEUE_SIZE):
                entries[i].valid.set(consts.zero1, when=flush)
            head.set(c(0, width=QUEUE_IDX_WIDTH), when=flush)
            tail.set(c(0, width=QUEUE_IDX_WIDTH), when=flush)
            count.set(c(0, width=QUEUE_IDX_WIDTH + 1), when=flush)
        entries_used = count.out()
        return (entries, issue_result, queue_full, queue_empty, entries_used)
