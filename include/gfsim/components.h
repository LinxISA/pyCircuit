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

  void setInput(uint64_t value) {
    inputProposal_ = value;
    hasInput_ = true;
  }

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

  void receive(uint64_t value) { receivedProposals_.push_back(value); }

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

  void forward(uint64_t value) {
    forwardedProposal_ = value;
    hasProposal_ = true;
  }

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
    if (addr >= storage_.size())
      return false;
    writeProposals_[addr] = value;
    return true;
  }

  uint64_t read(size_t addr) const {
    if (addr >= storage_.size())
      return 0;
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

// ── ReadyValid ────────────────────────────────────────────────────────

template <typename T> class ReadyValid : public SimObject {
public:
  ReadyValid(std::string name, ObjectId id, SimObject *parent)
      : SimObject(ObjectKind::Link, std::move(name), id, parent) {}

  void setValid(bool v) { validProposal_ = v; }
  void setData(T data) { dataProposal_ = data; }
  bool isReady() const { return ready_; }
  void setReady(bool r) { readyProposal_ = r; }
  bool isValid() const { return valid_; }
  T data() const { return data_; }

  void doXfer(Epoch) override {
    valid_ = validProposal_;
    ready_ = readyProposal_;
    data_ = dataProposal_;
    if (valid_ && ready_) {
      lastTransferred_ = data_;
      ++transferCount_;
    }
  }

  T lastTransferred() const { return lastTransferred_; }
  uint64_t transferCount() const { return transferCount_; }
  bool isRunnable(Epoch) const override { return valid_ && !ready_; }

  void reset() override {
    valid_ = false;
    ready_ = false;
    validProposal_ = false;
    readyProposal_ = false;
    transferCount_ = 0;
  }

private:
  bool valid_ = false, ready_ = false;
  bool validProposal_ = false, readyProposal_ = false;
  T data_{}, dataProposal_{}, lastTransferred_{};
  uint64_t transferCount_ = 0;
};

// ── RequestResponse ──────────────────────────────────────────────────

template <typename Req, typename Resp>
class RequestResponse : public SimObject {
public:
  RequestResponse(std::string name, ObjectId id, SimObject *parent,
                  size_t maxInFlight = 16)
      : SimObject(ObjectKind::Link, std::move(name), id, parent),
        maxInFlight_(maxInFlight) {}

  bool sendRequest(Req req, uint64_t id) {
    if (inFlight_ >= maxInFlight_)
      return false;
    reqProposals_.push_back({std::move(req), id});
    return true;
  }

  const Req *peekRequest() const {
    return committedReqs_.empty() ? nullptr : &committedReqs_.front().req;
  }

  void sendResponse(Resp resp) { respProposals_.push_back(std::move(resp)); }
  bool hasResponse() const { return !committedResps_.empty(); }

  Resp popResponse() {
    Resp r = std::move(committedResps_.front());
    committedResps_.erase(committedResps_.begin());
    return r;
  }

  void doXfer(Epoch) override {
    for (auto &p : reqProposals_) {
      committedReqs_.push_back(std::move(p));
      ++inFlight_;
    }
    reqProposals_.clear();
    for (auto &r : respProposals_) {
      committedResps_.push_back(std::move(r));
      if (inFlight_ > 0)
        --inFlight_;
      ++totalCompleted_;
    }
    respProposals_.clear();
  }

  size_t inFlight() const { return inFlight_; }
  size_t maxInFlight() const { return maxInFlight_; }
  uint64_t totalCompleted() const { return totalCompleted_; }

  void reset() override {
    reqProposals_.clear();
    respProposals_.clear();
    committedReqs_.clear();
    committedResps_.clear();
    inFlight_ = 0;
    totalCompleted_ = 0;
  }

private:
  struct ReqEntry {
    Req req;
    uint64_t correlationId;
  };
  size_t maxInFlight_, inFlight_ = 0;
  uint64_t totalCompleted_ = 0;
  std::vector<ReqEntry> reqProposals_, committedReqs_;
  std::vector<Resp> respProposals_, committedResps_;
};

// ── PacketTraits ─────────────────────────────────────────────────────

template <typename T> struct PacketTraits {
  static constexpr const char *schema = nullptr;
  static constexpr size_t serializedSize = 0;
  static constexpr size_t alignment = alignof(T);
  static constexpr bool littleEndian = true;
};

template <typename T>
concept Packet = requires {
  { PacketTraits<T>::schema } -> std::convertible_to<const char *>;
  { PacketTraits<T>::serializedSize } -> std::convertible_to<size_t>;
};

// ── Protocol state ────────────────────────────────────────────────────

enum class ProtocolPhase : uint8_t {
  Idle,
  Request,
  Response,
  Transfer,
  Backpressure,
};

class ProtocolState {
public:
  explicit ProtocolState(size_t maxCredits = 1)
      : maxCredits_(maxCredits), credits_(maxCredits) {}
  ProtocolPhase phase() const { return phase_; }
  size_t credits() const { return credits_; }
  size_t inFlight() const { return inFlight_; }
  bool canSend() const { return credits_ > 0 && inFlight_ < maxCredits_; }
  bool canReceive() const { return inFlight_ > 0; }
  void startRequest() {
    if (canSend()) {
      --credits_;
      ++inFlight_;
      phase_ = ProtocolPhase::Request;
    }
  }
  void completeRequest() {
    if (inFlight_ > 0) {
      --inFlight_;
      ++credits_;
    }
    phase_ = inFlight_ > 0 ? ProtocolPhase::Transfer : ProtocolPhase::Idle;
  }
  void setBackpressure(bool bp) {
    phase_ = bp ? ProtocolPhase::Backpressure : ProtocolPhase::Idle;
  }
  void reset() {
    phase_ = ProtocolPhase::Idle;
    credits_ = maxCredits_;
    inFlight_ = 0;
  }

private:
  ProtocolPhase phase_ = ProtocolPhase::Idle;
  size_t maxCredits_, credits_;
  size_t inFlight_ = 0;
};

// ── No-progress diagnostics ──────────────────────────────────────────

struct BlockedObject {
  ObjectId id = 0;
  std::string path;
  std::string reason;
};

struct NoProgressReport {
  std::vector<BlockedObject> blockedObjects;
  size_t queueOccupancy = 0;
  size_t pendingOffers = 0;
  size_t activeReservations = 0;
  std::optional<Event> nextEvent;
  uint64_t tracePosition = 0;
  std::string summary;
  bool empty() const { return blockedObjects.empty() && !nextEvent; }
};

} // namespace gfsim

#endif // GFSIM_COMPONENTS_H
