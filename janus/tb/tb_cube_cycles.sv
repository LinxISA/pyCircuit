// Simplified cycle count testbench for janus_cube_pyc
// Tests compute cycles for MATMUL instruction

module tb_cube_cycles;
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

  // L0 buffer base addresses (new scheme)
  localparam logic [63:0] L0A_BASE = BASE_ADDR + 64'h1000;  // 0x1000-0x4FFF
  localparam logic [63:0] L0B_BASE = BASE_ADDR + 64'h5000;  // 0x5000-0x8FFF

  // Control bits
  localparam logic [63:0] CTRL_START = 64'h01;
  localparam logic [63:0] CTRL_RESET = 64'h02;

  // Array size
  localparam int ARRAY_SIZE = 16;

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

  // Load L0A entry with dummy data
  // New address scheme: L0A at 0x1000-0x4FFF
  // Entry address = base + 0x1000 + (entry_idx << 8) + (row << 4) + col
  task automatic load_l0a_entry(input int entry_idx);
    logic [63:0] addr;
    int row, col;
    for (row = 0; row < ARRAY_SIZE; row++) begin
      for (col = 0; col < ARRAY_SIZE; col++) begin
        addr = L0A_BASE + (entry_idx << 8) + (row << 4) + col;
        mmio_write(addr, 64'h0001);  // dummy data
      end
    end
    $display("  Loaded L0A entry %0d (addr_base=0x%08x, last_addr=0x%08x)",
             entry_idx, L0A_BASE + (entry_idx << 8),
             L0A_BASE + (entry_idx << 8) + ((ARRAY_SIZE-1) << 4) + (ARRAY_SIZE-1));
  endtask

  // Load L0B entry with dummy data
  // New address scheme: L0B at 0x5000-0x8FFF
  // Entry address = base + 0x5000 + (entry_idx << 8) + (row << 4) + col
  task automatic load_l0b_entry(input int entry_idx);
    logic [63:0] addr;
    int row, col;
    for (row = 0; row < ARRAY_SIZE; row++) begin
      for (col = 0; col < ARRAY_SIZE; col++) begin
        addr = L0B_BASE + (entry_idx << 8) + (row << 4) + col;
        mmio_write(addr, 64'h0001);  // dummy data
      end
    end
    $display("  Loaded L0B entry %0d (addr_base=0x%08x, last_addr=0x%08x)",
             entry_idx, L0B_BASE + (entry_idx << 8),
             L0B_BASE + (entry_idx << 8) + ((ARRAY_SIZE-1) << 4) + (ARRAY_SIZE-1));
  endtask

  // Quick load L0A entry - just mark as valid by writing last element
  task automatic quick_load_l0a_entry(input int entry_idx);
    logic [63:0] addr;
    // Only write the last element (row=15, col=15) to mark entry as valid
    addr = L0A_BASE + (entry_idx << 8) + ((ARRAY_SIZE-1) << 4) + (ARRAY_SIZE-1);
    mmio_write(addr, 64'h0001);
    $display("  Quick loaded L0A entry %0d (last_addr=0x%08x)", entry_idx, addr);
  endtask

  // Quick load L0B entry - just mark as valid by writing last element
  task automatic quick_load_l0b_entry(input int entry_idx);
    logic [63:0] addr;
    // Only write the last element (row=15, col=15) to mark entry as valid
    addr = L0B_BASE + (entry_idx << 8) + ((ARRAY_SIZE-1) << 4) + (ARRAY_SIZE-1);
    mmio_write(addr, 64'h0001);
    $display("  Quick loaded L0B entry %0d (last_addr=0x%08x)", entry_idx, addr);
  endtask

  // Test MATMUL compute cycles
  task automatic test_matmul_cycles(input int M, input int K, input int N);
    logic [63:0] inst;
    int start_cycle;
    int end_cycle;
    int compute_cycles;
    int tile_size;
    int m_tiles, k_tiles, n_tiles;
    int total_uops;
    int theoretical;
    int i;

    tile_size = ARRAY_SIZE;
    m_tiles = (M + tile_size - 1) / tile_size;
    k_tiles = (K + tile_size - 1) / tile_size;
    n_tiles = (N + tile_size - 1) / tile_size;
    total_uops = m_tiles * k_tiles * n_tiles;
    theoretical = total_uops + 3;  // pipeline latency

    $display("\n=== Testing %0dx%0dx%0d MATMUL ===", M, K, N);
    $display("Tiles: %0d x %0d x %0d = %0d uops", m_tiles, k_tiles, n_tiles, total_uops);
    $display("Theoretical compute cycles: %0d", theoretical);

    // Reset
    mmio_write(ADDR_CONTROL, CTRL_RESET);
    repeat(10) @(posedge clk);

    // Quick load L0A entries (just mark as valid)
    $display("Quick loading L0A entries...");
    for (i = 0; i < m_tiles * k_tiles && i < 64; i++) begin
      quick_load_l0a_entry(i);
    end

    // Quick load L0B entries (just mark as valid)
    $display("Quick loading L0B entries...");
    for (i = 0; i < k_tiles * n_tiles && i < 64; i++) begin
      quick_load_l0b_entry(i);
    end

    // Write MATMUL instruction: [15:0]=M, [31:16]=K, [47:32]=N
    inst = {16'h0, N[15:0], K[15:0], M[15:0]};
    $display("Writing MATMUL instruction: M=%0d, K=%0d, N=%0d (inst=0x%016x)", M, K, N, inst);
    mmio_write(ADDR_MATMUL_INST, inst);

    // Wait a few cycles for instruction to be latched
    repeat(5) @(posedge clk);

    // Record start cycle
    start_cycle = cycle_count;

    // Start computation
    $display("Sending START command at cycle %0d", cycle_count);
    mmio_write(ADDR_CONTROL, CTRL_START);

    // Debug: monitor state for first 100 cycles
    $display("Starting computation, monitoring state...");
    $display("  Note: queue_empty=1 means no uops in queue");

    // Monitor internal decoder signals if accessible
    // Check if decoder is generating uops
    for (int dbg = 0; dbg < 200 && !done; dbg++) begin
      @(posedge clk);
      // Print every cycle for first 50 cycles to see detailed timing
      if (dbg < 50 || dbg % 20 == 0) begin
        $display("  cycle %0d: done=%b busy=%b queue_empty=%b queue_full=%b",
                 cycle_count, done, busy, queue_empty, queue_full);
      end
    end

    // Wait for done
    while (!done && (cycle_count - start_cycle) < 100000) begin
      @(posedge clk);
    end

    end_cycle = cycle_count;
    compute_cycles = end_cycle - start_cycle;

    if (done) begin
      $display("Actual compute cycles: %0d", compute_cycles);
      $display("Ratio (actual/theoretical): %.2f", real'(compute_cycles) / real'(theoretical));
    end else begin
      $display("TIMEOUT after %0d cycles!", compute_cycles);
    end
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

    $display("\n========================================");
    $display("Cube Accelerator Cycle Count Test");
    $display("========================================");

    // Test 16x16x16 (1 tile)
    test_matmul_cycles(16, 16, 16);

    // Test 32x32x32 (8 tiles)
    test_matmul_cycles(32, 32, 32);

    // Test 64x64x64 (64 tiles)
    test_matmul_cycles(64, 64, 64);

    $display("\n========================================");
    $display("Test Complete");
    $display("========================================");

    $finish;
  end

  // Cycle counter
  always @(posedge clk) begin
    cycle_count <= cycle_count + 1;
  end

endmodule
