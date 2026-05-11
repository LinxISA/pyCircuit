from __future__ import annotations

import pycircuit
import pytest

pytestmark = pytest.mark.unit


def test_compile_cycle_aware_accepts_custom_active_low_reset_port() -> None:
    def build(m, domain) -> None:
        count = domain.signal(width=8, reset_value=0, name="count")
        count.assign(count + 1)
        m.output("count", pycircuit.wire_of(count))

    circuit = pycircuit.compile_cycle_aware(
        build,
        name="counter_rst_n",
        eager=True,
        reset_name="rst_n",
        reset_polarity="active_low",
    )

    mlir = circuit.emit_mlir()

    assert "%rst_n: !pyc.reset" in mlir
    assert 'arg_names = ["clk", "rst_n"]' in mlir
    assert 'pyc.reset_polarities = ["", "active_low"]' in mlir
    assert "pyc.reg %clk, %rst_n" in mlir


def test_create_domain_accepts_custom_active_low_reset_port() -> None:
    circuit = pycircuit.CycleAwareCircuit("manual_rst_n")
    domain = circuit.create_domain(
        "clk", reset_name="rst_n", reset_polarity="active_low"
    )
    active = domain.create_reset()
    circuit.output("reset_active", active)

    mlir = circuit.emit_mlir()

    assert "%rst_n: !pyc.reset" in mlir
    assert 'pyc.reset_polarities = ["", "active_low"]' in mlir
    assert "pyc.reset_active %rst_n" in mlir
