#!/usr/bin/env python3
"""Generate one canonical typed gfsim C++ file from serial Queue Python."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import tempfile

from agentic_circuit._queue_codegen import lower_queue_source_to_cpp


def main() -> int:
    parser = argparse.ArgumentParser(allow_abbrev=False)
    parser.add_argument("source", type=Path)
    parser.add_argument("--system", required=True)
    parser.add_argument("-o", "--output", required=True, type=Path)
    arguments = parser.parse_args()
    generated = lower_queue_source_to_cpp(
        arguments.source.read_text(encoding="utf-8"), arguments.system
    )
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(
        dir=arguments.output.parent, prefix=f".{arguments.output.name}.", text=True
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(generated)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, arguments.output)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
