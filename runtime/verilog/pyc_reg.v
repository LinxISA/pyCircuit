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
