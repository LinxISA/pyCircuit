#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "pyc_bits.hpp"

namespace pyc::cpp {

// Synchronous 1R1W memory with registered read output.
//
// - `DepthEntries` is in entries (not bytes).
// - Read output updates on the next posedge of `clk` when `ren` is asserted.
// - Write occurs on posedge when `wvalid` is asserted, with byte enables `wstrb`.
// - Read-during-write to the same address returns written data (write-first).
// - Addresses are low-bit indexed into host `size_t`; out-of-range indices read as 0
//   and writes are dropped.
template <unsigned AddrWidth, unsigned DataWidth, std::size_t DepthEntries>
class pyc_sync_mem {
public:
  static_assert(DataWidth > 0, "pyc_sync_mem requires DataWidth > 0");
  static_assert((DataWidth % 8) == 0, "pyc_sync_mem requires DataWidth divisible by 8");
  static_assert(DepthEntries > 0, "pyc_sync_mem DepthEntries must be > 0");
  static constexpr unsigned StrbWidth = DataWidth / 8;

  pyc_sync_mem(Wire<1> &clk,
               Wire<1> &rst,
               Wire<1> &ren,
               Wire<AddrWidth> &raddr,
               Wire<DataWidth> &rdata,
               Wire<1> &wvalid,
               Wire<AddrWidth> &waddr,
               Wire<DataWidth> &wdata,
               Wire<StrbWidth> &wstrb)
      : clk(clk), rst(rst), ren(ren), raddr(raddr), rdata(rdata), wvalid(wvalid), waddr(waddr), wdata(wdata),
        wstrb(wstrb) {}

  void tick_compute() {
    bool clkNow = clk.toBool();
    bool posedge = (!clkPrev) && clkNow;
    clkPrev = clkNow;
    pendingWrite = false;
    pendingRead = false;
    if (!posedge)
      return;

    if (rst.toBool()) {
      pendingRead = true;
      rdataNext = Wire<DataWidth>(0);
      return;
    }

    if (wvalid.toBool()) {
      pendingWrite = true;
      latchedWaddr = toIndex(waddr);
      latchedWdata = wdata;
      latchedWstrb = wstrb;
    }

    if (ren.toBool()) {
      pendingRead = true;
      latchedRaddr = toIndex(raddr);
      Wire<DataWidth> v = Wire<DataWidth>(0);
      if (latchedRaddr < DepthEntries)
        v = mem_[latchedRaddr];
      if (pendingWrite && (latchedWaddr == latchedRaddr))
        v = applyStrb(v, latchedWdata, latchedWstrb);
      rdataNext = v;
    }
  }

  void tick_commit() {
    if (pendingWrite && (latchedWaddr < DepthEntries))
      mem_[latchedWaddr] = applyStrb(mem_[latchedWaddr], latchedWdata, latchedWstrb);
    if (pendingRead)
      rdata = rdataNext;
    pendingWrite = false;
    pendingRead = false;
  }

  // Convenience for testbenches.
  void pokeEntry(std::size_t addr, Wire<DataWidth> value) {
    if (addr < DepthEntries)
      mem_[addr] = value;
  }
  void pokeEntry(std::size_t addr, std::uint64_t value) { pokeEntry(addr, Wire<DataWidth>(value)); }
  Wire<DataWidth> peekEntryBits(std::size_t addr) const {
    return (addr < DepthEntries) ? mem_[addr] : Wire<DataWidth>(0);
  }
  std::uint64_t peekEntry(std::size_t addr) const { return peekEntryBits(addr).value(); }

public:
  Wire<1> &clk;
  Wire<1> &rst;

  Wire<1> &ren;
  Wire<AddrWidth> &raddr;
  Wire<DataWidth> &rdata;

  Wire<1> &wvalid;
  Wire<AddrWidth> &waddr;
  Wire<DataWidth> &wdata;
  Wire<StrbWidth> &wstrb;

