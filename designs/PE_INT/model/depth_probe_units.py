from __future__ import annotations

from dataclasses import dataclass
from math import ceil, log2


@dataclass(frozen=True)
class DepthBreakdown:
    name: str
    logic_layers: int
    notes: str


def _clog2(v: int) -> int:
    if v <= 1:
        return 0
    return int(ceil(log2(v)))


def mux2_depth() -> int:
    return 1


def compressor_3to2_depth() -> int:
    # Full-adder equivalent: sum/carry in two-level logic factoring.
    return 2


def compressor_4to2_depth() -> int:
    # Commonly mapped as two cascaded 3:2-equivalent cells.
    return 3


def brent_kung_adder_depth(width: int) -> int:
    # Prefix depth (rough): ~2*log2(n)-1 stages, each stage ~1 eq layer,
    # plus propagate/generate and final XOR overhead.
    lg = _clog2(width)
    return max(3, (2 * lg - 1) + 2)


def booth_mul_depth(a_bits: int, b_bits: int) -> DepthBreakdown:
    groups = ceil(max(a_bits, b_bits) / 2)
    recode = 2  # booth recoder + sign handling
    pp_select = 2  # partial-product select/mux
    compress_tree = max(3, _clog2(groups) * compressor_4to2_depth())
    final_add = brent_kung_adder_depth(a_bits + b_bits)
    total = recode + pp_select + compress_tree + final_add
    return DepthBreakdown(
        name=f"booth_mul_{a_bits}x{b_bits}",
        logic_layers=total,
        notes=(
            f"groups={groups}, recode={recode}, pp_select={pp_select}, "
            f"compress={compress_tree}, final_add={final_add}"
        ),
    )


def dot8_reduce_depth(prod_bits: int) -> DepthBreakdown:
    # Eight products reduced by CSA tree + final CPA.
    csa_levels = 3
    csa_depth = csa_levels * compressor_3to2_depth()
    # Accum width grows by up to +3 bits for 8-term sum.
    final_add = brent_kung_adder_depth(prod_bits + 3)
    total = csa_depth + final_add
    return DepthBreakdown(
        name=f"dot8_reduce_{prod_bits}b",
        logic_layers=total,
        notes=f"csa_levels={csa_levels}, csa_depth={csa_depth}, final_add={final_add}",
    )


def dot16_split8_post_scale_depth(sum_bits: int) -> DepthBreakdown:
    # Two dot8 sums already available. This block does:
    # scale-by-(0/1/2) on lo + scale-by-(0/1/2) on hi + final add.
    shifter_mux = 2 * mux2_depth()  # two-level shift select for x1/x2/x4
    final_add = brent_kung_adder_depth(sum_bits + 1)
    total = shifter_mux + final_add
    return DepthBreakdown(
        name=f"dot16_split8_post_scale_{sum_bits}b",
        logic_layers=total,
        notes=f"shift_mux={shifter_mux}, final_add={final_add}",
    )
