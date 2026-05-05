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

from .constants import DEFAULT_PIPELINE_L, OUT0_W, OUT1_W, SPEC_LATENCY_L
from .mac_modes import comb1_generate_products, comb2_reduce_products, comb3_mode_merge


def _require_pycircuit() -> None:
    if cas is None or compile_cycle_aware is None or wire_of is None or mux is None:
        raise RuntimeError(
            "pyCircuit 未安裝。請先依 README 設定 PYTHONPATH，或安裝 LinxISA/pyCircuit frontend。"
        )


def _reg_sig(domain: CycleAwareDomain, value, name: str):
    return cas(domain, domain.cycle(value, name=name), cycle=0)


def _reg_vector(domain: CycleAwareDomain, values, *, prefix: str):
    return [_reg_sig(domain, val, f"{prefix}_{idx}") for idx, val in enumerate(values)]


def build(m: CycleAwareCircuit, domain: CycleAwareDomain, latency: int = DEFAULT_PIPELINE_L) -> None:
    """
    Explicit DS pipeline:
    input -> comb0 -> reg0 -> comb1 -> reg1 -> comb2 -> reg2 -> comb3 -> reg3/output
    """
    _require_pycircuit()
    if int(latency) != SPEC_LATENCY_L:
        raise ValueError(f"This top-level implementation requires fixed latency={SPEC_LATENCY_L}.")

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

    # reg0: input capture boundary (cycle-aware sampled transaction).
    domain.next()
    s0_vld = _reg_sig(domain, in_vld, "pe_int_s0_vld")
    s0_is_2a = _reg_sig(domain, c0_is_2a, "pe_int_s0_is_2a")
    s0_is_2b = _reg_sig(domain, c0_is_2b, "pe_int_s0_is_2b")
    s0_is_2c = _reg_sig(domain, c0_is_2c, "pe_int_s0_is_2c")
    s0_is_2d = _reg_sig(domain, c0_is_2d, "pe_int_s0_is_2d")
    s0_a = _reg_sig(domain, in_a, "pe_int_s0_a")
    s0_b = _reg_sig(domain, in_b, "pe_int_s0_b")
    s0_b1 = _reg_sig(domain, in_b1, "pe_int_s0_b1")
    s0_e1_a = _reg_sig(domain, in_e1_a, "pe_int_s0_e1_a")
    s0_e1_b0 = _reg_sig(domain, in_e1_b0, "pe_int_s0_e1_b0")
    s0_e1_b1 = _reg_sig(domain, in_e1_b1, "pe_int_s0_e1_b1")

    # comb1: decode + per-lane product generation.
    c1_products = comb1_generate_products(s0_a, s0_b, s0_b1)

    # reg1: post-product boundary.
    domain.next()
    s1_vld = _reg_sig(domain, s0_vld, "pe_int_s1_vld")
    s1_is_2a = _reg_sig(domain, s0_is_2a, "pe_int_s1_is_2a")
    s1_is_2b = _reg_sig(domain, s0_is_2b, "pe_int_s1_is_2b")
    s1_is_2c = _reg_sig(domain, s0_is_2c, "pe_int_s1_is_2c")
    s1_is_2d = _reg_sig(domain, s0_is_2d, "pe_int_s1_is_2d")
    s1_e1_a = _reg_sig(domain, s0_e1_a, "pe_int_s1_e1_a")
    s1_e1_b0 = _reg_sig(domain, s0_e1_b0, "pe_int_s1_e1_b0")
    s1_e1_b1 = _reg_sig(domain, s0_e1_b1, "pe_int_s1_e1_b1")
    s1_products = {
        "p2a": _reg_vector(domain, c1_products["p2a"], prefix="pe_int_s1_p2a"),
        "p2b0": _reg_vector(domain, c1_products["p2b0"], prefix="pe_int_s1_p2b0"),
        "p2b1": _reg_vector(domain, c1_products["p2b1"], prefix="pe_int_s1_p2b1"),
        "p2d0": _reg_vector(domain, c1_products["p2d0"], prefix="pe_int_s1_p2d0"),
        "p2d1": _reg_vector(domain, c1_products["p2d1"], prefix="pe_int_s1_p2d1"),
        "p2c0": _reg_vector(domain, c1_products["p2c0"], prefix="pe_int_s1_p2c0"),
        "p2c1": _reg_vector(domain, c1_products["p2c1"], prefix="pe_int_s1_p2c1"),
    }

    # comb2: dot-product reductions only.
    c2_reduced = comb2_reduce_products(s1_products)

    # reg2: post-reduction boundary.
    domain.next()
    s2_vld = _reg_sig(domain, s1_vld, "pe_int_s2_vld")
    s2_is_2a = _reg_sig(domain, s1_is_2a, "pe_int_s2_is_2a")
    s2_is_2b = _reg_sig(domain, s1_is_2b, "pe_int_s2_is_2b")
    s2_is_2c = _reg_sig(domain, s1_is_2c, "pe_int_s2_is_2c")
    s2_is_2d = _reg_sig(domain, s1_is_2d, "pe_int_s2_is_2d")
    s2_e1_a = _reg_sig(domain, s1_e1_a, "pe_int_s2_e1_a")
    s2_e1_b0 = _reg_sig(domain, s1_e1_b0, "pe_int_s2_e1_b0")
    s2_e1_b1 = _reg_sig(domain, s1_e1_b1, "pe_int_s2_e1_b1")
    s2_s2a = _reg_sig(domain, c2_reduced["s2a"], "pe_int_s2_s2a")
    s2_s2b0 = _reg_sig(domain, c2_reduced["s2b0"], "pe_int_s2_s2b0")
    s2_s2b1 = _reg_sig(domain, c2_reduced["s2b1"], "pe_int_s2_s2b1")
    s2_s2d0 = _reg_sig(domain, c2_reduced["s2d0"], "pe_int_s2_s2d0")
    s2_s2d1 = _reg_sig(domain, c2_reduced["s2d1"], "pe_int_s2_s2d1")
    s2_s2c0_lo = _reg_sig(domain, c2_reduced["s2c0_lo"], "pe_int_s2_s2c0_lo")
    s2_s2c0_hi = _reg_sig(domain, c2_reduced["s2c0_hi"], "pe_int_s2_s2c0_hi")
    s2_s2c1_lo = _reg_sig(domain, c2_reduced["s2c1_lo"], "pe_int_s2_s2c1_lo")
    s2_s2c1_hi = _reg_sig(domain, c2_reduced["s2c1_hi"], "pe_int_s2_s2c1_hi")

    # comb3: pure combinational post-scale + mode merge with reg2 one-hot controls.
    c3_reduced = {
        "s2a": s2_s2a,
        "s2b0": s2_s2b0,
        "s2b1": s2_s2b1,
        "s2d0": s2_s2d0,
        "s2d1": s2_s2d1,
        "s2c0_lo": s2_s2c0_lo,
        "s2c0_hi": s2_s2c0_hi,
        "s2c1_lo": s2_s2c1_lo,
        "s2c1_hi": s2_s2c1_hi,
    }
    c3_out0_raw, c3_out1_raw = comb3_mode_merge(
        c3_reduced,
        s2_e1_a,
        s2_e1_b0,
        s2_e1_b1,
        is_2a=s2_is_2a,
        is_2b=s2_is_2b,
        is_2c=s2_is_2c,
        is_2d=s2_is_2d,
    )
    c3_out0 = wire_of(c3_out0_raw)[0:OUT0_W]
    c3_out1 = wire_of(c3_out1_raw)[0:OUT1_W]

    # reg3/output commit: vld_out/out0/out1 from the same commit boundary.
    out1_hold = domain.state(width=OUT1_W, reset_value=0, name="pe_int_out1_hold")
    out1_commit = mux(
        wire_of(s2_vld),
        mux(wire_of(s2_is_2a), wire_of(out1_hold), c3_out1),
        wire_of(out1_hold),
    )
    out1_hold.set(out1_commit)

    domain.next()
    out_vld = _reg_sig(domain, s2_vld, "pe_int_out_vld")
    out0 = _reg_sig(domain, c3_out0, "pe_int_out0")
    out1 = _reg_sig(domain, out1_commit, "pe_int_out1")

    m.output("vld_out", wire_of(out_vld))
    m.output("out0", wire_of(out0))
    m.output("out1", wire_of(out1))


build.__pycircuit_name__ = "pe_int_ds_v05"


def emit_mlir(latency: int = DEFAULT_PIPELINE_L) -> str:
    _require_pycircuit()
    return compile_cycle_aware(build, name="pe_int", eager=True, latency=int(latency)).emit_mlir()


if __name__ == "__main__":
    out = Path(__file__).resolve().parents[2] / "build" / "pe_int.mlir"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(emit_mlir(), encoding="utf-8")
    print(f"Wrote {out}")
