from __future__ import annotations

from pathlib import Path

try:
    from pycircuit import CycleAwareCircuit, CycleAwareDomain, cas, compile_cycle_aware, mux, wire_of
except ImportError:  # pragma: no cover - allows local tests without pyCircuit.
    CycleAwareCircuit = object  # type: ignore[assignment]
    CycleAwareDomain = object  # type: ignore[assignment]
    cas = None  # type: ignore[assignment]
    compile_cycle_aware = None  # type: ignore[assignment]
    mux = None  # type: ignore[assignment]
    wire_of = None  # type: ignore[assignment]

from .constants import DEFAULT_PIPELINE_L, MODE_2A
from .mac_modes import comb1_generate_products, comb2_reduce_products, comb3_mode_merge
from .pipeline import out1_hold_policy


def _require_pycircuit() -> None:
    if cas is None or compile_cycle_aware is None or wire_of is None or mux is None:
        raise RuntimeError(
            "pyCircuit 未安裝。請先依 README 設定 PYTHONPATH，或安裝 LinxISA/pyCircuit frontend。"
        )


def build(m: CycleAwareCircuit, domain: CycleAwareDomain, latency: int = DEFAULT_PIPELINE_L) -> None:
    """
    Explicit DS pipeline:
    input -> comb0 -> reg0 -> comb1 -> reg1 -> comb2 -> reg2 -> comb3 -> reg3 -> output
    """
    _require_pycircuit()
    if int(latency) != DEFAULT_PIPELINE_L:
        raise ValueError(f"This top-level implementation requires fixed latency={DEFAULT_PIPELINE_L}.")

    _ = m.input("rst_n", width=1)
    in_vld = cas(domain, m.input("vld", width=1), cycle=0)
    in_mode = cas(domain, m.input("mode", width=2), cycle=0)
    in_a = cas(domain, m.input("a", width=80), cycle=0)
    in_b = cas(domain, m.input("b", width=80), cycle=0)
    in_b1 = cas(domain, m.input("b1", width=80), cycle=0)
    in_e1_a = cas(domain, m.input("e1_a", width=2), cycle=0)
    in_e1_b0 = cas(domain, m.input("e1_b0", width=2), cycle=0)
    in_e1_b1 = cas(domain, m.input("e1_b1", width=2), cycle=0)

    # comb0: input-side prelogic and control predecode.
    c0_is_2a = in_mode == 0
    c0_is_2b = in_mode == 1
    c0_is_2c = in_mode == 2
    c0_is_2d = in_mode == 3
    c0_out1_en = in_mode != MODE_2A

    # reg0: input capture boundary (cycle-aware sampled transaction).
    domain.next()
    s0_vld = cas(domain, domain.cycle(in_vld, name="pe_int_s0_vld"), cycle=0)
    s0_mode = cas(domain, domain.cycle(in_mode, name="pe_int_s0_mode"), cycle=0)
    s0_is_2a = cas(domain, domain.cycle(c0_is_2a, name="pe_int_s0_is_2a"), cycle=0)
    s0_is_2b = cas(domain, domain.cycle(c0_is_2b, name="pe_int_s0_is_2b"), cycle=0)
    s0_is_2c = cas(domain, domain.cycle(c0_is_2c, name="pe_int_s0_is_2c"), cycle=0)
    s0_is_2d = cas(domain, domain.cycle(c0_is_2d, name="pe_int_s0_is_2d"), cycle=0)
    s0_out1_en = cas(domain, domain.cycle(c0_out1_en, name="pe_int_s0_out1_en"), cycle=0)
    s0_a = cas(domain, domain.cycle(in_a, name="pe_int_s0_a"), cycle=0)
    s0_b = cas(domain, domain.cycle(in_b, name="pe_int_s0_b"), cycle=0)
    s0_b1 = cas(domain, domain.cycle(in_b1, name="pe_int_s0_b1"), cycle=0)
    s0_e1_a = cas(domain, domain.cycle(in_e1_a, name="pe_int_s0_e1_a"), cycle=0)
    s0_e1_b0 = cas(domain, domain.cycle(in_e1_b0, name="pe_int_s0_e1_b0"), cycle=0)
    s0_e1_b1 = cas(domain, domain.cycle(in_e1_b1, name="pe_int_s0_e1_b1"), cycle=0)

    # comb1: lane decode + lane multipliers.
    c1_products = comb1_generate_products(s0_a, s0_b, s0_b1)

    # reg1: product vectors + aligned controls.
    domain.next()
    s1_vld = cas(domain, domain.cycle(s0_vld, name="pe_int_s1_vld"), cycle=0)
    s1_mode = cas(domain, domain.cycle(s0_mode, name="pe_int_s1_mode"), cycle=0)
    s1_is_2a = cas(domain, domain.cycle(s0_is_2a, name="pe_int_s1_is_2a"), cycle=0)
    s1_is_2b = cas(domain, domain.cycle(s0_is_2b, name="pe_int_s1_is_2b"), cycle=0)
    s1_is_2c = cas(domain, domain.cycle(s0_is_2c, name="pe_int_s1_is_2c"), cycle=0)
    s1_is_2d = cas(domain, domain.cycle(s0_is_2d, name="pe_int_s1_is_2d"), cycle=0)
    s1_out1_en = cas(domain, domain.cycle(s0_out1_en, name="pe_int_s1_out1_en"), cycle=0)
    s1_e1_a = cas(domain, domain.cycle(s0_e1_a, name="pe_int_s1_e1_a"), cycle=0)
    s1_e1_b0 = cas(domain, domain.cycle(s0_e1_b0, name="pe_int_s1_e1_b0"), cycle=0)
    s1_e1_b1 = cas(domain, domain.cycle(s0_e1_b1, name="pe_int_s1_e1_b1"), cycle=0)
    s1_products = {
        "p2a": [cas(domain, domain.cycle(c1_products["p2a"][i], name=f"pe_int_s1_p2a_{i}"), cycle=0) for i in range(8)],
        "p2b0": [cas(domain, domain.cycle(c1_products["p2b0"][i], name=f"pe_int_s1_p2b0_{i}"), cycle=0) for i in range(8)],
        "p2b1": [cas(domain, domain.cycle(c1_products["p2b1"][i], name=f"pe_int_s1_p2b1_{i}"), cycle=0) for i in range(8)],
        "p2d0": [cas(domain, domain.cycle(c1_products["p2d0"][i], name=f"pe_int_s1_p2d0_{i}"), cycle=0) for i in range(8)],
        "p2d1": [cas(domain, domain.cycle(c1_products["p2d1"][i], name=f"pe_int_s1_p2d1_{i}"), cycle=0) for i in range(8)],
        "p2c0": [cas(domain, domain.cycle(c1_products["p2c0"][i], name=f"pe_int_s1_p2c0_{i}"), cycle=0) for i in range(16)],
        "p2c1": [cas(domain, domain.cycle(c1_products["p2c1"][i], name=f"pe_int_s1_p2c1_{i}"), cycle=0) for i in range(16)],
    }

    # comb2: dot reductions.
    c2_reduced = comb2_reduce_products(s1_products)

    # reg2: reduced sums + aligned controls.
    domain.next()
    s2_vld = cas(domain, domain.cycle(s1_vld, name="pe_int_s2_vld"), cycle=0)
    s2_mode = cas(domain, domain.cycle(s1_mode, name="pe_int_s2_mode"), cycle=0)
    s2_is_2a = cas(domain, domain.cycle(s1_is_2a, name="pe_int_s2_is_2a"), cycle=0)
    s2_is_2b = cas(domain, domain.cycle(s1_is_2b, name="pe_int_s2_is_2b"), cycle=0)
    s2_is_2c = cas(domain, domain.cycle(s1_is_2c, name="pe_int_s2_is_2c"), cycle=0)
    s2_is_2d = cas(domain, domain.cycle(s1_is_2d, name="pe_int_s2_is_2d"), cycle=0)
    s2_out1_en = cas(domain, domain.cycle(s1_out1_en, name="pe_int_s2_out1_en"), cycle=0)
    s2_e1_a = cas(domain, domain.cycle(s1_e1_a, name="pe_int_s2_e1_a"), cycle=0)
    s2_e1_b0 = cas(domain, domain.cycle(s1_e1_b0, name="pe_int_s2_e1_b0"), cycle=0)
    s2_e1_b1 = cas(domain, domain.cycle(s1_e1_b1, name="pe_int_s2_e1_b1"), cycle=0)
    s2_reduced = {
        "s2a": cas(domain, domain.cycle(c2_reduced["s2a"], name="pe_int_s2_s2a"), cycle=0),
        "s2b0": cas(domain, domain.cycle(c2_reduced["s2b0"], name="pe_int_s2_s2b0"), cycle=0),
        "s2b1": cas(domain, domain.cycle(c2_reduced["s2b1"], name="pe_int_s2_s2b1"), cycle=0),
        "s2d0": cas(domain, domain.cycle(c2_reduced["s2d0"], name="pe_int_s2_s2d0"), cycle=0),
        "s2d1": cas(domain, domain.cycle(c2_reduced["s2d1"], name="pe_int_s2_s2d1"), cycle=0),
        "s2c0_lo": cas(domain, domain.cycle(c2_reduced["s2c0_lo"], name="pe_int_s2_s2c0_lo"), cycle=0),
        "s2c0_hi": cas(domain, domain.cycle(c2_reduced["s2c0_hi"], name="pe_int_s2_s2c0_hi"), cycle=0),
        "s2c1_lo": cas(domain, domain.cycle(c2_reduced["s2c1_lo"], name="pe_int_s2_s2c1_lo"), cycle=0),
        "s2c1_hi": cas(domain, domain.cycle(c2_reduced["s2c1_hi"], name="pe_int_s2_s2c1_hi"), cycle=0),
    }

    # comb3: post-scale, mode-merge and out1 hold-next preparation.
    c3_out0_raw, c3_out1_raw = comb3_mode_merge(s2_reduced, s2_mode, s2_e1_a, s2_e1_b0, s2_e1_b1)
    c3_vld_out = s2_vld
    c3_out1_update = s2_vld & s2_out1_en

    # reg3: output register boundary and explicit out1 hold register update.
    out1 = out1_hold_policy(
        domain,
        vld_aligned=c3_vld_out,
        out1_en_aligned=s2_out1_en,
        out1_aligned=wire_of(c3_out1_raw)[0:16],
        prefix="pe_int",
    )
    domain.next()
    out_vld = cas(domain, domain.cycle(c3_vld_out, name="pe_int_s3_vld_out"), cycle=0)
    out0 = cas(domain, domain.cycle(wire_of(c3_out0_raw)[0:19], name="pe_int_s3_out0"), cycle=0)
    out1 = cas(domain, domain.cycle(out1, name="pe_int_s3_out1"), cycle=0)

    m.output("vld_out", wire_of(out_vld))
    m.output("out0", wire_of(out0))
    m.output("out1", wire_of(out1))


build.__pycircuit_name__ = "pe_int_fix7"


def emit_mlir(latency: int = DEFAULT_PIPELINE_L) -> str:
    _require_pycircuit()
    return compile_cycle_aware(build, name="pe_int", eager=True, latency=int(latency)).emit_mlir()


if __name__ == "__main__":
    out = Path(__file__).resolve().parents[2] / "build" / "pe_int.mlir"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(emit_mlir(), encoding="utf-8")
    print(f"Wrote {out}")
