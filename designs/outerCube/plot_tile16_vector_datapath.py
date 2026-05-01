#!/usr/bin/env python3
"""
Generate small-tile (16 B) vector datapath illustrations inspired by
`vector4k.md`: A and B holding registers as contiguous blocks (all A, then all
B), ▭ width ∝ E, circles = 2-input execution units, crossed wires.

Only **FP32** (E = 4 B/elem) and **FP16 / BF16** (E = 2 B/elem) are supported;
smaller-precision formats (FP8 / MXFP4 / HiFP4) are intentionally excluded.

Writes 4 PNGs (one montage per operation type: 8 subplots each) to an output
directory (default: ./tile16_figures next to this script). Types 1–4: elementwise,
reduce, expand, mergesort (compare–swap levels).
"""

from __future__ import annotations

import argparse
import os
from dataclasses import dataclass
from typing import Callable, List, Sequence, Tuple

from matplotlib.axes import Axes

import matplotlib.pyplot as plt
from matplotlib.patches import Circle, FancyArrowPatch, Rectangle

TILE_BYTES = 16
# Reference element size (bytes) for rectangle width scaling in figures.
E_REF = 4.0


@dataclass(frozen=True)
class TileShape:
    idx: int
    R: int
    C: int
    E: float  # bytes per logical element — only 4.0 (FP32) or 2.0 (FP16/BF16) supported
    label: str

    def __post_init__(self) -> None:
        if self.E not in (4.0, 2.0):
            raise ValueError(
                f"Unsupported element size E={self.E}; only FP32 (4 B) and "
                f"FP16/BF16 (2 B) are allowed."
            )

    @property
    def N(self) -> int:
        assert abs(self.R * self.C * self.E - TILE_BYTES) < 1e-6, self
        return int(round(TILE_BYTES / self.E))

    def fmt_desc(self) -> str:
        return f"R×C = {self.R}×{self.C}, E = {self.E} B/elem → N = {self.N}"


def canonical_shapes() -> List[TileShape]:
    # Eight distinct (R, C, E) with R*C*E = 16 B; only FP32 (E=4) and FP16/BF16 (E=2).
    # FP32 has 3 legal shapes, FP16 has 4; one shape appears with both FP16 and BF16
    # labels to emphasise that BF16 shares E=2 B/elem storage with FP16.
    return [
        TileShape(1, 1, 4, 4.0, "1×4 FP32 (wide row)"),
        TileShape(2, 2, 2, 4.0, "2×2 FP32"),
        TileShape(3, 4, 1, 4.0, "4×1 FP32 (tall col)"),
        TileShape(4, 1, 8, 2.0, "1×8 FP16 (wide row)"),
        TileShape(5, 2, 4, 2.0, "2×4 FP16"),
        TileShape(6, 4, 2, 2.0, "4×2 FP16"),
        TileShape(7, 8, 1, 2.0, "8×1 FP16 (tall col)"),
        TileShape(8, 2, 4, 2.0, "2×4 BF16 (same E as FP16)"),
    ]


def _rect(ax: Axes, cx: float, cy: float, w: float, h: float, **kwargs) -> Rectangle:
    """Axis-aligned rectangle centered at (cx, cy)."""
    kwargs.setdefault("zorder", 2)
    r = Rectangle((cx - w / 2, cy - h / 2), w, h, fill=True, **kwargs)
    ax.add_patch(r)
    return r


def _rect_top(cy: float, h: float) -> float:
    return cy + h / 2


def _rect_bottom(cy: float, h: float) -> float:
    return cy - h / 2


def _circ_top(cy: float, r: float) -> float:
    return cy + r


def _circ_bottom(cy: float, r: float) -> float:
    return cy - r


