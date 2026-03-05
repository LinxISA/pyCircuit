#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


_TOP_RE = re.compile(r"\bpyc\.top\s*=\s*@([A-Za-z_][A-Za-z0-9_\$]*)\b")
_FUNC_RE = re.compile(r"\bfunc\.func\s+@([A-Za-z_][A-Za-z0-9_\$]*)\b")
_SSA_RE = re.compile(r"%[A-Za-z0-9_]+")

_INSTANCE_CALLEE_RE = re.compile(r"\bcallee\s*=\s*@([A-Za-z_][A-Za-z0-9_\$]*)\b")
_INSTANCE_NAME_RE = re.compile(r'\bname\s*=\s*"((?:\\.|[^"\\])*)"')

_ALIAS_RE = re.compile(r"^\s*(%[A-Za-z0-9_]+)\s*=\s*pyc\.alias\s+(%[A-Za-z0-9_]+)\b")


@dataclass(frozen=True)
class ModulePorts:
    arg_names: tuple[str, ...]
    result_names: tuple[str, ...]


@dataclass(frozen=True)
class InstanceRec:
    name: str
    callee: str
    operands: tuple[str, ...]
    results: tuple[str, ...]
    index: int


def _ensure_graphviz() -> None:
    try:
        import graphviz  # noqa: F401
    except Exception:  # noqa: BLE001
        raise SystemExit(
            "error: missing python package `graphviz`.\n"
            "Install:\n"
            "  python3 -m pip install graphviz\n"
            "\n"
            "If `pip` is blocked (PEP 668 / externally-managed Python), use a venv:\n"
            "  python3 -m venv .venv\n"
            "  . .venv/bin/activate\n"
            "  python -m pip install graphviz\n"
        )

    try:
        subprocess.run(["dot", "-V"], check=False, capture_output=True)
    except FileNotFoundError as e:
        raise SystemExit(
            "error: missing Graphviz `dot` executable in PATH.\n"
            "Install (macOS):\n"
            "  brew install graphviz\n"
        ) from e


def _brace_delta(s: str) -> int:
    """Count '{'/'}' outside of quoted strings."""
    delta = 0
    in_str = False
    esc = False
    for ch in s:
        if esc:
            esc = False
            continue
        if in_str and ch == "\\":
            esc = True
            continue
        if ch == '"':
            in_str = not in_str
            continue
        if in_str:
            continue
        if ch == "{":
            delta += 1
        elif ch == "}":
            delta -= 1
    return delta


def _find_matching(s: str, start: int, open_ch: str, close_ch: str) -> int:
    """Return index of matching close_ch for the open_ch at s[start]."""
    if start < 0 or start >= len(s) or s[start] != open_ch:
        raise ValueError("start must point to an opening delimiter")
    depth = 0
    in_str = False
    esc = False
    for i in range(start, len(s)):
        ch = s[i]
        if esc:
            esc = False
            continue
        if in_str and ch == "\\":
            esc = True
            continue
        if ch == '"':
            in_str = not in_str
            continue
        if in_str:
            continue
        if ch == open_ch:
            depth += 1
        elif ch == close_ch:
            depth -= 1
            if depth == 0:
                return i
    raise ValueError("unmatched delimiter")


def _extract_json_array(line: str, key: str) -> list[str] | None:
    # `arg_names = ["a", "b"]` and `result_names = [...]` are JSON-like arrays.
    i = line.find(key)
    if i < 0:
        return None
    i = line.find("[", i)
    if i < 0:
        return None
    try:
        j = _find_matching(line, i, "[", "]")
    except ValueError:
        return None
    lit = line[i : j + 1]
    try:
        v = json.loads(lit)
    except Exception:  # noqa: BLE001
        return None
    if not isinstance(v, list) or not all(isinstance(x, str) for x in v):
        return None
    return [str(x) for x in v]


def _parse_instance_line(line: str, *, index: int) -> InstanceRec | None:
    if "pyc.instance" not in line:
        return None

    before, after = line.split("pyc.instance", 1)

    results: list[str] = []
    if "=" in before:
        lhs = before.split("=", 1)[0]
        results = _SSA_RE.findall(lhs)

    attr_start = after.find("{")
    if attr_start < 0:
        return None

    operands_part = after[:attr_start]
    operands = _SSA_RE.findall(operands_part)

    try:
        attr_end = _find_matching(after, attr_start, "{", "}")
    except ValueError:
        return None
    attrs = after[attr_start + 1 : attr_end]

    m_callee = _INSTANCE_CALLEE_RE.search(attrs)
    if not m_callee:
        return None
    callee = str(m_callee.group(1))

    m_name = _INSTANCE_NAME_RE.search(attrs)
    if m_name:
        # Decode escapes correctly.
        try:
            name = json.loads('"' + m_name.group(1) + '"')
        except Exception:  # noqa: BLE001
            name = str(m_name.group(1))
    else:
        name = callee

    return InstanceRec(
        name=str(name),
        callee=callee,
        operands=tuple(operands),
        results=tuple(results),
        index=int(index),
    )


