from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    cas,
    wire_of,
)

MODE_RUN = 0
MODE_SET_HOUR = 1
MODE_SET_MIN = 2
MODE_SET_SEC = 3


def _to_bcd8(m: CycleAwareCircuit, domain: CycleAwareDomain, v, width: int):
    vw = wire_of(v)
    ten = 10
    ones_w = vw % ten
    tens_w = vw // ten
    return cas(domain, m.cat(tens_w[0:4], ones_w[0:4]), cycle=v.cycle)


def build(
    m: CycleAwareCircuit, domain: CycleAwareDomain, clk_freq: int = 50_000_000
) -> None:
    prescaler_w = max((int(clk_freq) - 1).bit_length(), 1)

    prescaler = domain.signal(width=prescaler_w, reset_value=0, name="prescaler")
    sec = domain.signal(width=6, reset_value=0, name="sec")
    minute = domain.signal(width=6, reset_value=0, name="minute")
    hour = domain.signal(width=5, reset_value=0, name="hour")
    mode = domain.signal(width=2, reset_value=MODE_RUN, name="mode")
    blink = domain.signal(width=1, reset_value=0, name="blink")

    btn_set = cas(domain, m.input("btn_set", width=1), cycle=0)
    btn_plus = cas(domain, m.input("btn_plus", width=1), cycle=0)
    btn_minus = cas(domain, m.input("btn_minus", width=1), cycle=0)

    tick_1hz = prescaler == (clk_freq - 1)
    prescaler_n = 0 if tick_1hz else (prescaler + 1)

    is_run = mode == MODE_RUN
    is_set_hour = mode == MODE_SET_HOUR
    is_set_min = mode == MODE_SET_MIN
    is_set_sec = mode == MODE_SET_SEC

    sec_wrap = sec == 59
    min_wrap = minute == 59
    hr_wrap = hour == 23

    sec_next_run = 0 if sec_wrap else (sec + 1)
    min_next_run = (0 if min_wrap else (minute + 1)) if sec_wrap else minute
    hr_next_run = (0 if hr_wrap else (hour + 1)) if (sec_wrap & min_wrap) else hour

    tick_run = tick_1hz & is_run
    sec_n_tick = sec_next_run if tick_run else sec
    min_n_tick = min_next_run if tick_run else minute
    hr_n_tick = hr_next_run if tick_run else hour

    mode_next = MODE_RUN if (mode == MODE_SET_SEC) else (mode + 1)

    hour_p = (
        (0 if (hour == 23) else (hour + 1)) if (btn_plus & is_set_hour) else hr_n_tick
    )
    min_p = (
        (0 if (minute == 59) else (minute + 1))
        if (btn_plus & is_set_min)
        else min_n_tick
    )
    sec_p = (0 if (sec == 59) else (sec + 1)) if (btn_plus & is_set_sec) else sec_n_tick

    hour_n = (
        (23 if (hour == 0) else (hour - 1)) if (btn_minus & is_set_hour) else hour_p
    )
    min_n = (
        (59 if (minute == 0) else (minute - 1)) if (btn_minus & is_set_min) else min_p
    )
    sec_n = (59 if (sec == 0) else (sec - 1)) if (btn_minus & is_set_sec) else sec_p

    m.output("hours_bcd", wire_of(_to_bcd8(m, domain, hour, 5)))
    m.output("minutes_bcd", wire_of(_to_bcd8(m, domain, minute, 6)))
    m.output("seconds_bcd", wire_of(_to_bcd8(m, domain, sec, 6)))
    m.output("setting_mode", wire_of(mode))
    m.output("colon_blink", wire_of(blink))

    domain.next()

    prescaler <<= prescaler_n
    sec <<= sec_n
    minute <<= min_n
    hour <<= hour_n
    mode.assign(mode_next, when=btn_set)
    blink.assign(~blink, when=tick_1hz)


build.__pycircuit_name__ = "digital_clock"


if __name__ == "__main__":
    pass