def _ab_block_layout(
    E: float,
    n_a: int,
    n_b: int,
    x0: float,
    x1: float,
) -> Tuple[float, float, float, float, List[float], List[float]]:
    """
    All A operands contiguous, then a gap, then all B operands contiguous.
    Same width w for every element rectangle (w ∝ E). Returns:
    (w, h, gap_inside_block, gap_between_ab, xa_centers, xb_centers).
    """
    # E ∈ {2.0, 4.0} post-simplification (FP16/BF16 or FP32). Keep a small floor
    # for numerical safety if a future caller passes an unexpected E.
    e = max(float(E), 1.0)
    span = x1 - x0
    gap_block = min(0.032, 0.09 * span)
    g_in = min(0.009, 0.14 * span / max(n_a + n_b, 2))
    gaps_inside = max(n_a - 1, 0) * g_in + max(n_b - 1, 0) * g_in
    coef = n_a + n_b
    slack = span - gap_block - gaps_inside
    if slack <= 0:
        slack = 0.01 * span
        gap_block *= 0.5
        g_in *= 0.5
        gaps_inside = max(n_a - 1, 0) * g_in + max(n_b - 1, 0) * g_in
        slack = span - gap_block - gaps_inside
    K = slack / (coef * E_REF)
    w = max(0.0025, K * e)
    if coef * w + gaps_inside + gap_block > span:
        w = max(0.0025, (span - gap_block - gaps_inside) / max(coef, 1))
    h = min(0.032, 0.52 / max(n_a + n_b, 1))

    xa: List[float] = []
    x = x0
    for i in range(n_a):
        xa.append(x + w / 2)
        x += w
        if i < n_a - 1:
            x += g_in
    x += gap_block
    xb: List[float] = []
    for i in range(n_b):
        xb.append(x + w / 2)
        x += w
        if i < n_b - 1:
            x += g_in
    return w, h, g_in, gap_block, xa, xb


def _circle(ax, cx: float, cy: float, r: float, **kwargs) -> Circle:
    kwargs.setdefault("zorder", 2)
    c = Circle((cx, cy), r, fill=True, **kwargs)
    ax.add_patch(c)
    return c


def _wire_straight(
    ax: Axes,
    p0: Tuple[float, float],
    p1: Tuple[float, float],
    *,
    color: str = "#444",
    lw: float = 0.9,
    zorder: float = 4,
) -> None:
    """Straight segment (exact endpoints; draws above operand patches)."""
    ax.plot(
        [p0[0], p1[0]],
        [p0[1], p1[1]],
        color=color,
        linewidth=lw,
        solid_capstyle="round",
        zorder=zorder,
        clip_on=False,
    )


def _wire(
    ax,
    p0: Tuple[float, float],
    p1: Tuple[float, float],
    rad: float = 0.0,
    color: str = "#444",
    lw: float = 0.9,
    zorder: float = 4,
) -> None:
    if abs(rad) < 1e-9:
        _wire_straight(ax, p0, p1, color=color, lw=lw, zorder=zorder)
        return
    style = "arc3,rad=" + str(rad)
    a = FancyArrowPatch(
        p0,
        p1,
        arrowstyle="-",
        linestyle="-",
        linewidth=lw,
        color=color,
        connectionstyle=style,
        mutation_scale=1,
        zorder=zorder,
        clip_on=False,
    )
    ax.add_patch(a)


def _odd_even_transposition_stages(n: int, max_stages: int) -> List[List[Tuple[int, int]]]:
    """Disjoint compare–swap pairs per stage (odd / even offset), straight multistage network."""
    out: List[List[Tuple[int, int]]] = []
    for p in range(max_stages):
        pairs: List[Tuple[int, int]] = []
        start = p % 2
        for i in range(start, n - 1, 2):
            pairs.append((i, i + 1))
        out.append(pairs)
    return out


def _title_block(ax: Axes, kind: str, spec: TileShape, *, fontsize: float = 11) -> None:
    ax.set_title(
        f"{kind} — {TILE_BYTES} B tile\n{spec.label} — {spec.fmt_desc()}",
        fontsize=fontsize,
        pad=6,
    )


