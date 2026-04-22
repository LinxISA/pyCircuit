`timescale 1ns/1ps

module tc_mode2a_sanity;
    reg         clk;
    reg         rst_n;
    reg         vld;
    reg  [1:0]  mode;
    reg  [79:0] a;
    reg  [79:0] b;
    reg  [79:0] b1;
    reg  [1:0]  e1_a;
    reg  [1:0]  e1_b0;
    reg  [1:0]  e1_b1;

    wire signed [18:0] out0;
    wire signed [15:0] out1;
    wire        vld_out;

    integer got;
    integer err;
    integer i;
    integer vec_idx;

    localparam integer N_CASE_2A = 1000;
    localparam integer N_TX_2A = 2000;
    localparam integer N_EXP_2A = 2000;
    string gen_dir;
    reg [79:0] pre_a [0:N_CASE_2A-1];
    reg [79:0] pre_b [0:N_CASE_2A-1];
    reg [79:0] a2a [0:N_CASE_2A-1];
    reg [79:0] b2a [0:N_CASE_2A-1];
    reg [15:0] exp_pre_o1 [0:N_CASE_2A-1];
    reg [18:0] exp_2a_o0 [0:N_CASE_2A-1];

    localparam [1:0] MODE_2A = 2'b00;
    localparam [1:0] MODE_2B = 2'b01;

    pe_int_l3 dut (
        .clk(clk), .rst_n(rst_n), .vld(vld), .mode(mode), .a(a), .b(b), .b1(b1),
        .e1_a(e1_a), .e1_b0(e1_b0), .e1_b1(e1_b1), .out0(out0), .out1(out1), .vld_out(vld_out)
    );

    `include "common_wave_dump.vh"

    always #5 clk = ~clk;

    always @(posedge clk) begin
        if (rst_n && vld_out) begin
            got <= got + 1;
            if (got >= N_EXP_2A) begin
                $display("[ERR][2a] unexpected extra output out0=%0d out1=%0d", $signed(out0), $signed(out1));
                err <= err + 1;
            end else begin
                vec_idx = got >> 1;
                if ((got & 1) == 0) begin
                    if (out1 !== exp_pre_o1[vec_idx]) begin
                        $display("[ERR][2a] idx=%0d preload out1 mismatch got=%0d exp=%0d",
                            vec_idx, $signed(out1), $signed(exp_pre_o1[vec_idx]));
                        err <= err + 1;
                    end
                end else begin
                    if (out0 !== exp_2a_o0[vec_idx]) begin
                        $display("[ERR][2a] idx=%0d out0 mismatch got=%0d exp=%0d",
                            vec_idx, $signed(out0), $signed(exp_2a_o0[vec_idx]));
                        err <= err + 1;
                    end
                    if (out1 !== exp_pre_o1[vec_idx]) begin
                        $display("[ERR][2a] idx=%0d out1 hold mismatch got=%0d exp=%0d",
                            vec_idx, $signed(out1), $signed(exp_pre_o1[vec_idx]));
                        err <= err + 1;
                    end
                end
            end
        end
    end

    initial begin
        clk = 0; rst_n = 0; vld = 0; mode = 0;
        a = 0; b = 0; b1 = 0; e1_a = 0; e1_b0 = 0; e1_b1 = 0;
        got = 0; err = 0;

        if (!$value$plusargs("GEN_DIR=%s", gen_dir)) begin
            gen_dir = "tb_rtl/case/generated";
        end
        $readmemh({gen_dir, "/tc_mode2a_sanity_pre_a.mem"}, pre_a);
        $readmemh({gen_dir, "/tc_mode2a_sanity_pre_b.mem"}, pre_b);
        $readmemh({gen_dir, "/tc_mode2a_sanity_a2a.mem"}, a2a);
        $readmemh({gen_dir, "/tc_mode2a_sanity_b2a.mem"}, b2a);
        $readmemh({gen_dir, "/tc_mode2a_sanity_exp_pre_o1.mem"}, exp_pre_o1);
        $readmemh({gen_dir, "/tc_mode2a_sanity_exp_2a_o0.mem"}, exp_2a_o0);

        repeat (3) @(posedge clk);
        rst_n = 1;
        repeat (3) @(posedge clk);

        for (i = 0; i < N_CASE_2A; i = i + 1) begin
            @(negedge clk);
            vld = 1; mode = MODE_2B; a = pre_a[i]; b = pre_b[i]; b1 = 0;
            @(negedge clk);
            vld = 1; mode = MODE_2A; a = a2a[i]; b = b2a[i]; b1 = 0;
        end
        @(negedge clk);
        vld = 0; mode = 0; a = 0; b = 0; b1 = 0;

        repeat (12) @(posedge clk);

        if (got !== N_EXP_2A) begin
            $display("[ERR][2a] expected %0d outputs, got %0d", N_EXP_2A, got);
            err = err + 1;
        end

        if (err == 0) begin
            $display("[PASS] tc_mode2a_sanity");
        end else begin
            $fatal(1, "[FAIL] tc_mode2a_sanity err=%0d", err);
        end
        $finish;
    end
endmodule