def _sanitize_dot_id(name: str) -> str:
    s = re.sub(r"[^A-Za-z0-9_]", "_", str(name))
    if not s:
        return "n"
    if s[0].isdigit():
        s = "n_" + s
    return s


def _tarjan_scc(nodes: Iterable[str], adj: dict[str, set[str]]) -> list[list[str]]:
    index = 0
    stack: list[str] = []
    on_stack: set[str] = set()
    indices: dict[str, int] = {}
    lowlink: dict[str, int] = {}
    out: list[list[str]] = []

    def strongconnect(v: str) -> None:
        nonlocal index
        indices[v] = index
        lowlink[v] = index
        index += 1
        stack.append(v)
        on_stack.add(v)

        for w in sorted(adj.get(v, set())):
            if w not in indices:
                strongconnect(w)
                lowlink[v] = min(lowlink[v], lowlink[w])
            elif w in on_stack:
                lowlink[v] = min(lowlink[v], indices[w])

        if lowlink[v] == indices[v]:
            comp: list[str] = []
            while True:
                w = stack.pop()
                on_stack.remove(w)
                comp.append(w)
                if w == v:
                    break
            out.append(comp)

    for v in sorted(nodes):
        if v not in indices:
            strongconnect(v)
    return out


def _resolve_alias(v: str, alias_of: dict[str, str]) -> str:
    cur = str(v)
    seen: set[str] = set()
    while cur in alias_of and cur not in seen:
        seen.add(cur)
        cur = alias_of[cur]
    return cur