def render_elementwise(
    ax: Axes,
    spec: TileShape,
    *,
    title_fontsize: float = 9,
    label_fs: float = 6,
    show_footer: bool = False,
) -> None:
    N = spec.N
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")
    _title_block(ax, "Element-wise (2-input / lane)", spec, fontsize=title_fontsize)

    # One row: all A contiguous, gap, all B contiguous; width ∝ E; ALU below each (A[i],B[i]) pair.
    x0, x1 = 0.04, 0.96
    y_reg, y_u, y_o = 0.86, 0.50, 0.30
    w, h, _g_in, _gap_ab, xa_list, xb_list = _ab_block_layout(float(spec.E), N, N, x0, x1)
    cr = min(0.022, 0.04 / max(N / 8, 1), 0.55 * h)

    ax.text(0.02, y_reg + h / 2 + 0.045, "A block | B block (width ∝ E)", fontsize=max(5.0, label_fs))

    for i in range(N):
        xa_c, xb_c = xa_list[i], xb_list[i]
        x_mid = 0.5 * (xa_c + xb_c)
        _rect(ax, xa_c, y_reg, w, h, facecolor="#cfe8ff", edgecolor="#1a4f8c", linewidth=1.0)
        ax.text(xa_c, y_reg + h / 2 + 0.028, f"A{i}", ha="center", va="bottom", fontsize=label_fs)
        _rect(ax, xb_c, y_reg, w, h, facecolor="#ffe8cf", edgecolor="#8c4f1a", linewidth=1.0)
        ax.text(xb_c, y_reg + h / 2 + 0.028, f"B{i}", ha="center", va="bottom", fontsize=label_fs)

        _circle(ax, x_mid, y_u, cr, facecolor="#e8e8ff", edgecolor="#333", linewidth=1.0)
        ax.text(x_mid, y_u, "⊕", ha="center", va="center", fontsize=max(5.0, label_fs), color="#222")

        rad_a = 0.18 * ((-1) ** i)
        rad_b = -rad_a
        _wire(
            ax,
            (xa_c, _rect_bottom(y_reg, h)),
            (x_mid, _circ_top(y_u, cr)),
            rad=rad_a,
        )
        _wire(
            ax,
            (xb_c, _rect_bottom(y_reg, h)),
            (x_mid, _circ_top(y_u, cr)),
            rad=rad_b,
        )

        wo, ho = w * 0.92, h * 0.92
        _rect(ax, x_mid, y_o, wo, ho, facecolor="#dff5df", edgecolor="#1a6c2e", linewidth=0.9)
        _wire_straight(
            ax,
            (x_mid, _circ_bottom(y_u, cr)),
            (x_mid, _rect_top(y_o, ho)),
            lw=0.7,
            color="#555",
        )

    if show_footer:
        ax.text(
            0.5,
            0.02,
            "▭ = element (width ∝ E); ○ = 2-input ALU; opposite bends = crossed operand paths.",
            ha="center",
            va="bottom",
            fontsize=6,
            color="#333",
        )


