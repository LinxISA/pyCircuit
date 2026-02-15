// Testbench for 64x64x64 MATMUL cycle count measurement
// Tests compute cycles with different PE array configurations

module tb_cube_64x64x64;
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
  logic queue_full;
  logic queue_empty;

  // Memory-mapped addresses
  localparam logic [63:0] BASE_ADDR = 64'h80000000;
  localparam logic [63:0] ADDR_CONTROL = BASE_ADDR + 64'h0000;
  localparam logic [63:0] ADDR_STATUS = BASE_ADDR + 64'h0008;
  localparam logic [63:0] ADDR_MATMUL_INST = BASE_ADDR + 64'h0010;

  // L0 buffer base addresses
  localparam logic [63:0] L0A_BASE = BASE_ADDR + 64'h1000;
  localparam logic [63:0] L0B_BASE = BASE_ADDR + 64'h5000;

  // Control bits
  localparam logic [63:0] CTRL_START = 64'h01;
  localparam logic [63:0] CTRL_RESET = 64'h02;

  // Array size (will be overridden by parameter)
  parameter int ARRAY_SIZE = 16;

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
    .busy(busy),
    .queue_full(queue_full),
    .queue_empty(queue_empty)
  );

  // Clock generation: 10ns period
  always #5 clk = ~clk;

  // Cycle counter
  int cycle_count;

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

  // Quick load L0A entry - just mark as valid by writing last element
  task automatic quick_load_l0a_entry(input int entry_idx);
    logic [63:0] addr;
    addr = L0A_BASE + (entry_idx << 8) + ((ARRAY_SIZE-1) << 4) + (ARRAY_SIZE-1);
    mmio_write(addr, 64'h0001);
  endtask

  // Quick load L0B entry - just mark as valid by writing last element
  task automatic quick_load_l0b_entry(input int entry_idx);
    logic [63:0] addr;
    addr = L0B_BASE + (entry_idx << 8) + ((ARRAY_SIZE-1) << 4) + (ARRAY_SIZE-1);
    mmio_write(addr, 64'h0001);
  endtask

  // Test 64x64x64 MATMUL compute cycles
  task automatic test_64x64x64_matmul();
    logic [63:0] inst;
    int start_cycle;
    int end_cycle;
    int compute_cycles;
    int tile_size;
    int m_tiles, k_tiles, n_tiles;
    int total_uops;
    int theoretical;
    int i;
    int M, K, N;

    M = 64;
    K = 64;
    N = 64;

    tile_size = ARRAY_SIZE;
    m_tiles = (M + tile_size - 1) / tile_size;
    k_tiles = (K + tile_size - 1) / tile_size;
    n_tiles = (N + tile_size - 1) / tile_size;
    total_uops = m_tiles * k_tiles * n_tiles;
    theoretical = total_uops + 3;  // pipeline latency

    $display("\n========================================");
    $display("64x64x64 MATMUL Test");
    $display("PE Array Size: %0dx%0d", ARRAY_SIZE, ARRAY_SIZE);
    $display("========================================");
    $display("Tiles: %0d x %0d x %0d = %0d uops", m_tiles, k_tiles, n_tiles, total_uops);
    $display("Theoretical compute cycles: %0d", theoretical);

    // Reset
    mmio_write(ADDR_CONTROL, CTRL_RESET);
    repeat(10) @(posedge clk);

    // Quick load L0A entries
    $display("Loading L0A entries (%0d entries)...", m_tiles * k_tiles);
    for (i = 0; i < m_tiles * k_tiles && i < 64; i++) begin
      quick_load_l0a_entry(i);
    end

    // Quick load L0B entries
    $display("Loading L0B entries (%0d entries)...", k_tiles * n_tiles);
    for (i = 0; i < k_tiles * n_tiles && i < 64; i++) begin
      quick_load_l0b_entry(i);
    end

    // Write MATMUL instruction: [15:0]=M, [31:16]=K, [47:32]=N
    inst = {16'h0, N[15:0], K[15:0], M[15:0]};
    $display("Writing MATMUL instruction: M=%0d, K=%0d, N=%0d", M, K, N);
    mmio_write(ADDR_MATMUL_INST, inst);

    // Wait a few cycles for instruction to be latched
    repeat(5) @(posedge clk);

    // Record start cycle
    start_cycle = cycle_count;

    // Start computation
    $display("Starting computation at cycle %0d...", cycle_count);
    mmio_write(ADDR_CONTROL, CTRL_START);

    // Wait for done
    while (!done && (cycle_count - start_cycle) < 100000) begin
      @(posedge clk);
    end

    end_cycle = cycle_count;
    compute_cycles = end_cycle - start_cycle;

    $display("\n========================================");
    $display("RESULTS (PE Array: %0dx%0d)", ARRAY_SIZE, ARRAY_SIZE);
    $display("========================================");
    if (done) begin
      $display("Actual compute cycles:     %0d", compute_cycles);
      $display("Theoretical compute cycles: %0d", theoretical);
      $display("Overhead cycles:           %0d", compute_cycles - theoretical);
      $display("Efficiency ratio:          %.2f%%", 100.0 * real'(theoretical) / real'(compute_cycles));
    end else begin
      $display("TIMEOUT after %0d cycles!", compute_cycles);
    end
    $display("========================================\n");
  endtask

  // Main test
  initial begin
    // Initialize
    clk = 0;
    rst = 1;
    mem_wvalid = 0;
    mem_waddr = 0;
    mem_wdata = 0;
    mem_raddr = 0;
    cycle_count = 0;

    // Reset sequence
    repeat(10) @(posedge clk);
    rst = 0;
    repeat(5) @(posedge clk);

    // Run test
    test_64x64x64_matmul();

    $finish;
  end

  // Cycle counter
  always @(posedge clk) begin
    cycle_count <= cycle_count + 1;
  end

endmodule
