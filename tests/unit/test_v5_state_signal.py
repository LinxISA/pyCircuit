from __future__ import annotations

import pytest

from pycircuit import CycleAwareCircuit


pytestmark = pytest.mark.unit


def test_state_signal_reads_rebase_to_current_occurrence() -> None:
    circuit = CycleAwareCircuit("rebased_state")
    domain = circuit.create_domain("clk")
    counter = domain.state(width=8, reset_value=0, name="counter")

    domain.next()
    expr = counter + 1

    assert expr.cycle == domain.cycle_index


def test_state_signal_feedback_does_not_insert_balance_registers() -> None:
    circuit = CycleAwareCircuit("counter_feedback")
    domain = circuit.create_domain("clk")
    counter = domain.state(width=8, reset_value=0, name="counter")

    domain.next()
    counter.set(counter + 1)

    mlir = circuit.emit_mlir()
    assert mlir.count("pyc.reg") == 1
    assert "_v5_bal_" not in mlir
