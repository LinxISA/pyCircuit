"""Cube v2 MATMUL Decoder and Uop Generator.

Decomposes MATMUL(M, K, N) instructions into micro-operations (uops) for the systolic array.
Each uop represents a 16×16 tile multiplication.
"""

from __future__ import annotations

from pycircuit import Circuit, Wire, jit_inline

from janus.cube.cube_v2_consts import (
    ACC_IDX_WIDTH,
    ARRAY_SIZE,
    L0_IDX_WIDTH,
    TILE_IDX_WIDTH,
)
from janus.cube.cube_v2_types import MatmulInst, UopGenState
from janus.cube.util import Consts


def _make_matmul_inst_regs(
    m: Circuit, clk: Wire, rst: Wire, consts: Consts
) -> MatmulInst:
    """Create registers for MATMUL instruction parameters."""
    with m.scope("MATMUL_INST"):
        return MatmulInst(
            m=m.out("m", clk=clk, rst=rst, width=16, init=0, en=consts.one1),
            k=m.out("k", clk=clk, rst=rst, width=16, init=0, en=consts.one1),
            n=m.out("n", clk=clk, rst=rst, width=16, init=0, en=consts.one1),
            addr_a=m.out("addr_a", clk=clk, rst=rst, width=64, init=0, en=consts.one1),
            addr_b=m.out("addr_b", clk=clk, rst=rst, width=64, init=0, en=consts.one1),
            addr_c=m.out("addr_c", clk=clk, rst=rst, width=64, init=0, en=consts.one1),
        )


def _make_uop_gen_state(
    m: Circuit, clk: Wire, rst: Wire, consts: Consts
) -> UopGenState:
    """Create registers for uop generation state."""
    with m.scope("UOP_GEN_STATE"):
        return UopGenState(
            m_tile=m.out("m_tile", clk=clk, rst=rst, width=TILE_IDX_WIDTH, init=0, en=consts.one1),
            k_tile=m.out("k_tile", clk=clk, rst=rst, width=TILE_IDX_WIDTH, init=0, en=consts.one1),
            n_tile=m.out("n_tile", clk=clk, rst=rst, width=TILE_IDX_WIDTH, init=0, en=consts.one1),
            m_tiles=m.out("m_tiles", clk=clk, rst=rst, width=TILE_IDX_WIDTH, init=0, en=consts.one1),
            k_tiles=m.out("k_tiles", clk=clk, rst=rst, width=TILE_IDX_WIDTH, init=0, en=consts.one1),
            n_tiles=m.out("n_tiles", clk=clk, rst=rst, width=TILE_IDX_WIDTH, init=0, en=consts.one1),
            generating=m.out("generating", clk=clk, rst=rst, width=1, init=0, en=consts.one1),
            gen_done=m.out("gen_done", clk=clk, rst=rst, width=1, init=0, en=consts.one1),
        )


