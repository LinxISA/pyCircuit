"""Load/Store Unit — 4-cycle load pipeline, store buffer, store-to-load forwarding.

Simplified model (no precise exceptions):
  - Load pipeline: addr calc → TLB → L1-D access → align (4 stages)
  - Store buffer: 16 entries, OoO commit
  - Store-to-load forwarding on address match
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
    LOAD_LATENCY_L1,
    PHYS_GREG_W,
    SCALAR_DATA_W,
)


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def lsu(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    data_w: int = SCALAR_DATA_W,
    tag_w: int = PHYS_GREG_W,
    addr_w: int = 64,
    pipe_depth: int = LOAD_LATENCY_L1,
    prefix: str = "lsu",
    inputs: dict | None = None,
) -> dict:
    # ── Cycle 0: Issue interface ─────────────────────────────────────
    ld_valid = _in(inputs, "ld_valid", m, domain, prefix, 1)
    ld_addr = _in(inputs, "ld_addr", m, domain, prefix, addr_w)
    ld_pdst = _in(inputs, "ld_pdst", m, domain, prefix, tag_w)

    st_valid = _in(inputs, "st_valid", m, domain, prefix, 1)
    st_addr = _in(inputs, "st_addr", m, domain, prefix, addr_w)
    st_data = _in(inputs, "st_data", m, domain, prefix, data_w)

    # ── Load pipeline (4 stages, modeled as shift register) ──────────
    ld_pipe_v = [
        domain.signal(width=1, reset_value=0, name=f"{prefix}_lpv_{s}")
        for s in range(pipe_depth)
    ]
    ld_pipe_tag = [
        domain.signal(width=tag_w, reset_value=0, name=f"{prefix}_lpt_{s}")
        for s in range(pipe_depth)
    ]
    ld_pipe_addr = [
        domain.signal(width=addr_w, reset_value=0, name=f"{prefix}_lpa_{s}")
        for s in range(pipe_depth)
    ]

    # ── Store buffer (simplified: single entry for forwarding demo) ──
    stb_valid = domain.signal(width=1, reset_value=0, name=f"{prefix}_stbv")
    stb_addr = domain.signal(width=addr_w, reset_value=0, name=f"{prefix}_stba")
    stb_data = domain.signal(width=data_w, reset_value=0, name=f"{prefix}_stbd")

    # Store-to-load forwarding check (combinational)
    fwd_hit = stb_valid & (stb_addr == ld_addr)
    fwd_data = stb_data

    # Load result from pipeline output
    ld_result_valid = ld_pipe_v[pipe_depth - 1]
    ld_result_tag = ld_pipe_tag[pipe_depth - 1]
    ld_result_data = cas(
        domain, m.const(0, width=data_w), cycle=0
    )  # placeholder: real impl reads cache
    ld_result_data = mux(fwd_hit, fwd_data, ld_result_data)

    outs = {
        "ld_result_valid": ld_result_valid,
        "ld_result_tag": ld_result_tag,
        "ld_result_data": ld_result_data,
    }
    if inputs is None:
        m.output(f"{prefix}_ld_result_valid", wire_of(outs["ld_result_valid"]))
        m.output(f"{prefix}_ld_result_tag", wire_of(outs["ld_result_tag"]))
        m.output(f"{prefix}_ld_result_data", wire_of(outs["ld_result_data"]))

    # ── Cycle 1: Pipeline advance ────────────────────────────────────
    domain.next()

    ld_pipe_v[0] <<= ld_valid
    ld_pipe_tag[0] <<= ld_pdst
    ld_pipe_addr[0] <<= ld_addr
    for s in range(1, pipe_depth):
        ld_pipe_v[s] <<= ld_pipe_v[s - 1]
        ld_pipe_tag[s] <<= ld_pipe_tag[s - 1]
        ld_pipe_addr[s] <<= ld_pipe_addr[s - 1]

    # Store buffer: write new entry
    stb_valid.assign(cas(domain, m.const(1, width=1), cycle=0), when=st_valid)
    stb_addr.assign(st_addr, when=st_valid)
    stb_data.assign(st_data, when=st_valid)

    return outs


lsu.__pycircuit_name__ = "lsu"


if __name__ == "__main__":
    pass