def render_reduce(
    ax: Axes,
    spec: TileShape,
    *,
    title_fontsize: float = 9,
    label_fs: float = 5.5,
    show_footer: bool = False,
) -> None:
    """Row-style reduce along C for fiber row 0; then binary reduction tree."""
    C = spec.C
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")
    _title_block(ax, "Reduce (row 0, axis C)", spec, fontsize=title_fontsize)

    N = C
    x0, x1 = 0.06, 0.94
    y_reg = 0.12
    y_leaf_circ = 0.30
    w, h, _g_in, _gap_ab, xa_list, xb_list = _ab_block_layout(float(spec.E), N, N, x0, x1)
    cr = min(0.024, 0.05 / max(N / 8, 1), 0.55 * h)

    ax.text(0.02, 0.94, f"fiber row 0; R={spec.R}", fontsize=max(5.0, label_fs))
    ax.text(0.02, y_reg + h / 2 + 0.05, "A block | B block (width ∝ E)", fontsize=max(5.0, label_fs))

    for i in range(N):
        xa_c, xb_c = xa_list[i], xb_list[i]
        x_mid = 0.5 * (xa_c + xb_c)
        _rect(ax, xa_c, y_reg, w, h, facecolor="#cfe8ff", edgecolor="#1a4f8c", linewidth=1.0)
        ax.text(xa_c, y_reg + h / 2 + 0.028, f"A0,{i}", ha="center", va="bottom", fontsize=label_fs)
        _rect(ax, xb_c, y_reg, w, h, facecolor="#ffe8cf", edgecolor="#8c4f1a", linewidth=1.0)
        ax.text(xb_c, y_reg + h / 2 + 0.028, f"B0,{i}", ha="center", va="bottom", fontsize=label_fs)

    leaf_out_x: List[float] = []
    for i in range(N):
        xa_c, xb_c = xa_list[i], xb_list[i]
        x_mid = 0.5 * (xa_c + xb_c)
        _circle(ax, x_mid, y_leaf_circ, cr, facecolor="#e8e8ff", edgecolor="#333", linewidth=1.0)
        rad_a = 0.12 * ((-1) ** i)
        _wire(
            ax,
            (xa_c, _rect_top(y_reg, h)),
            (x_mid, _circ_bottom(y_leaf_circ, cr)),
            rad=rad_a,
        )
        _wire(
            ax,
            (xb_c, _rect_top(y_reg, h)),
            (x_mid, _circ_bottom(y_leaf_circ, cr)),
            rad=-rad_a,
        )
        leaf_out_x.append(x_mid)

    leaves = [(x, y_leaf_circ) for x in leaf_out_x]
    cur = leaves
    dy = 0.09
    r_tree = cr * 1.05
    y = y_leaf_circ + cr + dy * 0.55
    depth = 0
    while len(cur) > 1:
        r_child = cr if depth == 0 else r_tree
        nxt: List[Tuple[float, float]] = []
        for j in range(0, len(cur), 2):
            if j + 1 < len(cur):
                xm = 0.5 * (cur[j][0] + cur[j + 1][0])
                xL, xR = cur[j][0], cur[j + 1][0]
            else:
                xm = cur[j][0]
                xL = xR = cur[j][0]
            _circle(ax, xm, y, r_tree, facecolor="#f3e8ff", edgecolor="#333", linewidth=1.0)
            _wire(
                ax,
                (xL, cur[j][1] + r_child),
                (xm, _circ_bottom(y, r_tree)),
                rad=0.05,
            )
            if j + 1 < len(cur):
                _wire(
                    ax,
                    (xR, cur[j + 1][1] + r_child),
                    (xm, _circ_bottom(y, r_tree)),
                    rad=-0.05,
                )
            nxt.append((xm, y))
        cur = nxt
        y += dy
        depth += 1

    root = cur[0]
    w_acc = max(w * 1.25, 0.014)
    y_acc_c = root[1] + 0.08
    _rect(
        ax,
        root[0],
        y_acc_c,
        w_acc,
        h,
        facecolor="#ffd7d7",
        edgecolor="#8c1a1a",
        linewidth=1.1,
    )
    _wire_straight(
        ax,
        (root[0], _circ_top(root[1], r_tree)),
        (root[0], _rect_bottom(y_acc_c, h)),
        lw=0.85,
        color="#333",
    )
    ax.text(root[0], y_acc_c + h / 2 + 0.045, "Acc", ha="center", va="bottom", fontsize=max(5.0, label_fs + 1))

    if show_footer:
        ax.text(
            0.5,
            0.01,
            "○ = 2-input combine; upper = reduction tree.",
            ha="center",
            fontsize=6,
        )


def _fanout_levels_top_down(
    C: int, x0: float, x1: float, y_top: float, y_bottom: float
) -> List[List[Tuple[float, float]]]:
    """Balanced pairing tree: index 0 is root, last level has C leaves (left-to-right x)."""
    xs = [x0 + (x1 - x0) * i / max(C - 1, 1) for i in range(C)]
    layers_bottom_up: List[List[Tuple[float, float]]] = [[(x, 0.0) for x in xs]]
    while len(layers_bottom_up[-1]) > 1:
        prev = layers_bottom_up[-1]
        nxt: List[Tuple[float, float]] = []
        for j in range(0, len(prev), 2):
            if j + 1 < len(prev):
                xm = 0.5 * (prev[j][0] + prev[j + 1][0])
            else:
                xm = prev[j][0]
            nxt.append((xm, 0.0))
        layers_bottom_up.append(nxt)
    layers = list(reversed(layers_bottom_up))
    nh = len(layers)
    for i, lev in enumerate(layers):
        if nh == 1:
            yi = y_top
        else:
            yi = y_top - i * ((y_top - y_bottom) / (nh - 1))
        layers[i] = [(x, yi) for (x, _) in lev]
    return layers


