from __future__ import annotations

from pycircuit import Circuit, compile_design, u


def _shift4(v: list, d: list, z):
    return [v[1], v[2], v[3], z], [d[1], d[2], d[3], d[3]]


def build(m: Circuit) -> None:
    clk = m.clock("clk")
    rst = m.reset("rst")

    in_valid = m.input("in_valid", width=1)
    in_data = m.input("in_data", width=8)
    out0_ready = m.input("out0_ready", width=1)
    out1_ready = m.input("out1_ready", width=1)

    vals = [m.out(f"val{i}", clk=clk, rst=rst, width=1, init=u(1, 0)) for i in range(4)]
    data = [m.out(f"data{i}", clk=clk, rst=rst, width=8, init=u(8, 0)) for i in range(4)]

    v0 = [x.out() for x in vals]
    d0 = [x.out() for x in data]
    out0_valid = v0[0]
    out1_valid = v0[1]
    pop0 = out0_valid & out0_ready
    pop1 = out1_valid & out1_ready & pop0
    in_ready = ~v0[3] | pop0
    push = in_valid & in_ready

    z1 = u(1, 0)
    s1_v, s1_d = _shift4(v0, d0, z1)
    a1_v = [s1_v[i] if pop0 else v0[i] for i in range(4)]
    a1_d = [s1_d[i] if pop0 else d0[i] for i in range(4)]

    s2_v, s2_d = _shift4(a1_v, a1_d, z1)
    a2_v = [s2_v[i] if pop1 else a1_v[i] for i in range(4)]
    a2_d = [s2_d[i] if pop1 else a1_d[i] for i in range(4)]

    en = []
    pref = push
    for i in range(4):
        en_i = pref & ~a2_v[i]
        en.append(en_i)
        pref = pref & a2_v[i]

    for i in range(4):
        vals[i].set(a2_v[i] | en[i])
        data[i].set(in_data if en[i] else a2_d[i])

    m.output("in_ready", in_ready)
    m.output("out0_valid", out0_valid)
    m.output("out0_data", d0[0])
    m.output("out1_valid", out1_valid)
    m.output("out1_data", d0[1])


if __name__ == "__main__":
    print(compile_design(build, name="issue_queue_2picker").emit_mlir())
