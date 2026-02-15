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
  assign out_data  = storage[rd_ptr];

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
