# -*- coding: utf-8 -*-
"""Digital Real-Time Clock with BCD LED display — pyCircuit unified signal model.

Architecture (single-cycle, no pipeline):

  cycle 0:  read all flop Q outputs → combinational logic
            (debounce, prescaler, clock arithmetic, BCD conversion)
            domain.next()
  cycle 1:  .set() D-inputs for all flops

Inputs:
  btn_set   — cycle through modes: RUN → SET_HOUR → SET_MIN → SET_SEC → RUN
  btn_plus  — increment the field being set (with wrap)
  btn_minus — decrement the field being set (with wrap)

Outputs:
  hours_bcd   [7:0]  — BCD hours   {tens[7:4], ones[3:0]}
  minutes_bcd [7:0]  — BCD minutes {tens[7:4], ones[3:0]}
  seconds_bcd [7:0]  — BCD seconds {tens[7:4], ones[3:0]}
  setting_mode [1:0] — 0=RUN, 1=SET_HOUR, 2=SET_MIN, 3=SET_SEC
  colon_blink  [0]   — toggles at 1 Hz (for blinking colon on display)

JIT parameters:
  CLK_FREQ    — system clock frequency in Hz (default 50 MHz)
  DEBOUNCE_MS — debounce window in milliseconds (default 20)
"""
from __future__ import annotations

from pycircuit import (
    CycleAwareCircuit,
    CycleAwareDomain,
    CycleAwareSignal,
    compile_cycle_aware,
    mux,
)

try:
    from .bcd import bin_to_bcd_24, bin_to_bcd_60
    from .debounce import make_debouncer, update_debouncer
except ImportError:
    # Fallback for direct file loading (e.g. pycircuit CLI)
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from bcd import bin_to_bcd_24, bin_to_bcd_60
    from debounce import make_debouncer, update_debouncer


# -- Mode encoding --
MODE_RUN      = 0
MODE_SET_HOUR = 1
MODE_SET_MIN  = 2
MODE_SET_SEC  = 3


