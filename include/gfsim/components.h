#ifndef GFSIM_COMPONENTS_H
#define GFSIM_COMPONENTS_H

#include "gfsim/core.h"
#include "gfsim/object.h"
#include "gfsim/queue.h"
#include "gfsim/resource.h"

#include <concepts>
#include <functional>
#include <map>
#include <string>

namespace gfsim {

// ── Baseline component concepts ───────────────────────────────────────

/// A component that can be registered in the SimSystem.
template <typename T>
concept Component = std::derived_from<T, SimObject> && requires(T &t, Epoch e) {
  { t.doWork(e) } -> std::same_as<void>;
  { t.doXfer(e) } -> std::same_as<void>;
};

// ── TraceSource ───────────────────────────────────────────────────────

/// Exactly one TraceSource owns the trace cursor.
/// It produces decoded trace records and advances only on committed Xfer.
class TraceSource : public SimObject {
public:
  TraceSource(std::string name, ObjectId id, SimObject *parent)
      : SimObject(ObjectKind::TraceSource, std::move(name), id, parent) {}

  /// Offer the current trace record (if any) to a downstream consumer.
  /// Returns true if a record is available.
  virtual bool hasRecord() const { return false; }

  /// Peek at the current trace record without consuming it.
  virtual uint64_t peekRecord() const { return 0; }

  /// Consume the current record (called at Xfer after commit).
  virtual void advance() { ++cursor_; }

  uint64_t cursor() const { return cursor_; }

  bool isRunnable(Epoch) const override { return hasRecord(); }

  void reset() override { cursor_ = 0; }

private:
  uint64_t cursor_ = 0;
};

// ── Compute ───────────────────────────────────────────────────────────

/// A stateless compute component that transforms inputs to outputs.
/// Pure, zero-delay, effect-free.
class Compute : public SimObject {
public:
  Compute(std::string name, ObjectId id, SimObject *parent)
      : SimObject(ObjectKind::Compute, std::move(name), id, parent) {}

  using ComputeFn = std::function<uint64_t(uint64_t)>;

  void setFunction(ComputeFn fn) { fn_ = std::move(fn); }

  void setInput(uint64_t value) { inputProposal_ = value; hasInput_ = true; }

  void doWork(Epoch) override {
    if (hasInput_ && fn_) {
      outputProposal_ = fn_(inputProposal_);
      hasOutput_ = true;
    }
  }

  void doXfer(Epoch) override {
    if (hasOutput_) {
      output_ = outputProposal_;
      hasOutput_ = false;
    }
    hasInput_ = false;
  }

  uint64_t output() const { return output_; }
  bool isRunnable(Epoch) const override { return hasInput_; }

  void reset() override {
    output_ = 0;
    outputProposal_ = 0;
    inputProposal_ = 0;
    hasInput_ = false;
    hasOutput_ = false;
  }

private:
  ComputeFn fn_;
  uint64_t output_ = 0;
  uint64_t outputProposal_ = 0;
  uint64_t inputProposal_ = 0;
  bool hasInput_ = false;
  bool hasOutput_ = false;
};

// ── Sink ──────────────────────────────────────────────────────────────

/// A terminal component that consumes data and produces statistics.
class Sink : public SimObject {
public:
  Sink(std::string name, ObjectId id, SimObject *parent)
      : SimObject(ObjectKind::Sink, std::move(name), id, parent) {}

  void receive(uint64_t value) {
    receivedProposals_.push_back(value);
  }

  void doXfer(Epoch) override {
    for (auto v : receivedProposals_) {
      received_.push_back(v);
      ++totalReceived_;
    }
    receivedProposals_.clear();
  }

  const std::vector<uint64_t> &received() const { return received_; }
  uint64_t totalReceived() const { return totalReceived_; }

  bool isRunnable(Epoch) const override { return false; }

  void reset() override {
    received_.clear();
    receivedProposals_.clear();
    totalReceived_ = 0;
  }

private:
  std::vector<uint64_t> received_;
  std::vector<uint64_t> receivedProposals_;
  uint64_t totalReceived_ = 0;
};

// ── Link ──────────────────────────────────────────────────────────────

/// A connector that forwards data between components.
class Link : public SimObject {
public:
  Link(std::string name, ObjectId id, SimObject *parent)
      : SimObject(ObjectKind::Link, std::move(name), id, parent) {}

  void forward(uint64_t value) { forwardedProposal_ = value; hasProposal_ = true; }

  void doXfer(Epoch) override {
    if (hasProposal_) {
      forwarded_ = forwardedProposal_;
      hasProposal_ = false;
    }
  }

  uint64_t value() const { return forwarded_; }
  bool hasValue() const { return !hasProposal_ && forwarded_ != 0; }

  bool isRunnable(Epoch) const override { return hasProposal_; }

  void reset() override {
    forwarded_ = 0;
    forwardedProposal_ = 0;
    hasProposal_ = false;
  }

private:
  uint64_t forwarded_ = 0;
  uint64_t forwardedProposal_ = 0;
  bool hasProposal_ = false;
};

// ── Memory ────────────────────────────────────────────────────────────

/// A storage component with read/write proposals and capacity.
class Memory : public SimObject {
public:
  Memory(std::string name, ObjectId id, SimObject *parent, size_t capacity)
      : SimObject(ObjectKind::Memory, std::move(name), id, parent),
        storage_(capacity, 0) {}

  size_t capacity() const { return storage_.size(); }

  bool proposeWrite(size_t addr, uint64_t value) {
    if (addr >= storage_.size()) return false;
    writeProposals_[addr] = value;
    return true;
  }

  uint64_t read(size_t addr) const {
    if (addr >= storage_.size()) return 0;
    return storage_[addr];
  }

  void doXfer(Epoch) override {
    for (auto &[addr, value] : writeProposals_)
      storage_[addr] = value;
    writeProposals_.clear();
  }

  void reset() override {
    std::fill(storage_.begin(), storage_.end(), 0);
    writeProposals_.clear();
  }

private:
  std::vector<uint64_t> storage_;
  std::map<size_t, uint64_t> writeProposals_;
};

} // namespace gfsim

#endif // GFSIM_COMPONENTS_H
