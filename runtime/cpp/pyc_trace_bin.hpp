#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "pyc_probe_registry.hpp"

namespace pyc::cpp {

// Decision 0016: Trace output is primarily a binary event stream.
// Decision 0037/0038: Support self-describing traces (ProbeDeclare records).
//
// File format: pyc4 trace binary v1 (little-endian on disk)
// - Header:
//   - magic[8] = "PYC4TRC1"
//   - u32 version = 1
//   - u32 flags (bit0: little_endian=1)
//   - u32 probe_count
//   - repeated probe declarations:
//       - u64 probe_id
//       - u8  kind (ProbeKind enum value)
//       - u32 width_bits
//       - u32 canonical_path_len_bytes
//       - bytes[canonical_path_len_bytes] (utf-8, no NUL)
//       - u16 type_sig_len_bytes
//       - bytes[type_sig_len_bytes]
// - Records:
//   - CycleRecord:
//       - u8  record_type = 1
//       - u64 cycle
//       - u32 event_count
//       - repeated ValueChange events:
//           - u64 probe_id
//           - u32 word_count (64-bit words, little-endian)
//           - repeated u64 words[word_count] (low word first)
//
// For now, type_sig supports only Bits:
//   - tag u8 = 0
//   - width_bits u32
//   - signed u8 (0 = unsigned)
class PycTraceBinWriter {
public:
  PycTraceBinWriter() = default;
  PycTraceBinWriter(const PycTraceBinWriter &) = delete;
  PycTraceBinWriter &operator=(const PycTraceBinWriter &) = delete;
  PycTraceBinWriter(PycTraceBinWriter &&) = default;
  PycTraceBinWriter &operator=(PycTraceBinWriter &&) = default;

  ~PycTraceBinWriter() { close(); }

  bool isOpen() const { return out_.is_open(); }

  bool open(const std::filesystem::path &path, std::vector<const ProbeRegistry::Entry *> probes) {
    close();
    out_.open(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!out_.is_open())
      return false;

    probes_.clear();
    probes_.reserve(probes.size());
    for (const auto *e : probes) {
      if (!e)
        continue;
      Traced t;
      t.probe_id = e->probe_id;
      t.kind = e->kind;
      t.width_bits = e->width_bits;
      t.path = e->path;
      t.ptr = e->ptr;
      t.word_count = wordsForWidth(t.width_bits);
      t.last_words.resize(t.word_count, 0);
      t.has_last = false;
      probes_.push_back(std::move(t));
    }

    writeHeader();
    out_.flush();
    return out_.good();
  }

  void close() {
    if (out_.is_open()) {
      out_.flush();
      out_.close();
    }
    probes_.clear();
  }

  void writeCycle(std::uint64_t cycle) {
    if (!out_.is_open())
      return;

    struct Ev {
      std::uint64_t probe_id = 0;
      std::uint32_t word_count = 0;
      std::vector<std::uint64_t> words{};
    };
    std::vector<Ev> evs;
    evs.reserve(probes_.size());

    for (auto &t : probes_) {
      if (t.kind != ProbeKind::Wire)
        continue;
      if (!t.ptr || t.word_count == 0)
        continue;

      std::vector<std::uint64_t> cur(t.word_count, 0);
      std::memcpy(cur.data(), t.ptr, static_cast<std::size_t>(t.word_count) * sizeof(std::uint64_t));

      const bool changed = (!t.has_last) || (cur != t.last_words);
      if (!changed)
        continue;

      t.last_words = cur;
      t.has_last = true;
      evs.push_back(Ev{t.probe_id, t.word_count, std::move(cur)});
    }

    // CycleRecord header.
    writeU8(1);
    writeU64LE(cycle);
    writeU32LE(static_cast<std::uint32_t>(evs.size()));
    for (const auto &ev : evs) {
      writeU64LE(ev.probe_id);
      writeU32LE(ev.word_count);
      for (std::uint32_t i = 0; i < ev.word_count; ++i)
        writeU64LE(ev.words[i]);
    }
  }

private:
  struct Traced {
    std::string path{};
    std::uint64_t probe_id = 0;
    ProbeKind kind = ProbeKind::Wire;
    std::uint32_t width_bits = 0;
    void *ptr = nullptr;
    std::uint32_t word_count = 0;
    std::vector<std::uint64_t> last_words{};
    bool has_last = false;
  };

