// pyCircuit Verilog primitives (concatenated)
/* verilator lint_off DECLFILENAME */

// --- pyc_reg.v
// Simple synchronous reset register (prototype).
module pyc_reg #(
  parameter WIDTH = 1
) (
  input             clk,
  input             rst,
  input             en,
  input  [WIDTH-1:0] d,
  input  [WIDTH-1:0] init,
  output reg [WIDTH-1:0] q
);
  always @(posedge clk) begin
    if (rst)
      q <= init;
    else if (en)
      q <= d;
  end
endmodule


// --- pyc_fifo.v
// Ready/valid FIFO with synchronous reset (prototype).
module pyc_fifo #(
  parameter WIDTH = 1,
  parameter DEPTH = 2
) (
  input             clk,
  input             rst,

  // Input (producer -> fifo)
  input             in_valid,
  output            in_ready,
  input  [WIDTH-1:0] in_data,

  // Output (fifo -> consumer)
  output            out_valid,
  input             out_ready,
  output [WIDTH-1:0] out_data
);
  `ifndef SYNTHESIS
  initial begin
    if (DEPTH <= 0) begin
      $display("ERROR: pyc_fifo DEPTH must be > 0");
      $finish;
    end
  end
  `endif

  function integer pyc_clog2;
    input integer value;
    integer i;
    begin
      pyc_clog2 = 0;
      for (i = value - 1; i > 0; i = i >> 1)
        pyc_clog2 = pyc_clog2 + 1;
    end
  endfunction

  localparam PTR_W = (DEPTH <= 1) ? 1 : pyc_clog2(DEPTH);

  reg [WIDTH-1:0] storage [0:DEPTH-1];
  reg [PTR_W-1:0] rd_ptr;
  reg [PTR_W-1:0] wr_ptr;
  reg [PTR_W:0]   count;

  assign in_ready  = (count < DEPTH) || (out_ready && out_valid);
  assign out_valid = (count != 0);
  // Define out_data when empty to keep C++/Verilog equivalence deterministic.
  assign out_data  = out_valid ? storage[rd_ptr] : {WIDTH{1'b0}};

  wire do_pop;
  wire do_push;
  assign do_pop  = out_valid && out_ready;
  assign do_push = in_valid && in_ready;

  function [PTR_W-1:0] bump_ptr;
    input [PTR_W-1:0] p;
    begin
      if (DEPTH <= 1)
        bump_ptr = {PTR_W{1'b0}};
      else if (p == (DEPTH - 1))
        bump_ptr = {PTR_W{1'b0}};
      else
        bump_ptr = p + 1'b1;
    end
  endfunction

  always @(posedge clk) begin
    if (rst) begin
      rd_ptr <= {PTR_W{1'b0}};
      wr_ptr <= {PTR_W{1'b0}};
      count <= {(PTR_W + 1){1'b0}};
    end else begin
      case ({do_push, do_pop})
        2'b00: begin
          // hold
        end
        2'b01: begin
          rd_ptr <= bump_ptr(rd_ptr);
          count <= count - 1'b1;
        end
        2'b10: begin
          storage[wr_ptr] <= in_data;
          wr_ptr <= bump_ptr(wr_ptr);
          count <= count + 1'b1;
        end
        2'b11: begin
          // push + pop in the same cycle
          storage[wr_ptr] <= in_data;
          rd_ptr <= bump_ptr(rd_ptr);
          wr_ptr <= bump_ptr(wr_ptr);
          count <= count;
        end
      endcase
    end
  end
endmodule


// --- pyc_byte_mem.v
// Byte-addressed memory (prototype).
//
// - `DEPTH` is in bytes.
// - Combinational little-endian read window.
// - Byte-enable write on posedge.
module pyc_byte_mem #(
  parameter ADDR_WIDTH = 64,
  parameter DATA_WIDTH = 64,
  parameter DEPTH = 1024,
  // Optional init file (Vivado: can be used for BRAM init; simulation: $readmemh).
  // If the file cannot be opened, initialization is skipped.
  parameter INIT_MEMH = ""
) (
  input                   clk,
  input                   rst,

  input  [ADDR_WIDTH-1:0] raddr,
  output reg [DATA_WIDTH-1:0] rdata,

  input                   wvalid,
  input  [ADDR_WIDTH-1:0] waddr,
  input  [DATA_WIDTH-1:0] wdata,
  input  [(DATA_WIDTH+7)/8-1:0] wstrb
);
  localparam STRB_WIDTH = (DATA_WIDTH + 7) / 8;

  // Byte storage.
  reg [7:0] mem [0:DEPTH-1];

  // Optional initialization.
  integer init_fd;
  integer init_i;
  initial begin
    `ifndef SYNTHESIS
    // Deterministic simulation init: keep C++/Verilog equivalence stable.
    for (init_i = 0; init_i < DEPTH; init_i = init_i + 1)
      mem[init_i] = 8'h00;
    `endif
    if (INIT_MEMH != "") begin
      init_fd = $fopen(INIT_MEMH, "r");
      if (init_fd != 0) begin
        $fclose(init_fd);
        $readmemh(INIT_MEMH, mem);
      end
    end
  end

  // Combinational read: assemble DATA_WIDTH bits from successive bytes.
  integer i;
  integer a;
  always @* begin
    a = raddr[31:0];
    rdata = {DATA_WIDTH{1'b0}};
    for (i = 0; i < STRB_WIDTH; i = i + 1) begin
      if ((a + i) < DEPTH)
        rdata[8 * i +: 8] = mem[a + i];
    end
  end

  // Byte-enable write.
  integer j;
  integer wa;
  always @(posedge clk) begin
    if (rst) begin
      // Reset does not clear memory (init happens via INIT_MEMH or sim-default 0s).
    end else if (wvalid) begin
      wa = waddr[31:0];
      for (j = 0; j < STRB_WIDTH; j = j + 1) begin
        if (wstrb[j] && ((wa + j) < DEPTH))
          mem[wa + j] <= wdata[8 * j +: 8];
      end
    end
  end
endmodule


// --- pyc_sync_mem.v
// Synchronous 1R1W memory with registered read data (prototype).
//
// - `DEPTH` is in entries (not bytes).
// - Read is synchronous: when `ren` is asserted, `rdata` updates on the next
//   rising edge of `clk`.
// - Write is synchronous with byte enables `wstrb`.
//
// Note: Read-during-write to the same address returns the pre-write data
// ("old-data") by default.
module pyc_sync_mem #(
  parameter ADDR_WIDTH = 64,
  parameter DATA_WIDTH = 64,
  parameter DEPTH = 1024
) (
  input                   clk,
  input                   rst,

  input                   ren,
  input  [ADDR_WIDTH-1:0] raddr,
  output reg [DATA_WIDTH-1:0] rdata,

  input                   wvalid,
  input  [ADDR_WIDTH-1:0] waddr,
  input  [DATA_WIDTH-1:0] wdata,
  input  [(DATA_WIDTH+7)/8-1:0] wstrb
);
  `ifndef SYNTHESIS
  initial begin
    if (DEPTH <= 0) begin
      $display("ERROR: pyc_sync_mem DEPTH must be > 0");
      $finish;
    end
  end
  `endif

  localparam STRB_WIDTH = (DATA_WIDTH + 7) / 8;
  localparam LAST_LANE_BITS = DATA_WIDTH - 8 * (STRB_WIDTH - 1);
  // Use a compact address for FPGA-friendly inference. For non-power-of-two
  // depths, some addresses in [DEPTH, 2**ADDR_BITS) are unused.
  localparam ADDR_BITS = (DEPTH <= 1) ? 1 : $clog2(DEPTH);

  // Storage.
  `ifdef PYC_TARGET_FPGA
  (* ram_style = "block" *)
  (* ramstyle = "M20K" *)
  reg [DATA_WIDTH-1:0] mem [0:DEPTH-1];
  `else
  reg [DATA_WIDTH-1:0] mem [0:DEPTH-1];
  `endif

  `ifndef SYNTHESIS
  // Deterministic simulation init: keep C++/Verilog equivalence stable.
  integer init_i;
  initial begin
    for (init_i = 0; init_i < DEPTH; init_i = init_i + 1)
      mem[init_i] = {DATA_WIDTH{1'b0}};
  end
  `endif

  integer i;
  reg [DATA_WIDTH-1:0] rd_word;
  wire [ADDR_BITS-1:0] ra = raddr[ADDR_BITS-1:0];
  wire [ADDR_BITS-1:0] wa = waddr[ADDR_BITS-1:0];

  always @(posedge clk) begin
    if (rst) begin
      rdata <= {DATA_WIDTH{1'b0}};
    end else begin
      // Write with per-lane strobes; last lane may be narrower than 8 bits.
      if (wvalid) begin
        `ifndef SYNTHESIS
        if (wa < DEPTH) begin
          for (i = 0; i < STRB_WIDTH - 1; i = i + 1) begin
            if (wstrb[i])
              mem[wa][8 * i +: 8] <= wdata[8 * i +: 8];
          end
          if (wstrb[STRB_WIDTH-1])
            mem[wa][8*(STRB_WIDTH-1) +: LAST_LANE_BITS] <= wdata[8*(STRB_WIDTH-1) +: LAST_LANE_BITS];
        end
        `else
        for (i = 0; i < STRB_WIDTH - 1; i = i + 1) begin
          if (wstrb[i])
            mem[wa][8 * i +: 8] <= wdata[8 * i +: 8];
        end
        if (wstrb[STRB_WIDTH-1])
          mem[wa][8*(STRB_WIDTH-1) +: LAST_LANE_BITS] <= wdata[8*(STRB_WIDTH-1) +: LAST_LANE_BITS];
        `endif
      end

      // Registered read.
      if (ren) begin
        `ifndef SYNTHESIS
        if (ra < DEPTH) begin
          rd_word = mem[ra];
          rdata <= rd_word;
        end else begin
          rdata <= {DATA_WIDTH{1'b0}};
        end
        `else
        rd_word = mem[ra];
        rdata <= rd_word;
        `endif
      end
    end
  end
endmodule


// --- pyc_sync_mem_dp.v
// Synchronous 2R1W memory with registered read data (prototype).
//
// - `DEPTH` is in entries (not bytes).
// - Both reads are synchronous (registered outputs).
// - One write port with byte enables `wstrb`.
// - Read-during-write to the same address returns pre-write data ("old-data") by default.
module pyc_sync_mem_dp #(
  parameter ADDR_WIDTH = 64,
  parameter DATA_WIDTH = 64,
  parameter DEPTH = 1024
) (
  input                   clk,
  input                   rst,

  input                   ren0,
  input  [ADDR_WIDTH-1:0] raddr0,
  output reg [DATA_WIDTH-1:0] rdata0,

  input                   ren1,
  input  [ADDR_WIDTH-1:0] raddr1,
  output reg [DATA_WIDTH-1:0] rdata1,

  input                   wvalid,
  input  [ADDR_WIDTH-1:0] waddr,
  input  [DATA_WIDTH-1:0] wdata,
  input  [(DATA_WIDTH+7)/8-1:0] wstrb
);
  `ifndef SYNTHESIS
  initial begin
    if (DEPTH <= 0) begin
      $display("ERROR: pyc_sync_mem_dp DEPTH must be > 0");
      $finish;
    end
  end
  `endif

  localparam STRB_WIDTH = (DATA_WIDTH + 7) / 8;
  localparam LAST_LANE_BITS = DATA_WIDTH - 8 * (STRB_WIDTH - 1);
  localparam ADDR_BITS = (DEPTH <= 1) ? 1 : $clog2(DEPTH);

  `ifdef PYC_TARGET_FPGA
  (* ram_style = "block" *)
  (* ramstyle = "M20K" *)
  reg [DATA_WIDTH-1:0] mem [0:DEPTH-1];
  `else
  reg [DATA_WIDTH-1:0] mem [0:DEPTH-1];
  `endif

  `ifndef SYNTHESIS
  // Deterministic simulation init: keep C++/Verilog equivalence stable.
  integer init_i;
  initial begin
    for (init_i = 0; init_i < DEPTH; init_i = init_i + 1)
      mem[init_i] = {DATA_WIDTH{1'b0}};
  end
  `endif

  integer i;
  reg [DATA_WIDTH-1:0] rd0;
  reg [DATA_WIDTH-1:0] rd1;
  wire [ADDR_BITS-1:0] ra0 = raddr0[ADDR_BITS-1:0];
  wire [ADDR_BITS-1:0] ra1 = raddr1[ADDR_BITS-1:0];
  wire [ADDR_BITS-1:0] wa = waddr[ADDR_BITS-1:0];

  always @(posedge clk) begin
    if (rst) begin
      rdata0 <= {DATA_WIDTH{1'b0}};
      rdata1 <= {DATA_WIDTH{1'b0}};
    end else begin
      // Write with per-lane strobes; last lane may be narrower than 8 bits.
      if (wvalid) begin
        `ifndef SYNTHESIS
        if (wa < DEPTH) begin
          for (i = 0; i < STRB_WIDTH - 1; i = i + 1) begin
            if (wstrb[i])
              mem[wa][8 * i +: 8] <= wdata[8 * i +: 8];
          end
          if (wstrb[STRB_WIDTH-1])
            mem[wa][8*(STRB_WIDTH-1) +: LAST_LANE_BITS] <= wdata[8*(STRB_WIDTH-1) +: LAST_LANE_BITS];
        end
        `else
        for (i = 0; i < STRB_WIDTH - 1; i = i + 1) begin
          if (wstrb[i])
            mem[wa][8 * i +: 8] <= wdata[8 * i +: 8];
        end
        if (wstrb[STRB_WIDTH-1])
          mem[wa][8*(STRB_WIDTH-1) +: LAST_LANE_BITS] <= wdata[8*(STRB_WIDTH-1) +: LAST_LANE_BITS];
        `endif
      end

      // Registered read port 0.
      if (ren0) begin
        `ifndef SYNTHESIS
        if (ra0 < DEPTH) begin
          rd0 = mem[ra0];
          rdata0 <= rd0;
        end else begin
          rdata0 <= {DATA_WIDTH{1'b0}};
        end
        `else
        rd0 = mem[ra0];
        rdata0 <= rd0;
        `endif
      end

      // Registered read port 1.
      if (ren1) begin
        `ifndef SYNTHESIS
        if (ra1 < DEPTH) begin
          rd1 = mem[ra1];
          rdata1 <= rd1;
        end else begin
          rdata1 <= {DATA_WIDTH{1'b0}};
        end
        `else
        rd1 = mem[ra1];
        rdata1 <= rd1;
        `endif
      end
    end
  end
endmodule


// --- pyc_async_fifo.v
// Async ready/valid FIFO with gray-code pointers (prototype).
//
// - Strict ready/valid handshake (no combinational cross-domain paths).
// - `DEPTH` must be a power of two and >= 2.
// - Synchronous resets (per domain).
//
// This is a minimal, synthesizable async FIFO suitable for CDC of data streams.
module pyc_async_fifo #(
  parameter WIDTH = 1,
  parameter DEPTH = 2
) (
  // Write domain (producer -> fifo)
  input               in_clk,
  input               in_rst,
  input               in_valid,
  output              in_ready,
  input  [WIDTH-1:0]  in_data,

  // Read domain (fifo -> consumer)
  input               out_clk,
  input               out_rst,
  output              out_valid,
  input               out_ready,
  output [WIDTH-1:0]  out_data
);
  `ifndef SYNTHESIS
  initial begin
    if (DEPTH < 2) begin
      $display("ERROR: pyc_async_fifo DEPTH must be >= 2");
      $finish;
    end
    if ((DEPTH & (DEPTH - 1)) != 0) begin
      $display("ERROR: pyc_async_fifo DEPTH must be a power of two");
      $finish;
    end
  end
  `endif

  function integer pyc_clog2;
    input integer value;
    integer i;
    begin
      pyc_clog2 = 0;
      for (i = value - 1; i > 0; i = i >> 1)
        pyc_clog2 = pyc_clog2 + 1;
    end
  endfunction

  localparam AW = pyc_clog2(DEPTH);

  // Storage.
  reg [WIDTH-1:0] mem [0:DEPTH-1];

  // --- pointer helpers ---
  function [AW:0] bin2gray;
    input [AW:0] b;
    begin
      bin2gray = (b >> 1) ^ b;
    end
  endfunction

  // --- write domain ---
  reg [AW:0] wptr_bin;
  reg [AW:0] wptr_gray;
  wire [AW:0] wptr_bin_next;
  wire [AW:0] wptr_gray_next;
  reg         wfull;

  // Read pointer gray (owned by read domain), referenced for synchronization.
  reg [AW:0] rptr_gray;

  reg [AW:0] rptr_gray_w1;
  reg [AW:0] rptr_gray_w2;

  wire wfull_next;
  wire do_push;

  assign in_ready = ~wfull;
  assign do_push = in_valid && in_ready;
  assign wptr_bin_next = wptr_bin + (do_push ? {{AW{1'b0}}, 1'b1} : {AW+1{1'b0}});
  assign wptr_gray_next = bin2gray(wptr_bin_next);
  // Full detection compares next wptr gray against synchronized rptr gray with
  // the top 2 bits inverted (classic async FIFO technique). For DEPTH=2, AW=1
  // and there are no "lower" bits to append.
  generate
    if (AW == 1) begin : gen_wfull_aw1
      assign wfull_next = (wptr_gray_next == ~rptr_gray_w2);
    end else begin : gen_wfull_awn
      assign wfull_next = (wptr_gray_next == {~rptr_gray_w2[AW:AW-1], rptr_gray_w2[AW-2:0]});
    end
  endgenerate

  integer wi;
  always @(posedge in_clk) begin
    if (in_rst) begin
      wptr_bin <= {AW+1{1'b0}};
      wptr_gray <= {AW+1{1'b0}};
      wfull <= 1'b0;
      rptr_gray_w1 <= {AW+1{1'b0}};
      rptr_gray_w2 <= {AW+1{1'b0}};
    end else begin
      // Sync read pointer into write clock domain.
      rptr_gray_w1 <= rptr_gray;
      rptr_gray_w2 <= rptr_gray_w1;

      if (do_push) begin
        mem[wptr_bin[AW-1:0]] <= in_data;
      end
      wptr_bin <= wptr_bin_next;
      wptr_gray <= wptr_gray_next;
      wfull <= wfull_next;
    end
  end

  reg [AW:0] rptr_bin;
  // rptr_gray is declared above (referenced by the write domain sync flops).
  wire [AW:0] rptr_bin_next;
  wire [AW:0] rptr_gray_next;

  reg [AW:0] wptr_gray_r1;
  reg [AW:0] wptr_gray_r2;

  reg out_valid_r;
  reg [WIDTH-1:0] out_data_r;

  wire empty_now;
  wire empty_next;
  wire do_pop;

  assign empty_now = (rptr_gray == wptr_gray_r2);
  assign out_valid = out_valid_r;
  assign out_data = out_data_r;

  assign do_pop = out_valid_r && out_ready;
  assign rptr_bin_next = rptr_bin + (do_pop ? {{AW{1'b0}}, 1'b1} : {AW+1{1'b0}});
  assign rptr_gray_next = bin2gray(rptr_bin_next);
  assign empty_next = (rptr_gray_next == wptr_gray_r2);

  integer ri;
  always @(posedge out_clk) begin
    if (out_rst) begin
      rptr_bin <= {AW+1{1'b0}};
      rptr_gray <= {AW+1{1'b0}};
      wptr_gray_r1 <= {AW+1{1'b0}};
      wptr_gray_r2 <= {AW+1{1'b0}};
      out_valid_r <= 1'b0;
      out_data_r <= {WIDTH{1'b0}};
    end else begin
      // Sync write pointer into read clock domain.
      wptr_gray_r1 <= wptr_gray;
      wptr_gray_r2 <= wptr_gray_r1;

      if (!out_valid_r) begin
        // Fill output register when data becomes available.
        if (!empty_now) begin
          out_valid_r <= 1'b1;
          out_data_r <= mem[rptr_bin[AW-1:0]];
        end
      end else if (do_pop) begin
        // Pop current word; either refill with next word or go empty.
        rptr_bin <= rptr_bin_next;
        rptr_gray <= rptr_gray_next;
        if (empty_next) begin
          out_valid_r <= 1'b0;
          out_data_r <= {WIDTH{1'b0}};
        end else begin
          out_valid_r <= 1'b1;
          out_data_r <= mem[rptr_bin_next[AW-1:0]];
        end
      end
    end
  end
endmodule


// --- pyc_cdc_sync.v
// CDC synchronizer (prototype).
//
// This is a simple multi-stage flop pipeline in the destination clock domain.
// It is suitable for single-bit control signals. For multi-bit buses, prefer a
// proper CDC protocol (async FIFO, handshake, etc).
module pyc_cdc_sync #(
  parameter WIDTH = 1,
  parameter STAGES = 2
) (
  input               clk,
  input               rst,
  input  [WIDTH-1:0]  in,
  output [WIDTH-1:0]  out
);
  `ifndef SYNTHESIS
  initial begin
    if (STAGES < 1) begin
      $display("ERROR: pyc_cdc_sync STAGES must be >= 1");
      $finish;
    end
  end
  `endif

  `ifdef PYC_TARGET_FPGA
  (* async_reg = "true" *)
  reg [WIDTH-1:0] pipe [0:STAGES-1];
  `else
  reg [WIDTH-1:0] pipe [0:STAGES-1];
  `endif

  integer i;
  always @(posedge clk) begin
    if (rst) begin
      for (i = 0; i < STAGES; i = i + 1)
        pipe[i] <= {WIDTH{1'b0}};
    end else begin
      pipe[0] <= in;
      for (i = 1; i < STAGES; i = i + 1)
        pipe[i] <= pipe[i - 1];
    end
  end

  assign out = pipe[STAGES-1];
endmodule


/* verilator lint_on DECLFILENAME */
