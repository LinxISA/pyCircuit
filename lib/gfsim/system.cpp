#include "gfsim/object.h"
#include "gfsim/queue.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace gfsim {

// ── SimSystem::Impl ───────────────────────────────────────────────────

struct SimSystem::Impl {
  // Object registry: ID → SimObject*
  std::map<ObjectId, SimObject *> objects;

  // Work set: objects scheduled to run at the current epoch
  std::set<ObjectId> workSet;

  // Global event queue
  EventQueue eventQueue{"events", 0, nullptr};

  // Committed event counter
  uint64_t committedEventCount = 0;
};

// ── SimSystem ─────────────────────────────────────────────────────────

SimSystem::~SimSystem() = default;

SimSystem::SimSystem(std::string name)
    : SimObject(ObjectKind::System, std::move(name), kSystemObjectId),
      root_(std::make_unique<Module>("root", 2, this)),
      impl_(std::make_unique<Impl>()) {
  setPath("/" + std::string(this->name()));
  root_->setPath(std::string(path()));
  impl_->objects[kSystemObjectId] = this;
}

void SimSystem::registerObject(SimObject *obj) {
  impl_->objects[obj->id()] = obj;
}

SimObject *SimSystem::lookup(ObjectId id) const {
  auto it = impl_->objects.find(id);
  return it != impl_->objects.end() ? it->second : nullptr;
}

void SimSystem::scheduleWork(ObjectId id, Epoch epoch) {
  if (epoch == epoch_)
    impl_->workSet.insert(id);
}

void SimSystem::scheduleEvent(Event event) {
  impl_->eventQueue.proposeSchedule(event);
}

std::optional<Event> SimSystem::nextEvent() const {
  return impl_->eventQueue.nextEvent();
}

// ── Step: advance one (time, delta) ───────────────────────────────────

bool SimSystem::step() {
  if (terminated_) return false;

  // Check caps
  if (epoch_.time >= maxTicks_) {
    terminated_ = true;
    result_.classification = TerminationClass::Incomplete;
    result_.finalEpoch = epoch_;
    result_.diagnosticCode = "max_ticks_reached";
    return false;
  }
  if (impl_->committedEventCount >= maxEvents_) {
    terminated_ = true;
    result_.classification = TerminationClass::Incomplete;
    result_.finalEpoch = epoch_;
    result_.diagnosticCode = "max_events_reached";
    return false;
  }

  // Phase 1: Work — execute all scheduled objects
  for (auto id : impl_->workSet) {
    auto *obj = lookup(id);
    if (obj) obj->doWork(epoch_);
  }

  // Phase 2: Arbitration — each object arbitrates its proposals locally
  for (auto id : impl_->workSet) {
    auto *obj = lookup(id);
    if (obj) obj->doArbitrate(epoch_);
  }

  // Phase 3: Xfer — commit accepted proposals atomically
  // Also commit the event queue
  impl_->eventQueue.doXfer(epoch_);
  for (auto id : impl_->workSet) {
    auto *obj = lookup(id);
    if (obj) obj->doXfer(epoch_);
  }

  impl_->workSet.clear();

  // Phase 4: Process events due at the current epoch
  while (auto event = impl_->eventQueue.popNext()) {
    if (event->readyTime.time > epoch_.time) {
      // This event is in the future; put it back
      impl_->eventQueue.proposeSchedule(*event);
      impl_->eventQueue.doXfer(epoch_);
      break;
    }
    if (event->readyTime == epoch_) {
      scheduleWork(event->targetId, epoch_);
      ++impl_->committedEventCount;
    }
  }

  // Advance to next delta or time
  if (!impl_->workSet.empty()) {
    // More work at the same time — advance delta
    if (epoch_.delta >= kMaxDeltasPerTick) {
      terminated_ = true;
      result_.classification = TerminationClass::Failed;
      result_.finalEpoch = epoch_;
      result_.diagnosticCode = "max_deltas_exceeded";
      return false;
    }
    epoch_ = epoch_.nextDelta();
    return true;
  }

  // No more work at this time — advance to next event time
  auto next = impl_->eventQueue.nextEvent();
  if (next) {
    epoch_ = {next->readyTime.time, 0};
    return true;
  }

  // No future events — simulation complete
  terminated_ = true;
  result_.classification = TerminationClass::Completed;
  result_.finalEpoch = epoch_;
  result_.committedEventCount = impl_->committedEventCount;
  return false;
}

// ── Run: full simulation loop ─────────────────────────────────────────

TerminationResult SimSystem::run() {
  epoch_ = {0, 0};

  // Initial scheduling: all processes get initial wake
  root_->walk([this](SimObject &obj) {
    if (obj.kind() == ObjectKind::Process ||
        obj.kind() == ObjectKind::TraceSource)
      scheduleWork(obj.id(), epoch_);
  });

  while (!terminated_) {
    if (!step()) break;
  }

  result_.finalEpoch = epoch_;
  result_.committedEventCount = impl_->committedEventCount;
  return result_;
}

} // namespace gfsim
