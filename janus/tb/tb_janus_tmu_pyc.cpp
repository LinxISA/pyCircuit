#include <array>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>

#include <pyc/cpp/pyc_konata.hpp>
#include <pyc/cpp/pyc_tb.hpp>

#include "janus_tmu_pyc_gen.hpp"

using pyc::cpp::Testbench;
using pyc::cpp::Wire;

namespace {

constexpr int kNodes = 8;
constexpr int kAddrBits = 20;
constexpr int kTagBits = 20;
constexpr int kWords = 32;
constexpr std::uint64_t kDeadlockCycles = 800;
constexpr int kDefaultCases = 20;
constexpr int kDefaultRoundsPerCase = 20;
constexpr int kWarmupCycles = 32;

using DataWord = Wire<64>;
using DataLineU64 = std::array<std::uint64_t, kWords>;

struct NodePorts {
  Wire<1> *req_valid = nullptr;
  Wire<1> *req_write = nullptr;
  Wire<kAddrBits> *req_addr = nullptr;
  Wire<kTagBits> *req_tag = nullptr;
  std::array<DataWord *, kWords> req_data{};
  Wire<1> *req_ready = nullptr;
  Wire<1> *resp_ready = nullptr;
  Wire<1> *resp_valid = nullptr;
  Wire<kTagBits> *resp_tag = nullptr;
  std::array<DataWord *, kWords> resp_data{};
  Wire<1> *resp_is_write = nullptr;
};

static bool envFlag(const char *name) {
  const char *v = std::getenv(name);
  if (!v)
    return false;
  return !(v[0] == '0' && v[1] == '\0');
}

static std::uint32_t makeAddr(std::uint32_t index, std::uint32_t pipe, std::uint32_t offset = 0) {
  return (index << 11) | (pipe << 8) | (offset & 0xFFu);
}

static DataLineU64 makeDataU64(std::uint32_t seed) {
  DataLineU64 out{};
  for (unsigned i = 0; i < kWords; i++) {
    std::uint64_t word = (static_cast<std::uint64_t>(seed) << 32) | i;
    out[i] = word;
  }
  return out;
}

static void zeroReq(NodePorts &n) {
  *n.req_valid = Wire<1>(0);
  *n.req_write = Wire<1>(0);
  *n.req_addr = Wire<kAddrBits>(0);
  *n.req_tag = Wire<kTagBits>(0);
  for (auto *w : n.req_data)
    *w = DataWord(0);
}

static void setRespReady(NodePorts &n, bool ready) { *n.resp_ready = Wire<1>(ready ? 1u : 0u); }

struct PendingReq {
  std::uint32_t addr = 0;
  std::uint32_t tag = 0;
  DataLineU64 data{};
  bool is_write = true;
};

struct ExpectedResp {
  DataLineU64 data{};
  bool is_write = true;
  std::uint64_t issue_cycle = 0;
};

struct NodeDriver {
  bool active = false;
  std::uint64_t active_since = 0;
  PendingReq current{};
  std::deque<PendingReq> queue;
  std::map<std::uint32_t, ExpectedResp> expected;
};

struct RespSample {
  bool valid = false;
  std::uint32_t tag = 0;
  bool is_write = false;
  DataLineU64 data{};
  bool dbg_use_bypass = false;
  bool dbg_pick_cw = false;
  bool dbg_pick_cc = false;
  std::uint32_t dbg_mgb_cw_tag = 0;
  std::uint64_t dbg_mgb_cw_w0 = 0;
  std::uint32_t dbg_mgb_cc_tag = 0;
  std::uint64_t dbg_mgb_cc_w0 = 0;
};

static void driveNodeReq(NodePorts &n, const PendingReq *req) {
  if (!req) {
    *n.req_valid = Wire<1>(0);
    *n.req_write = Wire<1>(0);
    *n.req_addr = Wire<kAddrBits>(0);
    *n.req_tag = Wire<kTagBits>(0);
    for (unsigned i = 0; i < kWords; i++)
      *n.req_data[i] = DataWord(0);
    return;
  }
  *n.req_valid = Wire<1>(1);
  *n.req_write = Wire<1>(req->is_write ? 1u : 0u);
  *n.req_addr = Wire<kAddrBits>(req->addr);
  *n.req_tag = Wire<kTagBits>(req->tag);
  for (unsigned i = 0; i < kWords; i++)
    *n.req_data[i] = DataWord(req->data[i]);
}

static std::uint32_t lcgNext(std::uint32_t &state) {
  state = state * 1664525u + 1013904223u;
  return state;
}

static void runCongestedCase(Testbench<pyc::gen::janus_tmu_pyc> &tb,
                             pyc::gen::janus_tmu_pyc &dut,
                             std::array<NodePorts, kNodes> &nodes,
                             int case_id,
                             int reqs_per_node,
                             std::uint64_t &cycle,
                             std::map<std::uint32_t, DataLineU64> &mem,
                             std::ofstream &trace) {
  std::array<NodeDriver, kNodes> drv{};
  const std::array<Wire<1> *, kNodes> dbg_resp_use_bypass = {
      &dut.dbg_resp_use_bypass0, &dut.dbg_resp_use_bypass1, &dut.dbg_resp_use_bypass2,
      &dut.dbg_resp_use_bypass3, &dut.dbg_resp_use_bypass4, &dut.dbg_resp_use_bypass5,
      &dut.dbg_resp_use_bypass6, &dut.dbg_resp_use_bypass7};
  const std::array<Wire<1> *, kNodes> dbg_resp_pick_cw = {
      &dut.dbg_resp_pick_cw0, &dut.dbg_resp_pick_cw1, &dut.dbg_resp_pick_cw2, &dut.dbg_resp_pick_cw3,
      &dut.dbg_resp_pick_cw4, &dut.dbg_resp_pick_cw5, &dut.dbg_resp_pick_cw6, &dut.dbg_resp_pick_cw7};
  const std::array<Wire<1> *, kNodes> dbg_resp_pick_cc = {
      &dut.dbg_resp_pick_cc0, &dut.dbg_resp_pick_cc1, &dut.dbg_resp_pick_cc2, &dut.dbg_resp_pick_cc3,
      &dut.dbg_resp_pick_cc4, &dut.dbg_resp_pick_cc5, &dut.dbg_resp_pick_cc6, &dut.dbg_resp_pick_cc7};
  const std::array<Wire<kTagBits> *, kNodes> dbg_mgb_cw_tag = {
      &dut.dbg_mgb_cw_tag0, &dut.dbg_mgb_cw_tag1, &dut.dbg_mgb_cw_tag2, &dut.dbg_mgb_cw_tag3,
      &dut.dbg_mgb_cw_tag4, &dut.dbg_mgb_cw_tag5, &dut.dbg_mgb_cw_tag6, &dut.dbg_mgb_cw_tag7};
  const std::array<Wire<64> *, kNodes> dbg_mgb_cw_w0 = {
      &dut.dbg_mgb_cw_w0_0, &dut.dbg_mgb_cw_w0_1, &dut.dbg_mgb_cw_w0_2, &dut.dbg_mgb_cw_w0_3,
      &dut.dbg_mgb_cw_w0_4, &dut.dbg_mgb_cw_w0_5, &dut.dbg_mgb_cw_w0_6, &dut.dbg_mgb_cw_w0_7};
  const std::array<Wire<kTagBits> *, kNodes> dbg_mgb_cc_tag = {
      &dut.dbg_mgb_cc_tag0, &dut.dbg_mgb_cc_tag1, &dut.dbg_mgb_cc_tag2, &dut.dbg_mgb_cc_tag3,
      &dut.dbg_mgb_cc_tag4, &dut.dbg_mgb_cc_tag5, &dut.dbg_mgb_cc_tag6, &dut.dbg_mgb_cc_tag7};
  const std::array<Wire<64> *, kNodes> dbg_mgb_cc_w0 = {
      &dut.dbg_mgb_cc_w0_0, &dut.dbg_mgb_cc_w0_1, &dut.dbg_mgb_cc_w0_2, &dut.dbg_mgb_cc_w0_3,
      &dut.dbg_mgb_cc_w0_4, &dut.dbg_mgb_cc_w0_5, &dut.dbg_mgb_cc_w0_6, &dut.dbg_mgb_cc_w0_7};

  std::array<std::uint32_t, kNodes> rng_state{};
  std::array<bool, kNodes> last_write_valid{};
  std::array<std::uint32_t, kNodes> last_write_addr{};
  std::array<DataLineU64, kNodes> last_write_data{};

  for (int n = 0; n < kNodes; n++) {
    rng_state[n] = 0x1234abcdU ^ (static_cast<std::uint32_t>(case_id) << 8) ^
                   static_cast<std::uint32_t>(n);
    const int base = case_id * 16;
    for (int seq = 0; seq < reqs_per_node; seq++) {
      const int pipe = (n + (seq % kNodes)) % kNodes;
      const int index = base + seq;
      PendingReq req{};
      std::uint32_t rnd = lcgNext(rng_state[n]);
      bool is_write = (rnd & 1u) != 0;
      if (seq == 0)
        is_write = true;

      bool use_last = (!is_write) && last_write_valid[n] && ((lcgNext(rng_state[n]) & 3u) == 0);
      if (use_last) {
        req.addr = last_write_addr[n];
      } else {
        req.addr = makeAddr(static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(pipe));
      }
      req.tag = req.addr;
      req.is_write = is_write;
      if (is_write) {
        req.data = makeDataU64(static_cast<std::uint32_t>((case_id << 12) | (n << 6) | seq));
        last_write_valid[n] = true;
        last_write_addr[n] = req.addr;
        last_write_data[n] = req.data;
      }
      drv[n].queue.push_back(req);
    }
  }

  std::uint64_t outstanding = static_cast<std::uint64_t>(reqs_per_node) * kNodes;
  std::uint64_t safety = 0;
  const bool progress_log = envFlag("PYC_TMU_PROGRESS");
  std::uint64_t progress_stride = 1000;
  if (progress_log) {
    if (const char *stride_env = std::getenv("PYC_TMU_PROGRESS_STRIDE")) {
      const auto v = static_cast<std::uint64_t>(std::strtoull(stride_env, nullptr, 10));
      if (v > 0)
        progress_stride = v;
    }
  }

  while (outstanding > 0) {
    for (int n = 0; n < kNodes; n++) {
      bool ready = false;
      if (cycle >= kWarmupCycles) {
        ready = (((cycle + static_cast<std::uint64_t>(n) + static_cast<std::uint64_t>(case_id)) % 2) == 0);
      }
      setRespReady(nodes[n], ready);
      if (!drv[n].active && !drv[n].queue.empty()) {
        drv[n].current = drv[n].queue.front();
        drv[n].queue.pop_front();
        drv[n].active = true;
        drv[n].active_since = cycle;
      }
      driveNodeReq(nodes[n], drv[n].active ? &drv[n].current : nullptr);
    }

    dut.eval();
    std::array<bool, kNodes> req_fire{};
    std::array<RespSample, kNodes> resp_sample{};
    for (int n = 0; n < kNodes; n++) {
      req_fire[n] = drv[n].active && nodes[n].req_ready->toBool();
      const bool ready = nodes[n].resp_ready->toBool();
      const bool valid = nodes[n].resp_valid->toBool();
      if (ready && valid) {
        resp_sample[n].valid = true;
        resp_sample[n].tag = static_cast<std::uint32_t>(nodes[n].resp_tag->value());
        resp_sample[n].is_write = nodes[n].resp_is_write->toBool();
        for (unsigned w = 0; w < kWords; w++) {
          resp_sample[n].data[w] = static_cast<std::uint64_t>(nodes[n].resp_data[w]->value());
        }
        resp_sample[n].dbg_use_bypass = dbg_resp_use_bypass[n]->toBool();
        resp_sample[n].dbg_pick_cw = dbg_resp_pick_cw[n]->toBool();
        resp_sample[n].dbg_pick_cc = dbg_resp_pick_cc[n]->toBool();
        resp_sample[n].dbg_mgb_cw_tag = static_cast<std::uint32_t>(dbg_mgb_cw_tag[n]->value());
        resp_sample[n].dbg_mgb_cw_w0 = static_cast<std::uint64_t>(dbg_mgb_cw_w0[n]->value());
        resp_sample[n].dbg_mgb_cc_tag = static_cast<std::uint32_t>(dbg_mgb_cc_tag[n]->value());
        resp_sample[n].dbg_mgb_cc_w0 = static_cast<std::uint64_t>(dbg_mgb_cc_w0[n]->value());
      }
    }

    tb.runCycles(1);
    cycle++;
    safety++;

    if (progress_log && (cycle % progress_stride) == 0) {
      std::cerr << "PROGRESS case=" << case_id << " cycle=" << cycle << " outstanding=" << outstanding << "\n";
    }

    for (int n = 0; n < kNodes; n++) {
      if (req_fire[n]) {
        ExpectedResp exp{};
        exp.issue_cycle = cycle;
        exp.is_write = drv[n].current.is_write;
        if (drv[n].current.is_write) {
          mem[drv[n].current.addr] = drv[n].current.data;
          exp.data = drv[n].current.data;
        } else {
          auto mit = mem.find(drv[n].current.addr);
          if (mit != mem.end())
            exp.data = mit->second;
        }
        drv[n].expected[drv[n].current.tag] = exp;
        if (trace.is_open()) {
          trace << cycle << ",accept," << n << "," << drv[n].current.tag << ","
                << (drv[n].current.is_write ? 1 : 0) << ",0x" << std::hex << drv[n].current.addr << std::dec
                << ",0x" << std::hex << drv[n].current.data[0] << std::dec << "\n";
        }
        drv[n].active = false;
      }
    }

    for (int n = 0; n < kNodes; n++) {
      if (!resp_sample[n].valid)
        continue;
      const std::uint32_t tag = resp_sample[n].tag;
      auto it = drv[n].expected.find(tag);
      if (it == drv[n].expected.end()) {
        std::cerr << "FAIL: unexpected resp tag. case=" << case_id << " node=" << n << " tag=0x" << std::hex
                  << tag << std::dec << "\n";
        std::exit(1);
      }
      if (resp_sample[n].is_write != it->second.is_write) {
        std::cerr << "FAIL: resp_is_write mismatch. case=" << case_id << " node=" << n << " tag=0x" << std::hex
                  << tag << std::dec << "\n";
        std::exit(1);
      }
      for (unsigned w = 0; w < kWords; w++) {
        if (resp_sample[n].data[w] != it->second.data[w]) {
          std::cerr << "FAIL: resp_data mismatch. case=" << case_id << " node=" << n << " tag=0x" << std::hex
                    << tag << std::dec << " word=" << w << " exp=0x" << std::hex << it->second.data[w]
                    << " got=0x"
                    << resp_sample[n].data[w] << std::dec << "\n";
          if (envFlag("PYC_TMU_DUMP")) {
            std::cerr << "  resp_dbg use_bypass=" << resp_sample[n].dbg_use_bypass
                      << " pick_cw=" << resp_sample[n].dbg_pick_cw << " pick_cc=" << resp_sample[n].dbg_pick_cc
                      << " mgb_cw_tag=0x" << std::hex << resp_sample[n].dbg_mgb_cw_tag << " mgb_cw_w0=0x"
                      << resp_sample[n].dbg_mgb_cw_w0 << " mgb_cc_tag=0x" << resp_sample[n].dbg_mgb_cc_tag
                      << " mgb_cc_w0=0x" << resp_sample[n].dbg_mgb_cc_w0 << std::dec << "\n";
            std::array<Wire<1> *, kNodes> dbg_spb_cw_v = {
                &dut.dbg_spb_cw_v0, &dut.dbg_spb_cw_v1, &dut.dbg_spb_cw_v2, &dut.dbg_spb_cw_v3,
                &dut.dbg_spb_cw_v4, &dut.dbg_spb_cw_v5, &dut.dbg_spb_cw_v6, &dut.dbg_spb_cw_v7};
            std::array<Wire<kTagBits> *, kNodes> dbg_spb_cw_tag = {
                &dut.dbg_spb_cw_tag0, &dut.dbg_spb_cw_tag1, &dut.dbg_spb_cw_tag2, &dut.dbg_spb_cw_tag3,
                &dut.dbg_spb_cw_tag4, &dut.dbg_spb_cw_tag5, &dut.dbg_spb_cw_tag6, &dut.dbg_spb_cw_tag7};
            std::array<Wire<64> *, kNodes> dbg_spb_cw_w0 = {
                &dut.dbg_spb_cw_w0_0, &dut.dbg_spb_cw_w0_1, &dut.dbg_spb_cw_w0_2, &dut.dbg_spb_cw_w0_3,
                &dut.dbg_spb_cw_w0_4, &dut.dbg_spb_cw_w0_5, &dut.dbg_spb_cw_w0_6, &dut.dbg_spb_cw_w0_7};
            std::array<Wire<1> *, kNodes> dbg_pipe_v = {&dut.dbg_pipe_v0, &dut.dbg_pipe_v1, &dut.dbg_pipe_v2,
                                                        &dut.dbg_pipe_v3, &dut.dbg_pipe_v4, &dut.dbg_pipe_v5,
                                                        &dut.dbg_pipe_v6, &dut.dbg_pipe_v7};
            std::array<Wire<kTagBits> *, kNodes> dbg_pipe_tag = {
                &dut.dbg_pipe_tag0, &dut.dbg_pipe_tag1, &dut.dbg_pipe_tag2, &dut.dbg_pipe_tag3,
                &dut.dbg_pipe_tag4, &dut.dbg_pipe_tag5, &dut.dbg_pipe_tag6, &dut.dbg_pipe_tag7};
            std::array<Wire<64> *, kNodes> dbg_pipe_w0 = {
                &dut.dbg_pipe_w0_0, &dut.dbg_pipe_w0_1, &dut.dbg_pipe_w0_2, &dut.dbg_pipe_w0_3,
                &dut.dbg_pipe_w0_4, &dut.dbg_pipe_w0_5, &dut.dbg_pipe_w0_6, &dut.dbg_pipe_w0_7};
            std::array<Wire<1> *, kNodes> dbg_rsp_cw_v = {
                &dut.dbg_rsp_cw_v0, &dut.dbg_rsp_cw_v1, &dut.dbg_rsp_cw_v2, &dut.dbg_rsp_cw_v3,
                &dut.dbg_rsp_cw_v4, &dut.dbg_rsp_cw_v5, &dut.dbg_rsp_cw_v6, &dut.dbg_rsp_cw_v7};
            std::array<Wire<kTagBits> *, kNodes> dbg_rsp_cw_tag = {
                &dut.dbg_rsp_cw_tag0, &dut.dbg_rsp_cw_tag1, &dut.dbg_rsp_cw_tag2, &dut.dbg_rsp_cw_tag3,
                &dut.dbg_rsp_cw_tag4, &dut.dbg_rsp_cw_tag5, &dut.dbg_rsp_cw_tag6, &dut.dbg_rsp_cw_tag7};
            std::array<Wire<64> *, kNodes> dbg_rsp_cw_w0 = {
                &dut.dbg_rsp_cw_w0_0, &dut.dbg_rsp_cw_w0_1, &dut.dbg_rsp_cw_w0_2, &dut.dbg_rsp_cw_w0_3,
                &dut.dbg_rsp_cw_w0_4, &dut.dbg_rsp_cw_w0_5, &dut.dbg_rsp_cw_w0_6, &dut.dbg_rsp_cw_w0_7};
            std::array<Wire<1> *, kNodes> dbg_rsp_cc_v = {
                &dut.dbg_rsp_cc_v0, &dut.dbg_rsp_cc_v1, &dut.dbg_rsp_cc_v2, &dut.dbg_rsp_cc_v3,
                &dut.dbg_rsp_cc_v4, &dut.dbg_rsp_cc_v5, &dut.dbg_rsp_cc_v6, &dut.dbg_rsp_cc_v7};
            std::array<Wire<kTagBits> *, kNodes> dbg_rsp_cc_tag = {
                &dut.dbg_rsp_cc_tag0, &dut.dbg_rsp_cc_tag1, &dut.dbg_rsp_cc_tag2, &dut.dbg_rsp_cc_tag3,
                &dut.dbg_rsp_cc_tag4, &dut.dbg_rsp_cc_tag5, &dut.dbg_rsp_cc_tag6, &dut.dbg_rsp_cc_tag7};
            std::array<Wire<64> *, kNodes> dbg_rsp_cc_w0 = {
                &dut.dbg_rsp_cc_w0_0, &dut.dbg_rsp_cc_w0_1, &dut.dbg_rsp_cc_w0_2, &dut.dbg_rsp_cc_w0_3,
                &dut.dbg_rsp_cc_w0_4, &dut.dbg_rsp_cc_w0_5, &dut.dbg_rsp_cc_w0_6, &dut.dbg_rsp_cc_w0_7};
            std::array<Wire<1> *, kNodes> dbg_mgb_cw_v = {
                &dut.dbg_mgb_cw_v0, &dut.dbg_mgb_cw_v1, &dut.dbg_mgb_cw_v2, &dut.dbg_mgb_cw_v3,
                &dut.dbg_mgb_cw_v4, &dut.dbg_mgb_cw_v5, &dut.dbg_mgb_cw_v6, &dut.dbg_mgb_cw_v7};
            std::array<Wire<kTagBits> *, kNodes> dbg_mgb_cw_tag = {
                &dut.dbg_mgb_cw_tag0, &dut.dbg_mgb_cw_tag1, &dut.dbg_mgb_cw_tag2, &dut.dbg_mgb_cw_tag3,
                &dut.dbg_mgb_cw_tag4, &dut.dbg_mgb_cw_tag5, &dut.dbg_mgb_cw_tag6, &dut.dbg_mgb_cw_tag7};
            std::array<Wire<64> *, kNodes> dbg_mgb_cw_w0 = {
                &dut.dbg_mgb_cw_w0_0, &dut.dbg_mgb_cw_w0_1, &dut.dbg_mgb_cw_w0_2, &dut.dbg_mgb_cw_w0_3,
                &dut.dbg_mgb_cw_w0_4, &dut.dbg_mgb_cw_w0_5, &dut.dbg_mgb_cw_w0_6, &dut.dbg_mgb_cw_w0_7};
            std::array<Wire<1> *, kNodes> dbg_mgb_cc_v = {
                &dut.dbg_mgb_cc_v0, &dut.dbg_mgb_cc_v1, &dut.dbg_mgb_cc_v2, &dut.dbg_mgb_cc_v3,
                &dut.dbg_mgb_cc_v4, &dut.dbg_mgb_cc_v5, &dut.dbg_mgb_cc_v6, &dut.dbg_mgb_cc_v7};
            std::array<Wire<kTagBits> *, kNodes> dbg_mgb_cc_tag = {
                &dut.dbg_mgb_cc_tag0, &dut.dbg_mgb_cc_tag1, &dut.dbg_mgb_cc_tag2, &dut.dbg_mgb_cc_tag3,
                &dut.dbg_mgb_cc_tag4, &dut.dbg_mgb_cc_tag5, &dut.dbg_mgb_cc_tag6, &dut.dbg_mgb_cc_tag7};
            std::array<Wire<64> *, kNodes> dbg_mgb_cc_w0 = {
                &dut.dbg_mgb_cc_w0_0, &dut.dbg_mgb_cc_w0_1, &dut.dbg_mgb_cc_w0_2, &dut.dbg_mgb_cc_w0_3,
                &dut.dbg_mgb_cc_w0_4, &dut.dbg_mgb_cc_w0_5, &dut.dbg_mgb_cc_w0_6, &dut.dbg_mgb_cc_w0_7};
            std::array<Wire<1> *, kNodes> dbg_resp_use_bypass = {
                &dut.dbg_resp_use_bypass0, &dut.dbg_resp_use_bypass1, &dut.dbg_resp_use_bypass2,
                &dut.dbg_resp_use_bypass3, &dut.dbg_resp_use_bypass4, &dut.dbg_resp_use_bypass5,
                &dut.dbg_resp_use_bypass6, &dut.dbg_resp_use_bypass7};
            std::array<Wire<1> *, kNodes> dbg_resp_pick_cw = {
                &dut.dbg_resp_pick_cw0, &dut.dbg_resp_pick_cw1, &dut.dbg_resp_pick_cw2, &dut.dbg_resp_pick_cw3,
                &dut.dbg_resp_pick_cw4, &dut.dbg_resp_pick_cw5, &dut.dbg_resp_pick_cw6, &dut.dbg_resp_pick_cw7};
            std::array<Wire<1> *, kNodes> dbg_resp_pick_cc = {
                &dut.dbg_resp_pick_cc0, &dut.dbg_resp_pick_cc1, &dut.dbg_resp_pick_cc2, &dut.dbg_resp_pick_cc3,
                &dut.dbg_resp_pick_cc4, &dut.dbg_resp_pick_cc5, &dut.dbg_resp_pick_cc6, &dut.dbg_resp_pick_cc7};
            const int pipe = static_cast<int>((tag >> 8) & 0x7u);
            auto dump_node = [&](int idx, const char *label) {
              std::cerr << "  dbg " << label << " spb_cw_v=" << dbg_spb_cw_v[idx]->toBool() << " spb_cw_tag=0x"
                        << std::hex << dbg_spb_cw_tag[idx]->value() << std::dec << " spb_cw_w0=0x" << std::hex
                        << dbg_spb_cw_w0[idx]->value() << std::dec << " pipe_v=" << dbg_pipe_v[idx]->toBool()
                        << " pipe_tag=0x" << std::hex << dbg_pipe_tag[idx]->value() << std::dec
                        << " pipe_w0=0x" << std::hex << dbg_pipe_w0[idx]->value() << std::dec
                        << " rsp_cw_v=" << dbg_rsp_cw_v[idx]->toBool() << " rsp_cw_tag=0x" << std::hex
                        << dbg_rsp_cw_tag[idx]->value() << std::dec << " rsp_cw_w0=0x" << std::hex
                        << dbg_rsp_cw_w0[idx]->value() << std::dec << " rsp_cc_v=" << dbg_rsp_cc_v[idx]->toBool()
                        << " rsp_cc_tag=0x" << std::hex << dbg_rsp_cc_tag[idx]->value() << std::dec
                        << " rsp_cc_w0=0x" << std::hex << dbg_rsp_cc_w0[idx]->value() << std::dec
                        << " mgb_cw_v=" << dbg_mgb_cw_v[idx]->toBool() << " mgb_cw_tag=0x" << std::hex
                        << dbg_mgb_cw_tag[idx]->value() << std::dec << " mgb_cw_w0=0x" << std::hex
                        << dbg_mgb_cw_w0[idx]->value() << std::dec << " mgb_cc_v=" << dbg_mgb_cc_v[idx]->toBool()
                        << " mgb_cc_tag=0x" << std::hex << dbg_mgb_cc_tag[idx]->value() << std::dec
                        << " mgb_cc_w0=0x" << std::hex << dbg_mgb_cc_w0[idx]->value() << std::dec
                        << " use_bypass=" << dbg_resp_use_bypass[idx]->toBool()
                        << " pick_cw=" << dbg_resp_pick_cw[idx]->toBool()
                        << " pick_cc=" << dbg_resp_pick_cc[idx]->toBool() << "\n";
            };
            dump_node(n, "node");
            dump_node(pipe, "pipe");
          }
          std::exit(1);
        }
      }
      if (trace.is_open()) {
        trace << cycle << ",resp," << n << "," << tag << "," << (it->second.is_write ? 1 : 0) << ",0x"
              << std::hex << it->second.data[0] << std::dec << "\n";
      }
      drv[n].expected.erase(it);
      outstanding--;
    }

    if (outstanding > 0) {
      for (int n = 0; n < kNodes; n++) {
        if (drv[n].active) {
          const std::uint64_t age = cycle - drv[n].active_since;
          if (age > kDeadlockCycles) {
            std::cerr << "FAIL: deadlock (req not accepted). case=" << case_id << " node=" << n << " tag=0x"
                      << std::hex << drv[n].current.tag << std::dec << " age=" << age << " cycle=" << cycle << "\n";
            std::exit(1);
          }
        }
      }
      for (int n = 0; n < kNodes; n++) {
        for (const auto &kv : drv[n].expected) {
          const std::uint64_t age = cycle - kv.second.issue_cycle;
          if (age > kDeadlockCycles) {
            std::cerr << "FAIL: deadlock. case=" << case_id << " node=" << n << " tag=0x" << std::hex
                      << kv.first << std::dec << " age=" << age << " cycle=" << cycle << "\n";
            std::exit(1);
          }
        }
      }
    }
    if (safety > 200000) {
      std::cerr << "FAIL: safety timeout. case=" << case_id << " cycle=" << cycle << "\n";
      std::exit(1);
    }
  }
}

static void runSimultaneousCase(Testbench<pyc::gen::janus_tmu_pyc> &tb,
                                pyc::gen::janus_tmu_pyc &dut,
                                std::array<NodePorts, kNodes> &nodes,
                                std::uint64_t &cycle,
                                std::map<std::uint32_t, DataLineU64> &mem,
                                std::ofstream &trace) {
  std::array<NodeDriver, kNodes> drv{};
  std::array<std::uint32_t, kNodes> rng_state{};
  for (int n = 0; n < kNodes; n++) {
    rng_state[n] = 0xA5A50000u ^ static_cast<std::uint32_t>(n);
    PendingReq req{};
    const int pipe = n;
    const int index = n;
    req.addr = makeAddr(static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(pipe));
    req.tag = req.addr;
    req.is_write = (lcgNext(rng_state[n]) & 1u) != 0;
    if (req.is_write) {
      req.data = makeDataU64(static_cast<std::uint32_t>(0xA500u | static_cast<std::uint32_t>(n)));
    }
    drv[n].current = req;
    drv[n].active = true;
    drv[n].active_since = cycle;
  }

  std::uint64_t outstanding = kNodes;
  std::uint64_t safety = 0;

  while (outstanding > 0) {
    for (int n = 0; n < kNodes; n++) {
      setRespReady(nodes[n], true);
      driveNodeReq(nodes[n], drv[n].active ? &drv[n].current : nullptr);
    }

    dut.eval();
    std::array<bool, kNodes> req_fire{};
    std::array<RespSample, kNodes> resp_sample{};
    for (int n = 0; n < kNodes; n++) {
      req_fire[n] = drv[n].active && nodes[n].req_ready->toBool();
      const bool ready = nodes[n].resp_ready->toBool();
      const bool valid = nodes[n].resp_valid->toBool();
      if (ready && valid) {
        resp_sample[n].valid = true;
        resp_sample[n].tag = static_cast<std::uint32_t>(nodes[n].resp_tag->value());
        resp_sample[n].is_write = nodes[n].resp_is_write->toBool();
        for (unsigned w = 0; w < kWords; w++) {
          resp_sample[n].data[w] = static_cast<std::uint64_t>(nodes[n].resp_data[w]->value());
        }
      }
    }

    tb.runCycles(1);
    cycle++;
    safety++;

    for (int n = 0; n < kNodes; n++) {
      if (req_fire[n]) {
        ExpectedResp exp{};
        exp.issue_cycle = cycle;
        exp.is_write = drv[n].current.is_write;
        if (drv[n].current.is_write) {
          mem[drv[n].current.addr] = drv[n].current.data;
          exp.data = drv[n].current.data;
        } else {
          auto mit = mem.find(drv[n].current.addr);
          if (mit != mem.end())
            exp.data = mit->second;
        }
        drv[n].expected[drv[n].current.tag] = exp;
        if (trace.is_open()) {
          trace << cycle << ",accept," << n << "," << drv[n].current.tag << ","
                << (drv[n].current.is_write ? 1 : 0) << ",0x" << std::hex << drv[n].current.addr << std::dec
                << ",0x" << std::hex << drv[n].current.data[0] << std::dec << "\n";
        }
        drv[n].active = false;
      }
    }

    for (int n = 0; n < kNodes; n++) {
      if (!resp_sample[n].valid)
        continue;
      const std::uint32_t tag = resp_sample[n].tag;
      auto it = drv[n].expected.find(tag);
      if (it == drv[n].expected.end()) {
        std::cerr << "FAIL: unexpected resp tag. case=simul node=" << n << " tag=0x" << std::hex << tag
                  << std::dec << "\n";
        std::exit(1);
      }
      if (resp_sample[n].is_write != it->second.is_write) {
        std::cerr << "FAIL: resp_is_write mismatch. case=simul node=" << n << " tag=0x" << std::hex << tag
                  << std::dec << "\n";
        std::exit(1);
      }
      for (unsigned w = 0; w < kWords; w++) {
        if (resp_sample[n].data[w] != it->second.data[w]) {
          std::cerr << "FAIL: resp_data mismatch. case=simul node=" << n << " tag=0x" << std::hex << tag
                    << std::dec << " word=" << w << " exp=0x" << std::hex << it->second.data[w] << " got=0x"
                    << resp_sample[n].data[w] << std::dec << "\n";
          std::exit(1);
        }
      }
      if (trace.is_open()) {
        trace << cycle << ",resp," << n << "," << tag << "," << (it->second.is_write ? 1 : 0) << ",0x"
              << std::hex << it->second.data[0] << std::dec << "\n";
      }
      drv[n].expected.erase(it);
      outstanding--;
    }

    if (outstanding > 0) {
      for (int n = 0; n < kNodes; n++) {
        if (drv[n].active) {
          const std::uint64_t age = cycle - drv[n].active_since;
          if (age > kDeadlockCycles) {
            std::cerr << "FAIL: deadlock (req not accepted). case=simul node=" << n << " tag=0x" << std::hex
                      << drv[n].current.tag << std::dec << " age=" << age << " cycle=" << cycle << "\n";
            std::exit(1);
          }
        }
      }
      for (int n = 0; n < kNodes; n++) {
        for (const auto &kv : drv[n].expected) {
          const std::uint64_t age = cycle - kv.second.issue_cycle;
          if (age > kDeadlockCycles) {
            std::cerr << "FAIL: deadlock. case=simul node=" << n << " tag=0x" << std::hex << kv.first << std::dec
                      << " age=" << age << " cycle=" << cycle << "\n";
            std::exit(1);
          }
        }
      }
    }
    if (safety > 20000) {
      std::cerr << "FAIL: safety timeout. case=simul cycle=" << cycle << "\n";
      std::exit(1);
    }
  }
}

} // namespace

