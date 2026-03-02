#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


MAGIC = b"PYC4TRC1"


class ParseError(RuntimeError):
    pass


@dataclass(frozen=True)
class ProbeDecl:
    probe_id: int
    kind: int
    width_bits: int
    canonical_path: str
    type_sig: bytes


def _take(buf: memoryview, off: int, n: int) -> tuple[memoryview, int]:
    if off + n > len(buf):
        raise ParseError(f"unexpected EOF at offset {off} need {n} bytes")
    return buf[off : off + n], off + n


def _u8(buf: memoryview, off: int) -> tuple[int, int]:
    b, off = _take(buf, off, 1)
    return int(b[0]), off


def _u16le(buf: memoryview, off: int) -> tuple[int, int]:
    b, off = _take(buf, off, 2)
    return int(struct.unpack_from("<H", b, 0)[0]), off


def _u32le(buf: memoryview, off: int) -> tuple[int, int]:
    b, off = _take(buf, off, 4)
    return int(struct.unpack_from("<I", b, 0)[0]), off


def _u64le(buf: memoryview, off: int) -> tuple[int, int]:
    b, off = _take(buf, off, 8)
    return int(struct.unpack_from("<Q", b, 0)[0]), off


def _bytes(buf: memoryview, off: int, n: int) -> tuple[bytes, int]:
    b, off = _take(buf, off, n)
    return bytes(b), off


def parse_pyctrace(path: Path) -> tuple[int, int, list[ProbeDecl], list[tuple[int, list[tuple[int, list[int]]]]]]:
    data = memoryview(path.read_bytes())
    off = 0

    magic, off = _bytes(data, off, 8)
    if magic != MAGIC:
        raise ParseError(f"bad magic: got={magic!r} exp={MAGIC!r}")

    version, off = _u32le(data, off)
    flags, off = _u32le(data, off)
    probe_count, off = _u32le(data, off)

    probes: list[ProbeDecl] = []
    for _ in range(probe_count):
        pid, off = _u64le(data, off)
        kind, off = _u8(data, off)
        width_bits, off = _u32le(data, off)
        path_len, off = _u32le(data, off)
        pbytes, off = _bytes(data, off, path_len)
        try:
            pstr = pbytes.decode("utf-8")
        except UnicodeDecodeError:
            pstr = pbytes.decode("utf-8", errors="replace")
        ts_len, off = _u16le(data, off)
        ts, off = _bytes(data, off, ts_len)
        probes.append(ProbeDecl(probe_id=pid, kind=kind, width_bits=width_bits, canonical_path=pstr, type_sig=ts))

    cycles: list[tuple[int, list[tuple[int, list[int]]]]] = []
    while off < len(data):
        rec_type, off = _u8(data, off)
        if rec_type == 1:
            cyc, off = _u64le(data, off)
            ev_count, off = _u32le(data, off)
            evs: list[tuple[int, list[int]]] = []
            for _ in range(ev_count):
                pid, off = _u64le(data, off)
                wcount, off = _u32le(data, off)
                words: list[int] = []
                for _ in range(wcount):
                    w, off = _u64le(data, off)
                    words.append(w)
                evs.append((pid, words))
            cycles.append((cyc, evs))
            continue
        raise ParseError(f"unknown record_type={rec_type} at offset={off - 1}")

    return version, flags, probes, cycles


def main() -> int:
    ap = argparse.ArgumentParser(description="Dump a pyCircuit v4.0 binary trace (.pyctrace).")
    ap.add_argument("path", type=Path)
    ap.add_argument("--max-cycles", type=int, default=10)
    ap.add_argument("--max-events", type=int, default=50)
    ap.add_argument("--no-header", action="store_true")
    ns = ap.parse_args()

    p = Path(ns.path).resolve()
    if not p.is_file():
        print(f"error: file not found: {p}", file=sys.stderr)
        return 2

    try:
        version, flags, probes, cycles = parse_pyctrace(p)
    except ParseError as e:
        print(f"error: {p}: {e}", file=sys.stderr)
        return 2

    if not ns.no_header:
        print(f"path: {p}")
        print(f"version: {version}")
        print(f"flags: 0x{flags:08x}")
        print(f"probe_count: {len(probes)}")
        for d in probes[: min(len(probes), 20)]:
            print(f"  - id=0x{d.probe_id:016x} kind={d.kind} width={d.width_bits} path={d.canonical_path!r}")
        if len(probes) > 20:
            print(f"  ... ({len(probes) - 20} more)")

    pid_to_path = {d.probe_id: d.canonical_path for d in probes}
    pid_to_width = {d.probe_id: d.width_bits for d in probes}

    max_cycles = max(0, int(ns.max_cycles))
    max_events = max(0, int(ns.max_events))
    for cyc, evs in cycles[: min(len(cycles), max_cycles)]:
        print(f"cycle {cyc}: {len(evs)} events")
        for pid, words in evs[: min(len(evs), max_events)]:
            width = pid_to_width.get(pid, 0)
            path = pid_to_path.get(pid, f"<unknown:0x{pid:016x}>")
            # Display low word first (internal representation).
            if width <= 64 and words:
                print(f"  - {path} = 0x{words[0]:x}")
            else:
                ws = " ".join(f"{w:016x}" for w in words)
                print(f"  - {path} = [words={len(words)}] {ws}")
        if len(evs) > max_events:
            print(f"  ... ({len(evs) - max_events} more)")

    if len(cycles) > max_cycles:
        print(f"... ({len(cycles) - max_cycles} more cycles)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

