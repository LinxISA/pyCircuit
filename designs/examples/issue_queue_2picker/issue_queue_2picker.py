from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    compile_cycle_aware,
    mux,
)


def _shift4(v: list, d: list, z):
    return [v[1], v[2], v[3], z], [d[1], d[2], d[3], d[3]]


def build(m: CycleAwareCircuit, domain: CycleAwareDomain) -> None:
    in_valid = cas(domain, m.input("in_valid", width=1), cycle=0)
    in_data = cas(domain, m.input("in_data", width=8), cycle=0)
    out0_ready = cas(domain, m.input("out0_ready", width=1), cycle=0)
    out1_ready = cas(domain, m.input("out1_ready", width=1), cycle=0)

    vals = [domain.state(width=1, reset_value=0, name=f"val{i}") for i in range(4)]
    data = [domain.state(width=8, reset_value=0, name=f"data{i}") for i in range(4)]

    v0 = [x for x in vals]
    d0 = [x for x in data]
    out0_valid = v0[0]
    out1_valid = v0[1]
    pop0 = out0_valid & out0_ready
    pop1 = out1_valid & out1_ready & pop0
    in_ready = ~v0[3] | pop0
    push = in_valid & in_ready

    zero1 = cas(domain, m.const(0, width=1), cycle=0)
    s1_v, s1_d = _shift4(v0, d0, zero1)
    a1_v = [mux(pop0, s1_v[i], v0[i]) for i in range(4)]
    a1_d = [mux(pop0, s1_d[i], d0[i]) for i in range(4)]

    s2_v, s2_d = _shift4(a1_v, a1_d, zero1)
    a2_v = [mux(pop1, s2_v[i], a1_v[i]) for i in range(4)]
    a2_d = [mux(pop1, s2_d[i], a1_d[i]) for i in range(4)]

    en = []
    pref = push
    for i in range(4):
        en_i = pref & ~a2_v[i]
        en.append(en_i)
        pref = pref & a2_v[i]

    m.output("in_ready", in_ready.wire)
    m.output("out0_valid", out0_valid.wire)
    m.output("out0_data", d0[0].wire)
    m.output("out1_valid", out1_valid.wire)
    m.output("out1_data", d0[1].wire)

    domain.next()

    for i in range(4):
        vals[i].set(a2_v[i] | en[i])
        data[i].set(mux(en[i], in_data, a2_d[i]))


build.__pycircuit_name__ = "issue_queue_2picker"


if __name__ == "__main__":
    print(compile_cycle_aware(build, name="issue_queue_2picker", eager=True).emit_mlir())
