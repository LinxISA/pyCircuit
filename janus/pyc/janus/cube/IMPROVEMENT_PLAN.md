# Cube Module Improvement and Integration Plan

---
name: cube-improvement-integration
description: Improve Cube module following linx-isa/linx-skills standards and integrate with Janus BCC
version: 1.0
date: 2026-02-09
---

## Overview

This plan outlines improvements to the existing Cube matrix multiplication accelerator and its integration with the Janus BCC CPU, following standards learned from linx-isa and linx-skills repositories.

**Current Status:**
- ✅ Basic 16×16 systolic array implemented (Cube v1)
- ✅ **Cube v2 implemented with 4-stage pipelined systolic array**
- ✅ **64-entry L0A, L0B, and ACC buffers**
- ✅ **64-entry issue queue with out-of-order execution**
- ✅ **MATMUL block instruction decoder**
- ✅ Memory-mapped interface defined (64-bit MMIO)
- ✅ C++ testbench with 2 tests (testIdentity, testSimple2x2)
- ✅ Verilog (~48MB) and C++ (~51MB) generation working
- ✅ **CUBE_V2_SPEC.md documentation complete**
- ✅ **PDF specification generated**
- ⚠️ Multiplication operator using addition placeholder
- ⚠️ No differential validation with reference model
- ⚠️ No integration with Janus BCC CPU
- ⚠️ Limited test coverage (v2 tests needed)

**Target Maturity Level:** Level 1 (Spec + correctness baseline)

---

## Phase 1: Documentation and Standards Alignment

### 1.1 Create Evidence Index

**Goal:** Establish traceability for architectural decisions

**Tasks:**
- [ ] Create `references/evidence.md` with unique IDs (CUBE-01, CUBE-02, etc.)
- [ ] Document systolic array architecture decision (CUBE-01)
- [ ] Document weight-stationary dataflow choice (CUBE-02)
- [ ] Document memory map layout rationale (CUBE-03)
- [ ] Document FSM state machine design (CUBE-04)
- [ ] Link to source materials (commits, specs, references)

**Done means:**
- Evidence index exists with at least 5 entries
- Each architectural decision has a unique ID and source link
- Future changes can reference evidence IDs

### 1.2 Create Specification Document

**Goal:** Formal specification following linx-isa-manual standards

**Status:** ✅ DONE (2026-02-10)
- Created `CUBE_V2_SPEC.md` with comprehensive architecture documentation
- Generated `CUBE_V2_SPEC.pdf` for distribution

**Tasks:**
- [x] Create `CUBE_V2_SPEC.md` using RFC 2119 normative language
- [x] Define MUST/SHOULD/MAY requirements for:
  - Memory-mapped register behavior
  - FSM state transitions
  - Timing guarantees
  - Error conditions
- [x] Add explicit pseudocode for PE operation
- [x] Document deterministic behavior for all bit patterns
- [ ] Define exception/trap behavior (if applicable)

**Done means:**
- ✅ CUBE_V2_SPEC.md exists with normative language
- ✅ All register bits defined
- ✅ Pseudocode is executable

### 1.3 Create Checklists

**Goal:** Clear completion criteria for all tasks

**Tasks:**
- [ ] Create `references/cube_rtl_checklist.md`
- [ ] Create `references/cube_integration_checklist.md`
- [ ] Create `references/cube_validation_checklist.md`
- [ ] Define "done means" criteria for each item

**Done means:**
- Three checklists exist with clear completion criteria
- Each checklist item has explicit verification method

---

## Phase 2: Trace Schema and Observability

### 2.1 Define Trace Schema Contract

**Goal:** Enable differential validation between C++ model and RTL