def _fanout_levels_from_leaf_xs(
    leaf_xs: List[float], y_top: float, y_bottom: float
) -> List[List[Tuple[float, float]]]:
    """Same pairing tree as `_fanout_levels_top_down`, but leaf x positions are explicit."""
    if not leaf_xs:
        return []
    layers_bottom_up: List[List[Tuple[float, float]]] = [[(x, 0.0) for x in leaf_xs]]
    while len(layers_bottom_up[-1]) > 1:
        prev = layers_bottom_up[-1]
        nxt: List[Tuple[float, float]] = []
        for j in range(0, len(prev), 2):
            if j + 1 < len(prev):
                xm = 0.5 * (prev[j][0] + prev[j + 1][0])
            else:
                xm = prev[j][0]
            nxt.append((xm, 0.0))
        layers_bottom_up.append(nxt)
    layers = list(reversed(layers_bottom_up))
    nh = len(layers)
    for i, lev in enumerate(layers):
        if nh == 1:
            yi = y_top
        else:
            yi = y_top - i * ((y_top - y_bottom) / (nh - 1))
        layers[i] = [(x, yi) for (x, _) in lev]
    return layers


def render_expand(
    ax: Axes,
    spec: TileShape,
    *,
    title_fontsize: float = 9,
    label_fs: float = 5.5,
    show_footer: bool = False,
) -> None:
    """Expand along C: v[r] in A fans out; B holds src row; leaf 2-input combines."""
    C = spec.C
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")
    _title_block(ax, "Expand (row 0, fanout C)", spec, fontsize=title_fontsize)

    y_fan_top = 0.82
    y_fan_bot = 0.52
    y_leaf_circ = 0.34
    y_row = 0.12

    x0, x1 = 0.05, 0.95
    w, h, _g_in, _gap_ab, xa_list, xb_list = _ab_block_layout(float(spec.E), 1, C, x0, x1)
    cr = min(0.026, 0.05 / max(C / 8, 1), 0.55 * h)
    xv = xa_list[0]
    leaves_x = list(xb_list)

    levels = _fanout_levels_from_leaf_xs(leaves_x, y_top=y_fan_top, y_bottom=y_fan_bot)
    root_fan_x, root_fan_y = levels[0][0]

    ax.text(0.02, 0.94, f"row 0; R={spec.R}", fontsize=max(5.0, label_fs))
    ax.text(0.02, y_row + h / 2 + 0.05, "A block (v) | B block (width ∝ E)", fontsize=max(5.0, label_fs))

    _rect(ax, xv, y_row, w, h, facecolor="#cfe8ff", edgecolor="#1a4f8c", linewidth=1.0)
    ax.text(xv, y_row + h / 2 + 0.03, "v[r]", ha="center", va="bottom", fontsize=max(5.0, label_fs + 0.5))
    for i, xb in enumerate(xb_list):
        _rect(ax, xb, y_row, w, h, facecolor="#ffe8cf", edgecolor="#8c4f1a", linewidth=1.0)
        ax.text(xb, y_row + h / 2 + 0.03, f"B0,{i}", ha="center", va="bottom", fontsize=label_fs)

    for lev in levels:
        for (x, y) in lev:
            _circle(ax, x, y, cr, facecolor="#eef8ff", edgecolor="#333", linewidth=1.0)

    _wire_straight(
        ax,
        (xv, _rect_top(y_row, h)),
        (root_fan_x, _circ_bottom(root_fan_y, cr)),
        lw=0.85,
        color="#333",
    )

    for li in range(len(levels) - 1):
        parents = levels[li]
        children = levels[li + 1]
        for pi, (xp, yp) in enumerate(parents):
            for ci in (2 * pi, 2 * pi + 1):
                if ci < len(children):
                    xc, yc = children[ci]
                    _wire(
                        ax,
                        (xp, _circ_bottom(yp, cr)),
                        (xc, _circ_top(yc, cr)),
                        rad=0.035 * ((-1) ** (ci + pi)),
                    )

    y_fan_leaves = levels[-1][0][1]
    for i, x_lane in enumerate(leaves_x):
        xb = xb_list[i]
        y_out_c = y_leaf_circ + 0.09
        wo, ho = w * 0.9, h * 0.9
        _circle(ax, x_lane, y_leaf_circ, cr, facecolor="#e8ffe8", edgecolor="#333", linewidth=1.0)
        _wire_straight(
            ax,
            (x_lane, _circ_bottom(y_fan_leaves, cr)),
            (x_lane, _circ_top(y_leaf_circ, cr)),
            lw=0.9,
            color="#444",
        )
        _wire(
            ax,
            (xb, _rect_top(y_row, h)),
            (x_lane, _circ_bottom(y_leaf_circ, cr)),
            rad=0.14 * ((-1) ** i),
            color="#8c4f1a",
            lw=1.0,
        )
        _rect(ax, x_lane, y_out_c, wo, ho, facecolor="#dff5df", edgecolor="#1a6c2e", linewidth=0.9)
        _wire_straight(
            ax,
            (x_lane, _circ_bottom(y_leaf_circ, cr)),
            (x_lane, _rect_top(y_out_c, ho)),
            lw=0.75,
            color="#555",
        )

    if show_footer:
        ax.text(
            0.5,
            0.01,
            "Fanout tree + leaf ○ with B lanes.",
            ha="center",
            fontsize=6,
        )