int main() {
  pyc::gen::janus_tmu_pyc dut{};
  Testbench<pyc::gen::janus_tmu_pyc> tb(dut);

  const bool trace_log = envFlag("PYC_TRACE");
  const bool trace_vcd = envFlag("PYC_VCD");

  std::filesystem::path out_dir{};
  if (trace_log || trace_vcd) {
    const char *trace_dir_env = std::getenv("PYC_TRACE_DIR");
    out_dir = trace_dir_env ? std::filesystem::path(trace_dir_env) : std::filesystem::path("janus/generated/janus_tmu_pyc");
    std::filesystem::create_directories(out_dir);
  }

  if (trace_log) {
    tb.enableLog((out_dir / "tb_janus_tmu_pyc_cpp.log").string());
  }

  if (trace_vcd) {
    tb.enableVcd((out_dir / "tb_janus_tmu_pyc_cpp.vcd").string(), /*top=*/"tb_janus_tmu_pyc_cpp");
    tb.vcdTrace(dut.clk, "clk");
    tb.vcdTrace(dut.rst, "rst");
    tb.vcdTrace(dut.n0_req_valid, "n0_req_valid");
    tb.vcdTrace(dut.n0_req_ready, "n0_req_ready");
    tb.vcdTrace(dut.n0_resp_valid, "n0_resp_valid");
    tb.vcdTrace(dut.n0_resp_is_write, "n0_resp_is_write");
    tb.vcdTrace(dut.n0_resp_tag, "n0_resp_tag");
    tb.vcdTrace(dut.n0_req_data_w0, "n0_req_data_w0");
    tb.vcdTrace(dut.n0_resp_data_w0, "n0_resp_data_w0");
    tb.vcdTrace(dut.dbg_req_cw_v0, "dbg_req_cw_v0");
    tb.vcdTrace(dut.dbg_req_cc_v0, "dbg_req_cc_v0");
    tb.vcdTrace(dut.dbg_rsp_cw_v0, "dbg_rsp_cw_v0");
    tb.vcdTrace(dut.dbg_rsp_cc_v0, "dbg_rsp_cc_v0");
    tb.vcdTrace(dut.dbg_req_cw_v1, "dbg_req_cw_v1");
    tb.vcdTrace(dut.dbg_req_cc_v1, "dbg_req_cc_v1");
    tb.vcdTrace(dut.dbg_rsp_cw_v1, "dbg_rsp_cw_v1");
    tb.vcdTrace(dut.dbg_rsp_cc_v1, "dbg_rsp_cc_v1");
    tb.vcdTrace(dut.dbg_req_cw_v2, "dbg_req_cw_v2");
    tb.vcdTrace(dut.dbg_req_cc_v2, "dbg_req_cc_v2");
    tb.vcdTrace(dut.dbg_rsp_cw_v2, "dbg_rsp_cw_v2");
    tb.vcdTrace(dut.dbg_rsp_cc_v2, "dbg_rsp_cc_v2");
    tb.vcdTrace(dut.dbg_req_cw_v3, "dbg_req_cw_v3");
    tb.vcdTrace(dut.dbg_req_cc_v3, "dbg_req_cc_v3");
    tb.vcdTrace(dut.dbg_rsp_cw_v3, "dbg_rsp_cw_v3");
    tb.vcdTrace(dut.dbg_rsp_cc_v3, "dbg_rsp_cc_v3");
    tb.vcdTrace(dut.dbg_req_cw_v4, "dbg_req_cw_v4");
    tb.vcdTrace(dut.dbg_req_cc_v4, "dbg_req_cc_v4");
    tb.vcdTrace(dut.dbg_rsp_cw_v4, "dbg_rsp_cw_v4");
    tb.vcdTrace(dut.dbg_rsp_cc_v4, "dbg_rsp_cc_v4");
    tb.vcdTrace(dut.dbg_req_cw_v5, "dbg_req_cw_v5");
    tb.vcdTrace(dut.dbg_req_cc_v5, "dbg_req_cc_v5");
    tb.vcdTrace(dut.dbg_rsp_cw_v5, "dbg_rsp_cw_v5");
    tb.vcdTrace(dut.dbg_rsp_cc_v5, "dbg_rsp_cc_v5");
    tb.vcdTrace(dut.dbg_req_cw_v6, "dbg_req_cw_v6");
    tb.vcdTrace(dut.dbg_req_cc_v6, "dbg_req_cc_v6");
    tb.vcdTrace(dut.dbg_rsp_cw_v6, "dbg_rsp_cw_v6");
    tb.vcdTrace(dut.dbg_rsp_cc_v6, "dbg_rsp_cc_v6");
    tb.vcdTrace(dut.dbg_req_cw_v7, "dbg_req_cw_v7");
    tb.vcdTrace(dut.dbg_req_cc_v7, "dbg_req_cc_v7");
    tb.vcdTrace(dut.dbg_rsp_cw_v7, "dbg_rsp_cw_v7");
    tb.vcdTrace(dut.dbg_rsp_cc_v7, "dbg_rsp_cc_v7");
  }

  tb.addClock(dut.clk, /*halfPeriodSteps=*/1);
  tb.reset(dut.rst, /*cyclesAsserted=*/2, /*cyclesDeasserted=*/1);

  std::ofstream trace;
  if (trace_log) {
    trace.open(out_dir / "tmu_trace.csv", std::ios::out | std::ios::trunc);
    trace << "cycle,event,node,tag,write,addr_or_word0,data_word0\n";
  }

  std::array<NodePorts, kNodes> nodes = {{
      {&dut.n0_req_valid, &dut.n0_req_write, &dut.n0_req_addr, &dut.n0_req_tag,
       {&dut.n0_req_data_w0, &dut.n0_req_data_w1, &dut.n0_req_data_w2, &dut.n0_req_data_w3, &dut.n0_req_data_w4, &dut.n0_req_data_w5, &dut.n0_req_data_w6, &dut.n0_req_data_w7, &dut.n0_req_data_w8, &dut.n0_req_data_w9, &dut.n0_req_data_w10, &dut.n0_req_data_w11, &dut.n0_req_data_w12, &dut.n0_req_data_w13, &dut.n0_req_data_w14, &dut.n0_req_data_w15, &dut.n0_req_data_w16, &dut.n0_req_data_w17, &dut.n0_req_data_w18, &dut.n0_req_data_w19, &dut.n0_req_data_w20, &dut.n0_req_data_w21, &dut.n0_req_data_w22, &dut.n0_req_data_w23, &dut.n0_req_data_w24, &dut.n0_req_data_w25, &dut.n0_req_data_w26, &dut.n0_req_data_w27, &dut.n0_req_data_w28, &dut.n0_req_data_w29, &dut.n0_req_data_w30, &dut.n0_req_data_w31}, &dut.n0_req_ready, &dut.n0_resp_ready, &dut.n0_resp_valid, &dut.n0_resp_tag,
       {&dut.n0_resp_data_w0, &dut.n0_resp_data_w1, &dut.n0_resp_data_w2, &dut.n0_resp_data_w3, &dut.n0_resp_data_w4, &dut.n0_resp_data_w5, &dut.n0_resp_data_w6, &dut.n0_resp_data_w7, &dut.n0_resp_data_w8, &dut.n0_resp_data_w9, &dut.n0_resp_data_w10, &dut.n0_resp_data_w11, &dut.n0_resp_data_w12, &dut.n0_resp_data_w13, &dut.n0_resp_data_w14, &dut.n0_resp_data_w15, &dut.n0_resp_data_w16, &dut.n0_resp_data_w17, &dut.n0_resp_data_w18, &dut.n0_resp_data_w19, &dut.n0_resp_data_w20, &dut.n0_resp_data_w21, &dut.n0_resp_data_w22, &dut.n0_resp_data_w23, &dut.n0_resp_data_w24, &dut.n0_resp_data_w25, &dut.n0_resp_data_w26, &dut.n0_resp_data_w27, &dut.n0_resp_data_w28, &dut.n0_resp_data_w29, &dut.n0_resp_data_w30, &dut.n0_resp_data_w31}, &dut.n0_resp_is_write},
      {&dut.n1_req_valid, &dut.n1_req_write, &dut.n1_req_addr, &dut.n1_req_tag,
       {&dut.n1_req_data_w0, &dut.n1_req_data_w1, &dut.n1_req_data_w2, &dut.n1_req_data_w3, &dut.n1_req_data_w4, &dut.n1_req_data_w5, &dut.n1_req_data_w6, &dut.n1_req_data_w7, &dut.n1_req_data_w8, &dut.n1_req_data_w9, &dut.n1_req_data_w10, &dut.n1_req_data_w11, &dut.n1_req_data_w12, &dut.n1_req_data_w13, &dut.n1_req_data_w14, &dut.n1_req_data_w15, &dut.n1_req_data_w16, &dut.n1_req_data_w17, &dut.n1_req_data_w18, &dut.n1_req_data_w19, &dut.n1_req_data_w20, &dut.n1_req_data_w21, &dut.n1_req_data_w22, &dut.n1_req_data_w23, &dut.n1_req_data_w24, &dut.n1_req_data_w25, &dut.n1_req_data_w26, &dut.n1_req_data_w27, &dut.n1_req_data_w28, &dut.n1_req_data_w29, &dut.n1_req_data_w30, &dut.n1_req_data_w31}, &dut.n1_req_ready, &dut.n1_resp_ready, &dut.n1_resp_valid, &dut.n1_resp_tag,
       {&dut.n1_resp_data_w0, &dut.n1_resp_data_w1, &dut.n1_resp_data_w2, &dut.n1_resp_data_w3, &dut.n1_resp_data_w4, &dut.n1_resp_data_w5, &dut.n1_resp_data_w6, &dut.n1_resp_data_w7, &dut.n1_resp_data_w8, &dut.n1_resp_data_w9, &dut.n1_resp_data_w10, &dut.n1_resp_data_w11, &dut.n1_resp_data_w12, &dut.n1_resp_data_w13, &dut.n1_resp_data_w14, &dut.n1_resp_data_w15, &dut.n1_resp_data_w16, &dut.n1_resp_data_w17, &dut.n1_resp_data_w18, &dut.n1_resp_data_w19, &dut.n1_resp_data_w20, &dut.n1_resp_data_w21, &dut.n1_resp_data_w22, &dut.n1_resp_data_w23, &dut.n1_resp_data_w24, &dut.n1_resp_data_w25, &dut.n1_resp_data_w26, &dut.n1_resp_data_w27, &dut.n1_resp_data_w28, &dut.n1_resp_data_w29, &dut.n1_resp_data_w30, &dut.n1_resp_data_w31}, &dut.n1_resp_is_write},
      {&dut.n2_req_valid, &dut.n2_req_write, &dut.n2_req_addr, &dut.n2_req_tag,
       {&dut.n2_req_data_w0, &dut.n2_req_data_w1, &dut.n2_req_data_w2, &dut.n2_req_data_w3, &dut.n2_req_data_w4, &dut.n2_req_data_w5, &dut.n2_req_data_w6, &dut.n2_req_data_w7, &dut.n2_req_data_w8, &dut.n2_req_data_w9, &dut.n2_req_data_w10, &dut.n2_req_data_w11, &dut.n2_req_data_w12, &dut.n2_req_data_w13, &dut.n2_req_data_w14, &dut.n2_req_data_w15, &dut.n2_req_data_w16, &dut.n2_req_data_w17, &dut.n2_req_data_w18, &dut.n2_req_data_w19, &dut.n2_req_data_w20, &dut.n2_req_data_w21, &dut.n2_req_data_w22, &dut.n2_req_data_w23, &dut.n2_req_data_w24, &dut.n2_req_data_w25, &dut.n2_req_data_w26, &dut.n2_req_data_w27, &dut.n2_req_data_w28, &dut.n2_req_data_w29, &dut.n2_req_data_w30, &dut.n2_req_data_w31}, &dut.n2_req_ready, &dut.n2_resp_ready, &dut.n2_resp_valid, &dut.n2_resp_tag,
       {&dut.n2_resp_data_w0, &dut.n2_resp_data_w1, &dut.n2_resp_data_w2, &dut.n2_resp_data_w3, &dut.n2_resp_data_w4, &dut.n2_resp_data_w5, &dut.n2_resp_data_w6, &dut.n2_resp_data_w7, &dut.n2_resp_data_w8, &dut.n2_resp_data_w9, &dut.n2_resp_data_w10, &dut.n2_resp_data_w11, &dut.n2_resp_data_w12, &dut.n2_resp_data_w13, &dut.n2_resp_data_w14, &dut.n2_resp_data_w15, &dut.n2_resp_data_w16, &dut.n2_resp_data_w17, &dut.n2_resp_data_w18, &dut.n2_resp_data_w19, &dut.n2_resp_data_w20, &dut.n2_resp_data_w21, &dut.n2_resp_data_w22, &dut.n2_resp_data_w23, &dut.n2_resp_data_w24, &dut.n2_resp_data_w25, &dut.n2_resp_data_w26, &dut.n2_resp_data_w27, &dut.n2_resp_data_w28, &dut.n2_resp_data_w29, &dut.n2_resp_data_w30, &dut.n2_resp_data_w31}, &dut.n2_resp_is_write},
      {&dut.n3_req_valid, &dut.n3_req_write, &dut.n3_req_addr, &dut.n3_req_tag,
       {&dut.n3_req_data_w0, &dut.n3_req_data_w1, &dut.n3_req_data_w2, &dut.n3_req_data_w3, &dut.n3_req_data_w4, &dut.n3_req_data_w5, &dut.n3_req_data_w6, &dut.n3_req_data_w7, &dut.n3_req_data_w8, &dut.n3_req_data_w9, &dut.n3_req_data_w10, &dut.n3_req_data_w11, &dut.n3_req_data_w12, &dut.n3_req_data_w13, &dut.n3_req_data_w14, &dut.n3_req_data_w15, &dut.n3_req_data_w16, &dut.n3_req_data_w17, &dut.n3_req_data_w18, &dut.n3_req_data_w19, &dut.n3_req_data_w20, &dut.n3_req_data_w21, &dut.n3_req_data_w22, &dut.n3_req_data_w23, &dut.n3_req_data_w24, &dut.n3_req_data_w25, &dut.n3_req_data_w26, &dut.n3_req_data_w27, &dut.n3_req_data_w28, &dut.n3_req_data_w29, &dut.n3_req_data_w30, &dut.n3_req_data_w31}, &dut.n3_req_ready, &dut.n3_resp_ready, &dut.n3_resp_valid, &dut.n3_resp_tag,
       {&dut.n3_resp_data_w0, &dut.n3_resp_data_w1, &dut.n3_resp_data_w2, &dut.n3_resp_data_w3, &dut.n3_resp_data_w4, &dut.n3_resp_data_w5, &dut.n3_resp_data_w6, &dut.n3_resp_data_w7, &dut.n3_resp_data_w8, &dut.n3_resp_data_w9, &dut.n3_resp_data_w10, &dut.n3_resp_data_w11, &dut.n3_resp_data_w12, &dut.n3_resp_data_w13, &dut.n3_resp_data_w14, &dut.n3_resp_data_w15, &dut.n3_resp_data_w16, &dut.n3_resp_data_w17, &dut.n3_resp_data_w18, &dut.n3_resp_data_w19, &dut.n3_resp_data_w20, &dut.n3_resp_data_w21, &dut.n3_resp_data_w22, &dut.n3_resp_data_w23, &dut.n3_resp_data_w24, &dut.n3_resp_data_w25, &dut.n3_resp_data_w26, &dut.n3_resp_data_w27, &dut.n3_resp_data_w28, &dut.n3_resp_data_w29, &dut.n3_resp_data_w30, &dut.n3_resp_data_w31}, &dut.n3_resp_is_write},
      {&dut.n4_req_valid, &dut.n4_req_write, &dut.n4_req_addr, &dut.n4_req_tag,
       {&dut.n4_req_data_w0, &dut.n4_req_data_w1, &dut.n4_req_data_w2, &dut.n4_req_data_w3, &dut.n4_req_data_w4, &dut.n4_req_data_w5, &dut.n4_req_data_w6, &dut.n4_req_data_w7, &dut.n4_req_data_w8, &dut.n4_req_data_w9, &dut.n4_req_data_w10, &dut.n4_req_data_w11, &dut.n4_req_data_w12, &dut.n4_req_data_w13, &dut.n4_req_data_w14, &dut.n4_req_data_w15, &dut.n4_req_data_w16, &dut.n4_req_data_w17, &dut.n4_req_data_w18, &dut.n4_req_data_w19, &dut.n4_req_data_w20, &dut.n4_req_data_w21, &dut.n4_req_data_w22, &dut.n4_req_data_w23, &dut.n4_req_data_w24, &dut.n4_req_data_w25, &dut.n4_req_data_w26, &dut.n4_req_data_w27, &dut.n4_req_data_w28, &dut.n4_req_data_w29, &dut.n4_req_data_w30, &dut.n4_req_data_w31}, &dut.n4_req_ready, &dut.n4_resp_ready, &dut.n4_resp_valid, &dut.n4_resp_tag,
       {&dut.n4_resp_data_w0, &dut.n4_resp_data_w1, &dut.n4_resp_data_w2, &dut.n4_resp_data_w3, &dut.n4_resp_data_w4, &dut.n4_resp_data_w5, &dut.n4_resp_data_w6, &dut.n4_resp_data_w7, &dut.n4_resp_data_w8, &dut.n4_resp_data_w9, &dut.n4_resp_data_w10, &dut.n4_resp_data_w11, &dut.n4_resp_data_w12, &dut.n4_resp_data_w13, &dut.n4_resp_data_w14, &dut.n4_resp_data_w15, &dut.n4_resp_data_w16, &dut.n4_resp_data_w17, &dut.n4_resp_data_w18, &dut.n4_resp_data_w19, &dut.n4_resp_data_w20, &dut.n4_resp_data_w21, &dut.n4_resp_data_w22, &dut.n4_resp_data_w23, &dut.n4_resp_data_w24, &dut.n4_resp_data_w25, &dut.n4_resp_data_w26, &dut.n4_resp_data_w27, &dut.n4_resp_data_w28, &dut.n4_resp_data_w29, &dut.n4_resp_data_w30, &dut.n4_resp_data_w31}, &dut.n4_resp_is_write},
      {&dut.n5_req_valid, &dut.n5_req_write, &dut.n5_req_addr, &dut.n5_req_tag,
       {&dut.n5_req_data_w0, &dut.n5_req_data_w1, &dut.n5_req_data_w2, &dut.n5_req_data_w3, &dut.n5_req_data_w4, &dut.n5_req_data_w5, &dut.n5_req_data_w6, &dut.n5_req_data_w7, &dut.n5_req_data_w8, &dut.n5_req_data_w9, &dut.n5_req_data_w10, &dut.n5_req_data_w11, &dut.n5_req_data_w12, &dut.n5_req_data_w13, &dut.n5_req_data_w14, &dut.n5_req_data_w15, &dut.n5_req_data_w16, &dut.n5_req_data_w17, &dut.n5_req_data_w18, &dut.n5_req_data_w19, &dut.n5_req_data_w20, &dut.n5_req_data_w21, &dut.n5_req_data_w22, &dut.n5_req_data_w23, &dut.n5_req_data_w24, &dut.n5_req_data_w25, &dut.n5_req_data_w26, &dut.n5_req_data_w27, &dut.n5_req_data_w28, &dut.n5_req_data_w29, &dut.n5_req_data_w30, &dut.n5_req_data_w31}, &dut.n5_req_ready, &dut.n5_resp_ready, &dut.n5_resp_valid, &dut.n5_resp_tag,
       {&dut.n5_resp_data_w0, &dut.n5_resp_data_w1, &dut.n5_resp_data_w2, &dut.n5_resp_data_w3, &dut.n5_resp_data_w4, &dut.n5_resp_data_w5, &dut.n5_resp_data_w6, &dut.n5_resp_data_w7, &dut.n5_resp_data_w8, &dut.n5_resp_data_w9, &dut.n5_resp_data_w10, &dut.n5_resp_data_w11, &dut.n5_resp_data_w12, &dut.n5_resp_data_w13, &dut.n5_resp_data_w14, &dut.n5_resp_data_w15, &dut.n5_resp_data_w16, &dut.n5_resp_data_w17, &dut.n5_resp_data_w18, &dut.n5_resp_data_w19, &dut.n5_resp_data_w20, &dut.n5_resp_data_w21, &dut.n5_resp_data_w22, &dut.n5_resp_data_w23, &dut.n5_resp_data_w24, &dut.n5_resp_data_w25, &dut.n5_resp_data_w26, &dut.n5_resp_data_w27, &dut.n5_resp_data_w28, &dut.n5_resp_data_w29, &dut.n5_resp_data_w30, &dut.n5_resp_data_w31}, &dut.n5_resp_is_write},
      {&dut.n6_req_valid, &dut.n6_req_write, &dut.n6_req_addr, &dut.n6_req_tag,
       {&dut.n6_req_data_w0, &dut.n6_req_data_w1, &dut.n6_req_data_w2, &dut.n6_req_data_w3, &dut.n6_req_data_w4, &dut.n6_req_data_w5, &dut.n6_req_data_w6, &dut.n6_req_data_w7, &dut.n6_req_data_w8, &dut.n6_req_data_w9, &dut.n6_req_data_w10, &dut.n6_req_data_w11, &dut.n6_req_data_w12, &dut.n6_req_data_w13, &dut.n6_req_data_w14, &dut.n6_req_data_w15, &dut.n6_req_data_w16, &dut.n6_req_data_w17, &dut.n6_req_data_w18, &dut.n6_req_data_w19, &dut.n6_req_data_w20, &dut.n6_req_data_w21, &dut.n6_req_data_w22, &dut.n6_req_data_w23, &dut.n6_req_data_w24, &dut.n6_req_data_w25, &dut.n6_req_data_w26, &dut.n6_req_data_w27, &dut.n6_req_data_w28, &dut.n6_req_data_w29, &dut.n6_req_data_w30, &dut.n6_req_data_w31}, &dut.n6_req_ready, &dut.n6_resp_ready, &dut.n6_resp_valid, &dut.n6_resp_tag,
       {&dut.n6_resp_data_w0, &dut.n6_resp_data_w1, &dut.n6_resp_data_w2, &dut.n6_resp_data_w3, &dut.n6_resp_data_w4, &dut.n6_resp_data_w5, &dut.n6_resp_data_w6, &dut.n6_resp_data_w7, &dut.n6_resp_data_w8, &dut.n6_resp_data_w9, &dut.n6_resp_data_w10, &dut.n6_resp_data_w11, &dut.n6_resp_data_w12, &dut.n6_resp_data_w13, &dut.n6_resp_data_w14, &dut.n6_resp_data_w15, &dut.n6_resp_data_w16, &dut.n6_resp_data_w17, &dut.n6_resp_data_w18, &dut.n6_resp_data_w19, &dut.n6_resp_data_w20, &dut.n6_resp_data_w21, &dut.n6_resp_data_w22, &dut.n6_resp_data_w23, &dut.n6_resp_data_w24, &dut.n6_resp_data_w25, &dut.n6_resp_data_w26, &dut.n6_resp_data_w27, &dut.n6_resp_data_w28, &dut.n6_resp_data_w29, &dut.n6_resp_data_w30, &dut.n6_resp_data_w31}, &dut.n6_resp_is_write},
      {&dut.n7_req_valid, &dut.n7_req_write, &dut.n7_req_addr, &dut.n7_req_tag,
       {&dut.n7_req_data_w0, &dut.n7_req_data_w1, &dut.n7_req_data_w2, &dut.n7_req_data_w3, &dut.n7_req_data_w4, &dut.n7_req_data_w5, &dut.n7_req_data_w6, &dut.n7_req_data_w7, &dut.n7_req_data_w8, &dut.n7_req_data_w9, &dut.n7_req_data_w10, &dut.n7_req_data_w11, &dut.n7_req_data_w12, &dut.n7_req_data_w13, &dut.n7_req_data_w14, &dut.n7_req_data_w15, &dut.n7_req_data_w16, &dut.n7_req_data_w17, &dut.n7_req_data_w18, &dut.n7_req_data_w19, &dut.n7_req_data_w20, &dut.n7_req_data_w21, &dut.n7_req_data_w22, &dut.n7_req_data_w23, &dut.n7_req_data_w24, &dut.n7_req_data_w25, &dut.n7_req_data_w26, &dut.n7_req_data_w27, &dut.n7_req_data_w28, &dut.n7_req_data_w29, &dut.n7_req_data_w30, &dut.n7_req_data_w31}, &dut.n7_req_ready, &dut.n7_resp_ready, &dut.n7_resp_valid, &dut.n7_resp_tag,
       {&dut.n7_resp_data_w0, &dut.n7_resp_data_w1, &dut.n7_resp_data_w2, &dut.n7_resp_data_w3, &dut.n7_resp_data_w4, &dut.n7_resp_data_w5, &dut.n7_resp_data_w6, &dut.n7_resp_data_w7, &dut.n7_resp_data_w8, &dut.n7_resp_data_w9, &dut.n7_resp_data_w10, &dut.n7_resp_data_w11, &dut.n7_resp_data_w12, &dut.n7_resp_data_w13, &dut.n7_resp_data_w14, &dut.n7_resp_data_w15, &dut.n7_resp_data_w16, &dut.n7_resp_data_w17, &dut.n7_resp_data_w18, &dut.n7_resp_data_w19, &dut.n7_resp_data_w20, &dut.n7_resp_data_w21, &dut.n7_resp_data_w22, &dut.n7_resp_data_w23, &dut.n7_resp_data_w24, &dut.n7_resp_data_w25, &dut.n7_resp_data_w26, &dut.n7_resp_data_w27, &dut.n7_resp_data_w28, &dut.n7_resp_data_w29, &dut.n7_resp_data_w30, &dut.n7_resp_data_w31}, &dut.n7_resp_is_write},
  }};

  std::map<std::uint32_t, DataLineU64> mem;

  for (auto &n : nodes) {
    zeroReq(n);
    setRespReady(n, false);
  }
  tb.reset(dut.rst, /*cyclesAsserted=*/2, /*cyclesDeasserted=*/1);
  std::uint64_t sim_cycle = 0;
  runSimultaneousCase(tb, dut, nodes, sim_cycle, mem, trace);

  int case_count = kDefaultCases;
  if (const char *cases_env = std::getenv("PYC_TMU_CASES")) {
    const int v = std::atoi(cases_env);
    if (v > 0)
      case_count = v;
  }
  int rounds_per_case = kDefaultRoundsPerCase;
  if (const char *rounds_env = std::getenv("PYC_TMU_ROUNDS")) {
    const int v = std::atoi(rounds_env);
    if (v > 0)
      rounds_per_case = v;
  }
  const int reqs_per_node = rounds_per_case * kNodes;

  for (int case_id = 0; case_id < case_count; case_id++) {
    for (auto &n : nodes) {
      zeroReq(n);
      setRespReady(n, false);
    }
    tb.reset(dut.rst, /*cyclesAsserted=*/2, /*cyclesDeasserted=*/1);
    std::uint64_t cycle = 0;
    runCongestedCase(tb, dut, nodes, case_id, reqs_per_node, cycle, mem, trace);
  }

  std::cout << "PASS: TMU tests\n";
  return 0;
}
