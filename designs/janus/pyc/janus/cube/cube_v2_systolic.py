"""Cube v2 Pipelined Systolic Array Implementation.

4-stage PE Cluster pipeline architecture:
- 4 PE Clusters, each processing 4 rows × 16 columns = 64 PEs
- Pipeline latency: 4 cycles
- Throughput: 1 uop/cycle (after pipeline fill)
- Data forwarding between clusters enables continuous execution

Optimization: Uses tree-based adder reduction for O(log n) depth instead of O(n) cascading.
"""
from __future__ import annotations
from dataclasses import dataclass
from typing import TYPE_CHECKING
from pycircuit import Circuit, Wire, jit_inline, unsigned
from janus.cube.cube_v2_consts import ACC_IDX_WIDTH, ARRAY_SIZE, INPUT_WIDTH, L0_IDX_WIDTH, OUTPUT_WIDTH
from janus.cube.cube_v2_types import PERegs, Uop
from janus.cube.util import Consts
if TYPE_CHECKING:
    from pycircuit import Reg
NUM_CLUSTERS = 4
ROWS_PER_CLUSTER = ARRAY_SIZE // NUM_CLUSTERS

def _tree_reduce_add(values: list[Wire]) -> Wire:
    """Reduce a list of values using tree-based addition for O(log n) depth.

    Args:
        values: List of Wire values to sum

    Returns:
        Sum of all values
    """
    n = len(values)
    if n == 1:
        return values[0]
    if n == 2:
        return values[0] + values[1]
    mid = n // 2
    left = _tree_reduce_add(values[:mid])
    right = _tree_reduce_add(values[mid:])
    return left + right

@dataclass(frozen=True)
class PipelineStageRegs:
    """Registers for one pipeline stage."""
    valid: Reg
    l0a_idx: Reg
    l0b_idx: Reg
    acc_idx: Reg
    is_first: Reg
    is_last: Reg

@dataclass(frozen=True)
class ClusterResult:
    """Result from a PE Cluster."""
    partial_sums: list[Wire]

def _make_pipeline_stage_regs(m: Circuit, clk: Wire, rst: Wire, consts: Consts, stage: int) -> PipelineStageRegs:
    """Create registers for a pipeline stage."""
    with m.scope(f'pipe_stage_{stage}'):
        return PipelineStageRegs(valid=m.out('valid', clk=clk, rst=rst, width=1, init=0, en=consts.one1), l0a_idx=m.out('l0a_idx', clk=clk, rst=rst, width=L0_IDX_WIDTH, init=0, en=consts.one1), l0b_idx=m.out('l0b_idx', clk=clk, rst=rst, width=L0_IDX_WIDTH, init=0, en=consts.one1), acc_idx=m.out('acc_idx', clk=clk, rst=rst, width=ACC_IDX_WIDTH, init=0, en=consts.one1), is_first=m.out('is_first', clk=clk, rst=rst, width=1, init=0, en=consts.one1), is_last=m.out('is_last', clk=clk, rst=rst, width=1, init=0, en=consts.one1))

def _make_partial_sum_regs(m: Circuit, clk: Wire, rst: Wire, consts: Consts, stage: int) -> list:
    """Create partial sum registers between pipeline stages."""
    regs = []
    with m.scope(f'partial_sums_{stage}'):
        for row in range(ROWS_PER_CLUSTER):
            row_regs = []
            for col in range(ARRAY_SIZE):
                reg = m.out(f'r{row}_c{col}', clk=clk, rst=rst, width=OUTPUT_WIDTH, init=0, en=consts.one1)
                row_regs.append(reg)
            regs.append(row_regs)
    return regs

def _make_cluster_pe_regs(m: Circuit, clk: Wire, rst: Wire, consts: Consts, cluster: int) -> list[list[PERegs]]:
    """Create PE registers for one cluster (4 rows × 16 cols)."""
    pe_array = []
    base_row = cluster * ROWS_PER_CLUSTER
    with m.scope(f'cluster_{cluster}'):
        for row in range(ROWS_PER_CLUSTER):
            pe_row = []
            for col in range(ARRAY_SIZE):
                with m.scope(f'pe_r{base_row + row}_c{col}'):
                    pe_regs = PERegs(weight=m.out('weight', clk=clk, rst=rst, width=INPUT_WIDTH, init=0, en=consts.one1), acc=m.out('acc', clk=clk, rst=rst, width=OUTPUT_WIDTH, init=0, en=consts.one1))
                    pe_row.append(pe_regs)
            pe_array.append(pe_row)
    return pe_array

@jit_inline
def _build_pe_cluster(m: Circuit, *, consts: Consts, cluster_idx: int, compute: Wire, clear_acc: Wire, l0a_rows: list[list[Wire]], l0b_matrix: list[list[Wire]], partial_in: list[list[Wire]], pe_array: list[list[PERegs]]) -> list[list[Wire]]:
    """Build one PE Cluster (4 rows × 16 cols = 64 PEs).

    Each PE computes: result = sum(A[row][k] × B[k][col] for k in 0..15) + partial_in

    Returns:
        partial_out: 4 rows × 16 cols of partial sums
    """
    c = m.const
    with m.scope(f'CLUSTER_{cluster_idx}'):
        partial_out = []
        for row in range(ROWS_PER_CLUSTER):
            row_results = []
            for col in range(ARRAY_SIZE):
                with m.scope(f'pe_r{row}_c{col}'):
                    products = []
                    for k in range(ARRAY_SIZE):
                        a_elem = l0a_rows[row][k]
                        b_elem = l0b_matrix[k][col]
                        product = unsigned(a_elem) + unsigned(b_elem)
                        products.append(product)
                    dot_product = _tree_reduce_add(products)
                    incoming = partial_in[row][col] if partial_in else c(0, width=OUTPUT_WIDTH)
                    current_acc = pe_array[row][col].acc.out()
                    acc_base = c(0, width=OUTPUT_WIDTH) if clear_acc else current_acc
                    result = acc_base + dot_product + incoming
                    pe_array[row][col].acc.set(result, when=compute)
                    row_results.append(result)
            partial_out.append(row_results)
        return partial_out

