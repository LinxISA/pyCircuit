#!/bin/bash
# Test 64x64x64 MATMUL with different PE array configurations
# Usage: ./test_pe_configs.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

# PE configurations to test
PE_SIZES=(16 8 4)

echo "========================================"
echo "64x64x64 MATMUL PE Configuration Test"
echo "========================================"
echo ""

for PE_SIZE in "${PE_SIZES[@]}"; do
    echo "----------------------------------------"
    echo "Testing PE Array: ${PE_SIZE}x${PE_SIZE}"
    echo "----------------------------------------"

    # Update ARRAY_SIZE in consts
    sed -i.bak "s/^ARRAY_SIZE = .*/ARRAY_SIZE = ${PE_SIZE}  # ${PE_SIZE}×${PE_SIZE} systolic array/" \
        janus/pyc/janus/cube/cube_v2_consts.py

    # Regenerate Verilog
    echo "Regenerating Verilog..."
    PYC_COMPILE=build/bin/pyc-compile bash janus/update_generated.sh 2>&1 | tail -5

    # Build and run test
    WORK_DIR="build/verilator_tb_64x64x64_pe${PE_SIZE}"
    rm -rf "$WORK_DIR"
    mkdir -p "$WORK_DIR"

    echo "Building Verilator simulation..."
    verilator --cc --exe --timing --build \
        -Wno-fatal \
        -I janus/generated/janus_cube_pyc \
        janus/generated/janus_cube_pyc/janus_cube_pyc.v \
        janus/tb/tb_cube_64x64x64.sv \
        janus/tb/tb_cube_64x64x64_main.cpp \
        --Mdir "$WORK_DIR" \
        -o tb_cube_64x64x64 2>&1 | tail -3

    echo "Running simulation..."
    "$WORK_DIR/tb_cube_64x64x64"

    echo ""
done

# Restore original ARRAY_SIZE
sed -i.bak "s/^ARRAY_SIZE = .*/ARRAY_SIZE = 16  # 16×16 systolic array/" \
    janus/pyc/janus/cube/cube_v2_consts.py
rm -f janus/pyc/janus/cube/cube_v2_consts.py.bak

echo "========================================"
echo "Test Complete - Restored ARRAY_SIZE=16"
echo "========================================"
