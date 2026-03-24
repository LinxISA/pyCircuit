// Simple synchronous reset register (prototype).
module pyc_reg #(
  parameter WIDTH = 1,
  parameter RST_ACTIVE_LOW = 0
) (
  input             clk,
  input             rst,
  input             en,
  input  [WIDTH-1:0] d,
  input  [WIDTH-1:0] init,
  output reg [WIDTH-1:0] q
);
  wire rst_active = RST_ACTIVE_LOW ? ~rst : rst;
  always @(posedge clk) begin
    if (rst_active)
      q <= init;
    else if (en)
      q <= d;
  end
endmodule