**Tasks:**
- [ ] Create `contracts/cube_trace_schema.md`
- [ ] Define mandatory trace fields per cycle:
  - `cycle` - Cycle number
  - `state` - FSM state
  - `pc` - Not applicable (no PC), use state instead
  - `control_valid` - Control register write
  - `control_data` - Control register value
  - `mem_wvalid` - Memory write valid
  - `mem_waddr` - Memory write address
  - `mem_wdata` - Memory write data
  - `mem_raddr` - Memory read address
  - `mem_rdata` - Memory read data
  - `pe_activity` - PE computation activity bitmap
  - `done` - Computation done flag
  - `busy` - Busy flag
- [ ] Define trace output format (CSV or JSON)
- [ ] Document comparison rules

**Done means:**
- Trace schema contract document exists
- All mandatory fields defined with bit widths
- Comparison rules documented

### 2.2 Enhance C++ Testbench Observability

**Goal:** Generate comparable traces from C++ model

**Tasks:**
- [ ] Add trace output to `tb_janus_cube_pyc.cpp`
- [ ] Implement CSV trace writer
- [ ] Log all mandatory trace fields per cycle
- [ ] Add `PYC_TRACE_CSV=1` environment variable
- [ ] Generate reference traces for existing tests

**Done means:**
- C++ testbench generates CSV traces when `PYC_TRACE_CSV=1`
- Traces include all mandatory fields from schema
- Reference traces exist for testIdentity and testSimple2x2

### 2.3 Add RTL Observability

**Goal:** Generate comparable traces from Verilog simulation

**Tasks:**
- [ ] Add trace output to Verilog testbench (if exists)
- [ ] Implement VPI/DPI trace logger
- [ ] Match C++ trace format exactly
- [ ] Test trace generation in simulation

**Done means:**
- Verilog simulation generates CSV traces
- Trace format matches C++ model exactly
- Can compare C++ vs Verilog traces

---

## Phase 3: Expanded Test Coverage

### 3.1 Add Directed Tests

**Goal:** Comprehensive functional coverage

**Tasks:**
- [ ] Test: Zero matrix × matrix = zero
- [ ] Test: Identity matrix × matrix = matrix
- [ ] Test: Diagonal matrix (scaling)
- [ ] Test: Full 16×16 matrix multiplication with known result
- [ ] Test: Maximum values (overflow detection)
- [ ] Test: Minimum values (underflow detection)
- [ ] Test: Random matrices with reference model comparison
- [ ] Test: Back-to-back operations (no reset between)
- [ ] Test: Reset during computation
- [ ] Test: Invalid control sequences

**Done means:**
- At least 10 directed tests implemented
- All tests pass in C++ model
- Reference outputs documented

### 3.2 Add Negative Tests

**Goal:** Verify error handling and illegal states

**Tasks:**
- [ ] Test: Write to read-only status register (should be ignored)
- [ ] Test: Start without loading weights
- [ ] Test: Start during computation (should be ignored)
- [ ] Test: Read results before done
- [ ] Test: Invalid memory addresses
- [ ] Test: Unaligned memory accesses

**Done means:**
- At least 5 negative tests implemented
- Behavior documented in SPEC.md
- Tests verify deterministic error handling

### 3.3 Create Differential Test Framework

**Goal:** Automated comparison between C++ and RTL

**Tasks:**
- [ ] Create `tools/cube_trace_diff.py` (inspired by linx-isa-emulator)
- [ ] Implement cycle-by-cycle trace comparison
- [ ] Report first mismatch as triage anchor
- [ ] Add to regression suite
- [ ] Document usage in README.md

**Done means:**
- `cube_trace_diff.py` exists and works
- Can compare C++ vs RTL traces automatically
- First mismatch reported with context

---

## Phase 4: Regression Infrastructure

### 4.1 Create Central Regression Script

**Goal:** One-command validation (inspired by linx-isa)

**Tasks:**
- [ ] Create `janus/tools/cube_regression.sh`
- [ ] Stage 1: Spec validation (lint, format check)
- [ ] Stage 2: C++ model tests (all testbenches)
- [ ] Stage 3: Verilog generation check
- [ ] Stage 4: Verilog simulation tests
- [ ] Stage 5: Differential validation (C++ vs RTL)
- [ ] Add to CI/CD pipeline

