"""Cube v2 Type Definitions using Dataclasses."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pycircuit import Reg, Wire


# =============================================================================
# Uop (Micro-operation) Types
# =============================================================================
@dataclass(frozen=True)
class Uop:
    """Micro-operation for a single 16×16 tile computation."""
    l0a_idx: Wire    # 7-bit: Index into L0A buffer (0-127)
    l0b_idx: Wire    # 7-bit: Index into L0B buffer (0-127)
    acc_idx: Wire    # 7-bit: Index into ACC buffer (0-127)
    is_first: Wire   # 1-bit: First uop for this ACC entry (clear accumulator)
    is_last: Wire    # 1-bit: Last uop for this ACC entry (result ready)


@dataclass(frozen=True)
class UopRegs:
    """Registers for storing a uop in the issue queue."""
    l0a_idx: Reg     # 7-bit
    l0b_idx: Reg     # 7-bit
    acc_idx: Reg     # 7-bit
    is_first: Reg    # 1-bit
    is_last: Reg     # 1-bit


# =============================================================================
# Issue Queue Entry
# =============================================================================
@dataclass(frozen=True)
class IssueQueueEntry:
    """Single entry in the issue queue."""
    valid: Reg       # 1-bit: Entry contains valid uop
    uop: UopRegs     # The micro-operation
    l0a_ready: Reg   # 1-bit: L0A data available
    l0b_ready: Reg   # 1-bit: L0B data available
    acc_ready: Reg   # 1-bit: ACC available for write
    issued: Reg      # 1-bit: Uop has been issued


# =============================================================================
# Buffer Entry Status
# =============================================================================
@dataclass(frozen=True)
class L0EntryStatus:
    """Status for a single L0A or L0B buffer entry."""
    valid: Reg       # 1-bit: Data loaded and ready
    loading: Reg     # 1-bit: Load in progress
    ref_count: Reg   # 8-bit: Number of pending uops using this entry


@dataclass(frozen=True)
class AccEntryStatus:
    """Status for a single ACC buffer entry."""
    valid: Reg       # 1-bit: Result ready for store
    computing: Reg   # 1-bit: Computation in progress
    pending_k: Reg   # 8-bit: Number of remaining K-dimension uops
    storing: Reg     # 1-bit: Store in progress


# =============================================================================
# Main FSM State
# =============================================================================
@dataclass(frozen=True)
class CubeV2State:
    """Main FSM state registers."""
    state: Reg           # 3-bit: Current FSM state
    cycle_count: Reg     # 32-bit: Cycle counter
    done: Reg            # 1-bit: Computation done flag
    busy: Reg            # 1-bit: Busy flag


# =============================================================================
# MATMUL Instruction Registers
# =============================================================================
@dataclass(frozen=True)
class MatmulInst:
    """MATMUL instruction parameters."""
    m: Reg           # 16-bit: Number of rows in A
    k: Reg           # 16-bit: Reduction dimension
    n: Reg           # 16-bit: Number of columns in B
    addr_a: Reg      # 64-bit: Base address of matrix A
    addr_b: Reg      # 64-bit: Base address of matrix B
    addr_c: Reg      # 64-bit: Base address of matrix C


# =============================================================================
# Uop Generator State
# =============================================================================
@dataclass(frozen=True)
class UopGenState:
    """State for uop generation."""
    m_tile: Reg      # 8-bit: Current M tile index
    k_tile: Reg      # 8-bit: Current K tile index
    n_tile: Reg      # 8-bit: Current N tile index
    m_tiles: Reg     # 8-bit: Total M tiles
    k_tiles: Reg     # 8-bit: Total K tiles
    n_tiles: Reg     # 8-bit: Total N tiles
    generating: Reg  # 1-bit: Generation in progress
    gen_done: Reg    # 1-bit: Generation complete


# =============================================================================
# Load/Store Controller State
# =============================================================================
@dataclass(frozen=True)
class LoadStoreState:
    """State for load/store controller."""
    state: Reg           # 4-bit: Current state
    entry_idx: Reg       # 7-bit: Target buffer entry index
    transfer_count: Reg  # 3-bit: Transfer counter (0-3)
    addr: Reg            # 64-bit: Current memory address


# =============================================================================
# Systolic Array State
# =============================================================================
@dataclass(frozen=True)
class SystolicState:
    """State for systolic array execution."""
    active: Reg          # 1-bit: Array is computing
    l0a_idx: Reg         # 7-bit: Current L0A entry being used
    l0b_idx: Reg         # 7-bit: Current L0B entry being used
    acc_idx: Reg         # 7-bit: Target ACC entry
    is_first: Reg        # 1-bit: First uop for this ACC
    is_last: Reg         # 1-bit: Last uop for this ACC
    cycle: Reg           # 6-bit: Cycle within computation (0-31)


# =============================================================================
# PE (Processing Element) Registers
# =============================================================================
@dataclass(frozen=True)
class PERegs:
    """Registers for a single processing element."""
    weight: Reg      # 16-bit: Weight value
    acc: Reg         # 32-bit: Accumulator


# =============================================================================
# Result Types (for function returns)
# =============================================================================
@dataclass(frozen=True)
class FsmResult:
    """Result from FSM logic."""
    load_weight: Wire    # Enable weight loading
    compute: Wire        # Enable computation
    done: Wire           # Computation complete


@dataclass(frozen=True)
class IssueResult:
    """Result from issue queue."""
    issue_valid: Wire    # A uop is being issued
    uop: Uop             # The issued uop


@dataclass(frozen=True)
class MmioReadResult:
    """Result from MMIO read logic."""
    rdata: Wire          # Read data (64-bit)


@dataclass(frozen=True)
class MmioWriteResult:
    """Result from MMIO write logic."""
    start: Wire          # Start signal
    reset_cube: Wire     # Reset signal
    load_l0a: Wire       # Load L0A trigger
    load_l0b: Wire       # Load L0B trigger
    store_acc: Wire      # Store ACC trigger
    entry_idx: Wire      # Entry index for load/store
