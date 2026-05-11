from __future__ import annotations

import pycircuit
import pytest

pytestmark = pytest.mark.unit


def test_state_signal_reads_rebase_to_current_occurrence() -> None:
    circuit = pycircuit.CycleAwareCircuit("rebased_state")
    domain = circuit.create_domain("clk")
    counter = domain.state(width=8, reset_value=0, name="counter")

    domain.next()
    expr = counter + 1

    assert expr.cycle == domain.cycle_index


def test_state_signal_feedback_does_not_insert_balance_registers() -> None:
    circuit = pycircuit.CycleAwareCircuit("counter_feedback")
    domain = circuit.create_domain("clk")
    counter = domain.state(width=8, reset_value=0, name="counter")

    domain.next()
    counter.set(counter + 1)

    mlir = circuit.emit_mlir()
    assert mlir.count("pyc.reg") == 1
    assert "_v5_bal_" not in mlir


def test_state_signal_slice_reads_rebase_to_current_occurrence() -> None:
    circuit = pycircuit.CycleAwareCircuit("rebased_slice")
    domain = circuit.create_domain("clk")
    counter = domain.state(width=8, reset_value=0, name="counter")

    domain.next()
    expr = counter[0]

    assert expr.cycle == domain.cycle_index


def test_cycle_aware_reverse_subtraction_compiles_through_jit() -> None:
    def build(m, domain) -> None:
        counter = domain.state(width=8, reset_value=0, name="counter")
        domain.next()
        result = 1 - counter
        m.output("result", result.wire)

    design = pycircuit.compile_cycle_aware(build, name="reverse_sub_smoke")
    mlir = design.emit_mlir()

    assert "_v5_bal_" not in mlir


def test_hierarchical_design_accessor_reports_hierarchical_design() -> None:
    def child(m, domain, *, inputs=None, prefix: str = "child"):
        value = pycircuit.submodule_input(
            inputs, "value", m, domain, prefix=prefix, width=8
        )
        m.output(f"{prefix}_out", pycircuit.wire_of(value))
        return {"out": value}

    def build(m, domain):
        value = pycircuit.cas(domain, m.input("value", width=8), cycle=0)
        return domain.call(child, inputs={"value": value}, prefix="u_child")

    circuit = pycircuit.compile_cycle_aware(
        build, name="hier_accessor", eager=True, hierarchical=True
    )

    assert circuit.hierarchical_design is not None
    assert circuit.hierarchical_design.top == "hier_accessor"


def test_hierarchical_design_accessor_is_none_for_flat_circuit() -> None:
    circuit = pycircuit.CycleAwareCircuit("flat")

    assert circuit.hierarchical_design is None
