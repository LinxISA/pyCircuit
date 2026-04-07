"""Memory Tile Engine (MTE) — tile LD/ST, GET/PUT, COPY, TRANSPOSE, ZERO.

Bridges memory ↔ TRegFile-4K (bulk) and scalar GPR ↔ TRegFile-4K (element).
Models the state machine for tracking outstanding tile operations.

Port usage:
  - TILE.LD: 1 W port × 1 write epoch (8 cy from TRegFile side)
  - TILE.ST: 1 R port × 1 read epoch
  - TILE.COPY: 1 R + 1 W port × 2 epochs
  - TILE.ZERO: 1 W port × 1 write epoch
  - TILE.GET: 1 R port × 1 read epoch + 1 cy extract → CDB
  - TILE.PUT: 1 R + 1 W port × 2 epochs (RMW)
  - TILE.TRANSPOSE: 1 R + 1 W port × 2 epochs via transpose buffer
  - TILE.MOVE: handled at rename, never reaches MTE
"""

from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    mux,
    wire_of,
)

from ...common.parameters import (
    MTE_TILECOPY,
    MTE_TILEGET,
    MTE_TILELOAD_L2,
    MTE_TILEPUT,
    MTE_TILEZERO,
    PHYS_GREG_W,
    PHYS_TREG_W,
    SCALAR_DATA_W,
    TREGFILE_EPOCH_CY,
)

MTE_LD = 0
MTE_ST = 1
MTE_COPY = 2
MTE_ZERO = 3
MTE_GET = 4
MTE_PUT = 5
MTE_TRANSPOSE = 6
MTE_GATHER = 7
MTE_OP_W = 3

LATENCY_W = 8  # up to 255 cycles


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def mte_unit(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    ttag_w: int = PHYS_TREG_W,
    stag_w: int = PHYS_GREG_W,
    data_w: int = SCALAR_DATA_W,
    op_w: int = MTE_OP_W,
    epoch_cy: int = TREGFILE_EPOCH_CY,
    prefix: str = "mte",
    inputs: dict | None = None,
) -> dict:
    # ── Cycle 0: Issue interface ─────────────────────────────────────
    issue_valid = _in(inputs, "issue_valid", m, domain, prefix, 1)
    issue_op = _in(inputs, "issue_op", m, domain, prefix, op_w)
    issue_ptsrc = _in(inputs, "ptsrc", m, domain, prefix, ttag_w)
    issue_ptdst = _in(inputs, "ptdst", m, domain, prefix, ttag_w)
    issue_pdst = _in(inputs, "pdst", m, domain, prefix, stag_w)  # scalar dest for GET

    # ── State: simple FSM per outstanding slot ───────────────────────
    busy = domain.signal(width=1, reset_value=0, name=f"{prefix}_busy")
    cur_op = domain.signal(width=op_w, reset_value=0, name=f"{prefix}_cop")
    cur_ptsrc = domain.signal(width=ttag_w, reset_value=0, name=f"{prefix}_cptsrc")
    cur_ptdst = domain.signal(width=ttag_w, reset_value=0, name=f"{prefix}_cptdst")
    cur_pdst = domain.signal(width=stag_w, reset_value=0, name=f"{prefix}_cpdst")
    counter = domain.signal(width=LATENCY_W, reset_value=0, name=f"{prefix}_ctr")

    # Determine target latency based on operation
    is_ld = cur_op == cas(domain, m.const(MTE_LD, width=op_w), cycle=0)
    is_st = cur_op == cas(domain, m.const(MTE_ST, width=op_w), cycle=0)
    is_copy = cur_op == cas(domain, m.const(MTE_COPY, width=op_w), cycle=0)
    is_zero = cur_op == cas(domain, m.const(MTE_ZERO, width=op_w), cycle=0)
    is_get = cur_op == cas(domain, m.const(MTE_GET, width=op_w), cycle=0)
    is_put = cur_op == cas(domain, m.const(MTE_PUT, width=op_w), cycle=0)
    is_trans = cur_op == cas(domain, m.const(MTE_TRANSPOSE, width=op_w), cycle=0)

    target_lat = cas(domain, m.const(MTE_TILECOPY, width=LATENCY_W), cycle=0)  # default
    target_lat = mux(
        is_ld,
        cas(domain, m.const(MTE_TILELOAD_L2, width=LATENCY_W), cycle=0),
        target_lat,
    )
    target_lat = mux(
        is_st,
        cas(domain, m.const(MTE_TILELOAD_L2, width=LATENCY_W), cycle=0),
        target_lat,
    )
    target_lat = mux(
        is_zero,
        cas(domain, m.const(MTE_TILEZERO, width=LATENCY_W), cycle=0),
        target_lat,
    )
    target_lat = mux(
        is_get, cas(domain, m.const(MTE_TILEGET, width=LATENCY_W), cycle=0), target_lat
    )
    target_lat = mux(
        is_put, cas(domain, m.const(MTE_TILEPUT, width=LATENCY_W), cycle=0), target_lat
    )

    done = busy & (counter >= target_lat)

    # Tile completion (for LD, COPY, ZERO, PUT, TRANSPOSE — produces new tile)
    tile_complete = done & (is_ld | is_copy | is_zero | is_put | is_trans)
    # Scalar completion (for GET — produces scalar value)
    scalar_complete = done & is_get

    cdb_data_sig = cas(domain, m.const(0, width=data_w), cycle=0)  # placeholder

    # TRegFile port requests
    rd_phase = busy & (
        counter < cas(domain, m.const(epoch_cy, width=LATENCY_W), cycle=0)
    )
    wr_phase = busy & (
        counter >= cas(domain, m.const(epoch_cy, width=LATENCY_W), cycle=0)
    )
    needs_rd = is_st | is_copy | is_get | is_put | is_trans
    needs_wr = is_ld | is_copy | is_zero | is_put | is_trans

    outs = {
        "tcb_valid": tile_complete,
        "tcb_ptag": cur_ptdst,
        "cdb_valid": scalar_complete,
        "cdb_tag": cur_pdst,
        "cdb_data": cdb_data_sig,
        "busy": busy,
        "ready": ~busy,
        "rd_req": rd_phase & needs_rd,
        "rd_ptile": cur_ptsrc,
        "wr_req": wr_phase & needs_wr,
        "wr_ptile": cur_ptdst,
    }

    if inputs is None:
        for k in outs:
            m.output(f"{prefix}_{k}", wire_of(outs[k]))

    # ── Cycle 1: FSM ─────────────────────────────────────────────────
    domain.next()

    start = issue_valid & (~busy)

    busy <<= mux(
        done, issue_valid, mux(start, cas(domain, m.const(1, width=1), cycle=0), busy)
    )
    cur_op <<= mux(start, issue_op, cur_op)
    cur_ptsrc <<= mux(start, issue_ptsrc, cur_ptsrc)
    cur_ptdst <<= mux(start, issue_ptdst, cur_ptdst)
    cur_pdst <<= mux(start, issue_pdst, cur_pdst)
    counter <<= mux(
        start | done,
        cas(domain, m.const(0, width=LATENCY_W), cycle=0),
        mux(
            busy,
            (counter + cas(domain, m.const(1, width=LATENCY_W), cycle=0)).trunc(
                LATENCY_W
            ),
            counter,
        ),
    )

    return outs


mte_unit.__pycircuit_name__ = "mte_unit"


if __name__ == "__main__":
    pass
