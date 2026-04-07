"""Davinci core global parameters.

All architectural widths, counts, and depths derive from Davinci_supersclar.md §1.1.
"""


# ── Scalar register file ─────────────────────────────────────────────
ARCH_GREGS          = 32          # X0–X31
PHYS_GREGS          = 128         # P0–P127
SCALAR_DATA_W       = 64          # 64-bit GPRs
ARCH_GREG_W         = 5           # log2(32)
PHYS_GREG_W         = 7           # log2(128)
SCALAR_FREELIST_SZ  = PHYS_GREGS - ARCH_GREGS  # 96

# ── Tile register file (TRegFile-4K) ─────────────────────────────────
ARCH_TREGS          = 32          # T0–T31
PHYS_TREGS          = 256         # PT0–PT255
TILE_SIZE_BYTES     = 4096        # 4 KB per tile
ARCH_TREG_W         = 5           # log2(32)
PHYS_TREG_W         = 8           # log2(256)
TILE_FREELIST_SZ    = PHYS_TREGS - ARCH_TREGS  # 224
TREGFILE_BANKS      = 64
TREGFILE_GROUPS     = 8
TREGFILE_EPOCH_CY   = 8
TREGFILE_R_PORTS    = 8           # R0–R7
TREGFILE_W_PORTS    = 8           # W0–W7
TREGFILE_PORT_BW    = 512         # bytes per port per cycle

# ── Pipeline widths ──────────────────────────────────────────────────
FETCH_WIDTH         = 4           # instructions per cycle
DECODE_WIDTH        = FETCH_WIDTH
RENAME_WIDTH        = FETCH_WIDTH
DISPATCH_WIDTH      = FETCH_WIDTH
INSTR_WIDTH         = 32          # bits per instruction

# ── Reservation station sizes ────────────────────────────────────────
SCALAR_RS_ENTRIES   = 32
LSU_RS_ENTRIES      = 24
VEC_RS_ENTRIES      = 16
CUBE_RS_ENTRIES     = 4
MTE_RS_ENTRIES      = 16

# ── Issue widths ─────────────────────────────────────────────────────
SCALAR_ISSUE_WIDTH  = 6           # 4 ALU + 1 MUL + 1 BRU
LSU_ISSUE_WIDTH     = 2           # 1 load + 1 store
VEC_ISSUE_WIDTH     = 1
CUBE_ISSUE_WIDTH    = 1
MTE_ISSUE_WIDTH     = 2

# ── Execution latencies (cycles) ────────────────────────────────────
ALU_LATENCY         = 1
MUL_LATENCY         = 4
DIV_LATENCY_MIN     = 12
DIV_LATENCY_MAX     = 20
BRANCH_LATENCY      = 1
LOAD_LATENCY_L1     = 4
LOAD_LATENCY_L2     = 12
STORE_LATENCY       = 4
VEC_LATENCY         = 16          # 2 epochs
CUBE_BASE_LATENCY   = 19          # pipeline + 1 OPA step
MTE_TILELOAD_L2     = 72          # 64 mem + 8 write epoch
MTE_TILECOPY        = 16
MTE_TILEZERO        = 8
MTE_TILEGET         = 9           # 8 read epoch + 1 extract
MTE_TILEPUT         = 16

# ── Branch prediction ────────────────────────────────────────────────
BTB_ENTRIES         = 2048
BTB_WAYS            = 4
RAS_DEPTH           = 16
CHECKPOINT_SLOTS    = 8
CHECKPOINT_W        = 3           # log2(8)
MISPREDICT_PENALTY  = 6

# ── Instruction buffer ───────────────────────────────────────────────
IBUF_ENTRIES        = 16

# ── Load / Store unit ────────────────────────────────────────────────
LOAD_QUEUE_ENTRIES  = 16
STORE_BUF_ENTRIES   = 16
L1D_MSHRS           = 8

# ── CDB / TCB ────────────────────────────────────────────────────────
CDB_PORTS           = 6           # 4 ALU + 1 MUL/LSU + 1 TILE.GET
TCB_PORTS           = 4           # vec + cube + 2× MTE

# ── Scalar reference counting ────────────────────────────────────────
SCALAR_REFCNT_W     = 4           # max 15 concurrent readers
TILE_REFCNT_W       = 3           # max 7 concurrent readers

# ── Opcode domain encoding (opcode[6:5]) ─────────────────────────────
DOMAIN_SCALAR       = 0b00        # 00 or 01
DOMAIN_SCALAR_ALT   = 0b01
DOMAIN_VEC_MTE      = 0b10
DOMAIN_CUBE         = 0b11

# ── Micro-op width ───────────────────────────────────────────────────
UOP_W               = 8           # micro-op opcode width
AGE_W               = 6           # RS age counter width

# ── Scalar RF ports ──────────────────────────────────────────────────
SCALAR_RF_RPORTS    = 12          # 8 rename + 4 issue
SCALAR_RF_WPORTS    = 6           # matches CDB

# ── MTE ──────────────────────────────────────────────────────────────
MTE_ORB_ENTRIES     = 32          # outstanding request buffer
MTE_TRANSPOSE_BUF   = TILE_SIZE_BYTES  # 4 KB

# ── Cache hierarchy ──────────────────────────────────────────────────
L1I_SIZE_KB         = 64
L1I_WAYS            = 4
L1I_LINE_BYTES      = 64
L1D_SIZE_KB         = 64
L1D_WAYS            = 4
L1D_LINE_BYTES      = 64
L2_SIZE_KB          = 512
L2_WAYS             = 8
L2_LINE_BYTES       = 64
