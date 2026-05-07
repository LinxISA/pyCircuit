from __future__ import annotations

from depth_probe_units import (
    booth_mul_depth,
    dot16_split8_post_scale_depth,
    dot8_reduce_depth,
)


def main() -> None:
    units = [
        booth_mul_depth(8, 8),
        booth_mul_depth(8, 5),
        booth_mul_depth(8, 4),
        booth_mul_depth(5, 5),
        dot8_reduce_depth(16),  # for S8xS8/S8xS5
        dot8_reduce_depth(12),  # for S8xS4
        dot8_reduce_depth(10),  # for S5xS5 lanes
        dot16_split8_post_scale_depth(14),
    ]

    print("=== PE_INT logic-depth probe (manual structural estimate) ===")
    for u in units:
        print(f"{u.name:32s} : {u.logic_layers:2d}  | {u.notes}")

    # Recommended stage partition under ~25 layers target:
    # Stage A: multiplier lanes only
    # Stage B: lane reduce (CSA + final add)
    # Stage C: post-scale / mode-select / output glue
    stage_a = max(
        booth_mul_depth(8, 8).logic_layers,
        booth_mul_depth(8, 5).logic_layers,
        booth_mul_depth(8, 4).logic_layers,
        booth_mul_depth(5, 5).logic_layers,
    )
    stage_b = max(
        dot8_reduce_depth(16).logic_layers,
        dot8_reduce_depth(12).logic_layers,
        dot8_reduce_depth(10).logic_layers,
    )
    # mode select + out1 hold mux + scaling tail
    mode_select_and_hold = 3
    stage_c = dot16_split8_post_scale_depth(14).logic_layers + mode_select_and_hold

    print("\n=== Suggested comb-stage budget ===")
    print(f"comb_mul_gen         : {stage_a:2d}")
    print(f"comb_reduce_dot      : {stage_b:2d}")
    print(f"comb_post_scale_mux  : {stage_c:2d}")


if __name__ == "__main__":
    main()
