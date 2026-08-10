#include "gfsim/object.h"
#include "gfsim/queue.h"

#include <map>
#include <set>
#include <utility>

namespace gfsim {

struct SimSystem::Impl {
  std::map<ObjectId, SimObject *> objects;
  std::map<Epoch, std::set<ObjectId>> scheduledWork;
  EventQueue eventQueue{"events", kInvalidObjectId, nullptr};
  DispatchTable dispatch;
  uint64_t committedEventCount = 0;
  bool executingEpoch = false;
};

SimSystem::~SimSystem() = default;

SimSystem::SimSystem(std::string name)
    : SimObject(ObjectKind::System, std::move(name), kSystemObjectId),
      root_(std::make_unique<Module>("root", kRootObjectId, this)),
      impl_(std::make_unique<Impl>()) {
  setPath("/" + std::string(this->name()));
  root_->setPath(std::string(path()));
  impl_->objects[kSystemObjectId] = this;
}

bool SimSystem::fail(std::string code, std::string message) {
  terminated_ = true;
  impl_->executingEpoch = false;
  result_.classification = TerminationClass::Failed;
  result_.finalEpoch = epoch_;
  result_.committedEventCount = impl_->committedEventCount;
  result_.diagnosticCode = std::move(code);
  result_.message = std::move(message);
  return false;
}

void SimSystem::registerObject(SimObject *obj) {
  if (!obj || obj->id() == kInvalidObjectId ||
      impl_->objects.contains(obj->id())) {
    fail("invalid_object_registration",
         "runtime object registration is null, invalid, or duplicate");
    return;
  }
  impl_->objects[obj->id()] = obj;
}

bool SimSystem::setDispatchTable(std::span<const DispatchRow> rows) {
  DispatchTable candidate(rows);
  if (!candidate.validate())
    return fail("invalid_dispatch_table",
                "dispatch rows must be complete and densely indexed");
  impl_->dispatch = candidate;
  return true;
}

SimObject *SimSystem::lookup(ObjectId id) const {
  if (const DispatchRow *row = impl_->dispatch.lookup(id))
    return static_cast<SimObject *>(row->object);
  auto it = impl_->objects.find(id);
  return it != impl_->objects.end() ? it->second : nullptr;
}

bool SimSystem::scheduleWork(ObjectId id, Epoch epoch) {
  if (terminated_)
    return false;
  if (epoch.delta >= kMaxDeltasPerTick)
    return fail("max_deltas_exceeded",
                "scheduled work exceeds the causal delta limit");
  if (epoch < epoch_)
    return fail("work_before_current_epoch",
                "work cannot be scheduled before the committed epoch");
  if (!lookup(id))
    return fail("unknown_work_target",
                "work target is absent from the static dispatch table");
  if (impl_->executingEpoch && epoch == epoch_) {
    if (epoch_.delta + 1 >= kMaxDeltasPerTick)
      return fail("max_deltas_exceeded",
                  "causal continuation exceeds the delta limit");
    epoch = epoch_.nextDelta();
  }
  impl_->scheduledWork[epoch].insert(id);
  return true;
}

bool SimSystem::scheduleEvent(Event event) {
  if (terminated_)
    return false;
  if (event.readyTime.delta >= kMaxDeltasPerTick)
    return fail("max_deltas_exceeded",
                "scheduled event exceeds the causal delta limit");
  if (event.readyTime < epoch_)
    return fail("event_before_current_epoch",
                "events cannot be scheduled before the committed epoch");
  if (!lookup(event.targetId))
    return fail("unknown_event_target",
                "event target is absent from the static dispatch table");
  if (!impl_->eventQueue.proposeSchedule(event))
    return fail("event_queue_capacity_exceeded",
                "the global event queue capacity was exceeded");
  return true;
}

std::optional<Event> SimSystem::nextEvent() const {
  return impl_->eventQueue.nextEvent();
}

bool SimSystem::step() {
  if (terminated_)
    return false;

  auto stopAtEventCap = [this] {
    if (impl_->committedEventCount < maxEvents_)
      return false;
    terminated_ = true;
    impl_->executingEpoch = false;
    result_.classification = TerminationClass::Incomplete;
    result_.finalEpoch = epoch_;
    result_.committedEventCount = impl_->committedEventCount;
    result_.diagnosticCode = "max_events_reached";
    return true;
  };

  if (epoch_.time >= maxTicks_) {
    terminated_ = true;
    result_.classification = TerminationClass::Incomplete;
    result_.finalEpoch = epoch_;
    result_.diagnosticCode = "max_ticks_reached";
    return false;
  }
  if (stopAtEventCap())
    return false;

  // Events committed by a previous epoch activate their target at their exact
  // ready epoch before the immutable Work snapshot is observed.
  while (auto event = impl_->eventQueue.nextEvent()) {
    if (event->readyTime < epoch_)
      return fail("event_before_current_epoch",
                  "the event queue contains a stale event");
    if (event->readyTime != epoch_)
      break;
    if (stopAtEventCap())
      return false;
    impl_->eventQueue.popNext();
    if (!scheduleWork(event->targetId, epoch_))
      return false;
    ++impl_->committedEventCount;
  }

  std::set<ObjectId> currentWork;
  if (auto current = impl_->scheduledWork.find(epoch_);
      current != impl_->scheduledWork.end()) {
    currentWork = std::move(current->second);
    impl_->scheduledWork.erase(current);
  }

  impl_->executingEpoch = true;
  for (ObjectId id : currentWork) {
    if (const DispatchRow *row = impl_->dispatch.lookup(id))
      row->work(row->object, epoch_);
    else if (SimObject *object = lookup(id))
      object->doWork(epoch_);
    if (terminated_)
      return false;
  }

  for (ObjectId id : currentWork) {
    if (const DispatchRow *row = impl_->dispatch.lookup(id))
      row->xfer(row->object, epoch_, XferPhase::Arbitrate);
    else if (SimObject *object = lookup(id))
      object->doArbitrate(epoch_);
    if (terminated_)
      return false;
  }

  for (ObjectId id : currentWork) {
    if (const DispatchRow *row = impl_->dispatch.lookup(id))
      row->xfer(row->object, epoch_, XferPhase::Commit);
    else if (SimObject *object = lookup(id))
      object->doXfer(epoch_);
    if (terminated_)
      return false;
  }
  impl_->eventQueue.doXfer(epoch_);

  // An event committed for the active epoch is a causal continuation. Its
  // target runs at the next delta, never inside the closed Work snapshot.
  while (auto event = impl_->eventQueue.nextEvent()) {
    if (event->readyTime < epoch_)
      return fail("event_before_current_epoch",
                  "the event queue contains a stale event");
    if (event->readyTime != epoch_)
      break;
    if (stopAtEventCap())
      return false;
    impl_->eventQueue.popNext();
    if (!scheduleWork(event->targetId, epoch_))
      return false;
    ++impl_->committedEventCount;
  }
  impl_->executingEpoch = false;

  std::optional<Epoch> nextEpoch;
  if (!impl_->scheduledWork.empty())
    nextEpoch = impl_->scheduledWork.begin()->first;
  if (auto event = impl_->eventQueue.nextEvent();
      event && (!nextEpoch || event->readyTime < *nextEpoch))
    nextEpoch = event->readyTime;

  if (!nextEpoch) {
    terminated_ = true;
    result_.classification = TerminationClass::Completed;
    result_.finalEpoch = epoch_;
    result_.committedEventCount = impl_->committedEventCount;
    return false;
  }
  if (*nextEpoch <= epoch_)
    return fail("non_monotonic_epoch",
                "scheduler failed to advance beyond the committed epoch");
  epoch_ = *nextEpoch;
  return true;
}

TerminationResult SimSystem::run() {
  epoch_ = {0, 0};

  root_->walk([this](SimObject &obj) {
    if (obj.kind() == ObjectKind::Process ||
        obj.kind() == ObjectKind::TraceSource)
      scheduleWork(obj.id(), epoch_);
  });

  while (!terminated_)
    if (!step())
      break;

  result_.finalEpoch = epoch_;
  result_.committedEventCount = impl_->committedEventCount;
  return result_;
}

void SimSystem::reset() {
  epoch_ = {0, 0};
  terminated_ = false;
  result_ = TerminationResult{};
  impl_->scheduledWork.clear();
  impl_->eventQueue.reset();
  impl_->committedEventCount = 0;
  impl_->executingEpoch = false;
  if (!impl_->dispatch.empty()) {
    for (ObjectId id = 0; id < impl_->dispatch.size(); ++id) {
      const DispatchRow *row = impl_->dispatch.lookup(id);
      row->reset(row->object);
    }
  } else {
    root_->reset();
  }
}

} // namespace gfsim
