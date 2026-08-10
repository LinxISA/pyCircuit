#ifndef GFSIM_COMPONENTS_H
#define GFSIM_COMPONENTS_H

#include "gfsim/core.h"
#include "gfsim/object.h"
#include "gfsim/queue.h"
#include "gfsim/resource.h"

#include <algorithm>
#include <concepts>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace gfsim {

// ── Baseline component concepts ───────────────────────────────────────

/// A component that can be registered in the SimSystem.
template <typename T>
concept Component = std::derived_from<T, SimObject> && requires(T &t, Epoch e) {
  { T::contractName } -> std::convertible_to<std::string_view>;
  { T::componentKind } -> std::convertible_to<ObjectKind>;
  { t.doWork(e) } -> std::same_as<void>;
  { t.doXfer(e) } -> std::same_as<void>;
  { std::as_const(t).hasPendingCommit() } -> std::same_as<bool>;
};

// ── TraceSource ───────────────────────────────────────────────────────

/// Exactly one TraceSource owns the trace cursor.
/// It produces decoded trace records and advances only on committed Xfer.
template <typename Record = uint64_t> class TraceSource : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.std.TraceSource";
  static constexpr ObjectKind componentKind = ObjectKind::TraceSource;

  TraceSource(std::string name, ObjectId id, SimObject *parent)
      : SimObject(ObjectKind::TraceSource, std::move(name), id, parent) {}

  /// Offer the current trace record (if any) to a downstream consumer.
  /// Returns true if a record is available.
  virtual bool hasRecord() const { return false; }

  /// Peek at the current trace record without consuming it.
  virtual Record peekRecord() const { return {}; }

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
template <typename T> struct IdentityComputePolicy {
  T operator()(const T &input) const { return input; }
};

template <typename Input = uint64_t, typename Output = Input,
          typename FunctionalPolicy = IdentityComputePolicy<Input>>
  requires std::invocable<const FunctionalPolicy &, const Input &> &&
           std::convertible_to<
               std::invoke_result_t<const FunctionalPolicy &, const Input &>,
               Output>
class Compute : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.std.Compute";
  static constexpr ObjectKind componentKind = ObjectKind::Compute;

  Compute(std::string name, ObjectId id, SimObject *parent,
          FunctionalPolicy policy = {})
      : SimObject(ObjectKind::Compute, std::move(name), id, parent),
        policy_(std::move(policy)) {}

  void setInput(Input value) {
    inputProposal_ = std::move(value);
    hasInput_ = true;
  }

  void doWork(Epoch) override {
    if (hasInput_) {
      outputProposal_ = std::invoke(std::as_const(policy_), inputProposal_);
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

  bool hasPendingCommit() const override { return hasOutput_; }

  const Output &output() const { return output_; }
  bool isRunnable(Epoch) const override { return hasInput_; }

  void reset() override {
    output_ = {};
    outputProposal_ = {};
    inputProposal_ = {};
    hasInput_ = false;
    hasOutput_ = false;
  }

private:
  [[no_unique_address]] FunctionalPolicy policy_;
  Output output_{};
  Output outputProposal_{};
  Input inputProposal_{};
  bool hasInput_ = false;
  bool hasOutput_ = false;
};

// ── Sink ──────────────────────────────────────────────────────────────

/// A terminal component that consumes data and produces statistics.
template <typename T = uint64_t> class Sink : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.std.Sink";
  static constexpr ObjectKind componentKind = ObjectKind::Sink;

  Sink(std::string name, ObjectId id, SimObject *parent)
      : SimObject(ObjectKind::Sink, std::move(name), id, parent) {}

  void receive(T value) { receivedProposals_.push_back(std::move(value)); }

  void doXfer(Epoch) override {
    for (auto v : receivedProposals_) {
      received_.push_back(v);
      ++totalReceived_;
    }
    receivedProposals_.clear();
  }

  bool hasPendingCommit() const override { return !receivedProposals_.empty(); }

  const std::vector<T> &received() const { return received_; }
  uint64_t totalReceived() const { return totalReceived_; }

  bool isRunnable(Epoch) const override { return false; }

  void reset() override {
    received_.clear();
    receivedProposals_.clear();
    totalReceived_ = 0;
  }

private:
  std::vector<T> received_;
  std::vector<T> receivedProposals_;
  uint64_t totalReceived_ = 0;
};

// ── Link ──────────────────────────────────────────────────────────────

