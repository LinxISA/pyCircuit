#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>

#include <cpp/pyc_print.hpp>
#include <cpp/pyc_tb.hpp>

using pyc::cpp::Testbench;
using pyc::cpp::Wire;
using pyc::cpp::pyc_fifo;

namespace {

struct Dut {
  Wire<1> clk{0};
  Wire<1> rst{0};

  Wire<1> in_valid{0};
  Wire<1> in_ready{0};
  Wire<8> in_data{0};

  Wire<1> out_valid{0};
  Wire<1> out_ready{0};
  Wire<8> out_data{0};

  pyc_fifo<8, 2> u_fifo;

  Dut() : u_fifo(clk, rst, in_valid, in_ready, in_data, out_valid, out_ready, out_data) {}

  void eval() { u_fifo.eval(); }
  void tick() { u_fifo.tick(); }
};

} // namespace

int main() {
  Dut dut;
  Testbench<Dut> tb(dut);

  const char *trace_dir_env = std::getenv("PYC_TRACE_DIR");
  std::filesystem::path out_root = trace_dir_env ? std::filesystem::path(trace_dir_env) : std::filesystem::path(".pycircuit_out/examples");
  std::filesystem::path out_dir = out_root / "tb_fifo";
  std::filesystem::create_directories(out_dir);

  tb.enableLog((out_dir / "tb_fifo_cpp.log").string());
  tb.enableVcd((out_dir / "tb_fifo_cpp.vcd").string(), /*top=*/"tb_fifo");
  tb.vcdTrace(dut.clk, "clk");
  tb.vcdTrace(dut.rst, "rst");
  tb.vcdTrace(dut.in_valid, "in_valid");
  tb.vcdTrace(dut.in_ready, "in_ready");
  tb.vcdTrace(dut.in_data, "in_data");
  tb.vcdTrace(dut.out_valid, "out_valid");
  tb.vcdTrace(dut.out_ready, "out_ready");
  tb.vcdTrace(dut.out_data, "out_data");

  tb.addClock(dut.clk, /*halfPeriodSteps=*/1);
  tb.reset(dut.rst, /*cyclesAsserted=*/2, /*cyclesDeasserted=*/1);

  std::deque<std::uint64_t> expected{};

  auto cycle = [&](bool in_valid, std::uint8_t in_data, bool out_ready) {
    dut.in_valid = Wire<1>(in_valid ? 1u : 0u);
    dut.in_data = Wire<8>(in_data);
    dut.out_ready = Wire<1>(out_ready ? 1u : 0u);

    // Handshake is sampled at posedge. Evaluate combinationally before the edge.
    dut.eval();
    bool do_push = dut.in_valid.toBool() && dut.in_ready.toBool();
    bool do_pop = dut.out_valid.toBool() && dut.out_ready.toBool();

    if (do_pop) {
      if (expected.empty()) {
        std::cerr << "ERROR: unexpected pop\n";
        return false;
      }
      std::uint64_t got = dut.out_data.value();
      std::uint64_t exp = expected.front();
      expected.pop_front();
      if (got != exp) {
        std::cerr << "ERROR: pop mismatch, got=0x" << std::hex << got << " exp=0x" << exp << std::dec << "\n";
        return false;
      }
    }
    if (do_push)
      expected.push_back(dut.in_data.value());

    tb.runCycles(1);
    tb.log() << "t=" << tb.timeSteps() << " " << dut.u_fifo << "\n";
    return true;
  };

  // Fill while downstream is stalled (Depth=2).
  if (!cycle(true, 0x11, false))
    return 1;
  if (!cycle(true, 0x22, false))
    return 1;
  // Third push should be back-pressured while full.
  if (!cycle(true, 0x33, false))
    return 1;

  // Enable downstream and test push+pop in the same cycle while full.
  if (!cycle(true, 0x44, true))
    return 1;

  // Drain.
  while (!expected.empty()) {
    if (!cycle(false, 0x00, true))
      return 1;
  }

  // One extra cycle: FIFO should be empty.
  if (!cycle(false, 0x00, true))
    return 1;

  if (dut.out_valid.toBool()) {
    std::cerr << "ERROR: FIFO not empty at end\n";
    return 1;
  }

  tb.log() << "OK\n";
  return 0;
}