  bool clkPrev = false;
  bool pendingWrite = false;
  bool pendingRead = false;
  std::size_t latchedWaddr = 0;
  std::size_t latchedRaddr = 0;
  Wire<DataWidth> latchedWdata{};
  Wire<StrbWidth> latchedWstrb{};
  Wire<DataWidth> rdataNext{};

private:
  static constexpr std::size_t toIndex(Wire<AddrWidth> addr) {
    if constexpr (AddrWidth <= (sizeof(std::size_t) * 8u))
      return static_cast<std::size_t>(addr.value());
    constexpr unsigned hostBits = static_cast<unsigned>(sizeof(std::size_t) * 8u);
    constexpr unsigned useBits = (AddrWidth < hostBits) ? AddrWidth : hostBits;
    std::size_t out = 0;
    for (unsigned i = 0; i < useBits; i++) {
      if (addr.bit(i))
        out |= (std::size_t{1} << i);
    }
    return out;
  }

  static constexpr Wire<DataWidth> applyStrb(Wire<DataWidth> oldV, Wire<DataWidth> newV, Wire<StrbWidth> strb) {
    if constexpr (DataWidth <= 64) {
      std::uint64_t out = oldV.value();
      std::uint64_t src = newV.value();
      for (unsigned i = 0; i < StrbWidth; i++) {
        if (!strb.bit(i))
          continue;
        std::uint64_t mask = (0xFFull << (8u * i));
        out = (out & ~mask) | (src & mask);
      }
      return Wire<DataWidth>(out);
    }
    Wire<DataWidth> v = oldV;
    for (unsigned i = 0; i < StrbWidth; i++) {
      if (!strb.bit(i))
        continue;
      Wire<8> byte = extract<8, DataWidth>(newV, 8u * i);
      Wire<DataWidth> byteData = shl<DataWidth>(zext<DataWidth, 8>(byte), 8u * i);
      Wire<DataWidth> byteMask = shl<DataWidth>(zext<DataWidth, 8>(Wire<8>(0xFFu)), 8u * i);
      v = (v & ~byteMask) | byteData;
    }
    return v;
  }

  std::array<Wire<DataWidth>, DepthEntries> mem_{};
};

// Synchronous 2R1W memory (dual read ports) with registered read outputs.
template <unsigned AddrWidth, unsigned DataWidth, std::size_t DepthEntries>
class pyc_sync_mem_dp {
public:
  static_assert(DataWidth > 0, "pyc_sync_mem_dp requires DataWidth > 0");
  static_assert((DataWidth % 8) == 0, "pyc_sync_mem_dp requires DataWidth divisible by 8");
  static_assert(DepthEntries > 0, "pyc_sync_mem_dp DepthEntries must be > 0");
  static constexpr unsigned StrbWidth = DataWidth / 8;

  pyc_sync_mem_dp(Wire<1> &clk,
                  Wire<1> &rst,
                  Wire<1> &ren0,
                  Wire<AddrWidth> &raddr0,
                  Wire<DataWidth> &rdata0,
                  Wire<1> &ren1,
                  Wire<AddrWidth> &raddr1,
                  Wire<DataWidth> &rdata1,
                  Wire<1> &wvalid,
                  Wire<AddrWidth> &waddr,
                  Wire<DataWidth> &wdata,
                  Wire<StrbWidth> &wstrb)
      : clk(clk), rst(rst), ren0(ren0), raddr0(raddr0), rdata0(rdata0), ren1(ren1), raddr1(raddr1), rdata1(rdata1),
        wvalid(wvalid), waddr(waddr), wdata(wdata), wstrb(wstrb) {}

  void tick_compute() {
    bool clkNow = clk.toBool();
    bool posedge = (!clkPrev) && clkNow;
    clkPrev = clkNow;
    pendingWrite = false;
    pendingRead0 = false;
    pendingRead1 = false;
    if (!posedge)
      return;

    if (rst.toBool()) {
      pendingRead0 = true;
      pendingRead1 = true;
      rdata0Next = Wire<DataWidth>(0);
      rdata1Next = Wire<DataWidth>(0);
      return;
    }

    if (wvalid.toBool()) {
      pendingWrite = true;
      latchedWaddr = toIndex(waddr);
      latchedWdata = wdata;
      latchedWstrb = wstrb;
    }

    if (ren0.toBool()) {
      pendingRead0 = true;
      latchedRaddr0 = toIndex(raddr0);
      Wire<DataWidth> v = Wire<DataWidth>(0);
      if (latchedRaddr0 < DepthEntries)
        v = mem_[latchedRaddr0];
      if (pendingWrite && (latchedWaddr == latchedRaddr0))
        v = applyStrb(v, latchedWdata, latchedWstrb);
      rdata0Next = v;
    }

    if (ren1.toBool()) {
      pendingRead1 = true;
      latchedRaddr1 = toIndex(raddr1);
      Wire<DataWidth> v = Wire<DataWidth>(0);
      if (latchedRaddr1 < DepthEntries)
        v = mem_[latchedRaddr1];
      if (pendingWrite && (latchedWaddr == latchedRaddr1))
        v = applyStrb(v, latchedWdata, latchedWstrb);
      rdata1Next = v;
    }
  }