def render_mergesort(
    ax: Axes,
    spec: TileShape,
    *,
    title_fontsize: float = 9,
    label_fs: float = 5.5,
    show_footer: bool = False,
) -> None:
    """
    Multi-level compare–swap (2-in / 2-out) only: straight wires from data to each ○,
    straight wires between levels on fixed tracks; no shuffle bus / no horizontal rails.
    """
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")
    _title_block(ax, "Mergesort (compare–swap)", spec, fontsize=title_fontsize)

    Ntile = spec.N
    n_vis = min(Ntile, 8)
    if n_vis < 2:
        n_vis = 2
    if n_vis % 2 == 1:
        n_vis -= 1
    m = n_vis // 2
    E = float(spec.E)
    x0, x1 = 0.06, 0.94
    w, h, _g, _ggap, xa_list, xb_list = _ab_block_layout(E, m, m, x0, x1)

    y_reg = 0.84
    for i in range(m):
        _rect(ax, xa_list[i], y_reg, w, h, facecolor="#cfe8ff", edgecolor="#1a4f8c", linewidth=0.9)
        ax.text(
            xa_list[i],
            y_reg + h / 2 + 0.02,
            f"A{i}",
            ha="center",
            va="bottom",
            fontsize=max(4.5, label_fs - 0.5),
        )
    for i in range(m):
        _rect(ax, xb_list[i], y_reg, w, h, facecolor="#ffe8cf", edgecolor="#8c4f1a", linewidth=0.9)
        ax.text(
            xb_list[i],
            y_reg + h / 2 + 0.02,
            f"B{i}",
            ha="center",
            va="bottom",
            fontsize=max(4.5, label_fs - 0.5),
        )

    note = f"n={n_vis} lanes" + (f" (tile N={Ntile})" if n_vis < Ntile else "")
    ax.text(0.02, 0.97, note, fontsize=max(4.5, label_fs - 0.5))

    n = 2 * m
    xs_lane: List[float] = []
    for i in range(m):
        xs_lane.append(xa_list[i])
        xs_lane.append(xb_list[i])
    y_rect_bot = _rect_bottom(y_reg, h)
    cr = min(0.017, 0.32 / n, 0.45 * h)
    leg = max(0.028, 0.05 - 0.002 * n)
    max_stages = max(3, min(2 * n - 1, 10))
    stages = _odd_even_transposition_stages(n, max_stages)

    y_track = [y_rect_bot] * n
    for pairs in stages:
        y_in = min(y_track)
        y_cs = y_in - leg - cr
        y_next = y_cs - cr - 0.012
        paired: set[int] = set()
        for (i, j) in pairs:
            paired.add(i)
            paired.add(j)
            xm = 0.5 * (xs_lane[i] + xs_lane[j])
            _circle(ax, xm, y_cs, cr, facecolor="#f0e8ff", edgecolor="#333", linewidth=0.85)
            ax.text(
                xm,
                y_cs,
                "C&S",
                ha="center",
                va="center",
                fontsize=max(4.0, label_fs - 1.0),
                color="#222",
            )
            _wire_straight(ax, (xs_lane[i], y_track[i]), (xm, _circ_top(y_cs, cr)), lw=0.75, color="#444")
            _wire_straight(ax, (xs_lane[j], y_track[j]), (xm, _circ_top(y_cs, cr)), lw=0.75, color="#444")
            dx = cr * 0.55
            _wire_straight(ax, (xm - dx, _circ_bottom(y_cs, cr)), (xs_lane[i], y_next), lw=0.75, color="#444")
            _wire_straight(ax, (xm + dx, _circ_bottom(y_cs, cr)), (xs_lane[j], y_next), lw=0.75, color="#444")
            y_track[i] = y_next
            y_track[j] = y_next
        for k in range(n):
            if k not in paired:
                _wire_straight(ax, (xs_lane[k], y_track[k]), (xs_lane[k], y_next), lw=0.65, color="#888")
                y_track[k] = y_next

    y_cur = min(y_track)
    y_out = max(0.07, y_cur - 0.025)
    wo, ho = w * 0.82, h * 0.82
    for k in range(n):
        _wire_straight(
            ax,
            (xs_lane[k], y_track[k]),
            (xs_lane[k], _rect_top(y_out, ho)),
            lw=0.55,
            color="#666",
        )
    for k in range(n):
        _rect(ax, xs_lane[k], y_out, wo, ho, facecolor="#dff5df", edgecolor="#1a6c2e", linewidth=0.8)
    ax.text(0.5, y_out - ho / 2 - 0.018, "merged lane values", ha="center", va="top", fontsize=5)

    if show_footer:
        ax.text(
            0.5,
            0.015,
            "▭ = key; ○ = compare–swap (2 in / 2 out); only straight segments, no shuffle / bus lines.",
            ha="center",
            fontsize=5,
            color="#333",
        )


