from __future__ import annotations

from pycircuit import Circuit, compile, module, u


@module(value_params={"gain": "i8", "bias": "i32", "enable": "i1"})
def _lane(m: Circuit, x, gain, bias, enable, *, width: int = 32):
    y_add = (x + gain + bias)[0:width]
    y = y_add if enable else x
    m.output("y", y)


@module
def build(m: Circuit, *, width: int = 32):
    seed = m.input("seed", width=width)

    lane0 = m.new(
        _lane,
        name="lane0",
        params={"width": width},
        bind={"x": seed, "gain": 1, "bias": 5, "enable": 1},
    ).outputs
    lane1 = m.new(
        _lane,
        name="lane1",
        params={"width": width},
        bind={"x": seed, "gain": 3, "bias": 9, "enable": u(1, 1)},
    ).outputs
    lane2 = m.new(
        _lane,
        name="lane2",
        params={"width": width},
        bind={"x": seed, "gain": u(8, 7), "bias": u(width, 11), "enable": u(1, 0)},
    ).outputs

    acc = (lane0.read() + lane1.read() + lane2.read())[0:width]
    m.output("acc", acc)


build.__pycircuit_name__ = "boundary_value_ports"

if __name__ == "__main__":
    print(compile(build, name="boundary_value_ports", width=32).emit_mlir())
