#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pyc_bits.hpp"

namespace pyc::cpp {

// Byte-addressed memory with async read + sync write.
//
// - `DepthBytes` is in bytes.
// - `rdata` is assembled little-endian from successive bytes at `raddr`.
// - Write uses `wstrb` byte enables relative to `waddr`.
// - Addresses are low-bit indexed into host `size_t`; out-of-range bytes are 0.
template <unsigned AddrWidth, unsigned DataWidth, std::size_t DepthBytes>
class pyc_byte_mem {
public:
  static_assert(DataWidth > 0, "pyc_byte_mem requires DataWidth > 0");
  static_assert((DataWidth % 8) == 0, "pyc_byte_mem requires DataWidth divisible by 8");
  static constexpr unsigned StrbWidth = DataWidth / 8;

  pyc_byte_mem(Wire<1> &clk,
               Wire<1> &rst,
               Wire<AddrWidth> &raddr,
               Wire<DataWidth> &rdata,
               Wire<1> &wvalid,
               Wire<AddrWidth> &waddr,
               Wire<DataWidth> &wdata,
               Wire<StrbWidth> &wstrb)
      : clk(clk), rst(rst), raddr(raddr), rdata(rdata), wvalid(wvalid), waddr(waddr), wdata(wdata), wstrb(wstrb),
        mem_(DepthBytes, 0u) {
    eval();
  }

  void eval() {
    std::size_t base = toIndex(raddr);
    if (evalValid && !dataDirty && base == lastEvalRaddr)
      return;

    Wire<DataWidth> v = Wire<DataWidth>(0);
    for (unsigned i = 0; i < StrbWidth; i++) {
      std::size_t ai = base + static_cast<std::size_t>(i);
      std::uint8_t b = (ai < DepthBytes) ? mem_[ai] : 0u;
      Wire<DataWidth> byteW = zext<DataWidth, 8>(Wire<8>(b));
      v = v | shl<DataWidth>(byteW, 8u * i);
    }
    rdata = v;
    lastEvalRaddr = base;
    evalValid = true;
    dataDirty = false;
  }

  void tick_compute() {
    bool clkNow = clk.toBool();
    bool posedge = (!clkPrev) && clkNow;
    clkPrev = clkNow;
    if (!posedge)
      return;

    pendingWrite = false;
    if (rst.toBool())
      return;

    if (wvalid.toBool()) {
      pendingWrite = true;
      latchedAddr = toIndex(waddr);
      latchedData = wdata;
      latchedStrb = wstrb;
    }
  }

  void tick_commit() {
    if (pendingWrite) {
      std::size_t base = latchedAddr;
      for (unsigned i = 0; i < StrbWidth; i++) {
        if (!latchedStrb.bit(i))
          continue;
        std::size_t ai = base + static_cast<std::size_t>(i);
        if (ai >= DepthBytes)
          continue;
        Wire<8> byte = extract<8, DataWidth>(latchedData, 8u * i);
        mem_[ai] = static_cast<std::uint8_t>(byte.value() & 0xFFu);
      }
      dataDirty = true;
    }
    pendingWrite = false;
    eval();
  }

  // Convenience for testbenches.
  void pokeByte(std::size_t addr, std::uint8_t value) {
    if (addr < DepthBytes) {
      mem_[addr] = value;
      dataDirty = true;
    }
  }
  std::uint8_t peekByte(std::size_t addr) const { return (addr < DepthBytes) ? mem_[addr] : 0u; }

  std::uint32_t peek32(std::size_t addr) const {
    std::uint32_t v = 0;
    for (unsigned i = 0; i < 4; i++) {
      std::size_t ai = addr + i;
      std::uint8_t b = (ai < DepthBytes) ? mem_[ai] : 0u;
      v |= (static_cast<std::uint32_t>(b) << (8u * i));
    }
    return v;
  }

public:
  Wire<1> &clk;
  Wire<1> &rst;

  Wire<AddrWidth> &raddr;
  Wire<DataWidth> &rdata;

  Wire<1> &wvalid;
  Wire<AddrWidth> &waddr;
  Wire<DataWidth> &wdata;
  Wire<StrbWidth> &wstrb;

  bool clkPrev = false;
  bool pendingWrite = false;
  std::size_t latchedAddr = 0;
  Wire<DataWidth> latchedData{};
  Wire<StrbWidth> latchedStrb{};
  bool evalValid = false;
  bool dataDirty = true;
  std::size_t lastEvalRaddr = 0;

  std::vector<std::uint8_t> mem_;

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
};

} // namespace pyc::cpp
