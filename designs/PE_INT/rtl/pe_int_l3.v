module pe_int_l3 (
    input  wire         clk,
    input  wire         rst_n,
    input  wire         vld,
    input  wire [1:0]   mode,
    input  wire [79:0]  a,
    input  wire [79:0]  b,
    input  wire [79:0]  b1,
    input  wire [1:0]   e1_a,
    input  wire [1:0]   e1_b0,
    input  wire [1:0]   e1_b1,
    output reg  signed [18:0] out0,
    output reg  signed [15:0] out1,
    output reg          vld_out
);

localparam MODE_2A = 2'b00;
localparam MODE_2B = 2'b01;
localparam MODE_2C = 2'b10;
localparam MODE_2D = 2'b11;

reg [1:0] rst_rel;
wire rst_pipe_n;
assign rst_pipe_n = rst_rel[1];

reg         s0_vld, s1_vld, s2_vld;
reg [1:0]   s0_mode, s1_mode, s2_mode;
reg signed [31:0] s0_sum0, s0_sum1;
reg signed [31:0] s1_sum0, s1_sum1;
reg signed [31:0] s2_sum0, s2_sum1;

integer i;
reg signed [7:0]  a_s8, b_s8;
reg signed [3:0]  b0_s4, b1_s4;
reg signed [4:0]  a_s5, b0_s5, b1_s5;
reg signed [31:0] c_sum0, c_sum1;
reg signed [31:0] lo0, hi0, lo1, hi1;
reg [1:0] sh_lo0, sh_hi0, sh_lo1, sh_hi1;

always @* begin
    c_sum0 = 32'sd0;
    c_sum1 = 32'sd0;
    lo0 = 32'sd0;
    hi0 = 32'sd0;
    lo1 = 32'sd0;
    hi1 = 32'sd0;
    sh_lo0 = e1_a[0] + e1_b0[0];
    sh_hi0 = e1_a[1] + e1_b0[1];
    sh_lo1 = e1_a[0] + e1_b1[0];
    sh_hi1 = e1_a[1] + e1_b1[1];

    case (mode)
        MODE_2A: begin
            for (i = 0; i < 8; i = i + 1) begin
                a_s8 = $signed({a[5*(2*i+1)+4 -: 4], a[5*(2*i)+4 -: 4]});
                b_s8 = $signed({b[5*(2*i+1)+4 -: 4], b[5*(2*i)+4 -: 4]});
                c_sum0 = c_sum0 + a_s8 * b_s8;
            end
        end
        MODE_2B: begin
            for (i = 0; i < 8; i = i + 1) begin
                a_s8  = $signed({a[5*(2*i+1)+4 -: 4], a[5*(2*i)+4 -: 4]});
                b0_s4 = $signed(b[5*i+4 -: 4]);
                b1_s4 = $signed(b[40 + 5*i + 4 -: 4]);
                c_sum0 = c_sum0 + a_s8 * b0_s4;
                c_sum1 = c_sum1 + a_s8 * b1_s4;
            end
        end
        MODE_2C: begin
            for (i = 0; i < 16; i = i + 1) begin
                a_s5  = $signed(a[5*i+4 -: 5]);
                b0_s5 = $signed(b[5*i+4 -: 5]);
                b1_s5 = $signed(b1[5*i+4 -: 5]);
                if (i < 8) begin
                    lo0 = lo0 + a_s5 * b0_s5;
                    lo1 = lo1 + a_s5 * b1_s5;
                end else begin
                    hi0 = hi0 + a_s5 * b0_s5;
                    hi1 = hi1 + a_s5 * b1_s5;
                end
            end
            c_sum0 = (lo0 <<< sh_lo0) + (hi0 <<< sh_hi0);
            c_sum1 = (lo1 <<< sh_lo1) + (hi1 <<< sh_hi1);
        end
        default: begin
            for (i = 0; i < 8; i = i + 1) begin
                a_s8  = $signed({a[5*(2*i+1)+4 -: 4], a[5*(2*i)+4 -: 4]});
                b0_s5 = $signed(b[5*i+4 -: 5]);
                b1_s5 = $signed(b[40 + 5*i + 4 -: 5]);
                c_sum0 = c_sum0 + a_s8 * b0_s5;
                c_sum1 = c_sum1 + a_s8 * b1_s5;
            end
        end
    endcase
end

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        rst_rel <= 2'b00;
    end else begin
        rst_rel <= {rst_rel[0], 1'b1};
    end
end

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        s0_vld <= 1'b0;
        s1_vld <= 1'b0;
        s2_vld <= 1'b0;
        s0_mode <= MODE_2A;
        s1_mode <= MODE_2A;
        s2_mode <= MODE_2A;
        s0_sum0 <= 32'sd0;
        s0_sum1 <= 32'sd0;
        s1_sum0 <= 32'sd0;
        s1_sum1 <= 32'sd0;
        s2_sum0 <= 32'sd0;
        s2_sum1 <= 32'sd0;
        vld_out <= 1'b0;
        out0 <= 19'sd0;
        out1 <= 16'sd0;
    end else if (!rst_pipe_n) begin
        s0_vld <= 1'b0;
        s1_vld <= 1'b0;
        s2_vld <= 1'b0;
        s0_mode <= MODE_2A;
        s1_mode <= MODE_2A;
        s2_mode <= MODE_2A;
        s0_sum0 <= 32'sd0;
        s0_sum1 <= 32'sd0;
        s1_sum0 <= 32'sd0;
        s1_sum1 <= 32'sd0;
        s2_sum0 <= 32'sd0;
        s2_sum1 <= 32'sd0;
        vld_out <= 1'b0;
        out0 <= 19'sd0;
        out1 <= 16'sd0;
    end else begin
        vld_out <= s2_vld;
        if (s2_vld) begin
            out0 <= s2_sum0[18:0];
            out1 <= s2_sum1[15:0];
        end

        s2_vld <= s1_vld;
        s2_mode <= s1_mode;
        s2_sum0 <= s1_sum0;
        s2_sum1 <= s1_sum1;
        s1_vld <= s0_vld;
        s1_mode <= s0_mode;
        s1_sum0 <= s0_sum0;
        s1_sum1 <= s0_sum1;

        s0_vld <= vld;
        s0_mode <= mode;
        if (vld) begin
            s0_sum0 <= c_sum0;
            if (mode == MODE_2A) begin
                // Keep previous secondary-lane value in 2a to preserve out1 hold behavior.
                s0_sum1 <= s0_sum1;
            end else begin
                s0_sum1 <= c_sum1;
            end
        end else begin
            s0_sum0 <= 32'sd0;
            s0_sum1 <= s0_sum1;
        end
    end
end

endmodule