**Done means:**
- `bash janus/tools/cube_regression.sh` runs all validation
- Exit code 0 means all tests pass
- Integrated into main regression suite

### 4.2 Add Coverage Tracking

**Goal:** Ensure all functionality is tested

**Tasks:**
- [ ] Track FSM state coverage (all states visited)
- [ ] Track PE activity coverage (all PEs used)
- [ ] Track memory address coverage (all registers accessed)
- [ ] Track control sequence coverage (all valid sequences)
- [ ] Generate coverage report

**Done means:**
- Coverage tracking implemented
- Report shows 100% FSM state coverage
- Report shows 100% PE coverage

---

## Phase 5: Janus BCC Integration

### 5.1 Define Integration Contract

**Goal:** Specify CPU-accelerator interface

**Tasks:**
- [ ] Create `contracts/cube_bcc_integration.md`
- [ ] Define memory bus protocol (AXI-Lite or custom)
- [ ] Define interrupt mechanism (if needed)
- [ ] Define address space allocation in Janus memory map
- [ ] Define DMA requirements (if needed)
- [ ] Document integration constraints

**Done means:**
- Integration contract document exists
- Memory bus protocol specified
- Address space allocated in Janus memory map

### 5.2 Implement Memory Bus Adapter

**Goal:** Connect Cube to Janus memory bus

**Tasks:**
- [ ] Study Janus BCC memory bus interface
- [ ] Create `cube_bus_adapter.py` module
- [ ] Implement bus protocol translation
- [ ] Add bus adapter to Cube build function
- [ ] Test bus adapter in isolation

**Done means:**
- Bus adapter module exists
- Connects Cube MMIO to Janus bus
- Passes standalone tests

### 5.3 Integrate with Janus Top-Level

**Goal:** Instantiate Cube in Janus SoC

**Tasks:**
- [ ] Modify `janus/pyc/janus/top.py` to include Cube
- [ ] Connect Cube to memory bus
- [ ] Allocate address space (e.g., 0x80000000-0x80001000)
- [ ] Add Cube to Janus memory map documentation
- [ ] Regenerate Janus with Cube included

**Done means:**
- Cube instantiated in Janus top-level
- Janus generates successfully with Cube
- Memory map updated

### 5.4 Create Integration Tests

**Goal:** Verify CPU can control Cube

**Tasks:**
- [ ] Write C program to control Cube from CPU
- [ ] Test: CPU writes weights, activations, starts computation
- [ ] Test: CPU polls status, reads results
- [ ] Test: CPU performs multiple matrix multiplications
- [ ] Run on Janus C++ model
- [ ] Run on Janus Verilog simulation

**Done means:**
- C program successfully controls Cube from CPU
- Tests pass on C++ model
- Tests pass on Verilog simulation

---

## Phase 6: Performance Optimization

### 6.1 Implement True Multiplication

**Goal:** Replace addition placeholder with real multiplication

**Tasks:**
- [ ] Check pyCircuit multiplication support status
- [ ] If available, update `_build_pe()` to use multiplication
- [ ] Update tests with correct expected results
- [ ] Verify timing still meets requirements
- [ ] Update documentation

**Done means:**
- PE uses real multiplication operator
- All tests updated and passing
- Documentation reflects true MAC operation

### 6.2 Add Performance Counters

**Goal:** Measure accelerator utilization

**Tasks:**
- [ ] Add cycle counter register
- [ ] Add computation counter (number of operations)
- [ ] Add idle cycle counter
- [ ] Add busy cycle counter
- [ ] Expose via MMIO registers
- [ ] Document in memory map

**Done means:**
- Performance counters implemented
- Accessible via MMIO
- Documented in SPEC.md

### 6.3 Optimize Critical Path

**Goal:** Improve maximum frequency

**Tasks:**
- [ ] Analyze critical path in synthesis reports
- [ ] Add pipeline stages if needed
- [ ] Optimize PE datapath
- [ ] Re-run timing analysis
- [ ] Document timing improvements