@jit_inline
def build_pipelined_systolic_array(m: Circuit, *, clk: Wire, rst: Wire, consts: Consts, issue_valid: Wire, issue_uop: Uop, l0a_data: list[list[Wire]], l0b_data: list[list[Wire]], stall: Wire) -> tuple[list[PipelineStageRegs], Wire, list[Wire], Wire, Wire, Wire, Wire]:
    """Build 4-stage pipelined systolic array.

    Returns:
        (pipe_regs, write_valid, write_data, write_acc_idx, write_is_first, write_is_last, busy)
    """
    c = m.const
    with m.scope('PIPELINED_SYSTOLIC'):
        pipe_regs = []
        for stage in range(NUM_CLUSTERS):
            regs = _make_pipeline_stage_regs(m, clk, rst, consts, stage)
            pipe_regs.append(regs)
        partial_sum_regs = []
        for stage in range(NUM_CLUSTERS):
            regs = _make_partial_sum_regs(m, clk, rst, consts, stage)
            partial_sum_regs.append(regs)
        cluster_pes = []
        for cluster in range(NUM_CLUSTERS):
            pes = _make_cluster_pe_regs(m, clk, rst, consts, cluster)
            cluster_pes.append(pes)
        advance = ~stall
        with m.scope('PIPELINE_CTRL'):
            pipe_regs[0].valid.set(issue_valid, when=advance)
            pipe_regs[0].l0a_idx.set(issue_uop.l0a_idx, when=advance & issue_valid)
            pipe_regs[0].l0b_idx.set(issue_uop.l0b_idx, when=advance & issue_valid)
            pipe_regs[0].acc_idx.set(issue_uop.acc_idx, when=advance & issue_valid)
            pipe_regs[0].is_first.set(issue_uop.is_first, when=advance & issue_valid)
            pipe_regs[0].is_last.set(issue_uop.is_last, when=advance & issue_valid)
            pipe_regs[0].valid.set(consts.zero1, when=advance & ~issue_valid)
            for stage in range(1, NUM_CLUSTERS):
                prev = pipe_regs[stage - 1]
                curr = pipe_regs[stage]
                curr.valid.set(prev.valid.out(), when=advance)
                curr.l0a_idx.set(prev.l0a_idx.out(), when=advance)
                curr.l0b_idx.set(prev.l0b_idx.out(), when=advance)
                curr.acc_idx.set(prev.acc_idx.out(), when=advance)
                curr.is_first.set(prev.is_first.out(), when=advance)
                curr.is_last.set(prev.is_last.out(), when=advance)
        with m.scope('COMPUTE'):
            for cluster in range(NUM_CLUSTERS):
                stage_valid = pipe_regs[cluster].valid.out()
                compute = stage_valid & advance
                is_first = pipe_regs[cluster].is_first.out()
                clear_acc = is_first & c(1 if cluster == 0 else 0, width=1)
                base_row = cluster * ROWS_PER_CLUSTER
                l0a_rows = l0a_data[base_row:base_row + ROWS_PER_CLUSTER]
                if cluster == 0:
                    partial_in = None
                else:
                    partial_in = [[partial_sum_regs[cluster - 1][row][col].out() for col in range(ARRAY_SIZE)] for row in range(ROWS_PER_CLUSTER)]
                partial_out = _build_pe_cluster(m, consts=consts, cluster_idx=cluster, compute=compute, clear_acc=clear_acc, l0a_rows=l0a_rows, l0b_matrix=l0b_data, partial_in=partial_in, pe_array=cluster_pes[cluster])
                for row in range(ROWS_PER_CLUSTER):
                    for col in range(ARRAY_SIZE):
                        partial_sum_regs[cluster][row][col].set(partial_out[row][col], when=compute)
        with m.scope('OUTPUT'):
            final_stage = pipe_regs[NUM_CLUSTERS - 1]
            write_valid = final_stage.valid.out() & advance
            write_acc_idx = final_stage.acc_idx.out()
            write_is_first = final_stage.is_first.out()
            write_is_last = final_stage.is_last.out()
            write_data = []
            for cluster in range(NUM_CLUSTERS):
                for row in range(ROWS_PER_CLUSTER):
                    for col in range(ARRAY_SIZE):
                        write_data.append(partial_sum_regs[cluster][row][col].out())
        busy = consts.zero1
        for stage in range(NUM_CLUSTERS):
            busy = busy | pipe_regs[stage].valid.out()
        return (pipe_regs, write_valid, write_data, write_acc_idx, write_is_first, write_is_last, busy)
build_systolic_array = build_pipelined_systolic_array
