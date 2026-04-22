from __future__ import annotations

from pathlib import Path

from pe_int import DEFAULT_PIPELINE_L, MODE_2A, MODE_2B, MODE_2C, MODE_2D, build, emit_mlir

build.__pycircuit_name__ = "pe_int_fix7"

__all__ = [
    "MODE_2A",
    "MODE_2B",
    "MODE_2C",
    "MODE_2D",
    "DEFAULT_PIPELINE_L",
    "build",
    "emit_mlir",
]


if __name__ == "__main__":
    out = Path(__file__).resolve().parents[1] / "build" / "pe_int.mlir"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(emit_mlir(), encoding="utf-8")
    print(f"Wrote {out}")
