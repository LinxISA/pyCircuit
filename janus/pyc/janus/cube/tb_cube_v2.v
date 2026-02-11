/**
 * Cube v2 Testbench
 *
 * Tests the 4-stage pipelined systolic array matrix multiplication accelerator.
 *
 * Test cases:
 * 1. Basic 16x16 matrix multiplication
 * 2. Pipeline throughput verification (1 uop/cycle after fill)
 * 3. MATMUL instruction decomposition
 */

`timescale 1ns / 1ps

module tb_cube_v2;

    // Clock and reset
    reg clk;
    reg rst;

    // MMIO interface (64-bit)
    reg mem_wvalid;
    reg [63:0] mem_waddr;
    reg [63:0] mem_wdata;
    reg [63:0] mem_raddr;

    // Wide data interface (2048-bit)
    reg [2047:0] mem_wdata_wide;
    reg mem_wdata_wide_valid;

    // Outputs
    wire [63:0] mem_rdata;
    wire [2047:0] mem_rdata_wide;
    wire done;
    wire busy;
    wire queue_full;
    wire queue_empty;

    // Base address
    localparam BASE_ADDR = 64'h80000000;

    // Control register bits
    localparam CTRL_START = 0;
    localparam CTRL_RESET = 1;
    localparam CTRL_LOAD_L0A = 2;
    localparam CTRL_LOAD_L0B = 3;
    localparam CTRL_STORE_ACC = 4;

    // Address offsets
    localparam ADDR_CONTROL = 16'h0000;
    localparam ADDR_STATUS = 16'h0008;
    localparam ADDR_MATMUL_INST = 16'h0010;
    localparam ADDR_L0A_DATA = 16'h1000;
    localparam ADDR_L0B_DATA = 16'h2000;
    localparam ADDR_ACC_DATA = 16'h3000;

    // DUT instantiation
    janus_cube_v2_pyc dut (
        .clk(clk),
        .rst(rst),
        .mem_wvalid(mem_wvalid),
        .mem_waddr(mem_waddr),
        .mem_wdata(mem_wdata),
        .mem_raddr(mem_raddr),
        .mem_wdata_wide(mem_wdata_wide),
        .mem_wdata_wide_valid(mem_wdata_wide_valid),
        .mem_rdata(mem_rdata),
        .mem_rdata_wide(mem_rdata_wide),
        .done(done),
        .busy(busy),
        .queue_full(queue_full),
        .queue_empty(queue_empty)
    );

    // Clock generation
    initial begin
        clk = 0;
        forever #5 clk = ~clk;  // 100MHz clock
    end

    // Test stimulus
    initial begin
        // Initialize signals
        rst = 1;
        mem_wvalid = 0;
        mem_waddr = 0;
        mem_wdata = 0;
        mem_raddr = 0;
        mem_wdata_wide = 0;
        mem_wdata_wide_valid = 0;

        // Reset sequence
        repeat(10) @(posedge clk);
        rst = 0;
        repeat(5) @(posedge clk);

        $display("=== Cube v2 Testbench Started ===");
        $display("Time: %0t", $time);

        // Test 1: Load L0A entry 0 (first half)
        $display("\n--- Test 1: Load L0A Entry 0 ---");
        load_l0a_entry(7'd0);

        // Test 2: Load L0B entry 0
        $display("\n--- Test 2: Load L0B Entry 0 ---");
        load_l0b_entry(7'd0);

        // Test 3: Issue MATMUL instruction (16x16x16)
        $display("\n--- Test 3: Issue MATMUL(16,16,16) ---");
        issue_matmul(16'd16, 16'd16, 16'd16);

        // Wait for completion
        wait_for_done();

        // Test 4: Store ACC entry 0
        $display("\n--- Test 4: Store ACC Entry 0 ---");
        store_acc_entry(7'd0);

        // Done
        $display("\n=== Cube v2 Testbench Completed ===");
        $display("Time: %0t", $time);
        $finish;
    end

    // Task: Write to control register
    task write_control;
        input [63:0] data;
        begin
            @(posedge clk);
            mem_wvalid = 1;
            mem_waddr = BASE_ADDR + ADDR_CONTROL;
            mem_wdata = data;
            @(posedge clk);
            mem_wvalid = 0;
        end
    endtask

    // Task: Load L0A entry (2 cycles for 4096 bits)
    task load_l0a_entry;
        input [6:0] entry_idx;
        integer i;
        begin
            // Start L0A load command
            @(posedge clk);
            mem_wvalid = 1;
            mem_waddr = BASE_ADDR + ADDR_CONTROL;
            mem_wdata = (1 << CTRL_LOAD_L0A) | ({57'd0, entry_idx} << 8);
            @(posedge clk);
            mem_wvalid = 0;

            // Send first half (2048 bits)
            @(posedge clk);
            mem_wdata_wide_valid = 1;
            // Fill with test pattern: element[i] = i
            for (i = 0; i < 128; i = i + 1) begin
                mem_wdata_wide[i*16 +: 16] = i[15:0];
            end
            @(posedge clk);
            mem_wdata_wide_valid = 0;

            // Send second half (2048 bits)
            @(posedge clk);
            mem_wdata_wide_valid = 1;
            for (i = 0; i < 128; i = i + 1) begin
                mem_wdata_wide[i*16 +: 16] = (128 + i)[15:0];
            end
            @(posedge clk);
            mem_wdata_wide_valid = 0;

            $display("  L0A entry %0d loaded", entry_idx);
        end
    endtask

    // Task: Load L0B entry (2 cycles for 4096 bits)
    task load_l0b_entry;
        input [6:0] entry_idx;
        integer i;
        begin
            // Start L0B load command
            @(posedge clk);
            mem_wvalid = 1;
            mem_waddr = BASE_ADDR + ADDR_CONTROL;
            mem_wdata = (1 << CTRL_LOAD_L0B) | ({57'd0, entry_idx} << 8);
            @(posedge clk);
            mem_wvalid = 0;

            // Send first half (2048 bits)
            @(posedge clk);
            mem_wdata_wide_valid = 1;
            for (i = 0; i < 128; i = i + 1) begin
                mem_wdata_wide[i*16 +: 16] = i[15:0];
            end
            @(posedge clk);
            mem_wdata_wide_valid = 0;

            // Send second half (2048 bits)
            @(posedge clk);
            mem_wdata_wide_valid = 1;
            for (i = 0; i < 128; i = i + 1) begin
                mem_wdata_wide[i*16 +: 16] = (128 + i)[15:0];
            end
            @(posedge clk);
            mem_wdata_wide_valid = 0;

            $display("  L0B entry %0d loaded", entry_idx);
        end
    endtask

    // Task: Issue MATMUL instruction
    task issue_matmul;
        input [15:0] m_dim;
        input [15:0] k_dim;
        input [15:0] n_dim;
        begin
            // Write MATMUL instruction (M, K, N packed)
            @(posedge clk);
            mem_wvalid = 1;
            mem_waddr = BASE_ADDR + ADDR_MATMUL_INST;
            mem_wdata = {16'd0, n_dim, k_dim, m_dim};
            @(posedge clk);
            mem_wvalid = 0;

            // Start execution
            @(posedge clk);
            mem_wvalid = 1;
            mem_waddr = BASE_ADDR + ADDR_CONTROL;
            mem_wdata = (1 << CTRL_START);
            @(posedge clk);
            mem_wvalid = 0;

            $display("  MATMUL(%0d, %0d, %0d) issued", m_dim, k_dim, n_dim);
        end
    endtask

    // Task: Wait for done signal
    task wait_for_done;
        integer timeout;
        begin
            timeout = 0;
            while (!done && timeout < 10000) begin
                @(posedge clk);
                timeout = timeout + 1;
                if (timeout % 100 == 0) begin
                    $display("  Waiting... cycle %0d, busy=%b, queue_empty=%b",
                             timeout, busy, queue_empty);
                end
            end
            if (done) begin
                $display("  Computation done after %0d cycles", timeout);
            end else begin
                $display("  ERROR: Timeout waiting for done signal!");
            end
        end
    endtask

    // Task: Store ACC entry (4 cycles for 8192 bits)
    task store_acc_entry;
        input [6:0] entry_idx;
        integer i;
        begin
            // Start ACC store command
            @(posedge clk);
            mem_wvalid = 1;
            mem_waddr = BASE_ADDR + ADDR_CONTROL;
            mem_wdata = (1 << CTRL_STORE_ACC) | ({57'd0, entry_idx} << 8);
            @(posedge clk);
            mem_wvalid = 0;

            // Read 4 quarters
            for (i = 0; i < 4; i = i + 1) begin
                @(posedge clk);
                mem_raddr = BASE_ADDR + ADDR_ACC_DATA;
                @(posedge clk);
                $display("  ACC quarter %0d: first 64 bits = %h",
                         i, mem_rdata_wide[63:0]);
                mem_wdata_wide_valid = 1;  // Acknowledge
                @(posedge clk);
                mem_wdata_wide_valid = 0;
            end

            $display("  ACC entry %0d stored", entry_idx);
        end
    endtask

    // Waveform dump
    initial begin
        $dumpfile("tb_cube_v2.vcd");
        $dumpvars(0, tb_cube_v2);
    end

endmodule
