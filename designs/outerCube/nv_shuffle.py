#!/usr/bin/env python3
"""
NVIDIA warp shuffle schematic (16 B register tile).

One PNG montage (4×2): rows = __shfl_sync (per-lane indexed read; here a fixed
permutation), __shfl_up_sync, __shfl_down_sync, __shfl_xor_sync; columns =
**FP32 / FP16 (BF16)** (E = 4 / 2 B). Smaller-precision formats (FP8, FP4) are
intentionally excluded — the two remaining columns are the only storage widths
supported by the current VEC-4K / VEC-512 subset.

Each pane: input lanes (▭ width ∝ E) on top, output lanes on bottom, curved wires
lane i ← lane src(i). Not cycle-accurate; illustration only.
"""

from __future__ import annotations

import argparse
import math
import os
from dataclasses import dataclass
from typing import List, Sequence, Tuple

from matplotlib.axes import Axes
from matplotlib.patches import Circle, FancyArrowPatch, Rectangle

import matplotlib.pyplot as plt

TILE_BYTES = 16
E_REF = 4.0


@dataclass(frozen=True)
class FloatForm:
    E: float  # bytes per element
    label: str

    @property
    def n(self) -> int:
        return int(round(TILE_BYTES / self.E))


def formats() -> List[FloatForm]:
    # Only FP32 and FP16/BF16 are supported. BF16 shares E = 2 B with FP16, so
    # one column covers both; the label mentions both for clarity.
    return [
        FloatForm(4.0, "FP32"),
        FloatForm(2.0, "FP16 / BF16"),
    ]


def _rect(ax: Axes, cx: float, cy: float, w: float, h: float, **kwargs) -> Rectangle:
    kwargs.setdefault("zorder", 2)
    r = Rectangle((cx - w / 2, cy - h / 2), w, h, fill=True, **kwargs)
    ax.add_patch(r)
    return r


def _rect_top(cy: float, h: float) -> float:
    return cy + h / 2


def _rect_bottom(cy: float, h: float) -> float:
    return cy - h / 2


def _lane_layout(E: float, n: int, x0: float, x1: float) -> Tuple[float, float, List[float]]:
    """Single contiguous row of n elements; rectangle width ∝ E (same spirit as tile16 script)."""
    e = max(float(E), 0.5)
    span = x1 - x0
    g_in = min(0.028, 0.11 * span / max(n, 2))
    gaps_inside = max(n - 1, 0) * g_in
    slack = span - gaps_inside
    if slack <= 0:
        g_in *= 0.5
        gaps_inside = max(n - 1, 0) * g_in
        slack = span - gaps_inside
    K = slack / (n * E_REF)
    w = max(0.0028, K * e)
    if n * w + gaps_inside > span:
        w = max(0.0028, (span - gaps_inside) / max(n, 1))
    h = min(0.038, 0.48 / max(n, 1))
    xs: List[float] = []
    x = x0
    for i in range(n):
        xs.append(x + w / 2)
        x += w
        if i < n - 1:
            x += g_in
    return w, h, xs


def _shfl_linear_permutation_params(n: int) -> Tuple[int, int]:
    """
    Coefficients (a, b) for src(dst) = (a * dst + b) % n with gcd(a, n) == 1 so each
    destination reads from a distinct source (full permutation on lanes).
    """
    if n <= 1:
        return 1, 0
    a = 1
    for cand in range(2, n):
        if math.gcd(cand, n) == 1:
            a = cand
            break
    b = 1 if n > 1 else 0
    return a, b


def _shuffle_src(
    op: str, n: int, dst: int, *, delta: int = 1, xor_mask: int = 4
) -> int:
    """Map destination lane -> source lane for schematic (CUDA-ish)."""
    if n <= 0:
        return 0
    if op == "shfl":
        a, b = _shfl_linear_permutation_params(n)
        return (a * dst + b) % n
    if op == "shfl_up":
        j = dst - delta
        return j if j >= 0 else 0
    if op == "shfl_down":
        j = dst + delta
        return j if j < n else n - 1
    if op == "shfl_xor":
        m = xor_mask % n
        if m == 0:
            m = 1 if n > 1 else 0
        return dst ^ m
    return dst