def _resolve_origins(
    v: str,
    *,
    alias_of: dict[str, str],
    drivers: dict[str, set[str]],
    origin: dict[str, tuple[str, int]],
    stack: set[str] | None = None,
) -> set[tuple[str, int]]:
    stack = set() if stack is None else stack
    cur = _resolve_alias(v, alias_of)
    if cur in origin:
        return {origin[cur]}
    if cur in stack:
        return set()
    stack.add(cur)
    out: set[tuple[str, int]] = set()
    for src in sorted(drivers.get(cur, set())):
        out |= _resolve_origins(src, alias_of=alias_of, drivers=drivers, origin=origin, stack=stack)
    stack.remove(cur)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate a module-instance connectivity graph from a textual .pyc (MLIR).")
    ap.add_argument("--pyc", required=True, help="Path to textual .pyc file (MLIR)")
    ap.add_argument("--module", default="", help="Target module symbol (default: module attribute pyc.top)")
    ap.add_argument("--out", required=True, help="Output SVG path")
    ap.add_argument("--edge-label-mode", choices=["ports", "count", "none"], default="ports")
    ap.add_argument("--edge-label-limit", type=int, default=4)
    ap.add_argument("--max-nodes", type=int, default=500)
    ap.add_argument("--max-edges", type=int, default=2000)
    args = ap.parse_args()

    pyc_path = Path(args.pyc).resolve()
    if not pyc_path.is_file():
        raise SystemExit(f"missing --pyc file: {pyc_path}")

    out_svg = Path(args.out).resolve()
    out_svg.parent.mkdir(parents=True, exist_ok=True)
    dot_path = out_svg.with_suffix(".dot")
    json_path = out_svg.with_suffix(".json")

    edge_label_limit = int(args.edge_label_limit)
    if edge_label_limit < 0:
        edge_label_limit = 0

    target_sym = str(args.module).strip() or ""
    module_ports: dict[str, ModulePorts] = {}
    instances: list[InstanceRec] = []
    alias_of: dict[str, str] = {}
    drivers: dict[str, set[str]] = {}

    in_target = False
    depth = 0

    with pyc_path.open("r", encoding="utf-8", errors="replace") as f:
        for raw_line in f:
            line = raw_line.rstrip("\n")

            if not target_sym:
                m = _TOP_RE.search(line)
                if m:
                    target_sym = str(m.group(1))

            mfunc = _FUNC_RE.search(line)
            if mfunc:
                sym = str(mfunc.group(1))
                arg_names = _extract_json_array(line, "arg_names")
                result_names = _extract_json_array(line, "result_names")
                if arg_names is not None and result_names is not None:
                    module_ports[sym] = ModulePorts(tuple(arg_names), tuple(result_names))

                if sym == target_sym:
                    in_target = True
                    depth = 0
                    depth += _brace_delta(line)
                    continue

            if not in_target:
                continue

            # Collect within the target module function body.
            if rec := _parse_instance_line(line, index=len(instances)):
                instances.append(rec)

            if m_alias := _ALIAS_RE.match(line):
                dst, src = str(m_alias.group(1)), str(m_alias.group(2))
                alias_of[dst] = src

            if line.lstrip().startswith("pyc.assign"):
                ssa = _SSA_RE.findall(line)
                if len(ssa) >= 2:
                    dst, src = ssa[0], ssa[1]
                    drivers.setdefault(dst, set()).add(src)

            depth += _brace_delta(line)
            if depth <= 0:
                in_target = False

    if not target_sym:
        raise SystemExit("error: cannot determine target module (missing --module and pyc.top not found)")

    if not instances:
        raise SystemExit(f"error: no pyc.instance ops found in target module: {target_sym}")

    if len(instances) > int(args.max_nodes):
        raise SystemExit(
            f"error: instance nodes {len(instances)} exceeds --max-nodes={args.max_nodes}\n"
            f"Hint: rerun with --max-nodes {len(instances) + 100} or graph a smaller module.\n"
        )

    inst_by_name: dict[str, InstanceRec] = {}
    name_counts: dict[str, int] = {}
    for inst in instances:
        n = inst.name
        name_counts[n] = name_counts.get(n, 0) + 1
        if n in inst_by_name:
            # Disambiguate duplicate names deterministically.
            suffix = name_counts[n] - 1
            n2 = f"{n}__{suffix}"
            inst_by_name[n2] = InstanceRec(
                name=n2,
                callee=inst.callee,
                operands=inst.operands,
                results=inst.results,
                index=inst.index,
            )
        else:
            inst_by_name[n] = inst

    # Rebuild instances list with unique names, stable by index.
    instances_u: list[InstanceRec] = sorted(inst_by_name.values(), key=lambda x: x.index)

    # Map SSA result -> (inst_name, out_idx)
    origin: dict[str, tuple[str, int]] = {}
    for inst in instances_u:
        for out_idx, ssa in enumerate(inst.results):
            origin[str(ssa)] = (inst.name, int(out_idx))

    # Aggregate edges by (src,dst).
    edges: dict[tuple[str, str], dict[str, Any]] = {}

    for dst in instances_u:
        dst_ports = module_ports.get(dst.callee)
        for in_idx, op in enumerate(dst.operands):
            srcs = _resolve_origins(op, alias_of=alias_of, drivers=drivers, origin=origin)
            if not srcs:
                continue
            for src_name, src_out_idx in sorted(srcs):
                if src_name not in inst_by_name:
                    continue
                src_inst = inst_by_name[src_name]

                src_ports = module_ports.get(src_inst.callee)
                src_port = (
                    src_ports.result_names[src_out_idx]
                    if src_ports and 0 <= src_out_idx < len(src_ports.result_names)
                    else f"out{src_out_idx}"
                )
                dst_port = (
                    dst_ports.arg_names[in_idx]
                    if dst_ports and 0 <= in_idx < len(dst_ports.arg_names)
                    else f"in{in_idx}"
                )

                key = (src_name, dst.name)
                ent = edges.setdefault(key, {"count": 0, "maps": set()})
                ent["count"] += 1
                ent["maps"].add((str(dst_port), str(src_port), int(in_idx), int(src_out_idx)))

    if len(edges) > int(args.max_edges):
        raise SystemExit(
            f"error: edges {len(edges)} exceeds --max-edges={args.max_edges}\n"
            f"Hint: rerun with --max-edges {len(edges) + 200} or graph a smaller module.\n"
        )

    # SCC report.
    node_names = [i.name for i in instances_u]
    adj: dict[str, set[str]] = {n: set() for n in node_names}
    for (src, dst) in edges.keys():
        if src in adj and dst in adj:
            adj[src].add(dst)
    sccs = _tarjan_scc(node_names, adj)
    multi_sccs = [sorted(c) for c in sccs if len(c) > 1]
    scc_members: set[str] = set()
    for c in multi_sccs:
        scc_members.update(c)
    largest_scc = max((len(c) for c in sccs), default=0)

    # Emit DOT + SVG (Graphviz required).
    _ensure_graphviz()
    import graphviz  # noqa: E402

    dot = graphviz.Digraph(name=f"{target_sym}_module_graph", format="svg", engine="dot")
    dot.attr(
        rankdir="LR",
        fontname="Helvetica",
        fontsize="10",
        nodesep="0.2",
        ranksep="0.55",
        splines="polyline",
        bgcolor="white",
        label=f"{target_sym} module graph (pyc.instance connectivity)",
        labelloc="t",
        labeljust="c",
        pad="0.25",
    )
    dot.attr("node", fontname="Helvetica", fontsize="8", style="filled", fillcolor="#F7F7F7")
    dot.attr("edge", fontsize="7", color="#9E9E9E", arrowsize="0.6")

    dot_id: dict[str, str] = {}
    used: set[str] = set()
    for inst in instances_u:
        base = _sanitize_dot_id(inst.name)
        nid = base
        k = 0
        while nid in used:
            k += 1
            nid = f"{base}__{k}"
        used.add(nid)
        dot_id[inst.name] = nid

        border = "#111111"
        pen = "1.1"
        if inst.name in scc_members:
            border = "#D32F2F"
            pen = "2.0"

        dot.node(
            nid,
            label=f"{inst.name}\\n{inst.callee}",
            shape="box",
            color=border,
            penwidth=pen,
        )

    def edge_label(ent: dict[str, Any]) -> str:
        cnt = int(ent["count"])
        if args.edge_label_mode == "none":
            return ""
        if args.edge_label_mode == "count":
            return f"n={cnt}"
        # ports
        maps = sorted(ent["maps"], key=lambda x: (x[0], x[1], x[2], x[3]))
        maps = maps[:edge_label_limit] if edge_label_limit else []
        lines = [f"n={cnt}"]
        for dst_port, src_port, _in_idx, _out_idx in maps:
            lines.append(f"{dst_port} <= {src_port}")
        return "\\n".join(lines)

    for (src, dst), ent in sorted(edges.items(), key=lambda x: (x[0][0], x[0][1])):
        if src not in dot_id or dst not in dot_id:
            continue
        lbl = edge_label(ent)
        if lbl:
            dot.edge(dot_id[src], dot_id[dst], label=lbl)
        else:
            dot.edge(dot_id[src], dot_id[dst])

    dot_path.write_text(dot.source, encoding="utf-8")

    stem = str(out_svg.with_suffix(""))
    rendered = dot.render(stem, cleanup=True)
    rendered_path = Path(rendered).resolve()
    if rendered_path != out_svg:
        # graphviz may append the format extension; normalize to the requested path.
        try:
            out_svg.unlink(missing_ok=True)  # type: ignore[arg-type]
        except Exception:  # noqa: BLE001
            pass
        rendered_path.replace(out_svg)

    # Emit JSON sidecar.
    nodes_json = []
    for inst in instances_u:
        mp = module_ports.get(inst.callee)
        nodes_json.append(
            {
                "name": inst.name,
                "callee": inst.callee,
                "index": inst.index,
                "arg_names": list(mp.arg_names) if mp else [],
                "result_names": list(mp.result_names) if mp else [],
            }
        )

    edges_json = []
    for (src, dst), ent in sorted(edges.items(), key=lambda x: (x[0][0], x[0][1])):
        maps = sorted(ent["maps"], key=lambda x: (x[0], x[1], x[2], x[3]))
        maps_json = [
            {"dst_port": dp, "src_port": sp, "dst_idx": di, "src_idx": si}
            for dp, sp, di, si in maps[:edge_label_limit] if edge_label_limit
        ]
        edges_json.append(
            {
                "src": src,
                "dst": dst,
                "count": int(ent["count"]),
                "mappings": maps_json,
            }
        )

    payload = {
        "version": 1,
        "input_pyc": str(pyc_path),
        "module": target_sym,
        "nodes": nodes_json,
        "edges": edges_json,
        "summary": {
            "instance_count": len(instances_u),
            "edge_count": len(edges),
            "scc_count": len(sccs),
            "largest_scc": int(largest_scc),
            "multi_node_scc_count": len(multi_sccs),
        },
        "sccs": [{"size": len(c), "nodes": sorted(c)} for c in multi_sccs],
    }
    json_path.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")

    print(str(out_svg))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
