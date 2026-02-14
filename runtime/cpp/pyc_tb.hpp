#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "pyc_bits.hpp"
#include "pyc_vcd.hpp"

namespace pyc::cpp {

struct TbClock {
  Wire<1> *clk = nullptr;
  std::uint64_t half_period_steps = 1;
  std::uint64_t phase_steps = 0;

  void set(bool high) const {
    if (clk)
      *clk = Wire<1>(high ? 1u : 0u);
  }

  void toggle() const {
    if (!clk)
      return;
    bool high = clk->toBool();
    *clk = Wire<1>(high ? 0u : 1u);
  }

  bool shouldToggle(std::uint64_t step) const {
    if (!clk || half_period_steps == 0)
      return false;
    return ((step + phase_steps) % half_period_steps) == 0;
  }
};

template <typename Dut>
class Testbench {
public:
  explicit Testbench(Dut &dut) : dut_(dut) {}

  bool enableVcd(const std::string &path, const std::string &top = "tb", const std::string &timescale = "1ns") {
    vcd_.emplace();
    return vcd_->open(path, top, timescale);
  }

  template <unsigned W>
  bool vcdTrace(Wire<W> &sig, const std::string &name) {
    if (!vcd_)
      return false;
    return vcd_->add(sig, name);
  }

  bool enableLog(const std::string &path) {
    log_.emplace(path, std::ios::out | std::ios::trunc);
    return log_->is_open();
  }

  bool logEnabled() const { return log_.has_value() && log_->is_open(); }

  std::ostream &log() { return logEnabled() ? *log_ : std::cerr; }

  void addClock(Wire<1> &clk, std::uint64_t halfPeriodSteps = 1, std::uint64_t phaseSteps = 0, bool startHigh = false) {
    TbClock c;
    c.clk = &clk;
    c.half_period_steps = (halfPeriodSteps == 0) ? 1 : halfPeriodSteps;
    c.phase_steps = phaseSteps;
    c.set(startHigh);
    clocks_.push_back(c);
    fast_clock0_enabled_ = (clocks_.size() == 1u) && (c.clk != nullptr) && (c.half_period_steps == 1u) && (c.phase_steps == 0u);
  }

  std::size_t numClocks() const { return clocks_.size(); }
  std::uint64_t timeSteps() const { return time_; }

  void step() {
    // Drive combinational logic before clock edges.
    dut_.eval();

    // Toggle all clocks that have an edge on this step.
    for (const auto &c : clocks_) {
      if (c.shouldToggle(time_))
        c.toggle();
    }

    // Sequential update (modules detect posedges internally).
    dut_.tick();

    // Re-evaluate combinational logic after state updates.
    dut_.eval();

    if (vcd_)
      vcd_->dump(time_);

    time_++;
  }

  void runSteps(std::uint64_t steps) {
    if (steps == 0)
      return;
    if (!vcd_) {
      for (std::uint64_t i = 0; i < steps; i++)
        stepNoDump();
      return;
    }
    for (std::uint64_t i = 0; i < steps; i++)
      step();
  }

  void runCycles(std::uint64_t cycles) { runCycles(/*clockIdx=*/0, cycles); }

  void runCycles(std::size_t clockIdx, std::uint64_t cycles) {
    if (clockIdx >= clocks_.size())
      return;
    const auto hp = clocks_[clockIdx].half_period_steps;
    runSteps(cycles * 2u * hp);
  }

  // Fast-path cycle stepping for the common single-clock case:
  // - one clock registered in this testbench
  // - selected clock has half_period_steps=1 and phase_steps=0
  //
  // For all other cases we conservatively fall back to the legacy runCycles().
  void runPosedgeCycles(std::uint64_t cycles) { runPosedgeCycles(/*clockIdx=*/0, cycles); }

  void runPosedgeCycles(std::size_t clockIdx, std::uint64_t cycles) {
    if (!(clockIdx == 0u && fast_clock0_enabled_)) {
      runCycles(clockIdx, cycles);
      return;
    }
    runPosedgeCyclesFast(cycles);
  }

  void runCyclesAuto(std::uint64_t cycles) { runCyclesAuto(/*clockIdx=*/0, cycles); }

  void runCyclesAuto(std::size_t clockIdx, std::uint64_t cycles) {
    if (clockIdx == 0u && fast_clock0_enabled_) {
      runPosedgeCyclesFast(cycles);
      return;
    }
    runCycles(clockIdx, cycles);
  }

  void reset(Wire<1> &rst, std::uint64_t cyclesAsserted = 2, std::uint64_t cyclesDeasserted = 1) {
    rst = Wire<1>(1);
    runCycles(cyclesAsserted);
    rst = Wire<1>(0);
    runCycles(cyclesDeasserted);
  }

private:
  void runPosedgeCyclesFast(std::uint64_t cycles) {
    if (cycles == 0)
      return;

    auto &c = clocks_[0];
    if (vcd_) {
      for (std::uint64_t i = 0; i < cycles; i++) {
        // Posedge phase.
        dut_.eval();
        c.set(true);
        dut_.tick();
        dut_.eval();
        vcd_->dump(time_);
        time_++;

        // Negedge bookkeeping (no extra combinational settle needed here).
        c.set(false);
        dut_.tick();
        vcd_->dump(time_);
        time_++;
      }
      return;
    }

    for (std::uint64_t i = 0; i < cycles; i++) {
      dut_.eval();
      c.set(true);
      dut_.tick();
      dut_.eval();
      time_++;
      c.set(false);
      dut_.tick();
      time_++;
    }
  }
  void stepNoDump() {
    dut_.eval();
    for (const auto &c : clocks_) {
      if (c.shouldToggle(time_))
        c.toggle();
    }
    dut_.tick();
    dut_.eval();
    time_++;
  }

  Dut &dut_;
  std::vector<TbClock> clocks_{};
  std::uint64_t time_ = 0;
  bool fast_clock0_enabled_ = false;
  std::optional<VcdWriter> vcd_{};
  std::optional<std::ofstream> log_{};
};

} // namespace pyc::cpp
