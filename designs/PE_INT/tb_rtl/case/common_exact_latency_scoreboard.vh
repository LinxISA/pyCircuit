localparam integer SPEC_LATENCY = 4;
localparam integer SB_LAST = SPEC_LATENCY - 1;

integer sb_i;
integer exp_rd;
integer checked;
integer accepted;
reg         exp_pipe_vld [0:SB_LAST];
reg  [18:0] exp_pipe_o0  [0:SB_LAST];
reg  [15:0] exp_pipe_o1  [0:SB_LAST];

task sb_reset;
    begin
        exp_rd = 0;
        checked = 0;
        accepted = 0;
        for (sb_i = 0; sb_i <= SB_LAST; sb_i = sb_i + 1) begin
            exp_pipe_vld[sb_i] = 0;
            exp_pipe_o0[sb_i] = 0;
            exp_pipe_o1[sb_i] = 0;
        end
    end
endtask

task sb_tick;
    reg [18:0] next_o0;
    reg [15:0] next_o1;
    begin
        if (exp_pipe_vld[SB_LAST]) begin
            if (!vld_out) begin
                $display("[ERR][%s] missing vld_out at checked=%0d", CASE_NAME, checked);
                err = err + 1;
            end else begin
                if (out0 !== exp_pipe_o0[SB_LAST] || out1 !== exp_pipe_o1[SB_LAST]) begin
                    $display("[ERR][%s] idx=%0d got(out0,out1)=(%0d,%0d) exp=(%0d,%0d)",
                        CASE_NAME,
                        checked,
                        $signed(out0),
                        $signed(out1),
                        $signed(exp_pipe_o0[SB_LAST]),
                        $signed(exp_pipe_o1[SB_LAST]));
                    err = err + 1;
                end
            end
            checked = checked + 1;
        end else if (vld_out) begin
            $display("[ERR][%s] unexpected vld_out out0=%0d out1=%0d", CASE_NAME, $signed(out0), $signed(out1));
            err = err + 1;
        end

        for (sb_i = SB_LAST; sb_i > 0; sb_i = sb_i - 1) begin
            exp_pipe_vld[sb_i] = exp_pipe_vld[sb_i - 1];
            exp_pipe_o0[sb_i] = exp_pipe_o0[sb_i - 1];
            exp_pipe_o1[sb_i] = exp_pipe_o1[sb_i - 1];
        end

        if (vld) begin
            if (exp_rd >= exp_count) begin
                $display("[ERR][%s] input transaction exceeds expected count exp_rd=%0d exp_count=%0d",
                    CASE_NAME, exp_rd, exp_count);
                err = err + 1;
                next_o0 = 0;
                next_o1 = 0;
            end else begin
                next_o0 = exp_o0[exp_rd];
                next_o1 = exp_o1[exp_rd];
            end
            exp_pipe_vld[0] = 1;
            exp_pipe_o0[0] = next_o0;
            exp_pipe_o1[0] = next_o1;
            exp_rd = exp_rd + 1;
            accepted = accepted + 1;
        end else begin
            exp_pipe_vld[0] = 0;
            exp_pipe_o0[0] = 0;
            exp_pipe_o1[0] = 0;
        end
    end
endtask

task sb_final_check;
    begin
        if (accepted !== exp_count) begin
            $display("[ERR][%s] expected %0d accepted inputs, accepted %0d", CASE_NAME, exp_count, accepted);
            err = err + 1;
        end
        if (checked !== exp_count) begin
            $display("[ERR][%s] expected %0d checked outputs, checked %0d", CASE_NAME, exp_count, checked);
            err = err + 1;
        end
    end
endtask