@jit_inline
def build_matmul_decoder(
    m: Circuit,
    *,
    clk: Wire,
    rst: Wire,
    consts: Consts,
    # Instruction input
    start: Wire,             # Start decoding
    inst_m: Wire,            # M dimension (16-bit)
    inst_k: Wire,            # K dimension (16-bit)
    inst_n: Wire,            # N dimension (16-bit)
    # Queue interface
    queue_full: Wire,        # Issue queue is full
    # Reset
    reset_decoder: Wire,     # Reset decoder state
) -> tuple[MatmulInst, UopGenState, Wire, Wire, Wire, Wire, Wire, Wire, Wire]:
    """Build MATMUL decoder and uop generator.

    Returns:
        (inst, gen_state, uop_valid, uop_l0a_idx, uop_l0b_idx, uop_acc_idx,
         uop_is_first, uop_is_last, gen_done)
    """
    c = m.const

    with m.scope("DECODER"):
        # Create instruction registers
        inst = _make_matmul_inst_regs(m, clk, rst, consts)

        # Create generation state
        gen_state = _make_uop_gen_state(m, clk, rst, consts)

        # Calculate tile counts on start
        # tiles = ceil(dim / 16) = (dim + 15) / 16
        with m.scope("TILE_CALC"):
            tile_size = c(ARRAY_SIZE, width=16)
            tile_mask = c(ARRAY_SIZE - 1, width=16)

            # M tiles
            m_plus = inst_m + tile_mask
            m_tiles_calc = m_plus >> 4  # Divide by 16

            # K tiles
            k_plus = inst_k + tile_mask
            k_tiles_calc = k_plus >> 4

            # N tiles
            n_plus = inst_n + tile_mask
            n_tiles_calc = n_plus >> 4

        # Latch instruction on start
        with m.scope("LATCH"):
            inst.m.set(inst_m, when=start)
            inst.k.set(inst_k, when=start)
            inst.n.set(inst_n, when=start)

            gen_state.m_tiles.set(m_tiles_calc.trunc(width=TILE_IDX_WIDTH), when=start)
            gen_state.k_tiles.set(k_tiles_calc.trunc(width=TILE_IDX_WIDTH), when=start)
            gen_state.n_tiles.set(n_tiles_calc.trunc(width=TILE_IDX_WIDTH), when=start)

            # Reset tile indices
            gen_state.m_tile.set(c(0, width=TILE_IDX_WIDTH), when=start)
            gen_state.k_tile.set(c(0, width=TILE_IDX_WIDTH), when=start)
            gen_state.n_tile.set(c(0, width=TILE_IDX_WIDTH), when=start)

            # Start generating
            gen_state.generating.set(consts.one1, when=start)
            gen_state.gen_done.set(consts.zero1, when=start)

        # Uop generation logic
        with m.scope("UOP_GEN"):
            generating = gen_state.generating.out()
            can_generate = generating & ~queue_full

            # Current tile indices
            m_tile = gen_state.m_tile.out()
            k_tile = gen_state.k_tile.out()
            n_tile = gen_state.n_tile.out()

            m_tiles = gen_state.m_tiles.out()
            k_tiles = gen_state.k_tiles.out()
            n_tiles = gen_state.n_tiles.out()

            # Calculate buffer indices
            # L0A index: m_tile * k_tiles + k_tile (mod 128)
            l0a_idx_full = m_tile * k_tiles + k_tile
            uop_l0a_idx = l0a_idx_full.trunc(width=L0_IDX_WIDTH)

            # L0B index: k_tile * n_tiles + n_tile (mod 128)
            l0b_idx_full = k_tile * n_tiles + n_tile
            uop_l0b_idx = l0b_idx_full.trunc(width=L0_IDX_WIDTH)

            # ACC index: m_tile * n_tiles + n_tile (mod 128)
            acc_idx_full = m_tile * n_tiles + n_tile
            uop_acc_idx = acc_idx_full.trunc(width=ACC_IDX_WIDTH)

            # Determine is_first and is_last
            # is_first: k_tile == 0 (first in K reduction)
            uop_is_first = k_tile.eq(c(0, width=TILE_IDX_WIDTH))

            # is_last: k_tile == k_tiles - 1 (last in K reduction)
            k_tiles_minus_1 = k_tiles - c(1, width=TILE_IDX_WIDTH)
            uop_is_last = k_tile.eq(k_tiles_minus_1)

            # Output valid uop
            uop_valid = can_generate

            # Advance tile indices (iterate: k, n, m order for better locality)
            with m.scope("ADVANCE"):
                # Next k_tile
                k_tile_next = k_tile + c(1, width=TILE_IDX_WIDTH)
                k_wrap = k_tile_next.eq(k_tiles)

                # Next n_tile (when k wraps)
                n_tile_next = n_tile + c(1, width=TILE_IDX_WIDTH)
                n_wrap = n_tile_next.eq(n_tiles)

                # Next m_tile (when n wraps)
                m_tile_next = m_tile + c(1, width=TILE_IDX_WIDTH)
                m_wrap = m_tile_next.eq(m_tiles)

                # All done when m wraps
                all_done = k_wrap & n_wrap & m_wrap

                # Update indices when generating
                # K advances every cycle
                new_k = k_wrap.select(c(0, width=TILE_IDX_WIDTH), k_tile_next)
                gen_state.k_tile.set(new_k, when=can_generate)

                # N advances when K wraps
                new_n = (k_wrap & n_wrap).select(c(0, width=TILE_IDX_WIDTH), n_tile_next)
                gen_state.n_tile.set(new_n, when=can_generate & k_wrap)

                # M advances when N wraps
                gen_state.m_tile.set(m_tile_next, when=can_generate & k_wrap & n_wrap)

                # Done when all tiles generated
                gen_state.generating.set(consts.zero1, when=can_generate & all_done)
                gen_state.gen_done.set(consts.one1, when=can_generate & all_done)

        # Reset logic
        with m.scope("RESET"):
            gen_state.generating.set(consts.zero1, when=reset_decoder)
            gen_state.gen_done.set(consts.zero1, when=reset_decoder)
            gen_state.m_tile.set(c(0, width=TILE_IDX_WIDTH), when=reset_decoder)
            gen_state.k_tile.set(c(0, width=TILE_IDX_WIDTH), when=reset_decoder)
            gen_state.n_tile.set(c(0, width=TILE_IDX_WIDTH), when=reset_decoder)

        gen_done = gen_state.gen_done.out()

        return (
            inst,
            gen_state,
            uop_valid,
            uop_l0a_idx,
            uop_l0b_idx,
            uop_acc_idx,
            uop_is_first,
            uop_is_last,
            gen_done,
        )
