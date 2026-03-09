#!/usr/bin/env python3
from __future__ import annotations

import platform
import sys


def main(argv: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    arch = args[0] if args else platform.machine()
    arch = {"aarch64": "arm64", "arm64": "arm64", "x86_64": "x86_64"}.get(arch, arch)
    release = platform.mac_ver()[0] or "11.0"
    major, minor, *_ = (release.split(".") + ["0", "0"])[:2]
    print(f"macosx_{major}_{minor}_{arch}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
