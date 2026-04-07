"""Reference counter array for physical register / tile freeing.

Each physical register has an orphan bit and a saturating reference count.
A register is freed when orphan=1 AND refcount=0.

Used identically for scalar PRF (128 entries, 4-bit refcnt) and
tile register file (256 entries, 3-bit refcnt).
"""

from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    mux,
    wire_of,
)


def _in(io, key, m, domain, prefix, width):
    if io is not None and key in io:
        return io[key]
    return cas(domain, m.input(f"{prefix}_{key}", width=width), cycle=0)


def ref_counter(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    *,
    entries: int = 128,
    refcnt_w: int = 4,
    inc_ports: int = 4,
    dec_ports: int = 6,
    orphan_ports: int = 4,
    prefix: str = "rc",
    inputs: dict | None = None,
) -> dict:
    tag_w = max(1, (entries - 1).bit_length())

    # ── Cycle 0: Inputs ──────────────────────────────────────────────
    inc_valid = [
        _in(inputs, f"inc_valid{i}", m, domain, prefix, 1) for i in range(inc_ports)
    ]
    inc_tag = [
        _in(inputs, f"inc_tag{i}", m, domain, prefix, tag_w) for i in range(inc_ports)
    ]

    dec_valid = [
        _in(inputs, f"dec_valid{i}", m, domain, prefix, 1) for i in range(dec_ports)
    ]
    dec_tag = [
        _in(inputs, f"dec_tag{i}", m, domain, prefix, tag_w) for i in range(dec_ports)
    ]

    mark_orphan_valid = [
        _in(inputs, f"orphan_valid{i}", m, domain, prefix, 1)
        for i in range(orphan_ports)
    ]
    mark_orphan_tag = [
        _in(inputs, f"orphan_tag{i}", m, domain, prefix, tag_w)
        for i in range(orphan_ports)
    ]

    # ── State: per-entry orphan bit + refcount ───────────────────────
    orphan = [
        domain.signal(width=1, reset_value=0, name=f"{prefix}_orphan_{i}")
        for i in range(entries)
    ]
    refcnt = [
        domain.signal(width=refcnt_w, reset_value=0, name=f"{prefix}_refcnt_{i}")
        for i in range(entries)
    ]

    # ── Combinational: compute free signals ──────────────────────────
    outs: dict = {}
    for i in range(entries):
        is_free = orphan[i] & (
            refcnt[i] == cas(domain, m.const(0, width=refcnt_w), cycle=0)
        )
        outs[f"free{i}"] = is_free
        if inputs is None:
            m.output(f"{prefix}_free{i}", wire_of(is_free))

    # ── Cycle 1: Sequential update ───────────────────────────────────
    domain.next()

    for e in range(entries):
        etag = cas(domain, m.const(e, width=tag_w), cycle=0)

        n_inc = cas(domain, m.const(0, width=refcnt_w), cycle=0)
        for p in range(inc_ports):
            hit = inc_valid[p] & (inc_tag[p] == etag)
            n_inc = n_inc + hit

        n_dec = cas(domain, m.const(0, width=refcnt_w), cycle=0)
        for p in range(dec_ports):
            hit = dec_valid[p] & (dec_tag[p] == etag)
            n_dec = n_dec + hit

        any_orphan_mark = cas(domain, m.const(0, width=1), cycle=0)
        for p in range(orphan_ports):
            hit = mark_orphan_valid[p] & (mark_orphan_tag[p] == etag)
            any_orphan_mark = any_orphan_mark | hit

        new_refcnt = (refcnt[e] + n_inc - n_dec).trunc(refcnt_w)
        new_orphan = orphan[e] | any_orphan_mark

        new_is_free = new_orphan & (
            new_refcnt == cas(domain, m.const(0, width=refcnt_w), cycle=0)
        )
        cleared_orphan = mux(
            new_is_free, cas(domain, m.const(0, width=1), cycle=0), new_orphan
        )
        cleared_refcnt = mux(
            new_is_free, cas(domain, m.const(0, width=refcnt_w), cycle=0), new_refcnt
        )

        zero_rc = cas(domain, m.const(0, width=refcnt_w), cycle=0)
        has_inc = ~(n_inc == zero_rc)
        has_dec = ~(n_dec == zero_rc)
        change = has_inc | has_dec | any_orphan_mark
        refcnt[e].assign(cleared_refcnt, when=change)
        orphan[e].assign(cleared_orphan, when=change)

    return outs


ref_counter.__pycircuit_name__ = "ref_counter"


if __name__ == "__main__":
    pass
