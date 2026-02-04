// pyCircuit C++ emission (prototype)
#include <pyc/cpp/pyc_sim.hpp>

namespace pyc::gen {

struct JitControlFlow {
  pyc::cpp::Wire<8> a{};
  pyc::cpp::Wire<8> b{};
  pyc::cpp::Wire<8> out{};

  pyc::cpp::Wire<8> pyc_constant_1{};
  pyc::cpp::Wire<8> pyc_constant_2{};
  pyc::cpp::Wire<9> pyc_constant_3{};
  pyc::cpp::Wire<8> a__jit_control_flow__L7{};
  pyc::cpp::Wire<8> b__jit_control_flow__L8{};
  pyc::cpp::Wire<8> pyc_add_4{};
  pyc::cpp::Wire<7> pyc_extract_5{};
  pyc::cpp::Wire<8> pyc_zext_6{};
  pyc::cpp::Wire<8> x__jit_control_flow__L10{};
  pyc::cpp::Wire<9> pyc_zext_7{};
  pyc::cpp::Wire<9> pyc_zext_8{};
  pyc::cpp::Wire<9> pyc_not_9{};
  pyc::cpp::Wire<9> pyc_add_10{};
  pyc::cpp::Wire<9> pyc_add_11{};
  pyc::cpp::Wire<1> pyc_extract_12{};
  pyc::cpp::Wire<8> pyc_add_13{};
  pyc::cpp::Wire<8> x__jit_control_flow__L12{};
  pyc::cpp::Wire<8> pyc_add_14{};
  pyc::cpp::Wire<8> x__jit_control_flow__L14{};
  pyc::cpp::Wire<8> pyc_mux_15{};
  pyc::cpp::Wire<8> x__jit_control_flow__L11{};
  pyc::cpp::Wire<8> acc__jit_control_flow__L16{};
  pyc::cpp::Wire<8> pyc_add_16{};
  pyc::cpp::Wire<8> acc__jit_control_flow__L18{};
  pyc::cpp::Wire<8> pyc_add_17{};
  pyc::cpp::Wire<8> acc__jit_control_flow__L18_2{};
  pyc::cpp::Wire<8> pyc_add_18{};
  pyc::cpp::Wire<8> acc__jit_control_flow__L18_3{};
  pyc::cpp::Wire<8> pyc_add_19{};
  pyc::cpp::Wire<8> acc__jit_control_flow__L18_4{};
  pyc::cpp::Wire<8> acc__jit_control_flow__L17{};
  pyc::cpp::Wire<8> pyc_comb_20{};


  JitControlFlow() {
    eval();
  }

  inline void eval_comb_0() {
    pyc_constant_1 = pyc::cpp::Wire<8>(2ull);
    pyc_constant_2 = pyc::cpp::Wire<8>(1ull);
    pyc_constant_3 = pyc::cpp::Wire<9>(1ull);
    a__jit_control_flow__L7 = a;
    b__jit_control_flow__L8 = b;
    pyc_add_4 = (a__jit_control_flow__L7 + b__jit_control_flow__L8);
    pyc_extract_5 = pyc::cpp::extract<7, 8>(pyc_add_4, 1u);
    pyc_zext_6 = pyc::cpp::zext<8, 7>(pyc_extract_5);
    x__jit_control_flow__L10 = pyc_zext_6;
    pyc_zext_7 = pyc::cpp::zext<9, 8>(a__jit_control_flow__L7);
    pyc_zext_8 = pyc::cpp::zext<9, 8>(b__jit_control_flow__L8);
    pyc_not_9 = (~pyc_zext_8);
    pyc_add_10 = (pyc_not_9 + pyc_constant_3);
    pyc_add_11 = (pyc_zext_7 + pyc_add_10);
    pyc_extract_12 = pyc::cpp::extract<1, 9>(pyc_add_11, 8u);
    pyc_add_13 = (x__jit_control_flow__L10 + pyc_constant_2);
    x__jit_control_flow__L12 = pyc_add_13;
    pyc_add_14 = (x__jit_control_flow__L10 + pyc_constant_1);
    x__jit_control_flow__L14 = pyc_add_14;
    pyc_mux_15 = (pyc_extract_12.toBool() ? x__jit_control_flow__L12 : x__jit_control_flow__L14);
    x__jit_control_flow__L11 = pyc_mux_15;
    acc__jit_control_flow__L16 = x__jit_control_flow__L11;
    pyc_add_16 = (acc__jit_control_flow__L16 + pyc_constant_2);
    acc__jit_control_flow__L18 = pyc_add_16;
    pyc_add_17 = (acc__jit_control_flow__L18 + pyc_constant_2);
    acc__jit_control_flow__L18_2 = pyc_add_17;
    pyc_add_18 = (acc__jit_control_flow__L18_2 + pyc_constant_2);
    acc__jit_control_flow__L18_3 = pyc_add_18;
    pyc_add_19 = (acc__jit_control_flow__L18_3 + pyc_constant_2);
    acc__jit_control_flow__L18_4 = pyc_add_19;
    acc__jit_control_flow__L17 = acc__jit_control_flow__L18_4;
    pyc_comb_20 = acc__jit_control_flow__L17;
  }

  inline void eval_comb_pass() {
    eval_comb_0();
  }

  void eval() {
    eval_comb_pass();
    out = pyc_comb_20;
  }

  void tick() {
    // Two-phase update: compute next state for all sequential elements,
    // then commit together. This avoids ordering artifacts between regs.
    // Phase 1: compute.
    // Phase 2: commit.
  }
};

} // namespace pyc::gen
