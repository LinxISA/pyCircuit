from __future__ import annotations
from dataclasses import dataclass
from pycircuit import Circuit, Wire, u

@dataclass(frozen=True)
class F3Out:
    window: Wire
    intra_flush: Wire
    cut_mask: Wire

def run_f3(m: Circuit, *, window: Wire, pred_taken: Wire) -> F3Out:
    window = m.wire(window)
    pred_taken = m.wire(pred_taken)
    cut_mask = u(64, 4294967295) if pred_taken else u(64, 18446744073709551615)
    return F3Out(window=window & cut_mask, intra_flush=pred_taken, cut_mask=cut_mask)