  static std::uint32_t wordsForWidth(std::uint32_t width_bits) {
    if (width_bits == 0)
      return 0;
    return (width_bits + 63u) / 64u;
  }

  void writeHeader() {
    static constexpr char kMagic[8] = {'P', 'Y', 'C', '4', 'T', 'R', 'C', '1'};
    out_.write(kMagic, sizeof(kMagic));
    writeU32LE(1);          // version
    writeU32LE(1u << 0u);   // flags: little-endian
    writeU32LE(static_cast<std::uint32_t>(probes_.size()));

    for (const auto &t : probes_) {
      writeU64LE(t.probe_id);
      writeU8(static_cast<std::uint8_t>(t.kind));
      writeU32LE(t.width_bits);

      const std::string &path = t.path;
      writeU32LE(static_cast<std::uint32_t>(path.size()));
      if (!path.empty())
        out_.write(path.data(), static_cast<std::streamsize>(path.size()));

      // Minimal type_sig for flat wires: Bits(width, unsigned).
      std::uint8_t type_sig[6];
      type_sig[0] = 0; // Bits
      const std::uint32_t w = t.width_bits;
      type_sig[1] = static_cast<std::uint8_t>((w >> 0) & 0xffu);
      type_sig[2] = static_cast<std::uint8_t>((w >> 8) & 0xffu);
      type_sig[3] = static_cast<std::uint8_t>((w >> 16) & 0xffu);
      type_sig[4] = static_cast<std::uint8_t>((w >> 24) & 0xffu);
      type_sig[5] = 0; // signed=0
      writeU16LE(static_cast<std::uint16_t>(sizeof(type_sig)));
      out_.write(reinterpret_cast<const char *>(type_sig), static_cast<std::streamsize>(sizeof(type_sig)));
    }
  }

  void writeU8(std::uint8_t v) { out_.put(static_cast<char>(v)); }

  void writeU16LE(std::uint16_t v) {
    std::uint8_t b[2];
    b[0] = static_cast<std::uint8_t>((v >> 0) & 0xffu);
    b[1] = static_cast<std::uint8_t>((v >> 8) & 0xffu);
    out_.write(reinterpret_cast<const char *>(b), sizeof(b));
  }

  void writeU32LE(std::uint32_t v) {
    std::uint8_t b[4];
    b[0] = static_cast<std::uint8_t>((v >> 0) & 0xffu);
    b[1] = static_cast<std::uint8_t>((v >> 8) & 0xffu);
    b[2] = static_cast<std::uint8_t>((v >> 16) & 0xffu);
    b[3] = static_cast<std::uint8_t>((v >> 24) & 0xffu);
    out_.write(reinterpret_cast<const char *>(b), sizeof(b));
  }

  void writeU64LE(std::uint64_t v) {
    std::uint8_t b[8];
    b[0] = static_cast<std::uint8_t>((v >> 0) & 0xffull);
    b[1] = static_cast<std::uint8_t>((v >> 8) & 0xffull);
    b[2] = static_cast<std::uint8_t>((v >> 16) & 0xffull);
    b[3] = static_cast<std::uint8_t>((v >> 24) & 0xffull);
    b[4] = static_cast<std::uint8_t>((v >> 32) & 0xffull);
    b[5] = static_cast<std::uint8_t>((v >> 40) & 0xffull);
    b[6] = static_cast<std::uint8_t>((v >> 48) & 0xffull);
    b[7] = static_cast<std::uint8_t>((v >> 56) & 0xffull);
    out_.write(reinterpret_cast<const char *>(b), sizeof(b));
  }

  std::ofstream out_{};
  std::vector<Traced> probes_{};
};

} // namespace pyc::cpp
