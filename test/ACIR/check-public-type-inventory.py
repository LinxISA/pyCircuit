#!/usr/bin/env python3

import re
import sys
from pathlib import Path


EXPECTED = {
    "address",
    "channel",
    "duration",
    "endpoint",
    "enum",
    "event",
    "flow",
    "list",
    "optional",
    "packet",
    "rate",
    "resource_ref",
    "resource_token",
    "struct",
    "transaction",
    "union",
    "vector",
}


def main() -> int:
    covered = set()
    for path in map(Path, sys.argv[1:]):
        covered.update(re.findall(r"!ac\.([a-z_]+)", path.read_text()))

    if covered == EXPECTED:
        return 0

    print("ACIR public type inventory mismatch", file=sys.stderr)
    print(f"missing: {sorted(EXPECTED - covered)}", file=sys.stderr)
    print(f"unexpected: {sorted(covered - EXPECTED)}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