/// A connector that forwards data between components.
template <typename T = uint64_t> class Link : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.std.Link";
  static constexpr ObjectKind componentKind = ObjectKind::Link;

  Link(std::string name, ObjectId id, SimObject *parent)
      : SimObject(ObjectKind::Link, std::move(name), id, parent) {}

  void forward(T value) {
    forwardedProposal_ = std::move(value);
    hasProposal_ = true;
  }

  void doXfer(Epoch) override {
    if (hasProposal_) {
      forwarded_ = forwardedProposal_;
      hasForwarded_ = true;
      hasProposal_ = false;
    }
  }

  bool hasPendingCommit() const override { return hasProposal_; }

  const T &value() const { return forwarded_; }
  bool hasValue() const { return hasForwarded_; }

  bool isRunnable(Epoch) const override { return hasProposal_; }

  void reset() override {
    forwarded_ = {};
    forwardedProposal_ = {};
    hasProposal_ = false;
    hasForwarded_ = false;
  }

private:
  T forwarded_{};
  T forwardedProposal_{};
  bool hasProposal_ = false;
  bool hasForwarded_ = false;
};

// ── Memory ────────────────────────────────────────────────────────────

/// A storage component with read/write proposals and capacity.
template <typename T = uint64_t> class Memory : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.std.Memory";
  static constexpr ObjectKind componentKind = ObjectKind::Memory;

  Memory(std::string name, ObjectId id, SimObject *parent, size_t capacity)
      : SimObject(ObjectKind::Memory, std::move(name), id, parent),
        storage_(capacity) {}

  size_t capacity() const { return storage_.size(); }

  bool proposeWrite(size_t addr, T value) {
    if (addr >= storage_.size())
      return false;
    writeProposals_[addr] = std::move(value);
    return true;
  }

  T read(size_t addr) const {
    if (addr >= storage_.size())
      return {};
    return storage_[addr];
  }

  void doXfer(Epoch) override {
    for (auto &[addr, value] : writeProposals_)
      storage_[addr] = value;
    writeProposals_.clear();
  }

  bool hasPendingCommit() const override { return !writeProposals_.empty(); }

  void reset() override {
    std::fill(storage_.begin(), storage_.end(), T{});
    writeProposals_.clear();
  }

private:
  std::vector<T> storage_;
  std::map<size_t, T> writeProposals_;
};

// ── Scheduler ────────────────────────────────────────────────────────

/// A finite deterministic scheduler. Proposal admission checks identity;
/// arbitration selects by stable keys and never by insertion order.
template <typename T> class Scheduler final : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.std.Scheduler";
  static constexpr ObjectKind componentKind = ObjectKind::Scheduler;

  Scheduler(std::string name, ObjectId id, SimObject *parent, size_t capacity)
      : SimObject(ObjectKind::Scheduler, std::move(name), id, parent),
        capacity_(capacity) {}

  bool proposeSchedule(T value, uint32_t priority, uint32_t portIndex,
                       uint32_t instanceIndex, ObjectId ownerId,
                       uint64_t transactionId) {
    if (ownerId == kInvalidObjectId || containsIdentity(ownerId, transactionId))
      return false;
    proposals_.push_back({std::move(value),
                          priority,
                          {},
                          portIndex,
                          instanceIndex,
                          ownerId,
                          transactionId});
    return true;
  }

  std::optional<T> proposePop() {
    if (popProposalCount_ >= committed_.size())
      return std::nullopt;
    return committed_[popProposalCount_++].value;
  }

  const T *peek() const {
    return committed_.empty() ? nullptr : &committed_.front().value;
  }

  void doArbitrate(Epoch epoch) override {
    for (Entry &entry : proposals_)
      entry.issueEpoch = epoch;
    std::sort(proposals_.begin(), proposals_.end(), stableLess);
    size_t occupied = committed_.size() + accepted_.size();
    size_t available = capacity_ > occupied ? capacity_ - occupied : 0;
    size_t acceptedCount = std::min(available, proposals_.size());
    accepted_.insert(
        accepted_.end(), std::make_move_iterator(proposals_.begin()),
        std::make_move_iterator(proposals_.begin() + acceptedCount));
    rejected_.insert(
        rejected_.end(),
        std::make_move_iterator(proposals_.begin() + acceptedCount),
        std::make_move_iterator(proposals_.end()));
    proposals_.clear();
  }

  void doXfer(Epoch) override {
    size_t popped = std::min(popProposalCount_, committed_.size());
    committed_.erase(committed_.begin(), committed_.begin() + popped);
    totalPops_ += popped;
    popProposalCount_ = 0;

    totalScheduled_ += accepted_.size();
    committed_.insert(committed_.end(),
                      std::make_move_iterator(accepted_.begin()),
                      std::make_move_iterator(accepted_.end()));
    accepted_.clear();
    std::sort(committed_.begin(), committed_.end(), stableLess);

    for (const Entry &entry : rejected_)
      rejectedTransactions_.push_back(entry.transactionId);
    rejected_.clear();
    highWatermark_ = std::max(highWatermark_, committed_.size());
  }

  bool hasPendingCommit() const override {
    return !proposals_.empty() || !accepted_.empty() || !rejected_.empty() ||
           popProposalCount_ != 0;
  }

  bool isRunnable(Epoch) const override { return !proposals_.empty(); }
  size_t capacity() const { return capacity_; }
  size_t size() const { return committed_.size(); }
  bool empty() const { return committed_.empty(); }
  size_t highWatermark() const { return highWatermark_; }
  uint64_t totalScheduled() const { return totalScheduled_; }
  uint64_t totalPops() const { return totalPops_; }
  const std::vector<uint64_t> &rejectedTransactions() const {
    return rejectedTransactions_;
  }

  bool validate() const {
    if (committed_.size() > capacity_)
      return false;
    for (size_t left = 0; left < committed_.size(); ++left)
      for (size_t right = left + 1; right < committed_.size(); ++right)
        if (sameIdentity(committed_[left], committed_[right]))
          return false;
    return true;
  }

  void reset() override {
    proposals_.clear();
    accepted_.clear();
    rejected_.clear();
    committed_.clear();
    rejectedTransactions_.clear();
    popProposalCount_ = 0;
    highWatermark_ = 0;
    totalScheduled_ = 0;
    totalPops_ = 0;
  }

