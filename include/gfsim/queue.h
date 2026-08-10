#ifndef GFSIM_QUEUE_H
#define GFSIM_QUEUE_H

#include "gfsim/core.h"
#include "gfsim/object.h"
#include "gfsim/packet.h"

#include <cstddef>
#include <optional>
#include <queue>
#include <set>
#include <vector>

namespace gfsim {

// ── SimQueue<T> ───────────────────────────────────────────────────────

/// FIFO data queue with entry capacity, optional byte capacity,
/// ordered read/write proposals, deterministic arbitration,
/// and occupancy/watermark statistics.
template <typename T> class SimQueue : public SimObject {
public:
  SimQueue(std::string name, ObjectId id, SimObject *parent,
           size_t entryCapacity, size_t byteCapacity = SIZE_MAX)
      : SimObject(ObjectKind::Queue, std::move(name), id, parent),
        entryCapacity_(entryCapacity), byteCapacity_(byteCapacity) {}

  // ── Capacity ────────────────────────────────────────────────────────

  size_t entryCapacity() const { return entryCapacity_; }
  size_t byteCapacity() const { return byteCapacity_; }

  size_t committedSize() const { return committed_.size(); }
  size_t committedBytes() const {
    if constexpr (PacketTraits<T>::serializedSize == 0)
      return 0;
    return committed_.size() * PacketTraits<T>::serializedSize;
  }
  bool isFull() const {
    return committedSize() >= entryCapacity_ ||
           exceedsByteCapacity(committedSize() + 1);
  }
  bool isEmpty() const { return committed_.empty(); }

  // ── Proposal interface ──────────────────────────────────────────────

  /// Propose to enqueue an element. Returns false if capacity exceeded.
  bool proposePush(T element) {
    size_t occupied = pushProposals_.size() + committedSize();
    if (occupied >= entryCapacity_ || exceedsByteCapacity(occupied + 1))
      return false;
    pushProposals_.push_back(std::move(element));
    return true;
  }

  /// Propose to dequeue the next element in FIFO order.
  std::optional<T> proposePop() {
    if (popProposalCount_ >= committed_.size())
      return std::nullopt;
    size_t index = popProposalCount_;
    ++popProposalCount_;
    return std::optional<T>(committed_[index]);
  }

  /// Peek at the front without proposing a pop.
  const T *peek() const {
    return committed_.empty() ? nullptr : &committed_.front();
  }

  // ── Arbitration ─────────────────────────────────────────────────────

  void doArbitrate(Epoch epoch) override {
    // Deterministic local arbitration: FIFO order.
    // Push proposals are appended in order.
    // Pop proposals are served from the front.
    // In v0.1, arbitration is simple FIFO.
  }

  // ── Xfer ────────────────────────────────────────────────────────────

  void doXfer(Epoch epoch) override {
    bool changed = hasPendingCommit();
    // Commit push proposals
    for (auto &elem : pushProposals_) {
      committed_.push_back(std::move(elem));
      ++totalPushes_;
    }
    pushProposals_.clear();

    // Commit pop proposals
    for (size_t i = 0; i < popProposalCount_ && !committed_.empty(); ++i) {
      committed_.erase(committed_.begin());
      ++totalPops_;
    }
    popProposalCount_ = 0;

    // Update statistics
    if (committedSize() > highWatermark_)
      highWatermark_ = committedSize();
    if (changed)
      lastUpdate_ = epoch;
  }

  bool hasPendingCommit() const override {
    return !pushProposals_.empty() || popProposalCount_ != 0;
  }

  RuntimeObjectState runtimeState(Epoch epoch) const override {
    RuntimeObjectState state = SimObject::runtimeState(epoch);
    state.queueOccupancy = committedSize();
    state.pendingOffers = pushProposals_.size();
    state.quiescent = committed_.empty() && !hasPendingCommit();
    if (!state.quiescent)
      state.reason = hasPendingCommit() ? "pending_commit" : "queue_not_empty";
    return state;
  }

  void collectStatistics(std::vector<StatSnapshot> &out) const override {
    auto append = [&](std::string suffix, uint64_t value, StatisticKind kind) {
      out.push_back({.name = std::move(suffix),
                     .objectPath = std::string(path()),
                     .kind = kind,
                     .value = value,
                     .lastUpdate = lastUpdate_});
    };
    append("queue_occupancy", committedSize(), StatisticKind::Gauge);
    append("queue_occupancy_peak", highWatermark_, StatisticKind::Gauge);
    append("accepted_transactions", totalPushes_, StatisticKind::Counter);
    append("completed_transactions", totalPops_, StatisticKind::Counter);
  }

  // ── Statistics ──────────────────────────────────────────────────────

  size_t highWatermark() const { return highWatermark_; }
  uint64_t totalPushes() const { return totalPushes_; }
  uint64_t totalPops() const { return totalPops_; }

  void reset() override {
    committed_.clear();
    pushProposals_.clear();
    popProposalCount_ = 0;
    highWatermark_ = 0;
    totalPushes_ = 0;
    totalPops_ = 0;
    lastUpdate_ = {};
  }

private:
  bool exceedsByteCapacity(size_t elementCount) const {
    if constexpr (PacketTraits<T>::serializedSize == 0)
      return false;
    return elementCount > byteCapacity_ / PacketTraits<T>::serializedSize;
  }

  size_t entryCapacity_;
  size_t byteCapacity_;
  std::vector<T> committed_;
  std::vector<T> pushProposals_;
  size_t popProposalCount_ = 0;
  size_t highWatermark_ = 0;
  uint64_t totalPushes_ = 0;
  uint64_t totalPops_ = 0;
  Epoch lastUpdate_;
};

/// Standard-library finite FIFO component. The distinct name is the public
/// component contract; SimQueue remains the underlying runtime primitive.
template <typename T> class Queue final : public SimQueue<T> {
public:
  static constexpr std::string_view contractName = "ac.std.Queue";
  static constexpr ObjectKind componentKind = ObjectKind::Queue;
  using SimQueue<T>::SimQueue;
};

// ── EventQueue ────────────────────────────────────────────────────────

/// Time-ordered event queue. Events are ordered by exact epoch, target object
/// ID, event kind, and payload.
class EventQueue : public SimObject {
public:
  EventQueue(std::string name, ObjectId id, SimObject *parent,
             size_t capacity = 1024)
      : SimObject(ObjectKind::EventQueue, std::move(name), id, parent),
        capacity_(capacity) {}

