#ifndef GFSIM_QUEUE_H
#define GFSIM_QUEUE_H

#include "gfsim/core.h"
#include "gfsim/object.h"

#include <queue>
#include <vector>
#include <set>
#include <cstddef>
#include <optional>

namespace gfsim {

// ── SimQueue<T> ───────────────────────────────────────────────────────

/// FIFO data queue with entry capacity, optional byte capacity,
/// ordered read/write proposals, deterministic arbitration,
/// and occupancy/watermark statistics.
template <typename T>
class SimQueue : public SimObject {
public:
  SimQueue(std::string name, ObjectId id, SimObject *parent,
           size_t entryCapacity, size_t byteCapacity = SIZE_MAX)
      : SimObject(ObjectKind::Queue, std::move(name), id, parent),
        entryCapacity_(entryCapacity), byteCapacity_(byteCapacity) {}

  // ── Capacity ────────────────────────────────────────────────────────

  size_t entryCapacity() const { return entryCapacity_; }
  size_t byteCapacity() const { return byteCapacity_; }

  size_t committedSize() const { return committed_.size(); }
  bool isFull() const { return committedSize() >= entryCapacity_; }
  bool isEmpty() const { return committed_.empty(); }

  // ── Proposal interface ──────────────────────────────────────────────

  /// Propose to enqueue an element. Returns false if capacity exceeded.
  bool proposePush(T element) {
    if (pushProposals_.size() + committedSize() >= entryCapacity_)
      return false;
    pushProposals_.push_back(std::move(element));
    return true;
  }

  /// Propose to dequeue the front element.
  std::optional<T> proposePop() {
    if (committed_.empty() && popProposalCount_ >= committedSize())
      return std::nullopt;
    ++popProposalCount_;
    return committed_.empty() ? std::nullopt
                              : std::optional<T>(committed_.front());
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
    // Commit push proposals
    for (auto &elem : pushProposals_)
      committed_.push_back(std::move(elem));
    pushProposals_.clear();

    // Commit pop proposals
    for (size_t i = 0; i < popProposalCount_ && !committed_.empty(); ++i)
      committed_.erase(committed_.begin());
    popProposalCount_ = 0;

    // Update statistics
    if (committedSize() > highWatermark_)
      highWatermark_ = committedSize();
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
  }

private:
  size_t entryCapacity_;
  size_t byteCapacity_;
  std::vector<T> committed_;
  std::vector<T> pushProposals_;
  size_t popProposalCount_ = 0;
  size_t highWatermark_ = 0;
  uint64_t totalPushes_ = 0;
  uint64_t totalPops_ = 0;
};

// ── EventQueue ────────────────────────────────────────────────────────

/// Time-ordered event queue. Events are ordered by exact epoch
/// followed by model-defined stable keys.
class EventQueue : public SimObject {
public:
  EventQueue(std::string name, ObjectId id, SimObject *parent,
             size_t capacity = 1024)
      : SimObject(ObjectKind::EventQueue, std::move(name), id, parent),
        capacity_(capacity) {}

  // ── Capacity ────────────────────────────────────────────────────────

  size_t capacity() const { return capacity_; }
  size_t size() const { return committed_.size(); }
  bool isFull() const { return committed_.size() + pushProposals_.size() >= capacity_; }

  // ── Proposal ────────────────────────────────────────────────────────

  bool proposeSchedule(Event event) {
    if (committed_.size() + pushProposals_.size() >= capacity_)
      return false;
    pushProposals_.insert(std::move(event));
    return true;
  }

  // ── Xfer ────────────────────────────────────────────────────────────

  void doXfer(Epoch epoch) override {
    for (auto &event : pushProposals_)
      committed_.insert(std::move(event));
    pushProposals_.clear();
  }

  // ── Query ───────────────────────────────────────────────────────────

  std::optional<Event> nextEvent() const {
    if (committed_.empty()) return std::nullopt;
    return *committed_.begin();
  }

  /// Pop the earliest event and return it.
  std::optional<Event> popNext() {
    if (committed_.empty()) return std::nullopt;
    auto it = committed_.begin();
    Event e = *it;
    committed_.erase(it);
    return e;
  }

  bool hasEventAt(Epoch epoch) const {
    for (const auto &e : committed_)
      if (e.readyTime == epoch) return true;
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
