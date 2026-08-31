"""Filesystem-backed access to installed package resources."""

from __future__ import annotations

from importlib.resources import files
from pathlib import Path


def resource_directory(name: str) -> Path:
    resource = files(f"agentic_circuit._data.{name}")
    path = Path(str(resource))
    if not path.is_dir():
        raise FileNotFoundError(f"Agentic Circuit {name} resources are unavailable")
    return path
