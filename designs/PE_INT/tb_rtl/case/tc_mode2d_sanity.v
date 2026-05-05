`timescale 1ns/1ps

module tc_mode2d_sanity;
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

    integer err;
    integer i;
    integer exp_count;
    localparam integer N_TX_2D = 1000;
    string gen_dir;
    reg [79:0] tx_a [0:N_TX_2D-1];
    reg [79:0] tx_b [0:N_TX_2D-1];
    reg [18:0] exp_o0 [0:N_TX_2D-1];
    reg [15:0] exp_o1 [0:N_TX_2D-1];

    localparam [1:0] MODE_2D = 2'b11;
    localparam string CASE_NAME = "2d";

    PE_INT dut (
        .clk(clk), .rst_n(rst_n), .vld(vld), .mode(mode), .a(a), .b(b), .b1(b1),
        .e1_a(e1_a), .e1_b0(e1_b0), .e1_b1(e1_b1), .out0(out0), .out1(out1), .vld_out(vld_out)
    );

    `include "common_wave_dump.vh"
    `include "common_exact_latency_scoreboard.vh"

    always #5 clk = ~clk;

    always @(posedge clk) begin
        if (!rst_n) begin
            sb_reset();
        end else begin
            sb_tick();
        end
    end

    initial begin
        clk = 0; rst_n = 0; vld = 0; mode = 0;
        a = 0; b = 0; b1 = 0; e1_a = 0; e1_b0 = 0; e1_b1 = 0;
        err = 0; exp_count = N_TX_2D; sb_reset();

        if (!$value$plusargs("GEN_DIR=%s", gen_dir)) begin
            gen_dir = "tb_rtl/case/generated";
        end
        $readmemh({gen_dir, "/tc_mode2d_sanity_tx_a.mem"}, tx_a);
        $readmemh({gen_dir, "/tc_mode2d_sanity_tx_b.mem"}, tx_b);
        $readmemh({gen_dir, "/tc_mode2d_sanity_exp_o0.mem"}, exp_o0);
        $readmemh({gen_dir, "/tc_mode2d_sanity_exp_o1.mem"}, exp_o1);

        repeat (3) @(posedge clk);
        rst_n = 1;
        repeat (3) @(posedge clk);

        for (i = 0; i < N_TX_2D; i = i + 1) begin
            @(negedge clk);
            vld = 1; mode = MODE_2D; a = tx_a[i]; b = tx_b[i]; b1 = 0;
            e1_a = 0; e1_b0 = 0; e1_b1 = 0;
        end

        @(negedge clk);
        vld = 0; mode = 0; a = 0; b = 0; b1 = 0; e1_a = 0; e1_b0 = 0; e1_b1 = 0;

        repeat (12) @(posedge clk);
        sb_final_check();

        if (err == 0) begin
            $display("[PASS] tc_mode2d_sanity");
        end else begin
            $fatal(1, "[FAIL] tc_mode2d_sanity err=%0d", err);
        end
        $finish;
    end
endmodule
