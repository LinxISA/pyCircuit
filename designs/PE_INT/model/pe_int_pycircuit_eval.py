from __future__ import annotations

from dataclasses import dataclass

from ref_model import MODE_2A, MacResult, compute_transaction, to_signed

DEFAULT_PIPELINE_L = 3


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
    """Cycle-accurate PE_INT model for golden checking."""

    def __init__(self) -> None:
        self.s0 = StagePayload()
        self.s1 = StagePayload()
        self.s2 = StagePayload()
        self.out0 = 0
        self.out1 = 0
        self.vld_out = 0
        # If reset has never been asserted, skip release window at power-up.
        self._rst_release_count = 2

    def reset(self) -> None:
        self.s0 = StagePayload()
        self.s1 = StagePayload()
        self.s2 = StagePayload()
        self.out0 = 0
        self.out1 = 0
        self.vld_out = 0
        self._rst_release_count = 0

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

        if self._rst_release_count < 2:
            self._rst_release_count += 1
            self.s0 = StagePayload()
            self.s1 = StagePayload()
            self.s2 = StagePayload()
            self.vld_out = 0
            self.out0 = 0
            self.out1 = 0
            return StepResult(vld_out=0, out0=0, out1=0)

        new_vld_out = self.s2.vld
        new_out0 = self.out0
        new_out1 = self.out1
        if self.s2.vld:
            new_out0 = to_signed(self.s2.out0_19, 19)
            if self.s2.mode != MODE_2A:
                new_out1 = to_signed(self.s2.out1_16, 16)

        if vld:
            mac: MacResult = compute_transaction(mode, a, b, b1, e1_a, e1_b0, e1_b1)
            next_s0 = StagePayload(
                vld=1,
                mode=mode & 0x3,
                out0_19=to_signed(mac.out0_19, 19),
                out1_16=to_signed(mac.out1_16, 16),
            )
        else:
            next_s0 = StagePayload(vld=0, mode=mode & 0x3, out0_19=0, out1_16=0)

        self.s2 = StagePayload(self.s1.vld, self.s1.mode, self.s1.out0_19, self.s1.out1_16)
        self.s1 = StagePayload(self.s0.vld, self.s0.mode, self.s0.out0_19, self.s0.out1_16)
        self.s0 = next_s0

        self.vld_out = new_vld_out
        self.out0 = to_signed(new_out0, 19)
        self.out1 = to_signed(new_out1, 16)
        return StepResult(vld_out=self.vld_out, out0=self.out0, out1=self.out1)