def _save_montage(
    out_path: str,
    shapes: List[TileShape],
    render: Callable[..., None],
    suptitle: str,
    footnote: str,
    *,
    figsize: Tuple[float, float] = (28, 16),
    dpi: int = 140,
) -> None:
    fig, axes = plt.subplots(2, 4, figsize=figsize, dpi=dpi)
    fig.suptitle(suptitle, fontsize=14, y=0.995)
    for ax, spec in zip(axes.ravel(), shapes):
        render(ax, spec, title_fontsize=8.5, label_fs=5.5, show_footer=False)
    fig.text(0.5, 0.012, footnote, ha="center", fontsize=9, color="#222")
    fig.subplots_adjust(left=0.03, right=0.97, top=0.93, bottom=0.06, hspace=0.38, wspace=0.22)
    fig.savefig(out_path, bbox_inches="tight")
    plt.close(fig)


def main(argv: Sequence[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="Plot 16 B tile vector datapath montages (4 PNGs).")
    p.add_argument(
        "-o",
        "--out-dir",
        default=os.path.join(os.path.dirname(__file__), "tile16_figures"),
        help="Directory for PNG output (created if missing).",
    )
    args = p.parse_args(list(argv) if argv is not None else None)

    os.makedirs(args.out_dir, exist_ok=True)
    shapes = canonical_shapes()

    _save_montage(
        os.path.join(args.out_dir, "elementwise_all.png"),
        shapes,
        render_elementwise,
        suptitle=f"Element-wise — 8 tile shapes ({TILE_BYTES} B holding register each)",
        footnote=(
            "▭ = element (width ∝ E); all A contiguous, then all B; ○ = 2-input unit; "
            "crossed bends on operand paths (cf. vector4k dual-port strip buffers)."
        ),
    )
    _save_montage(
        os.path.join(args.out_dir, "reduce_all.png"),
        shapes,
        render_reduce,
        suptitle=f"Reduce (representative row / fiber) — 8 tile shapes ({TILE_BYTES} B tile)",
        footnote=(
            "Per subplot: A block then B block (▭ width ∝ E); lane i pairing into ○, then reduction tree to Acc."
        ),
    )
    _save_montage(
        os.path.join(args.out_dir, "expand_all.png"),
        shapes,
        render_expand,
        suptitle=f"Expand / fanout — 8 tile shapes ({TILE_BYTES} B tile)",
        footnote=(
            "A block (v) then B block (▭ width ∝ E); v fans out; leaf ○ combines fanout with each B lane."
        ),
    )
    _save_montage(
        os.path.join(args.out_dir, "mergesort_all.png"),
        shapes,
        render_mergesort,
        suptitle=f"Mergesort (compare–swap) — 8 tile shapes ({TILE_BYTES} B tile)",
        footnote=(
            "Type 4: A|B keys (▭ ∝ E); multi-stage compare–swap (2-in/2-out ○); straight wires only, "
            "odd–even transposition layers (schematic multistage C&S, not a full minimal merge depth proof)."
        ),
    )

    print(f"Wrote 4 PNG montages to {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
