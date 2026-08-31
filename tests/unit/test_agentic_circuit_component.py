from __future__ import annotations

import importlib
import sys
import tomllib
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[2]
AC_ROOT = REPOSITORY / "components" / "agentic-circuit"


def test_two_distributions_use_distinct_namespaces_and_bsd_license() -> None:
    pyc_metadata = tomllib.loads(
        (REPOSITORY / "pyproject.toml").read_text(encoding="utf-8")
    )["project"]
    ac_metadata = tomllib.loads(
        (AC_ROOT / "pyproject.toml").read_text(encoding="utf-8")
    )["project"]

    assert pyc_metadata["name"] == "pycircuit-hisi"
    assert ac_metadata["name"] == "agentic-circuit"
    assert "License :: OSI Approved :: BSD License" in pyc_metadata["classifiers"]
    assert ac_metadata["license"] == "BSD-3-Clause"

    sys.path.insert(0, str(AC_ROOT / "src"))
    sys.path.insert(0, str(REPOSITORY / "compiler" / "frontend"))
    try:
        pycircuit = importlib.import_module("pycircuit")
        agentic_circuit = importlib.import_module("agentic_circuit")
    finally:
        del sys.path[:2]

    assert pycircuit.__name__ == "pycircuit"
    assert agentic_circuit.__name__ == "agentic_circuit"
    assert not hasattr(pycircuit, "agentic_circuit")
    assert pycircuit.module is not agentic_circuit.module


def test_acpy_contract_epoch_remains_0_3() -> None:
    metadata = tomllib.loads(
        (AC_ROOT / "pyproject.toml").read_text(encoding="utf-8")
    )
    assert metadata["tool"]["agentic-circuit"]["contract-epoch"] == "0.3"

    source = (AC_ROOT / "src" / "agentic_circuit" / "_acpy.py").read_text(
        encoding="utf-8"
    )
    assert 'schema: str = "agentic-circuit-acpy"' in source
    assert 'version: str = "0.1"' in source
    assert 'contract_epoch: str = "0.3"' in source
