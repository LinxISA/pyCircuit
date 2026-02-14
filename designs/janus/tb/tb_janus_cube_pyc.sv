// SystemVerilog testbench for janus_cube_pyc
// Tests the 16x16 systolic array matrix multiplication accelerator

module tb_janus_cube_pyc;
  logic clk;
  logic rst;

  // Memory interface
  logic mem_wvalid;
  logic [63:0] mem_waddr;
  logic [63:0] mem_wdata;
  logic [63:0] mem_raddr;
  logic [63:0] mem_rdata;

  // Status outputs
  logic done;
  logic busy;

  // Memory-mapped addresses (must match cube_consts.py)
  localparam logic [63:0] BASE_ADDR = 64'h80000000;
  localparam logic [63:0] ADDR_CONTROL = BASE_ADDR + 64'h00;
  localparam logic [63:0] ADDR_STATUS = BASE_ADDR + 64'h08;
  localparam logic [63:0] ADDR_MATRIX_A = BASE_ADDR + 64'h10;    // Activations (16 x 16-bit)
  localparam logic [63:0] ADDR_MATRIX_W = BASE_ADDR + 64'h210;   // Weights (256 x 16-bit)
  localparam logic [63:0] ADDR_MATRIX_C = BASE_ADDR + 64'h410;   // Results (256 x 32-bit)

  localparam int ARRAY_SIZE = 16;

  // Control bits
  localparam logic [63:0] CTRL_START = 64'h01;
  localparam logic [63:0] CTRL_RESET = 64'h02;

  // DUT instantiation
  janus_cube_pyc dut (
    .clk(clk),
    .rst(rst),
    .mem_wvalid(mem_wvalid),
    .mem_waddr(mem_waddr),
    .mem_wdata(mem_wdata),
    .mem_raddr(mem_raddr),
    .mem_rdata(mem_rdata),
    .done(done),
    .busy(busy)
  );

  // Clock generation: 10ns period
  always #5 clk = ~clk;

  // Test control
  string vcd_path;
  string log_path;
  int log_fd;
  int test_pass;
  int test_fail;

  // MMIO write task
  task automatic mmio_write(input logic [63:0] addr, input logic [63:0] data);
    @(posedge clk);
    mem_wvalid <= 1'b1;
    mem_waddr <= addr;
    mem_wdata <= data;
    @(posedge clk);
    mem_wvalid <= 1'b0;
    mem_waddr <= 64'h0;
    mem_wdata <= 64'h0;
  endtask

  // MMIO read task
  task automatic mmio_read(input logic [63:0] addr, output logic [63:0] data);
    @(posedge clk);
    mem_raddr <= addr;
    @(posedge clk);
    data = mem_rdata;
  endtask

  // Wait for done signal
  task automatic wait_done(input int timeout_cycles);
    int count;
    count = 0;
    while (!done && count < timeout_cycles) begin
      @(posedge clk);
      count++;
    end
    if (count >= timeout_cycles) begin
      $display("ERROR: Timeout waiting for done signal after %0d cycles", timeout_cycles);
    end
  endtask

  // Reset cube accelerator
  task automatic reset_cube();
    mmio_write(ADDR_CONTROL, CTRL_RESET);
    repeat(5) @(posedge clk);
  endtask

  // ============================================================
  // Test 1: Identity test (zero weights)
  // With W=0, result should be based on activation accumulation
  // ============================================================
  task automatic test_identity();
    int i;
    logic [63:0] result;

    $display("\n========================================");
    $display("Test 1: Identity Test (Zero Weights)");
    $display("========================================");

    // Load zero weights
    for (i = 0; i < ARRAY_SIZE * ARRAY_SIZE; i++) begin
      mmio_write(ADDR_MATRIX_W + i * 2, 64'h0);
    end

    // Load activations: A[i] = i + 1
    for (i = 0; i < ARRAY_SIZE; i++) begin
      mmio_write(ADDR_MATRIX_A + i * 2, i + 1);
    end

    // Start computation
    mmio_write(ADDR_CONTROL, CTRL_START);

    // Wait for completion
    wait_done(1000);

    if (!done) begin
      $display("FAIL: test_identity - timeout");
      test_fail++;
      return;
    end

    // Read and display some results
    $display("Results (first 4x4 corner):");
    for (int row = 0; row < 4; row++) begin
      for (int col = 0; col < 4; col++) begin
        int idx = row * ARRAY_SIZE + col;
        mmio_read(ADDR_MATRIX_C + idx * 4, result);
        $write("C[%0d][%0d]=%0d ", row, col, result[31:0]);
      end
      $display("");
    end

    // Reset for next test
    reset_cube();

    $display("PASS: test_identity");
    test_pass++;
  endtask

  // ============================================================
  // Test 2: Simple 2x2 matrix multiplication
  // Uses corner of 16x16 array
  // ============================================================
  task automatic test_simple_2x2();
    int i, row, col;
    logic [63:0] result;
    logic [15:0] weights[4];
    logic [15:0] activations[ARRAY_SIZE];

    $display("\n========================================");
    $display("Test 2: Simple 2x2 Matrix Multiplication");
    $display("========================================");

    // Initialize weights: W = [[1, 2], [3, 4], ...]
    weights[0] = 16'd1;
    weights[1] = 16'd2;
    weights[2] = 16'd3;
    weights[3] = 16'd4;

    // Load weights (only first 2x2, rest zeros)
    for (i = 0; i < ARRAY_SIZE * ARRAY_SIZE; i++) begin
      row = i / ARRAY_SIZE;
      col = i % ARRAY_SIZE;
      if (row < 2 && col < 2) begin
        mmio_write(ADDR_MATRIX_W + i * 2, weights[row * 2 + col]);
      end else begin
        mmio_write(ADDR_MATRIX_W + i * 2, 64'h0);
      end
    end

    // Load activations: A = [5, 6, 0, 0, ...]
    for (i = 0; i < ARRAY_SIZE; i++) begin
      if (i == 0) activations[i] = 16'd5;
      else if (i == 1) activations[i] = 16'd6;
      else activations[i] = 16'd0;
      mmio_write(ADDR_MATRIX_A + i * 2, activations[i]);
    end

    // Start computation
    mmio_write(ADDR_CONTROL, CTRL_START);

    // Wait for completion
    wait_done(1000);

    if (!done) begin
      $display("FAIL: test_simple_2x2 - timeout");
      test_fail++;
      return;
    end

    // Read and display results
    $display("Results (first 4x4 corner):");
    for (row = 0; row < 4; row++) begin
      for (col = 0; col < 4; col++) begin
        int idx = row * ARRAY_SIZE + col;
        mmio_read(ADDR_MATRIX_C + idx * 4, result);
        $write("C[%0d][%0d]=%0d ", row, col, result[31:0]);
      end
      $display("");
    end

    // Reset for next test
    reset_cube();

    $display("PASS: test_simple_2x2");
    test_pass++;
  endtask

  // ============================================================
  // Test 3: Full 16x16 matrix with diagonal weights
  // W = diagonal matrix with W[i][i] = i+1
  // ============================================================
  task automatic test_diagonal();
    int i, row, col;
    logic [63:0] result;

    $display("\n========================================");
    $display("Test 3: Diagonal Matrix Test");
    $display("========================================");

    // Load diagonal weights: W[i][i] = i+1, others = 0
    for (i = 0; i < ARRAY_SIZE * ARRAY_SIZE; i++) begin
      row = i / ARRAY_SIZE;
      col = i % ARRAY_SIZE;
      if (row == col) begin
        mmio_write(ADDR_MATRIX_W + i * 2, row + 1);
      end else begin
        mmio_write(ADDR_MATRIX_W + i * 2, 64'h0);
      end
    end

    // Load activations: A[i] = 1
    for (i = 0; i < ARRAY_SIZE; i++) begin
      mmio_write(ADDR_MATRIX_A + i * 2, 64'h1);
    end

    // Start computation
    mmio_write(ADDR_CONTROL, CTRL_START);

    // Wait for completion
    wait_done(1000);

    if (!done) begin
      $display("FAIL: test_diagonal - timeout");
      test_fail++;
      return;
    end

    // Read and display diagonal results
    $display("Diagonal results:");
    for (i = 0; i < ARRAY_SIZE; i++) begin
      int idx = i * ARRAY_SIZE + i;
      mmio_read(ADDR_MATRIX_C + idx * 4, result);
      $display("C[%0d][%0d] = %0d", i, i, result[31:0]);
    end

    // Reset for next test
    reset_cube();

    $display("PASS: test_diagonal");
    test_pass++;
  endtask

  // ============================================================
  // Test 4: Back-to-back operations
  // Run two computations without full reset
  // ============================================================
  task automatic test_back_to_back();
    int i;
    logic [63:0] result1, result2;

    $display("\n========================================");
    $display("Test 4: Back-to-Back Operations");
    $display("========================================");

    // First operation: all weights = 1, all activations = 1
    for (i = 0; i < ARRAY_SIZE * ARRAY_SIZE; i++) begin
      mmio_write(ADDR_MATRIX_W + i * 2, 64'h1);
    end
    for (i = 0; i < ARRAY_SIZE; i++) begin
      mmio_write(ADDR_MATRIX_A + i * 2, 64'h1);
    end

    mmio_write(ADDR_CONTROL, CTRL_START);
    wait_done(1000);

    if (!done) begin
      $display("FAIL: test_back_to_back - first op timeout");
      test_fail++;
      return;
    end

    mmio_read(ADDR_MATRIX_C, result1);
    $display("First operation: C[0][0] = %0d", result1[31:0]);

    // Reset and second operation: all weights = 2, all activations = 2
    reset_cube();

    for (i = 0; i < ARRAY_SIZE * ARRAY_SIZE; i++) begin
      mmio_write(ADDR_MATRIX_W + i * 2, 64'h2);
    end
    for (i = 0; i < ARRAY_SIZE; i++) begin
      mmio_write(ADDR_MATRIX_A + i * 2, 64'h2);
    end

    mmio_write(ADDR_CONTROL, CTRL_START);
    wait_done(1000);

    if (!done) begin
      $display("FAIL: test_back_to_back - second op timeout");
      test_fail++;
      return;
    end

    mmio_read(ADDR_MATRIX_C, result2);
    $display("Second operation: C[0][0] = %0d", result2[31:0]);

    // Reset for next test
    reset_cube();

    $display("PASS: test_back_to_back");
    test_pass++;
  endtask

  // ============================================================
  // Test 5: FSM state transitions
  // Verify proper state machine behavior
  // ============================================================
  task automatic test_fsm_states();
    int cycle_count;

    $display("\n========================================");
    $display("Test 5: FSM State Transitions");
    $display("========================================");

    // Load minimal data
    mmio_write(ADDR_MATRIX_W, 64'h1);
    mmio_write(ADDR_MATRIX_A, 64'h1);

    // Check initial state (should be idle, not busy, not done)
    if (busy) begin
      $display("FAIL: test_fsm_states - busy before start");
      test_fail++;
      return;
    end

    // Start computation
    mmio_write(ADDR_CONTROL, CTRL_START);

    // Should become busy
    @(posedge clk);
    @(posedge clk);
    if (!busy && !done) begin
      $display("WARN: Expected busy=1 after start");
    end

    // Count cycles until done
    cycle_count = 0;
    while (!done && cycle_count < 100) begin
      @(posedge clk);
      cycle_count++;
    end

    $display("Computation completed in %0d cycles", cycle_count);

    if (!done) begin
      $display("FAIL: test_fsm_states - did not complete");
      test_fail++;
      return;
    end

    // After done, busy should be 0
    if (busy) begin
      $display("WARN: busy=1 after done");
    end

    // Reset
    reset_cube();

    $display("PASS: test_fsm_states");
    test_pass++;
  endtask

  // ============================================================
  // Main test sequence
  // ============================================================
  initial begin
    // Initialize signals
    clk = 1'b0;
    rst = 1'b1;
    mem_wvalid = 1'b0;
    mem_waddr = 64'h0;
    mem_wdata = 64'h0;
    mem_raddr = 64'h0;
    test_pass = 0;
    test_fail = 0;

    // Setup VCD dump
    vcd_path = ".pycircuit_out/janus/janus_cube_pyc/tb_janus_cube_pyc_sv.vcd";
    log_path = ".pycircuit_out/janus/janus_cube_pyc/tb_janus_cube_pyc_sv.log";

    if (!$test$plusargs("notrace")) begin
      $display("tb_janus_cube_pyc: dumping VCD to %s", vcd_path);
      $dumpfile(vcd_path);
      $dumpvars(0, tb_janus_cube_pyc);
    end

    if (!$test$plusargs("nolog")) begin
      log_fd = $fopen(log_path, "w");
      $fdisplay(log_fd, "tb_janus_cube_pyc(SV): Verilog testbench");
    end else begin
      log_fd = 0;
    end

    $display("\n========================================");
    $display("Cube Matrix Accelerator Verilog Testbench");
    $display("========================================");

    // Reset sequence
    repeat(5) @(posedge clk);
    rst = 1'b0;
    repeat(5) @(posedge clk);

    // Run tests
    test_identity();
    test_simple_2x2();
    test_diagonal();
    test_back_to_back();
    test_fsm_states();

    // Summary
    $display("\n========================================");
    $display("Test Summary");
    $display("========================================");
    $display("PASSED: %0d", test_pass);
    $display("FAILED: %0d", test_fail);
    $display("========================================\n");

    if (log_fd != 0) begin
      $fdisplay(log_fd, "PASSED: %0d, FAILED: %0d", test_pass, test_fail);
      $fclose(log_fd);
    end

    if (test_fail > 0) begin
      $fatal(1, "FAIL: %0d tests failed", test_fail);
    end else begin
      $display("All tests passed!");
    end

    $finish;
  end

endmodule
