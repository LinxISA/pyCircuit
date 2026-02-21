from __future__ import annotations

import argparse
import ast
import hashlib
import importlib
import importlib.util
import inspect
import json
import os
import shutil
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path
from typing import Any

from .api_contract import collect_local_python_graph, nearest_project_root, scan_file
from .diagnostics import render_diagnostic
from .dsl import Module
from .design import FRONTEND_CONTRACT, Design, DesignError
from .jit import JitError, compile
from .tb import Tb, TbError, _sanitize_id
from .testbench import emit_testbench_pyc, testbench_payload_from_tb


def _default_top_name(src: Path) -> str:
    parts = [p for p in src.stem.replace("-", "_").split("_") if p]
    if not parts:
        return "Top"
    return "".join(p[:1].upper() + p[1:] for p in parts)


def _load_py_file(path: Path) -> object:
    path = path.resolve()
    spec = importlib.util.spec_from_file_location(path.stem, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Failed to import {path}")
    m = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = m
    spec.loader.exec_module(m)
    return m


def _resolve_emit_source(src_arg: str) -> tuple[Path | None, object]:
    if "." in src_arg and not Path(src_arg).exists():
        spec = importlib.util.find_spec(src_arg)
        src: Path | None = None
        if spec is not None and isinstance(spec.origin, str) and spec.origin.endswith(".py"):
            src = Path(spec.origin).resolve()
        mod = importlib.import_module(src_arg)
        return src, mod
    src = Path(src_arg).resolve()
    return src, _load_py_file(src)


def _scan_api_contract(entry: Path, *, project_root_override: str | None = None) -> None:
    if not entry.is_file():
        return
    root = Path(project_root_override).resolve() if project_root_override else nearest_project_root(entry)
    files = collect_local_python_graph(entry.resolve(), project_root=root)
    diags = []
    for f in files:
        diags.extend(scan_file(f, stage="api-contract"))
    if not diags:
        return
    for d in diags:
        print(render_diagnostic(d), file=sys.stderr)
    raise SystemExit(f"api contract check failed: {len(diags)} violation(s)")


def _cmd_emit(args: argparse.Namespace) -> int:
    src_arg = args.python_file
    out = Path(args.output)
    src, mod = _resolve_emit_source(src_arg)
    if src is not None:
        _scan_api_contract(src, project_root_override=args.project_root)
    if not hasattr(mod, "build"):
        raise SystemExit(f"{src_arg} must define a v3 entrypoint: `@module def build(m: Circuit, ...)`")
    build = getattr(mod, "build")

    if not callable(build):
        raise SystemExit("build must be a callable @module entrypoint: `def build(m: Circuit, ...)`")

    sig = inspect.signature(build)
    params = list(sig.parameters.values())
    if not params:
        raise SystemExit("build must use v3 JIT entry semantics: `@module def build(m: Circuit, ...)`")

    # Collect JIT-time parameters from defaults.
    jit_params: dict[str, object] = {}
    missing: list[str] = []
    for p in params[1:]:
        if p.default is inspect._empty:
            missing.append(p.name)
        else:
            jit_params[p.name] = p.default
    if missing:
        raise SystemExit(
            f"build() is treated as a JIT design function but missing default values for: {', '.join(missing)}"
        )

    # Apply CLI overrides.
    for spec in args.param:
        if "=" not in spec:
            raise SystemExit(f"--param expects name=value, got: {spec!r}")
        name, raw = spec.split("=", 1)
        name = name.strip()
        raw = raw.strip()
        if not name:
            raise SystemExit(f"--param expects name=value, got: {spec!r}")
        if name not in jit_params:
            raise SystemExit(f"unknown JIT parameter: {name!r} (available: {', '.join(jit_params.keys())})")
        try:
            val = ast.literal_eval(raw)
        except Exception:
            val = raw
        jit_params[name] = val

    top_name = _default_top_name(src if src is not None else Path(src_arg.replace(".", "/") + ".py"))
    override = getattr(build, "__pycircuit_name__", None)
    if isinstance(override, str) and override.strip():
        top_name = override.strip()
    try:
        design = compile(build, name=top_name, **jit_params)
    except (DesignError, JitError) as e:
        raise SystemExit(f"design compile failed: {e}") from e

    if isinstance(design, Design):
        out.write_text(design.emit_mlir(), encoding="utf-8")
        return 0

    raise SystemExit("internal error: compile did not return a Design")
    return 0


def _detect_pycc() -> Path:
    env = os.environ.get("PYCC")
    if env:
        p = Path(env)
        if p.is_file() and os.access(p, os.X_OK):
            return p
        raise SystemExit(f"PYCC is set but not executable: {p}")

    # Prefer in-tree builds when running from the repo.
    root = Path(__file__).resolve().parents[3]
    candidates = [
        root / "build-top" / "bin" / "pycc",
        root / "build" / "bin" / "pycc",
        root / "compiler" / "mlir" / "build2" / "bin" / "pycc",
        root / "compiler" / "mlir" / "build" / "bin" / "pycc",
    ]
    for c in candidates:
        if c.is_file() and os.access(c, os.X_OK):
            return c

    found = shutil.which("pycc")
    if found:
        return Path(found)

    raise SystemExit("missing pycc (set PYCC=... or build it with: flows/scripts/pyc build)")


def _as_int_width(ty: str) -> int:
    if ty == "!pyc.clock" or ty == "!pyc.reset":
        return 1
    if not ty.startswith("i"):
        raise SystemExit(f"unsupported port type for TB generation: {ty!r}")
    return int(ty[1:])


def _collect_build(mod: object, src: Path, args: argparse.Namespace) -> Module | Design:
    if not hasattr(mod, "build"):
        raise SystemExit(f"{src} must define a v3 entrypoint: `@module def build(m: Circuit, ...)`")
    build = getattr(mod, "build")

    if not callable(build):
        raise SystemExit("build must be a callable @module entrypoint: `def build(m: Circuit, ...)`")

    sig = inspect.signature(build)
    params = list(sig.parameters.values())
    if not params:
        raise SystemExit("build must use v3 JIT entry semantics: `@module def build(m: Circuit, ...)`")

    # Collect JIT-time parameters from defaults.
    jit_params: dict[str, object] = {}
    missing: list[str] = []
    for p in params[1:]:
        if p.default is inspect._empty:
            missing.append(p.name)
        else:
            jit_params[p.name] = p.default
    if missing:
        raise SystemExit(
            f"build() is treated as a JIT design function but missing default values for: {', '.join(missing)}"
        )

    # Apply CLI overrides.
    for spec in args.param:
        if "=" not in spec:
            raise SystemExit(f"--param expects name=value, got: {spec!r}")
        name, raw = spec.split("=", 1)
        name = name.strip()
        raw = raw.strip()
        if not name:
            raise SystemExit(f"--param expects name=value, got: {spec!r}")
        if name not in jit_params:
            raise SystemExit(f"unknown JIT parameter: {name!r} (available: {', '.join(jit_params.keys())})")
        try:
            val = ast.literal_eval(raw)
        except Exception:
            val = raw
        jit_params[name] = val

    top_name = _default_top_name(src)
    override = getattr(build, "__pycircuit_name__", None)
    if isinstance(override, str) and override.strip():
        top_name = override.strip()
    try:
        return compile(build, name=top_name, **jit_params)
    except (DesignError, JitError) as e:
        raise SystemExit(f"design compile failed: {e}") from e


class _TopIface:
    def __init__(self, *, sym: str, in_raw: list[str], in_tys: list[str], out_raw: list[str], out_tys: list[str]) -> None:
        self.sym = str(sym)
        self.in_raw = list(in_raw)
        self.in_tys = list(in_tys)
        self.out_raw = list(out_raw)
        self.out_tys = list(out_tys)

        all_raw = [*self.in_raw, *self.out_raw]
        if len(set(all_raw)) != len(all_raw):
            raise SystemExit("TB generation requires unique port names across inputs and outputs")

        used: dict[str, int] = {}
        all_names: list[str] = []
        for r in all_raw:
            base = _sanitize_id(r)
            n = used.get(base, 0) + 1
            used[base] = n
            all_names.append(base if n == 1 else f"{base}_{n}")
        self.in_names = all_names[: len(self.in_raw)]
        self.out_names = all_names[len(self.in_raw) :]

        self._by_raw: dict[str, tuple[str, str, str]] = {}
        for rn, sn, ty in zip(self.in_raw, self.in_names, self.in_tys):
            self._by_raw[rn] = ("in", sn, ty)
        for rn, sn, ty in zip(self.out_raw, self.out_names, self.out_tys):
            self._by_raw[rn] = ("out", sn, ty)

    def resolve(self, raw_name: str) -> tuple[str, str, str]:
        r = str(raw_name).strip()
        if r not in self._by_raw:
            raise SystemExit(f"unknown DUT port referenced by TB: {r!r}")
        return self._by_raw[r]


def _top_iface(design: Module | Design) -> _TopIface:
    if isinstance(design, Design):
        cm = design.lookup(design.top)
        if cm is None:
            raise SystemExit(f"internal: missing top module {design.top!r} in Design")
        return _TopIface(
            sym=cm.sym_name,
            in_raw=list(cm.arg_names),
            in_tys=list(cm.arg_types),
            out_raw=list(cm.result_names),
            out_tys=list(cm.result_types),
        )

    in_raw = [n for n, _ in getattr(design, "_args", [])]  # noqa: SLF001
    in_tys = [sig.ty for _, sig in getattr(design, "_args", [])]  # noqa: SLF001
    out_raw = [n for n, _ in getattr(design, "_results", [])]  # noqa: SLF001
    out_tys = [sig.ty for _, sig in getattr(design, "_results", [])]  # noqa: SLF001
    return _TopIface(sym=str(getattr(design, "name", "Top")), in_raw=in_raw, in_tys=in_tys, out_raw=out_raw, out_tys=out_tys)


def _render_tb_cpp(iface: _TopIface, t: Tb) -> str:
    has_clocks = bool(t.clocks)
    has_reset = t.reset_spec is not None
    if has_reset and not has_clocks:
        raise SystemExit("tb() with reset requires at least one clock via t.clock(...)")

    top = _sanitize_id(iface.sym)
    hdr = f"{iface.sym}.hpp"

    def mask_value(v: int | bool, width: int) -> int:
        if isinstance(v, bool):
            vv = 1 if v else 0
        else:
            vv = int(v)
        if width <= 0:
            raise SystemExit("internal: invalid width")
        return vv & ((1 << width) - 1)

    def wire_literal(v: int | bool, width: int) -> str:
        vv = mask_value(v, width)
        words = (width + 63) // 64
        raw_words = []
        for i in range(words):
            raw_words.append(f"0x{((vv >> (64 * i)) & ((1 << 64) - 1)):x}ull")
        return f"pyc::cpp::Wire<{width}>({{{', '.join(raw_words)}}})"

    # Group actions by cycle for compact emission.
    drives_by: dict[int, list[tuple[str, int | bool, str]]] = {}
    expects_pre_by: dict[int, list[tuple[str, int | bool, str | None, str]]] = {}
    expects_post_by: dict[int, list[tuple[str, int | bool, str | None, str]]] = {}
    prints_at: dict[int, list[tuple[str, list[tuple[str, str, int]]]]] = {}
    prints_every: list[tuple[str, int, int, list[tuple[str, str, int]]]] = []
    for d in t.drives:
        dir_, sn, ty = iface.resolve(d.port)
        if dir_ != "in":
            raise SystemExit(f"drive() requires input port, got output: {d.port!r}")
        drives_by.setdefault(int(d.at), []).append((sn, d.value, ty))
    for e in t.expects:
        _dir, sn, ty = iface.resolve(e.port)
        ph = str(getattr(e, "phase", "post")).strip().lower()
        if ph == "pre":
            expects_pre_by.setdefault(int(e.at), []).append((sn, e.value, e.msg, ty))
        else:
            expects_post_by.setdefault(int(e.at), []).append((sn, e.value, e.msg, ty))

    for p in getattr(t, "prints", []):
        fmt = str(p.fmt)
        port_specs: list[tuple[str, str, int]] = []
        for raw in p.ports:
            _dir, sn, ty = iface.resolve(raw)
            w = _as_int_width(ty)
            if w > 64:
                raise SystemExit(f"print() for i{w} not supported in C++ TB generator (prototype limitation)")
            port_specs.append((str(raw), sn, w))
        if p.at is not None:
            prints_at.setdefault(int(p.at), []).append((fmt, port_specs))
        else:
            st = 0 if p.start is None else int(p.start)
            ev = 1 if p.every is None else int(p.every)
            prints_every.append((fmt, st, ev, port_specs))

    rand_specs: list[tuple[str, int, int, int, int]] = []
    if t.random_streams:
        used_ports: set[str] = set()
        for r in t.random_streams:
            dir_, sn, ty = iface.resolve(r.port)
            if dir_ != "in":
                raise SystemExit(f"random() requires input port, got output: {r.port!r}")
            if ty == "!pyc.clock" or ty == "!pyc.reset":
                raise SystemExit(f"random() cannot target clock/reset ports: {r.port!r}")
            if sn in used_ports:
                raise SystemExit(f"duplicate random() stream for port: {r.port!r}")
            used_ports.add(sn)
            w = _as_int_width(ty)
            if w > 64:
                raise SystemExit(f"random() for i{w} not supported in C++ TB generator (prototype limitation)")
            rand_specs.append((sn, w, int(r.seed), int(r.start), int(r.every)))

    clk_sn = ""
    rst_sn = ""
    ca = 0
    cd = 0
    if has_clocks:
        clk = t.clocks[0].port
        _, clk_sn, _clk_ty = iface.resolve(clk)
    if has_reset:
        rst = t.reset_spec.port
        _, rst_sn, _rst_ty = iface.resolve(rst)
        ca = int(t.reset_spec.cycles_asserted)
        cd = int(t.reset_spec.cycles_deasserted)

    lines: list[str] = []
    lines.append("// Generated by pycircuit (prototype)\n")
    lines.append("#include <cstdint>\n")
    lines.append("#include <cstdlib>\n")
    lines.append("#include <filesystem>\n")
    lines.append("#include <iostream>\n\n")
    lines.append("#include <cpp/pyc_tb.hpp>\n\n")
    lines.append(f"#include \"{hdr}\"\n\n")
    lines.append("using pyc::cpp::Testbench;\n\n")
    lines.append("int main() {\n")
    lines.append(f"  pyc::gen::{top} dut;\n")
    lines.append(f"  Testbench<pyc::gen::{top}> tb(dut);\n\n")
    if rand_specs:
        lines.append("  // Random streams (deterministic).\n")
        for sn, _w, seed, _st, _ev in rand_specs:
            seed64 = int(seed) & ((1 << 64) - 1)
            lines.append(f"  std::uint64_t rng_{sn} = 0x{seed64:x}ull;\n")
        lines.append("\n")
    lines.append("  // Optional traces.\n")
    lines.append("  const char *trace_dir_env = std::getenv(\"PYC_TRACE_DIR\");\n")
    lines.append(
        "  std::filesystem::path out_dir = trace_dir_env ? std::filesystem::path(trace_dir_env) : std::filesystem::path(\".\");\n"
    )
    lines.append(f"  out_dir /= \"tb_{iface.sym}\";\n")
    lines.append("  std::filesystem::create_directories(out_dir);\n")
    lines.append(f"  tb.enableVcd((out_dir / \"tb_{iface.sym}.vcd\").string(), /*top=*/\"tb_{iface.sym}\");\n")
    for sn in [*iface.in_names, *iface.out_names]:
        lines.append(f"  tb.vcdTrace(dut.{sn}, \"{sn}\");\n")
    lines.append("\n")

    if has_clocks:
        for c in t.clocks:
            dir_, sn, _ = iface.resolve(c.port)
            if dir_ != "in":
                raise SystemExit(f"clock must be an input port, got output: {c.port!r}")
            lines.append(
                f"  tb.addClock(dut.{sn}, /*halfPeriodSteps=*/{int(c.half_period_steps)}, /*phaseSteps=*/{int(c.phase_steps)}, /*startHigh=*/{str(bool(c.start_high)).lower()});\n"
            )
    if has_reset:
        lines.append(f"  tb.reset(dut.{rst_sn}, /*cyclesAsserted=*/{int(ca)}, /*cyclesDeasserted=*/{int(cd)});\n\n")

    lines.append(f"  const std::uint64_t timeout_cycles = {int(t.timeout_cycles)}ull;\n")
    lines.append("  bool ok = false;\n")
    lines.append("  for (std::uint64_t cyc = 0; cyc < timeout_cycles; ++cyc) {\n")

    if rand_specs:
        lines.append("    // Random drives for this cycle (applied before explicit drives).\n")
        for sn, w, _seed, st, ev in rand_specs:
            mask = (1 << w) - 1 if w < 64 else (1 << 64) - 1
            lines.append(
                f"    if (cyc >= {int(st)}ull && ((cyc - {int(st)}ull) % {int(ev)}ull) == 0ull) {{\n"
                f"      rng_{sn} = rng_{sn} * 6364136223846793005ull + 1ull;\n"
                f"      dut.{sn} = pyc::cpp::Wire<{w}>(0x{mask:x}ull & rng_{sn});\n"
                f"    }}\n"
            )
        lines.append("\n")

    if drives_by:
        lines.append("    switch (cyc) {\n")
        for cyc in sorted(drives_by.keys()):
            lines.append(f"    case {cyc}:\n")
            for sn, val, ty in drives_by[cyc]:
                w = _as_int_width(ty)
                lines.append(f"      dut.{sn} = {wire_literal(val, w)};\n")
            lines.append("      break;\n")
        lines.append("    default: break;\n")
        lines.append("    }\n")

    if expects_pre_by:
        lines.append("    // Pre-step expects for this cycle.\n")
        lines.append("    switch (cyc) {\n")
        for cyc in sorted(expects_pre_by.keys()):
            lines.append(f"    case {cyc}: {{\n")
            for sn, val, msg, ty in expects_pre_by[cyc]:
                w = _as_int_width(ty)
                vv = mask_value(val, w)
                exp = wire_literal(val, w)
                m = msg if msg is not None else f"{sn} mismatch"
                if w == 1:
                    lines.append(
                        f"      if (dut.{sn}.value() != {vv}u) {{ std::cerr << \"ERROR(pre): {m}: got=\" << dut.{sn}.value() << \" exp={vv}\\n\"; return 1; }}\n"
                    )
                elif w <= 64:
                    lines.append(
                        f"      if (dut.{sn}.value() != {vv}u) {{ std::cerr << \"ERROR(pre): {m}: got=0x\" << std::hex << dut.{sn}.value() << \" exp=0x{vv:x}\" << std::dec << \"\\n\"; return 1; }}\n"
                    )
                else:
                    lines.append(f"      if (!(dut.{sn} == {exp})) {{ std::cerr << \"ERROR(pre): {m}\\n\"; return 1; }}\n")
            lines.append("      break; }\n")
        lines.append("    default: break;\n")
        lines.append("    }\n")

    if has_clocks:
        lines.append("    tb.runCyclesAuto(1);\n")
    else:
        lines.append("    tb.runSteps(1);\n")

    if expects_post_by:
        lines.append("    // Post-step expects for this cycle.\n")
        lines.append("    switch (cyc) {\n")
        for cyc in sorted(expects_post_by.keys()):
            lines.append(f"    case {cyc}: {{\n")
            for sn, val, msg, ty in expects_post_by[cyc]:
                w = _as_int_width(ty)
                vv = mask_value(val, w)
                exp = wire_literal(val, w)
                m = msg if msg is not None else f"{sn} mismatch"
                # Print decimal for i1, hex for <=64 wider signals.
                if w == 1:
                    lines.append(
                        f"      if (dut.{sn}.value() != {vv}u) {{ std::cerr << \"ERROR: {m}: got=\" << dut.{sn}.value() << \" exp={vv}\\n\"; return 1; }}\n"
                    )
                elif w <= 64:
                    lines.append(
                        f"      if (dut.{sn}.value() != {vv}u) {{ std::cerr << \"ERROR: {m}: got=0x\" << std::hex << dut.{sn}.value() << \" exp=0x{vv:x}\" << std::dec << \"\\n\"; return 1; }}\n"
                    )
                else:
                    lines.append(f"      if (!(dut.{sn} == {exp})) {{ std::cerr << \"ERROR: {m}\\n\"; return 1; }}\n")
            lines.append("      break; }\n")
        lines.append("    default: break;\n")
        lines.append("    }\n")

    if prints_at or prints_every:
        if prints_at:
            lines.append("    // Per-cycle prints.\n")
            lines.append("    switch (cyc) {\n")
            for cyc in sorted(prints_at.keys()):
                lines.append(f"    case {cyc}: {{\n")
                for fmt, ports in prints_at[cyc]:
                    msg_lit = json.dumps(f" {fmt}")
                    lines.append(f"      std::cerr << \"[tb] cyc=\" << cyc << {msg_lit}")
                    for raw, sn, w in ports:
                        raw_lit = json.dumps(f" {raw}=")
                        if w == 1:
                            lines.append(f" << {raw_lit} << dut.{sn}.value()")
                        else:
                            lines.append(f" << {raw_lit} << \"0x\" << std::hex << dut.{sn}.value() << std::dec")
                    lines.append(" << \"\\n\";\n")
                lines.append("      break; }\n")
            lines.append("    default: break;\n")
            lines.append("    }\n")
        if prints_every:
            lines.append("    // Periodic prints.\n")
            for fmt, st, ev, ports in prints_every:
                msg_lit = json.dumps(f" {fmt}")
                lines.append(f"    if (cyc >= {st}ull && ((cyc - {st}ull) % {ev}ull) == 0ull) {{\n")
                lines.append(f"      std::cerr << \"[tb] cyc=\" << cyc << {msg_lit}")
                for raw, sn, w in ports:
                    raw_lit = json.dumps(f" {raw}=")
                    if w == 1:
                        lines.append(f" << {raw_lit} << dut.{sn}.value()")
                    else:
                        lines.append(f" << {raw_lit} << \"0x\" << std::hex << dut.{sn}.value() << std::dec")
                lines.append(" << \"\\n\";\n")
                lines.append("    }\n")

    if t.finish_cycle is not None:
        lines.append(f"    if (cyc == {int(t.finish_cycle)}ull) {{ ok = true; break; }}\n")

    lines.append("  }\n")
    lines.append("  if (!ok) { std::cerr << \"TIMEOUT\\n\"; return 1; }\n")
    lines.append("  std::cerr << \"OK\\n\";\n")
    lines.append("  return 0;\n")
    lines.append("}\n")
    return "".join(lines)


def _render_tb_sv(iface: _TopIface, t: Tb) -> str:
    has_clocks = bool(t.clocks)
    has_reset = t.reset_spec is not None
    if has_reset and not has_clocks:
        raise SystemExit("tb() with reset requires at least one clock via t.clock(...)")

    top = str(iface.sym)
    mod_name = top  # func sym name is already a valid Verilog identifier in this repo.

    def sv_lit(width: int, v: int | bool) -> str:
        if isinstance(v, bool):
            vv = 1 if v else 0
        else:
            vv = int(v)
        if width <= 0:
            raise SystemExit("internal: invalid width")
        vv &= (1 << width) - 1
        if width == 1:
            return f"1'b{vv}"
        return f"{width}'h{vv:x}"

    def decl(name: str, ty: str) -> str:
        w = _as_int_width(ty)
        if w == 1:
            return f"  logic {name};\n"
        return f"  logic [{w - 1}:0] {name};\n"

    drives_by: dict[int, list[tuple[str, int | bool, str]]] = {}
    expects_pre_by: dict[int, list[tuple[str, int | bool, str | None, str]]] = {}
    expects_post_by: dict[int, list[tuple[str, int | bool, str | None, str]]] = {}
    prints_at: dict[int, list[tuple[str, list[str]]]] = {}
    prints_every: list[tuple[str, int, int, list[str]]] = []
    for d in t.drives:
        dir_, sn, ty = iface.resolve(d.port)
        if dir_ != "in":
            raise SystemExit(f"drive() requires input port, got output: {d.port!r}")
        drives_by.setdefault(int(d.at), []).append((sn, d.value, ty))
    for e in t.expects:
        _dir, sn, ty = iface.resolve(e.port)
        ph = str(getattr(e, "phase", "post")).strip().lower()
        if ph == "pre":
            expects_pre_by.setdefault(int(e.at), []).append((sn, e.value, e.msg, ty))
        else:
            expects_post_by.setdefault(int(e.at), []).append((sn, e.value, e.msg, ty))
    for p in getattr(t, "prints", []):
        fmt = str(p.fmt)
        ports = []
        for raw in p.ports:
            _dir, sn, _ty = iface.resolve(raw)
            ports.append(sn)
        if p.at is not None:
            prints_at.setdefault(int(p.at), []).append((fmt, ports))
        else:
            st = 0 if p.start is None else int(p.start)
            ev = 1 if p.every is None else int(p.every)
            prints_every.append((fmt, st, ev, ports))

    rand_specs: list[tuple[str, int, int, int, int]] = []
    if t.random_streams:
        used_ports: set[str] = set()
        for r in t.random_streams:
            dir_, sn, ty = iface.resolve(r.port)
            if dir_ != "in":
                raise SystemExit(f"random() requires input port, got output: {r.port!r}")
            if ty == "!pyc.clock" or ty == "!pyc.reset":
                raise SystemExit(f"random() cannot target clock/reset ports: {r.port!r}")
            if sn in used_ports:
                raise SystemExit(f"duplicate random() stream for port: {r.port!r}")
            used_ports.add(sn)
            w = _as_int_width(ty)
            if w > 64:
                raise SystemExit(f"random() for i{w} not supported in SV TB generator (prototype limitation)")
            rand_specs.append((sn, w, int(r.seed), int(r.start), int(r.every)))

    clk_sn = ""
    rst_sn = ""
    ca = 0
    cd = 0
    if has_clocks:
        clk = t.clocks[0].port
        _, clk_sn, _clk_ty = iface.resolve(clk)
    if has_reset:
        rst = t.reset_spec.port
        _, rst_sn, _rst_ty = iface.resolve(rst)
        ca = int(t.reset_spec.cycles_asserted)
        cd = int(t.reset_spec.cycles_deasserted)

    lines: list[str] = []
    lines.append("// Generated by pycircuit (prototype)\n")
    lines.append("`timescale 1ns/1ps\n\n")
    lines.append(f"module tb_{top};\n")
    lines.append("  /* verilator lint_off UNUSEDSIGNAL */\n")

    for n, ty in zip(iface.in_names, iface.in_tys):
        lines.append(decl(n, ty))
    for n, ty in zip(iface.out_names, iface.out_tys):
        lines.append(decl(n, ty))
    if rand_specs:
        lines.append("\n")
        lines.append("  // Random stream state.\n")
        for sn, _w, _seed, _st, _ev in rand_specs:
            lines.append(f"  longint unsigned rng_{sn};\n")
    lines.append("  integer timeout_cycles;\n")
    lines.append("  integer cyc;\n")
    lines.append("  logic __pyc_tb_active;\n")
    lines.append("  initial __pyc_tb_active = 1'b0;\n")
    lines.append("  logic __pyc_tb_done;\n")
    lines.append("  initial __pyc_tb_done = 1'b0;\n")
    lines.append("\n")

    lines.append(f"  {mod_name} dut (\n")
    conns = [f"    .{sn}({sn})" for sn in [*iface.in_names, *iface.out_names]]
    lines.append(",\n".join(conns))
    lines.append("\n  );\n\n")

    # Clock generation: currently only supports the first clock.
    if has_clocks:
        hp = int(t.clocks[0].half_period_steps)
        if hp != 1:
            lines.append("  // NOTE: half_period_steps != 1 is approximated by scaling delay.\n")
        lines.append("  initial begin\n")
        lines.append(f"    {clk_sn} = {1 if (t.clocks and t.clocks[0].start_high) else 0};\n")
        lines.append("  end\n")
        lines.append(f"  always #{hp} {clk_sn} = ~{clk_sn};\n\n")

    # Main stimulus loop.
    lines.append("  initial begin : __pyc_tb_main\n")
    # Initialize all driven inputs to 0.
    for sn, ty in zip(iface.in_names, iface.in_tys):
        if sn == clk_sn:
            continue
        w = _as_int_width(ty)
        lines.append(f"    {sn} = {w}'d0;\n")
    lines.append("    __pyc_tb_active = 1'b0;\n")
    lines.append("    __pyc_tb_done = 1'b0;\n")
    if rand_specs:
        lines.append("\n")
        lines.append("    // Random stream seeds.\n")
        for sn, _w, seed, _st, _ev in rand_specs:
            seed64 = int(seed) & ((1 << 64) - 1)
            lines.append(f"    rng_{sn} = 64'h{seed64:016x};\n")
    lines.append("\n")
    if has_reset:
        lines.append(f"    {rst_sn} = 1'b1;\n")
        lines.append(f"    repeat ({int(ca)}) @(posedge {clk_sn});\n")
        lines.append(f"    {rst_sn} = 1'b0;\n")
        lines.append(f"    repeat ({int(cd)}) @(posedge {clk_sn});\n\n")
    if has_clocks:
        # Align stimulus so cycle 0 drives are applied on a negedge, avoiding races
        # with posedge-triggered sequential logic in the DUT.
        lines.append(f"    @(negedge {clk_sn});\n\n")

    lines.append(f"    timeout_cycles = {int(t.timeout_cycles)};\n")
    lines.append("    for (cyc = 0; cyc < timeout_cycles; cyc = cyc + 1) begin\n")

    if rand_specs:
        lines.append("      // Random drives for this cycle (applied before explicit drives).\n")
        for sn, w, _seed, st, ev in rand_specs:
            hi = 63 if w >= 64 else (w - 1)
            lines.append(f"      if (cyc >= {int(st)} && (((cyc - {int(st)}) % {int(ev)}) == 0)) begin\n")
            lines.append("        // LCG: state = state * 6364136223846793005 + 1.\n")
            lines.append(f"        rng_{sn} = (rng_{sn} * 64'd6364136223846793005) + 64'd1;\n")
            lines.append(f"        {sn} = rng_{sn}[{hi}:0];\n")
            lines.append("      end\n")
        lines.append("\n")

    if drives_by:
        lines.append("      // Drives for this cycle (applied before posedge).\n")
        lines.append("      unique case (cyc)\n")
        for cyc in sorted(drives_by.keys()):
            lines.append(f"        {cyc}: begin\n")
            for sn, val, ty in drives_by[cyc]:
                w = _as_int_width(ty)
                lines.append(f"          {sn} = {sv_lit(w, val)};\n")
            lines.append("        end\n")
        lines.append("        default: begin end\n")
        lines.append("      endcase\n")

    if expects_pre_by:
        lines.append("      // Pre-step expects for this cycle (checked before posedge).\n")
        lines.append("      unique case (cyc)\n")
        for cyc in sorted(expects_pre_by.keys()):
            lines.append(f"        {cyc}: begin\n")
            for sn, val, msg, ty in expects_pre_by[cyc]:
                w = _as_int_width(ty)
                m = msg if msg is not None else f"{sn} mismatch"
                lines.append(f"          if ({sn} !== {sv_lit(w, val)}) $fatal(1, \"PRE: {m}\");\n")
            lines.append("        end\n")
        lines.append("        default: begin end\n")
        lines.append("      endcase\n")

    if has_clocks:
        lines.append(f"      @(posedge {clk_sn});\n")
        lines.append(f"      @(negedge {clk_sn});\n")
    else:
        lines.append("      #1;\n")
    lines.append("      __pyc_tb_active = 1'b1;\n")

    if expects_post_by:
        lines.append("      // Expects for this cycle (checked after posedge updates).\n")
        lines.append("      unique case (cyc)\n")
        for cyc in sorted(expects_post_by.keys()):
            lines.append(f"        {cyc}: begin\n")
            for sn, val, msg, ty in expects_post_by[cyc]:
                w = _as_int_width(ty)
                m = msg if msg is not None else f"{sn} mismatch"
                lines.append(f"          if ({sn} !== {sv_lit(w, val)}) $fatal(1, \"{m}\");\n")
            lines.append("        end\n")
        lines.append("        default: begin end\n")
        lines.append("      endcase\n")

    if prints_at:
        lines.append("      // Per-cycle prints.\n")
        lines.append("      unique case (cyc)\n")
        for cyc in sorted(prints_at.keys()):
            lines.append(f"        {cyc}: begin\n")
            for fmt, ports in prints_at[cyc]:
                esc = str(fmt).replace("\\", "\\\\").replace("\"", "\\\"")
                if ports:
                    suffix = "".join(f" {p}=%0h" for p in ports)
                    args = ", ".join(["cyc", *ports])
                    lines.append(f"          $display(\"[tb] cyc=%0d {esc}{suffix}\", {args});\n")
                else:
                    lines.append(f"          $display(\"[tb] cyc=%0d {esc}\", cyc);\n")
            lines.append("        end\n")
        lines.append("        default: begin end\n")
        lines.append("      endcase\n")

    if prints_every:
        lines.append("      // Periodic prints.\n")
        for fmt, st, ev, ports in prints_every:
            esc = str(fmt).replace("\\", "\\\\").replace("\"", "\\\"")
            lines.append(f"      if (cyc >= {st} && (((cyc - {st}) % {ev}) == 0)) begin\n")
            if ports:
                suffix = "".join(f" {p}=%0h" for p in ports)
                args = ", ".join(["cyc", *ports])
                lines.append(f"        $display(\"[tb] cyc=%0d {esc}{suffix}\", {args});\n")
            else:
                lines.append(f"        $display(\"[tb] cyc=%0d {esc}\", cyc);\n")
            lines.append("      end\n")

    if t.finish_cycle is not None:
        lines.append(f"      if (cyc == {int(t.finish_cycle)}) begin\n")
        lines.append("        __pyc_tb_done = 1'b1;\n")
        lines.append("        $display(\"OK\");\n")
        lines.append("        $finish;\n")
        lines.append("        disable __pyc_tb_main;\n")
        lines.append("      end\n")

    lines.append("    end\n")
    if t.finish_cycle is None:
        lines.append("    if (!__pyc_tb_done) $fatal(1, \"TIMEOUT\");\n")
    lines.append("  end\n\n")

    # SVA assertions.
    if t.sva_asserts:
        if not has_clocks:
            raise SystemExit("sva_assert requires t.clock(...) in testbench")
        lines.append("  // SVA assertions.\n")
        for i, a in enumerate(t.sva_asserts):
            nm = a.name or f"sva_{i}"
            clk_dir, clk_port, _ = iface.resolve(a.clock)
            if clk_dir != "in":
                raise SystemExit(f"sva_assert clock must be an input port, got output: {a.clock!r}")
            pv = f"__pyc_sva_past_valid_{i}"
            # Guard against `$past` being undefined in the first sampled cycle by
            # generating a per-assertion past-valid bit.
            lines.append(f"  logic {pv};\n")
            lines.append(f"  initial {pv} = 1'b0;\n")
            disable_terms = ["!__pyc_tb_active"]
            if a.reset:
                rst_dir, rst_port, _ = iface.resolve(a.reset)
                if rst_dir != "in":
                    raise SystemExit(f"sva_assert reset must be an input port, got output: {a.reset!r}")
                disable_terms.insert(0, rst_port)
                lines.append(f"  always_ff @(posedge {clk_port}) begin\n")
                lines.append(f"    if ({rst_port}) {pv} <= 1'b0; else {pv} <= 1'b1;\n")
                lines.append("  end\n")
            else:
                lines.append(f"  always_ff @(posedge {clk_port}) begin\n")
                lines.append(f"    {pv} <= 1'b1;\n")
                lines.append("  end\n")
            rst_expr = f" disable iff ({' || '.join(disable_terms)})"
            msg = a.msg or f"SVA {nm} failed"
            expr = f"(!{pv}) || ({a.expr})"
            # Sample on negedge so assertions observe values after posedge-triggered
            # sequential updates in common designs.
            lines.append(
                f"  assert property (@(negedge {clk_port}){rst_expr} {expr}) else $fatal(1, \"{msg}\");\n"
            )
        lines.append("\n")

    lines.append("  /* verilator lint_on UNUSEDSIGNAL */\n")
    lines.append("endmodule\n")
    return "".join(lines)


def _write_text_atomic(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(text, encoding="utf-8")
    os.replace(tmp, path)


def _run_backend_job(job: tuple[str, list[str]]) -> tuple[str, str]:
    name, cmd = job
    proc = subprocess.run(cmd, text=True, capture_output=True)
    if proc.returncode != 0:
        err = proc.stderr.strip()
        out = proc.stdout.strip()
        raise RuntimeError(f"backend job {name!r} failed ({proc.returncode})\ncmd: {' '.join(cmd)}\n{err}\n{out}")
    return (name, proc.stdout.strip())


def _emit_multi_pyc_artifacts(design: Design, *, out_dir: Path) -> tuple[Path, dict[str, Any], dict[str, Path]]:
    module_map = design.emit_module_mlir_map()
    module_dir = out_dir / "device" / "modules"
    module_dir.mkdir(parents=True, exist_ok=True)

    module_paths: dict[str, Path] = {}
    for sym in sorted(module_map.keys()):
        p = module_dir / f"{sym}.pyc"
        _write_text_atomic(p, module_map[sym])
        module_paths[sym] = p

    manifest = design.emit_project_manifest(module_dir_rel="device/modules")
    manifest_path = out_dir / "project_manifest.json"
    _write_text_atomic(manifest_path, json.dumps(manifest, sort_keys=True, indent=2) + "\n")
    return (manifest_path, manifest, module_paths)


def _collect_testbench_payload(mod: object, iface: _TopIface) -> tuple[str, str]:
    if not hasattr(mod, "tb") or not callable(getattr(mod, "tb")):
        raise SystemExit("build requires `@testbench def tb(t: Tb): ...`")
    tb_fn = getattr(mod, "tb")
    if not bool(getattr(tb_fn, "__pycircuit_testbench__", False)):
        raise SystemExit("build requires tb(...) to be decorated with `@testbench`")
    t = Tb()
    try:
        tb_fn(t)
    except TbError as e:
        raise SystemExit(f"tb() failed: {e}") from e
    payload_obj = testbench_payload_from_tb(
        top_symbol=iface.sym,
        in_raw=list(iface.in_raw),
        in_tys=list(iface.in_tys),
        out_raw=list(iface.out_raw),
        out_tys=list(iface.out_tys),
        tb=t,
    )
    tb_name = getattr(tb_fn, "__pycircuit_module_name__", None)
    if not isinstance(tb_name, str) or not tb_name.strip():
        tb_name = f"tb_{iface.sym}"
    tb_name = _sanitize_id(str(tb_name))
    payload = payload_obj.as_dict()
    payload["tb_name"] = str(tb_name)
    payload["cpp_text"] = _render_tb_cpp(iface, t)
    payload["sv_text"] = _render_tb_sv(iface, t)
    return (str(tb_name), json.dumps(payload, sort_keys=True, separators=(",", ":"), ensure_ascii=False))


def _emit_testbench_pyc_file(
    *,
    out_dir: Path,
    tb_name: str,
    payload_json: str,
) -> Path:
    tb_dir = out_dir / "tb"
    tb_dir.mkdir(parents=True, exist_ok=True)
    tb_pyc_path = tb_dir / f"{tb_name}.pyc"
    payload = json.loads(payload_json)
    _write_text_atomic(
        tb_pyc_path,
        emit_testbench_pyc(payload=payload, tb_name=tb_name, frontend_contract=FRONTEND_CONTRACT),
    )
    return tb_pyc_path


def _gather_cpp_sources(cpp_root: Path) -> list[Path]:
    out: list[Path] = []
    for p in sorted(cpp_root.rglob("*.cpp")):
        if p.is_file():
            out.append(p)
    return out


def _gather_cpp_headers(cpp_root: Path) -> list[Path]:
    out: list[Path] = []
    for p in sorted(cpp_root.rglob("*.hpp")):
        if p.is_file():
            out.append(p)
    return out


def _module_hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _canonical_hash(payload: dict[str, Any]) -> str:
    blob = json.dumps(payload, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    return hashlib.sha256(blob).hexdigest()


def _load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def _save_json(path: Path, data: dict[str, Any]) -> None:
    _write_text_atomic(path, json.dumps(data, sort_keys=True, indent=2) + "\n")


def _cmd_build(args: argparse.Namespace) -> int:
    src = Path(args.python_file).resolve()
    out_dir = Path(args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    _scan_api_contract(src, project_root_override=args.project_root)
    mod = _load_py_file(src)
    design = _collect_build(mod, src, args)
    if not isinstance(design, Design):
        raise SystemExit("internal error: expected Design from compile(...)")
    iface = _top_iface(design)

    pycc = _detect_pycc()
    jobs = max(1, int(args.jobs))
    if int(args.logic_depth) <= 0:
        raise SystemExit("--logic-depth must be > 0")
    logic_depth = int(args.logic_depth)

    manifest_path, manifest, module_paths = _emit_multi_pyc_artifacts(design, out_dir=out_dir)

    tb_name, tb_payload_json = _collect_testbench_payload(mod, iface)
    tb_pyc_path = _emit_testbench_pyc_file(out_dir=out_dir, tb_name=tb_name, payload_json=tb_payload_json)
    manifest["testbench"] = {"name": tb_name, "pyc": str(tb_pyc_path.relative_to(out_dir))}

    cache_path = out_dir / ".build_cache.json"
    cache = _load_json(cache_path) if cache_path.is_file() else {"module_hashes": {}, "pycc": str(pycc)}
    old_hashes = dict(cache.get("module_hashes", {}))
    module_hashes: dict[str, str] = {}

    device_cpp_root = out_dir / "device" / "cpp"
    device_v_root = out_dir / "device" / "verilog"
    tb_cpp_out = out_dir / "tb" / f"{tb_name}.cpp"
    tb_sv_out = out_dir / "tb" / f"{tb_name}.sv"
    device_cpp_root.mkdir(parents=True, exist_ok=True)
    device_v_root.mkdir(parents=True, exist_ok=True)

    target = str(args.target)
    do_cpp = target in {"cpp", "both"}
    do_v = target in {"verilator", "both"}

    build_flags = {
        "pycc": str(pycc.resolve()),
        "logic_depth": logic_depth,
        "profile": str(args.profile),
        "target": target,
        "frontend_contract": FRONTEND_CONTRACT,
    }
    build_flags_hash = _canonical_hash(build_flags)
    same_flags = str(cache.get("build_flags_hash", "")) == build_flags_hash

    pycc_jobs: list[tuple[str, list[str]]] = []
    for sym in sorted(module_paths.keys()):
        mp = module_paths[sym]
        h = _module_hash(mp)
        module_hashes[sym] = h
        unchanged = same_flags and old_hashes.get(sym) == h

        cpp_out_dir = device_cpp_root / sym
        cpp_ready = cpp_out_dir.is_dir() and any(cpp_out_dir.glob("*.cpp")) and any(cpp_out_dir.glob("*.hpp"))
        if do_cpp and not (unchanged and cpp_ready):
            cpp_out_dir.mkdir(parents=True, exist_ok=True)
            pycc_jobs.append(
                (
                    f"cpp:{sym}",
                    [
                        str(pycc),
                        str(mp),
                        "--emit=cpp",
                        "--out-dir",
                        str(cpp_out_dir),
                        "--cpp-split=module",
                        f"--logic-depth={logic_depth}",
                    ],
                )
            )

        verilog_out_dir = device_v_root / sym
        verilog_ready = verilog_out_dir.is_dir() and any(verilog_out_dir.glob("*.v"))
        if do_v and not (unchanged and verilog_ready):
            verilog_out_dir.mkdir(parents=True, exist_ok=True)
            pycc_jobs.append(
                (
                    f"verilog:{sym}",
                    [
                        str(pycc),
                        str(mp),
                        "--emit=verilog",
                        "--out-dir",
                        str(verilog_out_dir),
                        f"--logic-depth={logic_depth}",
                    ],
                )
            )

    if do_cpp:
        pycc_jobs.append((f"tb-cpp:{tb_name}", [str(pycc), str(tb_pyc_path), "-cpp", str(tb_cpp_out)]))
    if do_v:
        pycc_jobs.append((f"tb-sv:{tb_name}", [str(pycc), str(tb_pyc_path), "-verilog", str(tb_sv_out)]))

    if pycc_jobs:
        with ProcessPoolExecutor(max_workers=jobs) as pool:
            futs = {pool.submit(_run_backend_job, j): j[0] for j in pycc_jobs}
            for fut in as_completed(futs):
                _ = fut.result()

    if do_cpp:
        cpp_sources = _gather_cpp_sources(device_cpp_root)
        if not cpp_sources:
            raise SystemExit("build(cpp): no generated C++ sources found")
        if not tb_cpp_out.is_file():
            raise SystemExit(f"build(cpp): missing generated TB C++ source: {tb_cpp_out}")
        cpp_headers = _gather_cpp_headers(device_cpp_root)
        include_dirs: list[str] = []
        include_dirs.append(str(device_cpp_root))
        include_dirs.append(str(Path(__file__).resolve().parents[3] / "runtime"))
        for p in [*cpp_sources, *cpp_headers]:
            parent = str(p.parent)
            if parent not in include_dirs:
                include_dirs.append(parent)

        build_manifest = {
            "version": 1,
            "target_name": iface.sym,
            "tb_cpp": str(tb_cpp_out),
            "sources": [str(p) for p in cpp_sources],
            "headers": [str(p) for p in cpp_headers],
            "include_dirs": include_dirs,
            "cxx_standard": "c++17",
            "profile": str(args.profile),
        }
        cpp_manifest = out_dir / "cpp_project_manifest.json"
        _save_json(cpp_manifest, build_manifest)

        gen_script = Path(__file__).resolve().parents[3] / "flows" / "tools" / "gen_cmake_from_manifest.py"
        cmake_src = out_dir / "cpp_build" / "src"
        cmake_build = out_dir / "cpp_build" / "build"
        cmake_src.mkdir(parents=True, exist_ok=True)
        cmake_build.mkdir(parents=True, exist_ok=True)

        subprocess.run(
            [sys.executable, str(gen_script), "--manifest", str(cpp_manifest), "--out-dir", str(cmake_src)],
            check=True,
        )
        build_type = "Release" if str(args.profile) == "release" else "RelWithDebInfo"

        # Windows/MSYS2: `ninja.exe --version` can intermittently fail with
        # STATUS_DLL_INIT_FAILED in subprocesses. Prefer Makefiles here for
        # robustness.
        cmake_cmd = [
            "cmake",
            "-G",
            "Ninja",
            "-S",
            str(cmake_src),
            "-B",
            str(cmake_build),
            f"-DCMAKE_BUILD_TYPE={build_type}",
        ]
        if os.name == "nt":
            cmake_cmd = [
                "cmake",
                "-G",
                "MinGW Makefiles",
                "-S",
                str(cmake_src),
                "-B",
                str(cmake_build),
                f"-DCMAKE_BUILD_TYPE={build_type}",
                "-DCMAKE_MAKE_PROGRAM=mingw32-make",
            ]

        subprocess.run(cmake_cmd, check=True)
        subprocess.run(["cmake", "--build", str(cmake_build), "-j", str(jobs)], check=True)
        manifest["cpp_executable"] = str(cmake_build / "pyc_tb")

    if do_v:
        if not tb_sv_out.is_file():
            raise SystemExit(f"build(verilator): missing generated TB SV source: {tb_sv_out}")
        prim_file: Path | None = None
        verilog_module_sources: list[str] = []
        for p in sorted(device_v_root.rglob("*.v")):
            if not p.is_file():
                continue
            if p.name == "pyc_primitives.v":
                if prim_file is None:
                    prim_file = p
                continue
            verilog_module_sources.append(str(p))
        if not verilog_module_sources:
            raise SystemExit("build(verilator): no generated Verilog sources found")
        verilog_sources = ([str(prim_file)] if prim_file is not None else []) + verilog_module_sources
        verilog_manifest = {
            "version": 1,
            "top": tb_name,
            "tb_sv": str(tb_sv_out),
            "sources": verilog_sources,
            "include_dirs": [str(device_v_root)],
        }
        sim_manifest = out_dir / "verilator_manifest.json"
        _save_json(sim_manifest, verilog_manifest)
        manifest["verilator_manifest"] = str(sim_manifest.relative_to(out_dir))
        if bool(args.run_verilator):
            vbuild = out_dir / "verilator_build"

            # On Windows, MSYS2's `verilator` is typically a script (shebang) and
            # cannot be launched via CreateProcess directly. Prefer the real exe.
            verilator_exe = "verilator"
            if os.name == "nt":
                verilator_exe = (
                    shutil.which("verilator_bin.exe")
                    or shutil.which("verilator_bin")
                    or "verilator_bin.exe"
                )

            # Verilator needs a valid VERILATOR_ROOT on Windows; otherwise it may
            # form mixed /path\\include\\... strings and fail to locate std SV.
            run_env = None
            if os.name == "nt":
                run_env = os.environ.copy()
                vb = shutil.which(str(verilator_exe))
                if vb:
                    prefix = Path(vb).resolve().parents[1]
                    run_env["VERILATOR_ROOT"] = str(prefix / "share" / "verilator")

            cmd = [
                verilator_exe,
                "--binary",
                "-Wall",
                "-Wno-fatal",
                "-Wno-DECLFILENAME",
                "-Wno-UNUSEDSIGNAL",
                "-Wno-WIDTHEXPAND",
                "--quiet",
                # MSYS2/Windows Verilator wrapper does not support --quiet-build.
                "--timing",
                "--trace",
                "--top-module",
                tb_name,
                "--Mdir",
                str(vbuild),
                str(tb_sv_out),
                *verilog_sources,
            ]
            subprocess.run(cmd, check=True, env=run_env)
            vbin = vbuild / f"V{tb_name}"
            if os.name == "nt" and not vbin.is_file():
                vbin_exe = vbin.with_suffix(".exe")
                if vbin_exe.is_file():
                    vbin = vbin_exe
            manifest["verilator_binary"] = str(vbin)
            if not vbin.is_file():
                raise SystemExit(f"build(verilator): expected binary not found: {vbin}")
            run_args = list(getattr(args, "run_arg", []) or [])
            subprocess.run([str(vbin), *run_args], cwd=str(out_dir), check=True)

    cache = {
        "module_hashes": module_hashes,
        "pycc": str(pycc),
        "build_flags": build_flags,
        "build_flags_hash": build_flags_hash,
    }
    _save_json(cache_path, cache)
    _save_json(manifest_path, manifest)
    print(str(manifest_path))
    return 0


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="pycircuit")
    sub = p.add_subparsers(dest="cmd", required=True)

    emit = sub.add_parser("emit", help="Emit PYC MLIR (*.pyc) from a Python design file.")
    emit.add_argument("python_file", help="Python source defining `@module def build(m: Circuit, ...)`")
    emit.add_argument("-o", "--output", required=True, help="Output .pyc path")
    emit.add_argument(
        "--param",
        action="append",
        default=[],
        help="Override a JIT parameter (repeatable): name=value (parsed as a Python literal when possible)",
    )
    emit.add_argument(
        "--project-root",
        default=None,
        help="Optional project root for strict API contract scan (defaults to nearest .git/pyproject.toml).",
    )
    emit.set_defaults(fn=_cmd_emit)

    build = sub.add_parser("build", help="Canonical flow: multi-.pyc emit + parallel pycc + CMake/Verilator.")
    build.add_argument("python_file", help="Python source defining `@module build(...)` and `@testbench tb(...)`")
    build.add_argument("--out-dir", required=True, help="Output directory for project artifacts")
    build.add_argument(
        "--param",
        action="append",
        default=[],
        help="Override a JIT parameter (repeatable): name=value (parsed as a Python literal when possible)",
    )
    build.add_argument(
        "--project-root",
        default=None,
        help="Optional project root for strict API contract scan (defaults to nearest .git/pyproject.toml).",
    )
    build.add_argument("--jobs", type=int, default=max(1, os.cpu_count() or 1), help="Parallel backend jobs")
    build.add_argument("--profile", choices=["dev", "release"], default="release", help="C++ build profile")
    build.add_argument(
        "--target",
        choices=["cpp", "verilator", "both"],
        default="both",
        help="Backend targets to generate/build",
    )
    build.add_argument("--logic-depth", type=int, default=32, help="Max combinational logic depth for pycc")
    build.add_argument(
        "--run-verilator",
        action="store_true",
        help="Also run generated Verilator binary after build",
    )
    build.add_argument(
        "--run-arg",
        action="append",
        default=[],
        help="Argument passed to the Verilator binary when --run-verilator is set (repeatable).",
    )
    build.set_defaults(fn=_cmd_build)

    ns = p.parse_args(argv)
    return int(ns.fn(ns))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