private:
  struct Entry {
    T value;
    uint32_t priority;
    Epoch issueEpoch;
    uint32_t portIndex;
    uint32_t instanceIndex;
    ObjectId ownerId;
    uint64_t transactionId;
  };

  static auto stableKey(const Entry &entry) {
    return std::tie(entry.priority, entry.issueEpoch, entry.portIndex,
                    entry.instanceIndex, entry.ownerId, entry.transactionId);
  }
  static bool stableLess(const Entry &left, const Entry &right) {
    return stableKey(left) < stableKey(right);
  }
  static bool sameIdentity(const Entry &left, const Entry &right) {
    return left.ownerId == right.ownerId &&
           left.transactionId == right.transactionId;
  }
  bool containsIdentity(ObjectId ownerId, uint64_t transactionId) const {
    auto matches = [&](const Entry &entry) {
      return entry.ownerId == ownerId && entry.transactionId == transactionId;
    };
    return std::any_of(proposals_.begin(), proposals_.end(), matches) ||
           std::any_of(accepted_.begin(), accepted_.end(), matches) ||
           std::any_of(committed_.begin(), committed_.end(), matches);
  }

  size_t capacity_;
  std::vector<Entry> proposals_;
  std::vector<Entry> accepted_;
  std::vector<Entry> rejected_;
  std::vector<Entry> committed_;
  std::vector<uint64_t> rejectedTransactions_;
  size_t popProposalCount_ = 0;
  size_t highWatermark_ = 0;
  uint64_t totalScheduled_ = 0;
  uint64_t totalPops_ = 0;
};

// ── ReadyValid ────────────────────────────────────────────────────────

template <typename T> class ReadyValid : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.std.ready_valid";
  static constexpr ObjectKind componentKind = ObjectKind::Link;

  ReadyValid(std::string name, ObjectId id, SimObject *parent)
      : SimObject(ObjectKind::Link, std::move(name), id, parent) {}

  void setValid(bool v) {
    validProposal_ = v;
    hasProposal_ = true;
  }
  void setData(T data) {
    dataProposal_ = data;
    hasProposal_ = true;
  }
  bool isReady() const { return ready_; }
  void setReady(bool r) {
    readyProposal_ = r;
    hasProposal_ = true;
  }
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
    hasProposal_ = false;
  }

  bool hasPendingCommit() const override { return hasProposal_; }

  T lastTransferred() const { return lastTransferred_; }
  uint64_t transferCount() const { return transferCount_; }
  bool isRunnable(Epoch) const override { return valid_ && !ready_; }

  void reset() override {
    valid_ = false;
    ready_ = false;
    validProposal_ = false;
    readyProposal_ = false;
    transferCount_ = 0;
    hasProposal_ = false;
  }

private:
  bool valid_ = false, ready_ = false;
  bool hasProposal_ = false;
  bool validProposal_ = false, readyProposal_ = false;
  T data_{}, dataProposal_{}, lastTransferred_{};
  uint64_t transferCount_ = 0;
};

// ── RequestResponse ──────────────────────────────────────────────────

template <typename Req, typename Resp>
class RequestResponse : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.std.request_response";
  static constexpr ObjectKind componentKind = ObjectKind::Link;

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

  bool hasPendingCommit() const override {
    return !reqProposals_.empty() || !respProposals_.empty();
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