  void tick_commit() {
    if (pendingWrite && (latchedWaddr < DepthEntries))
      mem_[latchedWaddr] = applyStrb(mem_[latchedWaddr], latchedWdata, latchedWstrb);
    if (pendingRead0)
      rdata0 = rdata0Next;
    if (pendingRead1)
      rdata1 = rdata1Next;
    pendingWrite = false;
    pendingRead0 = false;
    pendingRead1 = false;
  }

  void pokeEntry(std::size_t addr, Wire<DataWidth> value) {
    if (addr < DepthEntries)
      mem_[addr] = value;
  }
  void pokeEntry(std::size_t addr, std::uint64_t value) { pokeEntry(addr, Wire<DataWidth>(value)); }
  Wire<DataWidth> peekEntryBits(std::size_t addr) const {
    return (addr < DepthEntries) ? mem_[addr] : Wire<DataWidth>(0);
  }
  std::uint64_t peekEntry(std::size_t addr) const { return peekEntryBits(addr).value(); }

public:
  Wire<1> &clk;
  Wire<1> &rst;

  Wire<1> &ren0;
  Wire<AddrWidth> &raddr0;
  Wire<DataWidth> &rdata0;

  Wire<1> &ren1;
  Wire<AddrWidth> &raddr1;
  Wire<DataWidth> &rdata1;

  Wire<1> &wvalid;
  Wire<AddrWidth> &waddr;
  Wire<DataWidth> &wdata;
  Wire<StrbWidth> &wstrb;

  bool clkPrev = false;
  bool pendingWrite = false;
  bool pendingRead0 = false;
  bool pendingRead1 = false;
  std::size_t latchedWaddr = 0;
  std::size_t latchedRaddr0 = 0;
  std::size_t latchedRaddr1 = 0;
  Wire<DataWidth> latchedWdata{};
  Wire<StrbWidth> latchedWstrb{};
  Wire<DataWidth> rdata0Next{};
  Wire<DataWidth> rdata1Next{};

private:
  static constexpr std::size_t toIndex(Wire<AddrWidth> addr) {
    if constexpr (AddrWidth <= (sizeof(std::size_t) * 8u))
      return static_cast<std::size_t>(addr.value());
    constexpr unsigned hostBits = static_cast<unsigned>(sizeof(std::size_t) * 8u);
    constexpr unsigned useBits = (AddrWidth < hostBits) ? AddrWidth : hostBits;
    std::size_t out = 0;
    for (unsigned i = 0; i < useBits; i++) {
      if (addr.bit(i))
        out |= (std::size_t{1} << i);
    }
    return out;
  }

  static constexpr Wire<DataWidth> applyStrb(Wire<DataWidth> oldV, Wire<DataWidth> newV, Wire<StrbWidth> strb) {
    if constexpr (DataWidth <= 64) {
      std::uint64_t out = oldV.value();
      std::uint64_t src = newV.value();
      for (unsigned i = 0; i < StrbWidth; i++) {
        if (!strb.bit(i))
          continue;
        std::uint64_t mask = (0xFFull << (8u * i));
        out = (out & ~mask) | (src & mask);
      }
      return Wire<DataWidth>(out);
    }
    Wire<DataWidth> v = oldV;
    for (unsigned i = 0; i < StrbWidth; i++) {
      if (!strb.bit(i))
        continue;
      Wire<8> byte = extract<8, DataWidth>(newV, 8u * i);
      Wire<DataWidth> byteData = shl<DataWidth>(zext<DataWidth, 8>(byte), 8u * i);
      Wire<DataWidth> byteMask = shl<DataWidth>(zext<DataWidth, 8>(Wire<8>(0xFFu)), 8u * i);
      v = (v & ~byteMask) | byteData;
    }
    return v;
  }

  std::array<Wire<DataWidth>, DepthEntries> mem_{};
};

} // namespace pyc::cpp
