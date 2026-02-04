// pyCircuit C++ emission (prototype)
#include <pyc/cpp/pyc_sim.hpp>

namespace pyc::gen {

struct FifoLoopback {
  pyc::cpp::Wire<1> clk{};
  pyc::cpp::Wire<1> rst{};
  pyc::cpp::Wire<1> in_valid{};
  pyc::cpp::Wire<8> in_data{};
  pyc::cpp::Wire<1> out_ready{};
  pyc::cpp::Wire<1> in_ready{};
  pyc::cpp::Wire<1> out_valid{};
  pyc::cpp::Wire<8> out_data{};

  pyc::cpp::Wire<1> in_valid__fifo_loopback__L10{};
  pyc::cpp::Wire<8> in_data__fifo_loopback__L11{};
  pyc::cpp::Wire<1> out_ready__fifo_loopback__L12{};
  pyc::cpp::Wire<1> pyc_comb_1{};
  pyc::cpp::Wire<8> pyc_comb_2{};
  pyc::cpp::Wire<1> pyc_comb_3{};
  pyc::cpp::Wire<1> q__in_valid{};
  pyc::cpp::Wire<8> q__in_data{};
  pyc::cpp::Wire<1> q__out_ready{};
  pyc::cpp::Wire<1> pyc_fifo_4{};
  pyc::cpp::Wire<1> pyc_fifo_5{};
  pyc::cpp::Wire<8> pyc_fifo_6{};

  pyc::cpp::pyc_fifo<8, 2> pyc_fifo_4_inst;

  FifoLoopback() :
      pyc_fifo_4_inst(clk, rst, q__in_valid, pyc_fifo_4, q__in_data, pyc_fifo_5, q__out_ready, pyc_fifo_6) {
    eval();
  }

  inline void eval_comb_0() {
    in_valid__fifo_loopback__L10 = in_valid;
    in_data__fifo_loopback__L11 = in_data;
    out_ready__fifo_loopback__L12 = out_ready;
    pyc_comb_1 = in_valid__fifo_loopback__L10;
    pyc_comb_2 = in_data__fifo_loopback__L11;
    pyc_comb_3 = out_ready__fifo_loopback__L12;
  }

  inline void eval_comb_pass() {
    eval_comb_0();
    q__in_valid = pyc_comb_1;
    q__in_data = pyc_comb_2;
    q__out_ready = pyc_comb_3;
  }

  void eval() {
    eval_comb_pass();
    for (unsigned _i = 0; _i < 1u; ++_i) {
      pyc_fifo_4_inst.eval();
      eval_comb_pass();
    }
    in_ready = pyc_fifo_4;
    out_valid = pyc_fifo_5;
    out_data = pyc_fifo_6;
  }

  void tick() {
    // Two-phase update: compute next state for all sequential elements,
    // then commit together. This avoids ordering artifacts between regs.
    // Phase 1: compute.
    pyc_fifo_4_inst.tick_compute();
    // Phase 2: commit.
    pyc_fifo_4_inst.tick_commit();
  }
};

} // namespace pyc::gen