**Done means:**
- Critical path identified
- Optimizations applied
- Timing improved (if possible)

---

## Phase 7: FPGA Bring-Up (Future)

### 7.1 FPGA Platform Contract

**Goal:** Define FPGA deployment requirements

**Tasks:**
- [ ] Create `contracts/cube_fpga_platform.md`
- [ ] Define target FPGA (ZYBO Z7-20 or other)
- [ ] Define clock frequency requirements
- [ ] Define resource utilization targets
- [ ] Define I/O constraints

**Done means:**
- FPGA platform contract exists
- Target platform specified
- Resource budgets defined

### 7.2 FPGA Synthesis and Implementation

**Goal:** Deploy Cube on FPGA

**Tasks:**
- [ ] Create Vivado project for Cube
- [ ] Add synthesis constraints
- [ ] Run synthesis and implementation
- [ ] Verify resource utilization
- [ ] Verify timing closure
- [ ] Generate bitstream

**Done means:**
- Cube synthesizes successfully
- Timing closure achieved
- Bitstream generated

### 7.3 FPGA Validation

**Goal:** Verify Cube on real hardware

**Tasks:**
- [ ] Program FPGA with bitstream
- [ ] Run hardware tests
- [ ] Compare with simulation results
- [ ] Measure actual performance
- [ ] Document FPGA results

**Done means:**
- Cube runs on FPGA
- Hardware tests pass
- Performance measured

---

## Maturity Gates

### Gate A1: Cube C++ Model Pass
**Status:** ✅ DONE (2026-02-09)
- Command: `bash janus/tools/run_janus_cube_pyc_cpp.sh`
- Result: PASS (testIdentity, testSimple2x2)
- Evidence: [janus/tb/tb_janus_cube_pyc.cpp](../tb/tb_janus_cube_pyc.cpp)

### Gate A2: Cube Verilog Generation Pass
**Status:** ✅ DONE (2026-02-10)
- Command: `bash janus/update_generated.sh`
- Result: PASS (generates janus_cube_pyc.v ~48MB, janus_cube_pyc_gen.hpp ~51MB)
- Evidence: [janus/generated/janus_cube_pyc/](../../generated/janus_cube_pyc/)
- Note: Now uses Cube v2 implementation with 4-stage pipelined systolic array

### Gate A2.5: Cube v2 Specification Complete
**Status:** ✅ DONE (2026-02-10)
- Deliverables:
  - `CUBE_V2_SPEC.md` - Complete architecture specification
  - `CUBE_V2_SPEC.pdf` - PDF version for distribution
- Evidence: [CUBE_V2_SPEC.md](CUBE_V2_SPEC.md)

### Gate A3: Cube Trace-Diff Pass
**Status:** ⏳ BLOCKED
- Command: `python3 tools/cube_trace_diff.py --cpp trace_cpp.csv --rtl trace_rtl.csv`
- Blocker: Trace schema not defined, trace output not implemented
- Target: Phase 2 completion

### Gate B1: Expanded Test Coverage Pass
**Status:** ⏳ NOT STARTED
- Command: `bash janus/tools/cube_regression.sh --stage tests`
- Blocker: Additional tests not implemented
- Target: Phase 3 completion

### Gate B2: Cube Regression Pass
**Status:** ⏳ NOT STARTED
- Command: `bash janus/tools/cube_regression.sh`
- Blocker: Regression script not created
- Target: Phase 4 completion

### Gate C1: Janus+Cube Integration Pass
**Status:** ⏳ NOT STARTED
- Command: `bash janus/tools/run_janus_with_cube_cpp.sh`
- Blocker: Integration not implemented
- Target: Phase 5 completion

### Gate C2: Janus+Cube CPU Control Pass
**Status:** ⏳ NOT STARTED
- Command: Run C program controlling Cube from CPU
- Blocker: Integration not complete
- Target: Phase 5 completion