  // ── Capacity ────────────────────────────────────────────────────────

  size_t capacity() const { return capacity_; }
  size_t size() const { return committed_.size(); }
  bool isFull() const {
    return committed_.size() + pushProposals_.size() >= capacity_;
  }

  // ── Proposal ────────────────────────────────────────────────────────

  bool proposeSchedule(Event event) {
    if (committed_.size() + pushProposals_.size() >= capacity_)
      return false;
    pushProposals_.insert(event);
    return true;
  }

  // ── Xfer ────────────────────────────────────────────────────────────

  void doXfer(Epoch epoch) override {
    for (auto &event : pushProposals_)
      committed_.insert(event);
    pushProposals_.clear();
  }

  bool hasPendingCommit() const override { return !pushProposals_.empty(); }

  // ── Query ───────────────────────────────────────────────────────────

  std::optional<Event> nextEvent() const {
    if (committed_.empty())
      return std::nullopt;
    return *committed_.begin();
  }

  /// Pop the earliest event and return it.
  std::optional<Event> popNext() {
    if (committed_.empty())
      return std::nullopt;
    auto it = committed_.begin();
    Event e = *it;
    committed_.erase(it);
    return e;
  }

  bool hasEventAt(Epoch epoch) const {
    for (const auto &e : committed_)
      if (e.readyTime == epoch)
        return true;
    return false;
  }

  void reset() override {
    committed_.clear();
    pushProposals_.clear();
  }

private:
  size_t capacity_;
  std::multiset<Event> committed_;
  std::multiset<Event> pushProposals_;
};

} // namespace gfsim

#endif // GFSIM_QUEUE_H
