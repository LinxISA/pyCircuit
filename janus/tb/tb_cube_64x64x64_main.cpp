#include "Vtb_cube_64x64x64.h"
#include "verilated.h"

vluint64_t main_time = 0;

double sc_time_stamp() {
    return main_time;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    Vtb_cube_64x64x64* top = new Vtb_cube_64x64x64;

    // Run for limited cycles
    while (!Verilated::gotFinish() && main_time < 500000) {
        top->eval();
        main_time++;
    }

    top->final();
    delete top;
    return 0;
}