### Gate D1: Cube FPGA Synthesis Pass
**Status:** ⏳ NOT STARTED
- Command: Vivado synthesis
- Blocker: FPGA work not started
- Target: Phase 7 completion

---

## Priority Roadmap

### Immediate (Next 1-2 weeks)
1. **Phase 1**: Documentation and standards alignment
2. **Phase 2**: Trace schema and observability
3. **Phase 3**: Expanded test coverage

### Short-term (Next 1 month)
4. **Phase 4**: Regression infrastructure
5. **Phase 5**: Janus BCC integration

### Medium-term (Next 2-3 months)
6. **Phase 6**: Performance optimization
7. **Phase 7**: FPGA bring-up

---

## Success Criteria

**Level 0: Bring-up Usable (CURRENT)**
- ✅ Basic functionality works
- ✅ C++ model passes basic tests
- ✅ Verilog generation works
- ✅ **Cube v2 4-stage pipelined architecture implemented**
- ✅ **64-entry buffers (L0A, L0B, ACC)**
- ✅ **64-entry issue queue with OoO execution**
- ✅ **CUBE_V2_SPEC.md documentation complete**

**Level 1: Spec + Correctness Baseline (TARGET)**
- [x] Formal specification exists with normative language
- [ ] Trace schema defined and implemented
- [ ] Differential validation passes (C++ vs RTL)
- [ ] Comprehensive test coverage (>90%)
- [ ] Regression suite passes
- [ ] Cube v2 specific tests implemented

**Level 2: System Integration Readiness**
- [ ] Integrated with Janus BCC CPU
- [ ] CPU can control accelerator
- [ ] End-to-end tests pass
- [ ] Performance counters implemented

**Level 3: Production Quality**
- [ ] FPGA deployment successful
- [ ] Hardware validation complete
- [ ] Performance optimized
- [ ] Documentation complete

---

## References

### Internal References
- [Cube README](README.md) - Current implementation overview
- [Cube v1 Source](cube.py) - Legacy implementation
- [Cube v2 Source](cube_v2.py) - Current implementation (4-stage pipelined)
- [Cube v2 Specification](CUBE_V2_SPEC.md) - Detailed architecture documentation
- [Cube v2 Specification PDF](CUBE_V2_SPEC.pdf) - PDF version
- [Cube Types](cube_types.py) - Dataclass definitions (v1)
- [Cube v2 Types](cube_v2_types.py) - Dataclass definitions (v2)
- [Cube Constants](cube_consts.py) - Constants and addresses (v1)
- [Cube v2 Constants](cube_v2_consts.py) - Constants and addresses (v2)
- [C++ Testbench](../../tb/tb_janus_cube_pyc.cpp) - Current tests

### External Standards
- [linx-isa Bring-up Phases](/Users/fengshulin/linx-isa/docs/bringup/README.md)
- [linx-isa Progress Tracking](/Users/fengshulin/linx-isa/docs/bringup/PROGRESS.md)
- [linx-skills RTL Development](/Users/fengshulin/linx-skills/linx-rtl-development/SKILL.md)
- [linx-skills ISA Manual Standards](/Users/fengshulin/linx-skills/linx-isa-manual/SKILL.md)

### Technical References
- [pyCircuit Usage Guide](../../../../docs/USAGE.md)
- [Janus BCC CPU](../bcc/janus_bcc_pyc.py)
- [Systolic Array Architecture](https://en.wikipedia.org/wiki/Systolic_array)

---

## Execution Notes

**Update Protocol:**
1. When completing a phase, update gate status in this document
2. Record command, result, and evidence
3. Update maturity level if gate criteria met
4. Create evidence entries for architectural decisions
5. Link to commits and artifacts

**Blocker Resolution:**
- Document blockers explicitly with root cause
- Identify dependencies and prerequisites
- Escalate if blocked for >1 week

**Review Cadence:**
- Weekly progress review
- Update gate status and evidence
- Adjust priorities based on blockers
