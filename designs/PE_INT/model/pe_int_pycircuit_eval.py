from __future__ import annotations

from dataclasses import dataclass
from typing import List

from ref_model import MODE_2A, MacResult, compute_transaction, to_signed

DEFAULT_PIPELINE_L = 4


@dataclass
class StagePayload:
    vld: int = 0
    mode: int = 0
    out0_19: int = 0
    out1_16: int = 0


@dataclass
class StepResult:
    vld_out: int
    out0: int
    out1: int


class PEIntL3Model:
    """
    Cycle-accurate model with normative 0-based latency:
    accepted at t0 -> committed output at t0+4.
    """

    def __init__(self) -> None:
        self.pipe: List[StagePayload] = [StagePayload() for _ in range(DEFAULT_PIPELINE_L)]
        self.out0 = 0
        self.out1 = 0
        self.vld_out = 0
        self.out1_hold = 0

    def reset(self) -> None:
        self.pipe = [StagePayload() for _ in range(DEFAULT_PIPELINE_L)]
        self.out0 = 0
        self.out1 = 0
        self.vld_out = 0
        self.out1_hold = 0

    def _next_stage0(
        self,
        *,
        vld: int,
        mode: int,
        a: int,
        b: int,
        b1: int,
        e1_a: tuple[int, int],
        e1_b0: tuple[int, int],
        e1_b1: tuple[int, int],
    ) -> StagePayload:
        if not vld:
            return StagePayload(vld=0, mode=mode & 0x3, out0_19=0, out1_16=0)
        mac: MacResult = compute_transaction(mode, a, b, b1, e1_a, e1_b0, e1_b1)
        return StagePayload(
            vld=1,
            mode=mode & 0x3,
            out0_19=to_signed(mac.out0_19, 19),
            out1_16=to_signed(mac.out1_16, 16),
        )

    def step(
        self,
        *,
        rst_n: int,
        vld: int,
        mode: int,
        a: int,
        b: int,
        b1: int,
        e1_a: tuple[int, int],
        e1_b0: tuple[int, int],
        e1_b1: tuple[int, int],
    ) -> StepResult:
        if not rst_n:
            self.reset()
            return StepResult(vld_out=0, out0=0, out1=0)

        commit = self.pipe[-1]
        if commit.vld:
            self.out0 = to_signed(commit.out0_19, 19)
            if commit.mode != MODE_2A:
                self.out1_hold = to_signed(commit.out1_16, 16)
        self.vld_out = int(commit.vld)
        self.out1 = to_signed(self.out1_hold, 16)

        for idx in range(DEFAULT_PIPELINE_L - 1, 0, -1):
            prev = self.pipe[idx - 1]
            self.pipe[idx] = StagePayload(prev.vld, prev.mode, prev.out0_19, prev.out1_16)
        self.pipe[0] = self._next_stage0(
            vld=vld,
            mode=mode,
            a=a,
            b=b,
            b1=b1,
            e1_a=e1_a,
            e1_b0=e1_b0,
            e1_b1=e1_b1,
        )
        return StepResult(vld_out=self.vld_out, out0=self.out0, out1=self.out1)
