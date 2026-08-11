"""Shared JSON value types and content digest spelling."""

from __future__ import annotations

import hashlib
from typing import TypeAlias


JsonScalar: TypeAlias = None | bool | int | float | str
JsonValue: TypeAlias = JsonScalar | list["JsonValue"] | dict[str, "JsonValue"]


def sha256_bytes(data: bytes) -> str:
    """Return a SHA-256 digest using the repository's public spelling."""

    return "sha256:" + hashlib.sha256(data).hexdigest()
