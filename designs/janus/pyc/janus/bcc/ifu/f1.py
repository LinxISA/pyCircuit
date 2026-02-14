from __future__ import annotations
from dataclasses import dataclass
from pycircuit import Circuit, Wire, u

@dataclass(frozen=True)
class F1Out:
    fetch_pc: Wire
    pred_taken: Wire
    pred_target: Wire

def run_f1(m: Circuit, *, fetch_pc: Wire, btb_hit: Wire, btb_target: Wire, bimodal_taken: Wire) -> F1Out:
    fetch_pc = m.wire(fetch_pc)
    btb_hit = m.wire(btb_hit)
    btb_target = m.wire(btb_target)
    bimodal_taken = m.wire(bimodal_taken)
    pred_taken = btb_hit & bimodal_taken
    pred_target = btb_target if pred_taken else fetch_pc + u(64, 8)
    return F1Out(fetch_pc=fetch_pc, pred_taken=pred_taken, pred_target=pred_target)