def _digital_clock_impl(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    CLK_FREQ: int,
    DEBOUNCE_MS: int,
) -> None:
    c = lambda v, w: domain.const(v, width=w)

    # ================================================================
    # Inputs
    # ================================================================
    btn_set_raw   = domain.input("btn_set",   width=1)
    btn_plus_raw  = domain.input("btn_plus",  width=1)
    btn_minus_raw = domain.input("btn_minus", width=1)

    # ================================================================
    # Flop definitions (Q outputs at cycle 0)
    # ================================================================

    # Prescaler — divides CLK_FREQ down to 1 Hz
    PRESCALER_W = max((CLK_FREQ - 1).bit_length(), 1)
    prescaler_r = domain.signal("prescaler", width=PRESCALER_W, reset=0)

    # Time registers (binary)
    seconds_r = domain.signal("seconds", width=6, reset=0)   # 0–59
    minutes_r = domain.signal("minutes", width=6, reset=0)   # 0–59
    hours_r   = domain.signal("hours",   width=5, reset=0)   # 0–23

    # Mode: RUN / SET_HOUR / SET_MIN / SET_SEC
    mode_r = domain.signal("mode", width=2, reset=MODE_RUN)

    # Colon blink toggle (flips every 1 Hz tick)
    blink_r = domain.signal("blink", width=1, reset=0)

    # Button debouncers (each creates 4 internal flops)
    DEBOUNCE_CYCLES = max(CLK_FREQ * DEBOUNCE_MS // 1000, 2)
    db_set   = make_debouncer(domain, btn_set_raw,   "set",   DEBOUNCE_CYCLES)
    db_plus  = make_debouncer(domain, btn_plus_raw,  "plus",  DEBOUNCE_CYCLES)
    db_minus = make_debouncer(domain, btn_minus_raw, "minus", DEBOUNCE_CYCLES)

    set_pulse   = db_set["pulse"]
    plus_pulse  = db_plus["pulse"]
    minus_pulse = db_minus["pulse"]

    # ================================================================
    # Combinational logic (cycle 0)
    # ================================================================

    # ---- Prescaler / 1 Hz tick ----
    tick_1hz = prescaler_r.eq(c(CLK_FREQ - 1, PRESCALER_W))
    prescaler_next = mux(tick_1hz, c(0, PRESCALER_W), prescaler_r + 1)

    # ---- Mode flags ----
    is_running  = mode_r.eq(c(MODE_RUN,      2))
    is_set_hour = mode_r.eq(c(MODE_SET_HOUR, 2))
    is_set_min  = mode_r.eq(c(MODE_SET_MIN,  2))
    is_set_sec  = mode_r.eq(c(MODE_SET_SEC,  2))

    # Mode cycling: RUN → SET_HOUR → SET_MIN → SET_SEC → RUN
    mode_next = mux(is_set_sec, c(MODE_RUN, 2), mode_r + 1)

    # ---- Seconds ----
    sec_is_59 = seconds_r.eq(c(59, 6))
    sec_is_0  = seconds_r.eq(c(0,  6))
    sec_inc   = mux(sec_is_59, c(0, 6), seconds_r + 1)   # 59 → 0
    sec_dec   = mux(sec_is_0,  c(59, 6), seconds_r - 1)  #  0 → 59
    sec_carry = tick_1hz & sec_is_59 & is_running         # carry to minutes

    # ---- Minutes ----
    min_is_59 = minutes_r.eq(c(59, 6))
    min_is_0  = minutes_r.eq(c(0,  6))
    min_inc   = mux(min_is_59, c(0, 6), minutes_r + 1)
    min_dec   = mux(min_is_0,  c(59, 6), minutes_r - 1)
    min_carry = sec_carry & min_is_59                      # carry to hours

    # ---- Hours ----
    hr_is_23 = hours_r.eq(c(23, 5))
    hr_is_0  = hours_r.eq(c(0,  5))
    hr_inc   = mux(hr_is_23, c(0, 5), hours_r + 1)
    hr_dec   = mux(hr_is_0,  c(23, 5), hours_r - 1)

    # ---- BCD conversion (purely combinational) ----
    sec_bcd = bin_to_bcd_60(domain, seconds_r, "sec")
    min_bcd = bin_to_bcd_60(domain, minutes_r, "min")
    hr_bcd  = bin_to_bcd_24(domain, hours_r,   "hr")

    # ================================================================
    # DFF boundary
    # ================================================================
    domain.next()   # → cycle 1

    # ================================================================
    # Flop D-input assignments (.set(), last-write-wins priority)
    # ================================================================

    # ---- Prescaler (always runs) ----
    prescaler_r.set(prescaler_next)

    # ---- Blink toggle ----
    blink_r.set(blink_r)                          # hold
    blink_r.set(~blink_r, when=tick_1hz)          # toggle on 1 Hz

    # ---- Mode ----
    mode_r.set(mode_r)                            # hold
    mode_r.set(mode_next, when=set_pulse)         # cycle on SET press

    # ---- Seconds (priority: hold < tick < +btn < −btn) ----
    seconds_r.set(seconds_r)                                        # hold
    seconds_r.set(sec_inc, when=tick_1hz & is_running)              # normal advance
    seconds_r.set(sec_inc, when=plus_pulse & is_set_sec)            # manual +
    seconds_r.set(sec_dec, when=minus_pulse & is_set_sec)           # manual −

    # ---- Minutes ----
    minutes_r.set(minutes_r)                                        # hold
    minutes_r.set(min_inc, when=sec_carry)                          # carry from seconds
    minutes_r.set(min_inc, when=plus_pulse & is_set_min)            # manual +
    minutes_r.set(min_dec, when=minus_pulse & is_set_min)           # manual −

    # ---- Hours ----
    hours_r.set(hours_r)                                            # hold
    hours_r.set(hr_inc, when=min_carry)                             # carry from minutes
    hours_r.set(hr_inc, when=plus_pulse & is_set_hour)              # manual +
    hours_r.set(hr_dec, when=minus_pulse & is_set_hour)             # manual −

    # ---- Debounce registers ----
    update_debouncer(db_set)
    update_debouncer(db_plus)
    update_debouncer(db_minus)

    # ================================================================
    # Outputs
    # ================================================================
    m.output("hours_bcd",    hr_bcd)
    m.output("minutes_bcd",  min_bcd)
    m.output("seconds_bcd",  sec_bcd)
    m.output("setting_mode", mode_r)
    m.output("colon_blink",  blink_r)


# ------------------------------------------------------------------
# Public entry point (with JIT parameters)
# ------------------------------------------------------------------

def digital_clock(
    m: CycleAwareCircuit,
    domain: CycleAwareDomain,
    CLK_FREQ: int = 50_000_000,
    DEBOUNCE_MS: int = 20,
) -> None:
    _digital_clock_impl(m, domain, CLK_FREQ, DEBOUNCE_MS)


# ------------------------------------------------------------------
# CLI entry point: pycircuit.cli expects `build` → Module.
# We compile via compile_cycle_aware and return the circuit.
# ------------------------------------------------------------------

def build():
    """CLI entry point — returns compiled Module."""
    return compile_cycle_aware(
        digital_clock, name="digital_clock",
        CLK_FREQ=50_000_000, DEBOUNCE_MS=20,
    )


# ------------------------------------------------------------------
# Standalone compile
# ------------------------------------------------------------------

if __name__ == "__main__":
    circuit = build()
    print(circuit.emit_mlir())
