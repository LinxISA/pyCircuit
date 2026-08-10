#ifndef GFSIM_OBJECT_H
#define GFSIM_OBJECT_H

#include "gfsim/core.h"
#include "gfsim/dispatch.h"

#include <cassert>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gfsim {

// ── SimObject ─────────────────────────────────────────────────────────

/// Every runtime object has one owning parent (except the root system),
/// a compile-time-assigned stable object ID, a stable local name,
/// and a canonical hierarchy path.
class SimObject {
public:
  SimObject(ObjectKind kind, std::string name, ObjectId id,
            SimObject *parent = nullptr)
      : kind_(kind), name_(std::move(name)), id_(id), parent_(parent) {}

  virtual ~SimObject() = default;

  ObjectKind kind() const { return kind_; }
  ObjectId id() const { return id_; }
  std::string_view name() const { return name_; }
  std::string_view path() const { return path_; }
  SimObject *parent() const { return parent_; }

  /// Set the canonical hierarchy path (called during construction).
  void setPath(std::string path) { path_ = std::move(path); }

  /// Set the parent pointer (called during hierarchy construction).
  void setParent(SimObject *p) { parent_ = p; }

  /// Children owned by this object (for modules).
  virtual std::vector<SimObject *> children() const { return {}; }

  // ── Work interface ──────────────────────────────────────────────────
  //
  // Each component implements Work generation (produce proposals for the
  // current epoch) and Xfer commit (accept/reject proposals atomically).

  /// Generate work proposals for the current epoch.
  /// Called only when the object is scheduled to run.
  virtual void doWork(Epoch epoch) {}

  /// Commit accepted proposals at the Xfer barrier.
  virtual void doXfer(Epoch epoch) {}

  /// Arbitration: select the winning proposal among candidates.
  virtual void doArbitrate(Epoch epoch) {}

  /// True when the next Xfer call will publish a committed state change.
  virtual bool hasPendingCommit() const { return false; }

  // ── Wake conditions ─────────────────────────────────────────────────

  /// Returns true if this object has work to do at the given epoch.
  virtual bool isRunnable(Epoch epoch) const { return false; }

  // ── Reset ───────────────────────────────────────────────────────────

  virtual void reset() {}

protected:
  ObjectKind kind_;
  std::string name_;
  ObjectId id_ = kInvalidObjectId;
  std::string path_;
  SimObject *parent_ = nullptr;
};

// ── Module ────────────────────────────────────────────────────────────

/// A module owns child modules and local runtime objects.
/// Generated C++ creates one class per specialized ACIR module definition.
class Module : public SimObject {
public:
  Module(std::string name, ObjectId id, SimObject *parent = nullptr)
      : SimObject(ObjectKind::Module, std::move(name), id, parent) {}

  /// Add a child object owned by this module.
  void addChild(std::unique_ptr<SimObject> child) {
    child->setParent(this);
    child->setPath(std::string(path()) + "/" + std::string(child->name()));
    children_.push_back(child.get());
    owned_.push_back(std::move(child));
  }

  std::vector<SimObject *> children() const override { return children_; }

  /// Find a child by name (linear scan, acceptable for small fan-out).
  SimObject *findChild(std::string_view name) const {
    for (auto *c : children_)
      if (c->name() == name)
        return c;
    return nullptr;
  }

  /// Walk all descendants recursively.
  template <typename F> void walk(F &&fn) {
    fn(*this);
    for (auto *c : children_) {
      if (auto *m = dynamic_cast<Module *>(c))
        m->walk(fn);
      else
        // NOLINTNEXTLINE(clang-analyzer-core.NonNullParamChecker)
        fn(*c); // the children_ invariant guarantees non-null entries
    }
  }

  /// Walk all descendants (const version).
  template <typename F> void walk(F &&fn) const {
    fn(*this);
    for (auto *c : children_) {
      if (auto *m = dynamic_cast<const Module *>(c))
        m->walk(std::forward<F>(fn));
      else
        fn(*c);
    }
  }

  void reset() override {
    for (auto *c : children_)
      c->reset();
  }

private:
  std::vector<SimObject *> children_;
  std::vector<std::unique_ptr<SimObject>> owned_;
};

// ── SimSystem ─────────────────────────────────────────────────────────

class SimQueueBase;
class EventQueue;
class Resource;

/// The system owns the root module, exact global epoch, event scheduling,
/// phase barriers, termination state, and deterministic sequencing.
class SimSystem : public SimObject {
public:
  explicit SimSystem(std::string name = "system");
  ~SimSystem() override;

  Module &root() { return *root_; }
  const Module &root() const { return *root_; }

  Epoch currentEpoch() const { return epoch_; }

  // ── Scheduling ──────────────────────────────────────────────────────

  /// Schedule an object for Work at the given epoch.
  bool scheduleWork(ObjectId id, Epoch epoch);

  /// Schedule an event for a future epoch.
  bool scheduleEvent(Event event);

  /// Install the generated dense static dispatch table.
  bool setDispatchTable(std::span<const DispatchRow> rows);

  /// Install canonical compressed activation adjacency.
  bool setActivationPlan(std::span<const uint32_t> offsets,
                         std::span<const ObjectId> targets);

  /// Get the next pending event (earliest ready time).
  std::optional<Event> nextEvent() const;

  // ── Simulation loop ─────────────────────────────────────────────────

  /// Run the simulation until termination or cap reached.
  TerminationResult run();

  /// Advance one (time, delta) step. Returns false if no more work.
  bool step();

  // ── Termination ─────────────────────────────────────────────────────

  bool isTerminated() const { return terminated_; }
  TerminationResult terminationResult() const { return result_; }

  // ── Object registry ─────────────────────────────────────────────────

  /// Register an object for ID-based lookup.
  void registerObject(SimObject *obj);

  /// Look up an object by its stable ID.
  SimObject *lookup(ObjectId id) const;

  void reset() override;

  // ── Caps ────────────────────────────────────────────────────────────

  void setMaxTicks(Tick max) { maxTicks_ = max; }
  void setMaxEvents(uint64_t max) { maxEvents_ = max; }
  void setMaxTraceRecords(uint64_t max) { maxTraceRecords_ = max; }

private:
  std::unique_ptr<Module> root_;
  Epoch epoch_;
  bool terminated_ = false;
  TerminationResult result_;

  Tick maxTicks_ = UINT64_MAX;
  uint64_t maxEvents_ = UINT64_MAX;
  uint64_t maxTraceRecords_ = UINT64_MAX;

  struct Impl;
  std::unique_ptr<Impl> impl_;

  bool fail(std::string code, std::string message);
};

} // namespace gfsim

#endif // GFSIM_OBJECT_H
