#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>

#include <pyc/cpp/pyc_tb.hpp>

#include "issue_queue_2picker.hpp"

using pyc::cpp::Testbench;
using pyc::cpp::Wire;

namespace {

struct Dut {
  pyc::gen::issue_queue_2picker u{};

  Wire<1> &clk = u.sys_clk;
  Wire<1> &rst = u.sys_rst;
  Wire<1> &in_valid = u.in_valid;
  Wire<8> &in_data = u.in_data;
  Wire<1> &out0_ready = u.out0_ready;
  Wire<1> &out1_ready = u.out1_ready;
  Wire<1> &in_ready = u.in_ready;
  Wire<1> &out0_valid = u.out0_valid;
  Wire<8> &out0_data = u.out0_data;
  Wire<1> &out1_valid = u.out1_valid;
  Wire<8> &out1_data = u.out1_data;

  void eval() { u.eval(); }
  void tick() { u.tick(); }
};

} // namespace

int main() {
  Dut dut;
  Testbench<Dut> tb(dut);

  const char *trace_dir_env = std::getenv("PYC_TRACE_DIR");
  std::filesystem::path out_root = trace_dir_env ? std::filesystem::path(trace_dir_env) : std::filesystem::path("examples/generated");
  std::filesystem::path out_dir = out_root / "tb_issue_queue_2picker";
  std::filesystem::create_directories(out_dir);

  tb.enableLog((out_dir / "tb_issue_queue_2picker_cpp.log").string());
  tb.enableVcd((out_dir / "tb_issue_queue_2picker_cpp.vcd").string(), /*top=*/"tb_issue_queue_2picker");
  tb.vcdTrace(dut.clk, "clk");
  tb.vcdTrace(dut.rst, "rst");
  tb.vcdTrace(dut.in_valid, "in_valid");
  tb.vcdTrace(dut.in_ready, "in_ready");
  tb.vcdTrace(dut.in_data, "in_data");
  tb.vcdTrace(dut.out0_valid, "out0_valid");
  tb.vcdTrace(dut.out0_ready, "out0_ready");
  tb.vcdTrace(dut.out0_data, "out0_data");
  tb.vcdTrace(dut.out1_valid, "out1_valid");
  tb.vcdTrace(dut.out1_ready, "out1_ready");
  tb.vcdTrace(dut.out1_data, "out1_data");

  tb.addClock(dut.clk, /*halfPeriodSteps=*/1);
  tb.reset(dut.rst, /*cyclesAsserted=*/2, /*cyclesDeasserted=*/1);

  std::deque<std::uint64_t> expected{};

  auto cycle = [&](bool in_valid, std::uint8_t in_data, bool out0_ready, bool out1_ready) {
    dut.in_valid = Wire<1>(in_valid ? 1u : 0u);
    dut.in_data = Wire<8>(in_data);
    dut.out0_ready = Wire<1>(out0_ready ? 1u : 0u);
    dut.out1_ready = Wire<1>(out1_ready ? 1u : 0u);

    dut.eval();

    bool do_push = dut.in_valid.toBool() && dut.in_ready.toBool();
    bool do_pop0 = dut.out0_valid.toBool() && dut.out0_ready.toBool();
    bool do_pop1 = dut.out1_valid.toBool() && dut.out1_ready.toBool() && do_pop0;

    if (do_pop0) {
      if (expected.empty()) {
        std::cerr << "ERROR: unexpected pop0\n";
        return false;
      }
      std::uint64_t got = dut.out0_data.value();
      std::uint64_t exp = expected.front();
      expected.pop_front();
      if (got != exp) {
        std::cerr << "ERROR: pop0 mismatch, got=0x" << std::hex << got << " exp=0x" << exp << std::dec << "\n";
        return false;
      }
    }
    if (do_pop1) {
      if (expected.empty()) {
        std::cerr << "ERROR: unexpected pop1\n";
        return false;
      }
      std::uint64_t got = dut.out1_data.value();
      std::uint64_t exp = expected.front();
      expected.pop_front();
      if (got != exp) {
        std::cerr << "ERROR: pop1 mismatch, got=0x" << std::hex << got << " exp=0x" << exp << std::dec << "\n";
        return false;
      }
    }
    if (do_push)
      expected.push_back(dut.in_data.value());

    tb.runCycles(1);
    return true;
  };

  if (!cycle(true, 0x11, false, false))
    return 1;
  if (!cycle(true, 0x22, false, false))
    return 1;
  if (!cycle(true, 0x33, false, false))
    return 1;
  if (!cycle(true, 0x44, false, false))
    return 1;

  if (!cycle(true, 0x55, false, false))
    return 1;

  if (!cycle(false, 0x00, true, true))
    return 1;
  if (!cycle(true, 0x66, true, true))
    return 1;
  if (!cycle(false, 0x00, true, false))
    return 1;
  if (!cycle(false, 0x00, true, true))
    return 1;

  while (!expected.empty()) {
    if (!cycle(false, 0x00, true, true))
      return 1;
  }

  if (dut.out0_valid.toBool() || dut.out1_valid.toBool()) {
    std::cerr << "ERROR: queue not empty at end\n";
    return 1;
  }

  tb.log() << "OK\n";
  return 0;
}
