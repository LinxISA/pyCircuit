from __future__ import annotations

from pycircuit import Circuit, cat, compile_cycle_aware, CycleAwareCircuit, CycleAwareDomain, function, module, u

MODE_RUN = 0
MODE_SET_HOUR = 1
MODE_SET_MIN = 2
MODE_SET_SEC = 3


@function
def _to_bcd8(m: Circuit, v):
    ones = v % u(v.width, 10)
    tens = v // u(v.width, 10)
    return cat(tens[0:4], ones[0:4])


def build(m: CycleAwareCircuit, domain: CycleAwareDomain, clk_freq: int = 50_000_000) -> None:
    cd = domain.clock_domain
    clk = cd.clk
    rst = cd.rst
    btn_set = m.input("btn_set", width=1)
    btn_plus = m.input("btn_plus", width=1)
    btn_minus = m.input("btn_minus", width=1)

    prescaler_w = max((int(clk_freq) - 1).bit_length(), 1)
    prescaler = m.out("prescaler", domain=cd, width=prescaler_w, init=u(prescaler_w, 0))
    sec = m.out("sec", domain=cd, width=6, init=u(6, 0))
    minute = m.out("minute", domain=cd, width=6, init=u(6, 0))
    hour = m.out("hour", domain=cd, width=5, init=u(5, 0))
    mode = m.out("mode", domain=cd, width=2, init=u(2, MODE_RUN))
    blink = m.out("blink", domain=cd, width=1, init=u(1, 0))

    tick_1hz = prescaler == u(prescaler_w, clk_freq - 1)

    prescaler_n = u(prescaler_w, 0) if tick_1hz else prescaler + 1
    sec_n = sec.out()
    min_n = minute.out()
    hour_n = hour.out()

    is_run = mode == u(2, MODE_RUN)
    is_set_hour = mode == u(2, MODE_SET_HOUR)
    is_set_min = mode == u(2, MODE_SET_MIN)
    is_set_sec = mode == u(2, MODE_SET_SEC)

    if tick_1hz & is_run:
        sec_n = sec + 1
        if sec == u(6, 59):
            sec_n = u(6, 0)
            min_n = minute + 1
            if minute == u(6, 59):
                min_n = u(6, 0)
                hour_n = hour + 1
                if hour == u(5, 23):
                    hour_n = u(5, 0)

    mode_n = mode + 1
    mode_n = u(2, MODE_RUN) if mode == u(2, MODE_SET_SEC) else mode_n
    mode.set(mode_n, when=btn_set)

    if btn_plus:
        hour_n = u(5, 0) if (is_set_hour & (hour == u(5, 23))) else (hour + 1 if is_set_hour else hour_n)
        min_n = u(6, 0) if (is_set_min & (minute == u(6, 59))) else (minute + 1 if is_set_min else min_n)
        sec_n = u(6, 0) if (is_set_sec & (sec == u(6, 59))) else (sec + 1 if is_set_sec else sec_n)

    if btn_minus:
        hour_n = u(5, 23) if (is_set_hour & (hour == u(5, 0))) else (hour - 1 if is_set_hour else hour_n)
        min_n = u(6, 59) if (is_set_min & (minute == u(6, 0))) else (minute - 1 if is_set_min else min_n)
        sec_n = u(6, 59) if (is_set_sec & (sec == u(6, 0))) else (sec - 1 if is_set_sec else sec_n)

    prescaler.set(prescaler_n)
    sec.set(sec_n)
    minute.set(min_n)
    hour.set(hour_n)
    blink.set(~blink, when=tick_1hz)

    m.output("hours_bcd", _to_bcd8(m, hour.out()))
    m.output("minutes_bcd", _to_bcd8(m, minute.out()))
    m.output("seconds_bcd", _to_bcd8(m, sec.out()))
    m.output("setting_mode", mode)
    m.output("colon_blink", blink)


build.__pycircuit_name__ = "digital_clock"


if __name__ == "__main__":
    print(compile_cycle_aware(build, name="digital_clock", clk_freq=50_000_000).emit_mlir())
