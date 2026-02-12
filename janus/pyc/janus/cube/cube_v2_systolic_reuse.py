"""Cube v2 Pipelined Systolic Array with Module Reuse.

Uses m.instance() to instantiate PE modules, reducing generated code size.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from pycircuit import Circuit, Wire

from janus.cube.cube_v2_consts import (
    ACC_IDX_WIDTH,
    ARRAY_SIZE,
    INPUT_WIDTH,
    L0_IDX_WIDTH,
    OUTPUT_WIDTH,
)
from janus.cube.cube_v2_pe import build_pe
from janus.cube.cube_v2_types import Uop
from janus.cube.util import Consts

if TYPE_CHECKING:
    from pycircuit import Reg

# Number of PE Clusters (pipeline stages)
NUM_CLUSTERS = 4
# Rows per cluster
ROWS_PER_CLUSTER = ARRAY_SIZE // NUM_CLUSTERS  # 16 / 4 = 4


@dataclass(frozen=True)
class PipelineStageRegs:
    """Registers for one pipeline stage."""
    valid: Reg
    l0a_idx: Reg
    l0b_idx: Reg
    acc_idx: Reg
    is_first: Reg
    is_last: Reg


def _make_pipeline_stage_regs(
    m: Circuit, clk: Wire, rst: Wire, consts: Consts, stage: int
) -> PipelineStageRegs:
    """Create registers for a pipeline stage."""
    with m.scope(f"pipe_stage_{stage}"):
        return PipelineStageRegs(
            valid=m.out("valid", clk=clk, rst=rst, width=1, init=0, en=consts.one1),
            l0a_idx=m.out("l0a_idx", clk=clk, rst=rst, width=L0_IDX_WIDTH, init=0, en=consts.one1),
            l0b_idx=m.out("l0b_idx", clk=clk, rst=rst, width=L0_IDX_WIDTH, init=0, en=consts.one1),
            acc_idx=m.out("acc_idx", clk=clk, rst=rst, width=ACC_IDX_WIDTH, init=0, en=consts.one1),
            is_first=m.out("is_first", clk=clk, rst=rst, width=1, init=0, en=consts.one1),
            is_last=m.out("is_last", clk=clk, rst=rst, width=1, init=0, en=consts.one1),
        )


def _make_partial_sum_regs(
    m: Circuit, clk: Wire, rst: Wire, consts: Consts, stage: int
) -> list:
    """Create partial sum registers between pipeline stages."""
    regs = []
    with m.scope(f"partial_sums_{stage}"):
        for row in range(ROWS_PER_CLUSTER):
            row_regs = []
            for col in range(ARRAY_SIZE):
                reg = m.out(
                    f"r{row}_c{col}",
                    clk=clk,
                    rst=rst,
                    width=OUTPUT_WIDTH,
                    init=0,
                    en=consts.one1,
                )
                row_regs.append(reg)
            regs.append(row_regs)
    return regs


def build_pipelined_systolic_array_reuse(
    m: Circuit,
    *,
    clk: Wire,
    rst: Wire,
    consts: Consts,
    issue_valid: Wire,
    issue_uop: Uop,
    l0a_data: list[list[Wire]],
    l0b_data: list[list[Wire]],
    stall: Wire,
) -> tuple[list[PipelineStageRegs], Wire, list[Wire], Wire, Wire, Wire, Wire]:
    """Build 4-stage pipelined systolic array using PE module instances.

    Returns:
        (pipe_regs, write_valid, write_data, write_acc_idx, write_is_first, write_is_last, busy)
    """
    c = m.const

    with m.scope("PIPELINED_SYSTOLIC"):
        # Create pipeline stage registers
        pipe_regs = []
        for stage in range(NUM_CLUSTERS):
            regs = _make_pipeline_stage_regs(m, clk, rst, consts, stage)
            pipe_regs.append(regs)

        # Create partial sum registers between stages
        partial_sum_regs = []
        for stage in range(NUM_CLUSTERS):
            regs = _make_partial_sum_regs(m, clk, rst, consts, stage)
            partial_sum_regs.append(regs)

        # Pipeline advancement (when not stalled)
        advance = ~stall

        with m.scope("PIPELINE_CTRL"):
            # Stage 0: Accept new uop from issue queue
            pipe_regs[0].valid.set(issue_valid, when=advance)
            pipe_regs[0].l0a_idx.set(issue_uop.l0a_idx, when=advance & issue_valid)
            pipe_regs[0].l0b_idx.set(issue_uop.l0b_idx, when=advance & issue_valid)
            pipe_regs[0].acc_idx.set(issue_uop.acc_idx, when=advance & issue_valid)
            pipe_regs[0].is_first.set(issue_uop.is_first, when=advance & issue_valid)
            pipe_regs[0].is_last.set(issue_uop.is_last, when=advance & issue_valid)

            # Clear stage 0 if no new uop
            pipe_regs[0].valid.set(consts.zero1, when=advance & ~issue_valid)

            # Stages 1-3: Shift from previous stage
            for stage in range(1, NUM_CLUSTERS):
                prev = pipe_regs[stage - 1]
                curr = pipe_regs[stage]

                curr.valid.set(prev.valid.out(), when=advance)
                curr.l0a_idx.set(prev.l0a_idx.out(), when=advance)
                curr.l0b_idx.set(prev.l0b_idx.out(), when=advance)
                curr.acc_idx.set(prev.acc_idx.out(), when=advance)
                curr.is_first.set(prev.is_first.out(), when=advance)
                curr.is_last.set(prev.is_last.out(), when=advance)

        # Instantiate PEs for each cluster using m.instance()
        with m.scope("COMPUTE"):
            for cluster in range(NUM_CLUSTERS):
                stage_valid = pipe_regs[cluster].valid.out()
                compute = stage_valid & advance
                is_first = pipe_regs[cluster].is_first.out()
                clear_acc = is_first & c(1 if cluster == 0 else 0, width=1)

                base_row = cluster * ROWS_PER_CLUSTER

                for row in range(ROWS_PER_CLUSTER):
                    for col in range(ARRAY_SIZE):
                        # Get incoming partial sum
                        if cluster == 0:
                            partial_in = c(0, width=OUTPUT_WIDTH)
                        else:
                            partial_in = partial_sum_regs[cluster - 1][row][col].out()

                        # Build port connections for PE instance
                        pe_ports = {
                            "clk": clk,
                            "rst": rst,
                            "compute": compute,
                            "clear_acc": clear_acc,
                            "partial_in": partial_in,
                        }

                        # Add L0A row inputs
                        for k in range(ARRAY_SIZE):
                            pe_ports[f"a{k}"] = l0a_data[base_row + row][k]

                        # Add L0B column inputs
                        for k in range(ARRAY_SIZE):
                            pe_ports[f"b{k}"] = l0b_data[k][col]

                        # Instantiate PE
                        pe_result = m.instance(
                            build_pe,
                            name=f"pe_c{cluster}_r{row}_c{col}",
                            **pe_ports,
                        )

                        # Store result to partial sum register
                        partial_sum_regs[cluster][row][col].set(
                            pe_result["result"], when=compute
                        )

        # Output from final stage (cluster 3)
        with m.scope("OUTPUT"):
            final_stage = pipe_regs[NUM_CLUSTERS - 1]
            write_valid = final_stage.valid.out() & advance
            write_acc_idx = final_stage.acc_idx.out()
            write_is_first = final_stage.is_first.out()
            write_is_last = final_stage.is_last.out()

            # Collect all 256 results (16 rows × 16 cols)
            write_data = []
            for cluster in range(NUM_CLUSTERS):
                for row in range(ROWS_PER_CLUSTER):
                    for col in range(ARRAY_SIZE):
                        write_data.append(partial_sum_regs[cluster][row][col].out())

        # Busy signal (any stage has valid data)
        busy = consts.zero1
        for stage in range(NUM_CLUSTERS):
            busy = busy | pipe_regs[stage].valid.out()

        return (
            pipe_regs,
            write_valid,
            write_data,
            write_acc_idx,
            write_is_first,
            write_is_last,
            busy,
        )
