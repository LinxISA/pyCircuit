// Common optional wave-dump hook for RTL testcases.
// Enable by runtime plusargs:
//   +WAVE=1               (enable dump)
//   +WAVE_FST=1           (optional, use wave.fst; default wave.vcd)
//
// Notes:
// - Requires DUT instance name to be `dut`.
// - For Verilator, binary must be built with --trace or --trace-fst.
// - For Icarus, VCD is the practical default.

integer __wave_enable;
integer __wave_fst;

initial begin
    __wave_enable = 0;
    __wave_fst = 0;
    if ($value$plusargs("WAVE=%d", __wave_enable) && (__wave_enable != 0)) begin
        if ($value$plusargs("WAVE_FST=%d", __wave_fst) && (__wave_fst != 0)) begin
            $display("[INFO] Wave dump enabled: wave.fst");
            $dumpfile("wave.fst");
        end else begin
            $display("[INFO] Wave dump enabled: wave.vcd");
            $dumpfile("wave.vcd");
        end
        $dumpvars(0, dut);
    end
end