def render_shuffle_pane(
    ax: Axes,
    op: str,
    op_title: str,
    cuda_name: str,
    spec: FloatForm,
    *,
    title_fs: float = 8.0,
    lane_fs: float = 5.0,
) -> None:
    n = spec.n
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")

    E = float(spec.E)
    x0, x1 = 0.05, 0.95
    w, h, xs = _lane_layout(E, n, x0, x1)

    y_in = 0.74
    y_out = 0.26
    ax.set_title(
        f"{op_title}\n{cuda_name} — {spec.label} — E={E:g} B, N={n} ({TILE_BYTES} B tile)",
        fontsize=title_fs,
        pad=4,
    )

    for i in range(n):
        _rect(ax, xs[i], y_in, w, h, facecolor="#cfe8ff", edgecolor="#1a4f8c", linewidth=0.9)
        if n <= 16:
            ax.text(
                xs[i],
                y_in + h / 2 + 0.022,
                f"L{i}",
                ha="center",
                va="bottom",
                fontsize=max(4.0, lane_fs),
                color="#123",
            )

    for i in range(n):
        _rect(ax, xs[i], y_out, w, h, facecolor="#dff5df", edgecolor="#1a6c2e", linewidth=0.9)
        if n <= 16:
            ax.text(
                xs[i],
                y_out - h / 2 - 0.018,
                f"O{i}",
                ha="center",
                va="top",
                fontsize=max(4.0, lane_fs),
                color="#123",
            )

    # Legend for op-specific params (tiny)
    if op == "shfl":
        pa, pb = _shfl_linear_permutation_params(n)
        note = f"per-lane srcLane: ( {pa}·i + {pb} ) mod {n} (each Oi reads a different Li)"
    elif op == "shfl_up":
        note = "delta=1 (from lower lane id)"
    elif op == "shfl_down":
        note = "delta=1 (from higher lane id)"
    else:
        m = 4 % n if n else 0
        if m == 0:
            m = 1 if n > 1 else 0
        note = f"maskLane={m} (XOR index)"

    ax.text(0.5, 0.06, note, ha="center", va="center", fontsize=max(3.8, lane_fs - 0.8), color="#444")

    xor_mask = 4
    for dst in range(n):
        src = _shuffle_src(op, n, dst, delta=1, xor_mask=xor_mask)
        x0a, y0a = xs[src], _rect_bottom(y_in, h)
        x1a, y1a = xs[dst], _rect_top(y_out, h)
        dx = x1a - x0a
        rad = 0.22 * (1.0 if dx >= 0 else -1.0) * (0.35 + min(abs(dx) * 1.8, 0.9))
        style = "arc3,rad=" + str(rad)
        a = FancyArrowPatch(
            (x0a, y0a),
            (x1a, y1a),
            arrowstyle="-|>",
            linestyle="-",
            linewidth=0.75,
            color="#333",
            connectionstyle=style,
            mutation_scale=7,
            zorder=3,
            clip_on=False,
        )
        ax.add_patch(a)

    # Optional small node at merge (purely decorative for dense N)
    if n <= 8:
        cr = min(0.014, 0.5 * h)
        for dst in range(n):
            xm = 0.5 * (xs[_shuffle_src(op, n, dst, xor_mask=xor_mask)] + xs[dst])
            ym = 0.5 * (y_in + y_out)
            c = Circle((xm, ym), cr, facecolor="#f6f0ff", edgecolor="#555", linewidth=0.6, zorder=1)
            ax.add_patch(c)


def _save_montage_4x2(out_path: str, *, dpi: int = 140) -> None:
    forms = formats()
    ops: List[Tuple[str, str, str]] = [
        ("shfl", "shfl (per-lane indexed)", "__shfl_sync"),
        ("shfl_up", "shfl_up", "__shfl_up_sync"),
        ("shfl_down", "shfl_down", "__shfl_down_sync"),
        ("shfl_xor", "shfl_xor", "__shfl_xor_sync"),
    ]

    fig, axes = plt.subplots(4, 2, figsize=(14, 22), dpi=dpi)
    fig.suptitle(
        f"NVIDIA warp shuffle — {TILE_BYTES} B tile, ▭ width ∝ element size (FP32 / FP16–BF16)",
        fontsize=13,
        y=0.995,
    )

    for r, (op, short, cuda) in enumerate(ops):
        for c, spec in enumerate(forms):
            render_shuffle_pane(axes[r, c], op, short, cuda, spec, title_fs=7.6, lane_fs=5.0)

    for c, spec in enumerate(forms):
        axes[0, c].text(
            0.5,
            1.02,
            spec.label,
            transform=axes[0, c].transAxes,
            ha="center",
            va="bottom",
            fontsize=10,
            fontweight="600",
            color="#111",
        )

    fig.text(
        0.5,
        0.012,
        (
            "Each column fixes E (bytes/element) with R×C×E = 16 B; rows are shuffle kinds. "
            "Only FP32 (E=4) and FP16/BF16 (E=2) are shown; smaller-precision formats are "
            "out of scope. Top row: each Oi uses its own srcLane(i) (here a linear permutation "
            "on lanes; broadcast is the special case srcLane(i)=const). "
            "Arrows: value at Oi from Lsrc (schematic; mask/warp participation omitted)."
        ),
        ha="center",
        fontsize=8.5,
        color="#222",
    )
    fig.subplots_adjust(left=0.04, right=0.96, top=0.94, bottom=0.04, hspace=0.42, wspace=0.20)
    fig.savefig(out_path, bbox_inches="tight")
    plt.close(fig)


def main(argv: Sequence[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="Plot NVIDIA warp shuffle 4×2 montage (one PNG).")
    p.add_argument(
        "-o",
        "--out",
        default=os.path.join(os.path.dirname(__file__), "tile16_figures", "nv_shuffle_all.png"),
        help="Output PNG path (directories created if missing).",
    )
    p.add_argument("--dpi", type=int, default=140, help="Figure DPI.")
    args = p.parse_args(list(argv) if argv is not None else None)

    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    _save_montage_4x2(args.out, dpi=args.dpi)
    print(f"Wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
