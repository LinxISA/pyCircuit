#include "gfsim/components.h"
#include "gfsim/core.h"
#include "gfsim/dispatch.h"
#include "gfsim/object.h"
#include "gfsim/queue.h"
#include "gfsim/resource.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <memory>
#include <random>

namespace gfsim {
namespace {

// ═══════════════════════════════════════════════════════════════════════
// Core types
// ═══════════════════════════════════════════════════════════════════════

TEST(GfsimCoreTest, EpochComparison) {
  Epoch a{0, 0}, b{0, 1}, c{1, 0};
  EXPECT_LT(a, b);
  EXPECT_LT(a, c);
  EXPECT_LT(b, c);
  EXPECT_EQ(a.nextDelta(), b);
}

TEST(GfsimCoreTest, EpochSameTime) {
  Epoch a{5, 0}, b{5, 3};
  EXPECT_TRUE(a.sameTime(b));
  EXPECT_FALSE(a.sameTime({6, 0}));
}

TEST(GfsimCoreTest, EpochNextDeltaCorrect) {
  Epoch e{10, 5};
  EXPECT_EQ(e.nextDelta(), (Epoch{10, 6}));
}

TEST(GfsimCoreTest, EventOrdering) {
  Event e1{{0, 0}, 1, 0, 0};
  Event e2{{0, 1}, 1, 0, 0};
  Event e3{{1, 0}, 1, 0, 0};
  EXPECT_LT(e1, e2);
  EXPECT_LT(e2, e3);
}

TEST(GfsimCoreTest, EventSameTimeOrderedByDelta) {
  Event e1{{5, 0}, 1, 0, 0};
  Event e2{{5, 1}, 1, 0, 0};
  Event e3{{5, 2}, 1, 0, 0};
  EXPECT_LT(e1, e2);
  EXPECT_LT(e2, e3);
}

TEST(GfsimCoreTest, EventSameTimeOrderedByKindThenPayload) {
  Event e1{{0, 0}, 1, 0, 0};
  Event e2{{0, 0}, 1, 1, 0};
  Event e3{{0, 0}, 1, 1, 1};
  EXPECT_LT(e1, e2);
  EXPECT_LT(e2, e3);
  EXPECT_LT(e1, e3);
}

TEST(GfsimCoreTest, EventTieBreaksByStableTargetId) {
  Event first{{0, 0}, 3, 1, 9};
  Event second{{0, 0}, 7, 1, 9};
  EXPECT_LT(first, second);
}

TEST(GfsimCoreTest, TerminationResultDefaults) {
  TerminationResult r;
  EXPECT_EQ(r.classification, TerminationClass::Incomplete);
  EXPECT_EQ(r.committedEventCount, 0u);
  EXPECT_EQ(r.tracePosition, 0u);
}

TEST(GfsimCoreTest, MaxDeltasPerTickIsFinite) {
  EXPECT_GT(kMaxDeltasPerTick, 0u);
  EXPECT_LT(kMaxDeltasPerTick, 1u << 20);
}

// ═══════════════════════════════════════════════════════════════════════
// Object hierarchy
// ═══════════════════════════════════════════════════════════════════════

TEST(GfsimObjectTest, ModuleAddsChildWithPath) {
  Module root("root", 1);
  auto child = std::make_unique<SimObject>(ObjectKind::Compute, "comp", 2);
  root.addChild(std::move(child));
  ASSERT_EQ(root.children().size(), 1u);
  EXPECT_EQ(root.children()[0]->name(), "comp");
  EXPECT_EQ(root.children()[0]->parent(), &root);
}

TEST(GfsimObjectTest, ModuleWalkVisitsAll) {
  Module root("root", 1);
  root.addChild(std::make_unique<SimObject>(ObjectKind::Compute, "a", 2));
  root.addChild(std::make_unique<SimObject>(ObjectKind::Sink, "b", 3));
  int count = 0;
  root.walk([&](SimObject &) { ++count; });
  EXPECT_EQ(count, 3);
}

TEST(GfsimObjectTest, NestedModuleWalkVisitsAllLevels) {
  Module root("root", 1);
  auto mid = std::make_unique<Module>("mid", 2);
  auto leaf = std::make_unique<SimObject>(ObjectKind::Compute, "leaf", 3);
  mid->addChild(std::move(leaf));
  root.addChild(std::move(mid));
  int count = 0;
  root.walk([&](SimObject &) { ++count; });
  EXPECT_EQ(count, 3);
}

TEST(GfsimObjectTest, FindChildByName) {
  Module root("root", 1);
  root.addChild(std::make_unique<SimObject>(ObjectKind::Compute, "alu", 2));
  root.addChild(std::make_unique<SimObject>(ObjectKind::Sink, "mem", 3));
  EXPECT_NE(root.findChild("alu"), nullptr);
  EXPECT_NE(root.findChild("mem"), nullptr);
  EXPECT_EQ(root.findChild("nonexistent"), nullptr);
}

TEST(GfsimObjectTest, ResetPropagatesToChildren) {
  Module root("root", 1);
  auto q = std::make_unique<SimQueue<int>>("q", 2, nullptr, 10);
  auto *qptr = q.get();
  root.addChild(std::move(q));
  qptr->proposePush(42);
  qptr->doArbitrate({0, 0});
  qptr->doXfer({0, 0});
  EXPECT_EQ(qptr->committedSize(), 1u);
  root.reset();
  EXPECT_EQ(qptr->committedSize(), 0u);
}

TEST(GfsimObjectTest, EmptyModuleWalkOnlySelf) {
  Module root("root", 1);
  int count = 0;
  root.walk([&](SimObject &) { ++count; });
  EXPECT_EQ(count, 1);
}

TEST(GfsimObjectTest, ChildPathReflectsHierarchy) {
  Module root("top", 1);
  auto mid = std::make_unique<Module>("mid", 2);
  auto leaf = std::make_unique<SimObject>(ObjectKind::Sink, "leaf", 3);
  mid->addChild(std::move(leaf));
  root.addChild(std::move(mid));
  // Paths are hierarchical; verify they are non-empty and distinct
  EXPECT_FALSE(root.children()[0]->path().empty());
  EXPECT_NE(root.path(), root.children()[0]->path());
}

// ═══════════════════════════════════════════════════════════════════════
// Queue
// ═══════════════════════════════════════════════════════════════════════

TEST(GfsimQueueTest, PushPopRoundTrip) {
  SimQueue<int> q("q", 1, nullptr, 10);
  EXPECT_TRUE(q.isEmpty());
  EXPECT_TRUE(q.proposePush(42));
  q.doArbitrate({0, 0});
  q.doXfer({0, 0});
  EXPECT_EQ(q.committedSize(), 1u);
  EXPECT_EQ(*q.peek(), 42);
}

TEST(GfsimQueueTest, CapacityEnforcement) {
  SimQueue<int> q("q", 1, nullptr, 2);
  EXPECT_TRUE(q.proposePush(1));
  EXPECT_TRUE(q.proposePush(2));
  EXPECT_FALSE(q.proposePush(3));
}

TEST(GfsimQueueTest, PopAfterPushRoundTrip) {
  SimQueue<int> q("q", 1, nullptr, 10);
  q.proposePush(42);
  q.doArbitrate({0, 0});
  q.doXfer({0, 0});
  auto popped = q.proposePop();
  ASSERT_TRUE(popped.has_value());
  EXPECT_EQ(*popped, 42);
  q.doArbitrate({0, 0});
  q.doXfer({0, 0});
  EXPECT_TRUE(q.isEmpty());
}

TEST(GfsimQueueTest, WatermarkTracksPeak) {
  SimQueue<int> q("q", 1, nullptr, 10);
  q.proposePush(1);
  q.proposePush(2);
  q.proposePush(3);
  q.doArbitrate({0, 0});
  q.doXfer({0, 0});
  EXPECT_EQ(q.highWatermark(), 3u);
  q.proposePop();
  q.doArbitrate({0, 0});
  q.doXfer({0, 0});
  EXPECT_EQ(q.highWatermark(), 3u); // persists after pop
  EXPECT_EQ(q.committedSize(), 2u);
}

TEST(GfsimQueueTest, MultiplePopsDrainQueue) {
  SimQueue<int> q("q", 1, nullptr, 3);
  q.proposePush(10);
  q.proposePush(20);
  q.doArbitrate({0, 0});
  q.doXfer({0, 0});
  q.proposePop();
  q.proposePop();
  q.doArbitrate({0, 0});
  q.doXfer({0, 0});
  EXPECT_TRUE(q.isEmpty());
}

TEST(GfsimQueueTest, PeekReturnsFrontWithoutConsuming) {
  SimQueue<int> q("q", 1, nullptr, 10);
  q.proposePush(7);
  q.doArbitrate({0, 0});
  q.doXfer({0, 0});
  EXPECT_EQ(*q.peek(), 7);
  EXPECT_EQ(q.committedSize(), 1u);
}

TEST(GfsimQueueTest, PopFromEmptyReturnsNullopt) {
  SimQueue<int> q("q", 1, nullptr, 10);
  auto popped = q.proposePop();
  EXPECT_FALSE(popped.has_value());
}

TEST(GfsimQueueTest, PeekFromEmptyReturnsNull) {
  SimQueue<int> q("q", 1, nullptr, 10);
  EXPECT_EQ(q.peek(), nullptr);
}

TEST(GfsimQueueTest, PendingCommitTracksAcceptedProposals) {
  SimQueue<int> queue("queue", 1, nullptr, 2);
  EXPECT_FALSE(queue.hasPendingCommit());
  ASSERT_TRUE(queue.proposePush(7));
  EXPECT_TRUE(queue.hasPendingCommit());
  queue.doXfer({0, 0});
  EXPECT_FALSE(queue.hasPendingCommit());
}

// ═══════════════════════════════════════════════════════════════════════
// EventQueue
// ═══════════════════════════════════════════════════════════════════════

TEST(GfsimEventQueueTest, EventsOrderedByEpoch) {
  EventQueue eq("events", 1, nullptr);
  EXPECT_TRUE(eq.proposeSchedule({{2, 0}, 1, 0, 0}));
  EXPECT_TRUE(eq.proposeSchedule({{1, 0}, 1, 0, 0}));
  eq.doXfer({0, 0});
  auto e = eq.popNext();
  ASSERT_TRUE(e.has_value());
  EXPECT_EQ(e->readyTime.time, 1u);
}

TEST(GfsimEventQueueTest, SameTickOrderedByDelta) {
  EventQueue eq("events", 1, nullptr);
  eq.proposeSchedule({{5, 2}, 1, 0, 0});
  eq.proposeSchedule({{5, 0}, 1, 0, 0});
  eq.proposeSchedule({{5, 1}, 1, 0, 0});
  eq.doXfer({0, 0});
  EXPECT_EQ(eq.popNext()->readyTime.delta, 0u);
  EXPECT_EQ(eq.popNext()->readyTime.delta, 1u);
  EXPECT_EQ(eq.popNext()->readyTime.delta, 2u);
}

TEST(GfsimEventQueueTest, HasEventAtChecksExactEpoch) {
  EventQueue eq("events", 1, nullptr);
  eq.proposeSchedule({{7, 0}, 1, 0, 0});
  eq.doXfer({0, 0});
  EXPECT_TRUE(eq.hasEventAt({7, 0}));
  EXPECT_FALSE(eq.hasEventAt({7, 1}));
  EXPECT_FALSE(eq.hasEventAt({8, 0}));
}

TEST(GfsimEventQueueTest, CapacityEnforcement) {
  EventQueue eq("events", 1, nullptr, 3);
  EXPECT_TRUE(eq.proposeSchedule({{1, 0}, 1, 0, 0}));
  EXPECT_TRUE(eq.proposeSchedule({{2, 0}, 1, 0, 0}));
  EXPECT_TRUE(eq.proposeSchedule({{3, 0}, 1, 0, 0}));
  EXPECT_FALSE(eq.proposeSchedule({{4, 0}, 1, 0, 0}));
}

TEST(GfsimEventQueueTest, PopNextFromEmptyReturnsNullopt) {
  EventQueue eq("events", 1, nullptr);
  EXPECT_FALSE(eq.popNext().has_value());
}

TEST(GfsimEventQueueTest, NextEventDoesNotConsume) {
  EventQueue eq("events", 1, nullptr);
  eq.proposeSchedule({{1, 0}, 1, 0, 0});
  eq.doXfer({0, 0});
  auto next = eq.nextEvent();
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(next->readyTime.time, 1u);
  auto popped = eq.popNext();
  ASSERT_TRUE(popped.has_value());
  EXPECT_EQ(popped->readyTime.time, 1u);
}

TEST(GfsimEventQueueTest, ResetClearsAllEvents) {
  EventQueue eq("events", 1, nullptr);
  eq.proposeSchedule({{1, 0}, 1, 0, 0});
  eq.proposeSchedule({{2, 0}, 1, 0, 0});
  eq.doXfer({0, 0});
  EXPECT_EQ(eq.size(), 2u);
  eq.reset();
  EXPECT_EQ(eq.size(), 0u);
  EXPECT_FALSE(eq.nextEvent().has_value());
}

// ═══════════════════════════════════════════════════════════════════════
// Resource
// ═══════════════════════════════════════════════════════════════════════

TEST(GfsimResourceTest, ReserveWithinCapacity) {
  Resource r("r", 1, nullptr, 10);
  EXPECT_TRUE(r.canReserve(5));
  EXPECT_TRUE(r.proposeReserve(1, 5, {0, 0}, 100));
  r.doArbitrate({0, 0});
  r.doXfer({0, 0});
  EXPECT_EQ(r.activeReservations(), 5u);
  EXPECT_EQ(r.availableCapacity(), 5u);
}

TEST(GfsimResourceTest, ReserveExceedsCapacityFails) {
  Resource r("r", 1, nullptr, 3);
  EXPECT_FALSE(r.proposeReserve(1, 5, {0, 0}, 100));
}

TEST(GfsimResourceTest, ReleaseAfterReserve) {
  Resource r("r", 1, nullptr, 10);
  r.proposeReserve(1, 5, {0, 0}, 100);
  r.doArbitrate({0, 0});
  r.doXfer({0, 0});
  EXPECT_EQ(r.activeReservations(), 5u);
  r.proposeRelease(1, 3);
  r.doXfer({0, 0});
  EXPECT_EQ(r.activeReservations(), 2u);
  EXPECT_EQ(r.availableCapacity(), 8u);
}

TEST(GfsimResourceTest, HighWatermarkPersistsAfterRelease) {
  Resource r("r", 1, nullptr, 10);
  r.proposeReserve(1, 7, {0, 0}, 100);
  r.doArbitrate({0, 0});
  r.doXfer({0, 0});
  EXPECT_EQ(r.highWatermark(), 7u);
  r.proposeRelease(1, 5);
  r.doXfer({0, 0});
  EXPECT_EQ(r.highWatermark(), 7u);
  EXPECT_EQ(r.activeReservations(), 2u);
}

TEST(GfsimResourceTest, MultipleReservationsUseStableOwnerOrder) {
  Resource r("r", 1, nullptr, 5);
  // Two reservers each want 3, total capacity is 5
  r.proposeReserve(10, 3, {0, 0}, 100);
  r.proposeReserve(20, 3, {0, 0}, 200);
  r.doArbitrate({0, 0});
  r.doXfer({0, 0});
  // First gets granted (3), second rejected (only 2 remaining)
  EXPECT_EQ(r.activeReservations(), 3u);
  EXPECT_EQ(r.availableCapacity(), 2u);
  EXPECT_TRUE(r.canReserve(2));
  EXPECT_FALSE(r.canReserve(3));
}

TEST(GfsimResourceTest, TotalStatisticsAccumulate) {
  Resource r("r", 1, nullptr, 10);
  r.proposeReserve(1, 3, {0, 0}, 100);
  r.doArbitrate({0, 0});
  r.doXfer({0, 0});
  r.proposeRelease(1, 1);
  r.doXfer({0, 0});
  r.proposeReserve(1, 4, {0, 0}, 200);
  r.doArbitrate({0, 0});
  r.doXfer({0, 0});
  EXPECT_EQ(r.totalReservations(), 7u);
  EXPECT_EQ(r.totalReleases(), 1u);
  EXPECT_EQ(r.activeReservations(), 6u);
}

TEST(GfsimResourceTest, ResetClearsAllState) {
  Resource r("r", 1, nullptr, 10);
  r.proposeReserve(1, 5, {0, 0}, 100);
  r.doArbitrate({0, 0});
  r.doXfer({0, 0});
  EXPECT_EQ(r.activeReservations(), 5u);
  r.reset();
  EXPECT_EQ(r.activeReservations(), 0u);
  EXPECT_EQ(r.availableCapacity(), 10u);
  EXPECT_EQ(r.highWatermark(), 0u);
  EXPECT_EQ(r.totalReservations(), 0u);
}

TEST(GfsimResourceTest, PendingCommitBeginsAfterArbitration) {
  Resource resource("resource", 1, nullptr, 1);
  ASSERT_TRUE(resource.proposeReserve(2, 1, {0, 0}, 3));
  EXPECT_FALSE(resource.hasPendingCommit());
  resource.doArbitrate({0, 0});
  EXPECT_TRUE(resource.hasPendingCommit());
  resource.doXfer({0, 0});
  EXPECT_FALSE(resource.hasPendingCommit());
}

TEST(GfsimResourceTest, ContentionUsesStableKeysNotProposalOrder) {
  for (unsigned seed = 0; seed < 32; ++seed) {
    Resource resource("resource", 1, nullptr, 5);
    struct Request {
      ObjectId owner;
      uint32_t amount;
      uint64_t transaction;
    };
    std::array requests = {Request{2, 3, 20}, Request{1, 3, 30},
                           Request{1, 2, 10}};
    std::mt19937 random(seed);
    std::shuffle(requests.begin(), requests.end(), random);
    for (const Request &request : requests)
      ASSERT_TRUE(resource.proposeReserve(request.owner, request.amount, {0, 0},
                                          request.transaction));

    resource.doArbitrate({0, 0});
    resource.doXfer({0, 0});
    EXPECT_TRUE(resource.hasReservation(10));
    EXPECT_TRUE(resource.hasReservation(30));
    EXPECT_FALSE(resource.hasReservation(20));
    EXPECT_EQ(resource.rejectedTransactions(), (std::vector<uint64_t>{20}));
    EXPECT_TRUE(resource.validate());
  }
}

TEST(GfsimResourceTest, ExplicitPriorityPrecedesStableTieBreakKeys) {
  Resource resource("resource", 1, nullptr, 1);
  ASSERT_TRUE(resource.proposeReserve({.ownerId = 1,
                                       .amount = 1,
                                       .issueTime = {0, 0},
                                       .readyTime = {1, 0},
                                       .transactionId = 10,
                                       .rootTransactionId = 10,
                                       .priority = 5}));
  ASSERT_TRUE(resource.proposeReserve({.ownerId = 2,
                                       .amount = 1,
                                       .issueTime = {0, 0},
                                       .readyTime = {1, 0},
                                       .transactionId = 20,
                                       .rootTransactionId = 20,
                                       .priority = 1}));
  resource.doArbitrate({0, 0});
  resource.doXfer({0, 0});
  EXPECT_TRUE(resource.hasReservation(20));
  EXPECT_FALSE(resource.hasReservation(10));
}

TEST(GfsimResourceTest, ReservationIdentityAndOwnerAreUnique) {
  Resource resource("resource", 1, nullptr, 4);
  ASSERT_TRUE(resource.proposeReserve(7, 2, {0, 0}, 100, {4, 0}, 55));
  EXPECT_FALSE(resource.proposeReserve(8, 1, {0, 0}, 100, {5, 0}, 66));
  resource.doArbitrate({0, 0});
  resource.doXfer({0, 0});

  const auto *reservation = resource.findReservation(100);
  ASSERT_NE(reservation, nullptr);
  EXPECT_EQ(reservation->ownerId, 7u);
  EXPECT_EQ(reservation->rootTransactionId, 55u);
  EXPECT_EQ(reservation->readyTime, (Epoch{4, 0}));
  EXPECT_TRUE(resource.validate());
}

TEST(GfsimResourceTest, ReleaseAndCancellationAreOwnerScoped) {
  Resource resource("resource", 1, nullptr, 4);
  ASSERT_TRUE(resource.proposeReserve(7, 2, {0, 0}, 100));
  ASSERT_TRUE(resource.proposeReserve(8, 2, {0, 0}, 200));
  resource.doArbitrate({0, 0});
  resource.doXfer({0, 0});

  EXPECT_FALSE(resource.proposeCancel(8, 100));
  EXPECT_TRUE(resource.proposeCancel(7, 100));
  EXPECT_FALSE(resource.proposeRelease(7, 1));
  EXPECT_TRUE(resource.proposeRelease(8, 1));
  resource.doXfer({1, 0});

  EXPECT_FALSE(resource.hasReservation(100));
  const auto *remaining = resource.findReservation(200);
  ASSERT_NE(remaining, nullptr);
  EXPECT_EQ(remaining->amount, 1u);
  EXPECT_EQ(resource.activeReservations(), 1u);
  EXPECT_TRUE(resource.validate());
}

TEST(GfsimResourceTest, ReadyReservationsUseExactEpochAndStableOrder) {
  Resource resource("resource", 1, nullptr, 3);
  ASSERT_TRUE(resource.proposeReserve(2, 1, {0, 0}, 20, {3, 1}, 20));
  ASSERT_TRUE(resource.proposeReserve(1, 1, {0, 0}, 10, {3, 1}, 10));
  resource.doArbitrate({0, 0});
  resource.doXfer({0, 0});

  EXPECT_TRUE(resource.readyReservations({3, 0}).empty());
  auto ready = resource.readyReservations({3, 1});
  ASSERT_EQ(ready.size(), 2u);
  EXPECT_EQ(ready[0]->transactionId, 10u);
  EXPECT_EQ(ready[1]->transactionId, 20u);
}

// ═══════════════════════════════════════════════════════════════════════
// Components
// ═══════════════════════════════════════════════════════════════════════

TEST(GfsimComponentsTest, ComputeTransformsInput) {
  Compute c("c", 1, nullptr);
  c.setFunction([](uint64_t x) { return x * 2; });
  c.setInput(21);
  c.doWork({0, 0});
  c.doXfer({0, 0});
  EXPECT_EQ(c.output(), 42u);
}

TEST(GfsimComponentsTest, SinkAccumulatesValues) {
  Sink s("s", 1, nullptr);
  s.receive(10);
  s.receive(20);
  s.doXfer({0, 0});
  EXPECT_EQ(s.totalReceived(), 2u);
  ASSERT_EQ(s.received().size(), 2u);
  EXPECT_EQ(s.received()[0], 10u);
  EXPECT_EQ(s.received()[1], 20u);
}

TEST(GfsimComponentsTest, MemoryReadWrite) {
  Memory m("m", 1, nullptr, 4);
  EXPECT_TRUE(m.proposeWrite(0, 42));
  EXPECT_TRUE(m.proposeWrite(2, 99));
  m.doXfer({0, 0});
  EXPECT_EQ(m.read(0), 42u);
  EXPECT_EQ(m.read(1), 0u);
  EXPECT_EQ(m.read(2), 99u);
}

TEST(GfsimComponentsTest, MemoryRejectsOutOfBoundsWrite) {
  Memory m("m", 1, nullptr, 2);
  EXPECT_TRUE(m.proposeWrite(0, 42));
  EXPECT_TRUE(m.proposeWrite(1, 43));
  EXPECT_FALSE(m.proposeWrite(2, 44));
}

TEST(GfsimComponentsTest, MemoryReadOutOfBoundsReturnsZero) {
  Memory m("m", 1, nullptr, 4);
  EXPECT_EQ(m.read(100), 0u);
}

TEST(GfsimComponentsTest, LinkForwardsValue) {
  Link l("l", 1, nullptr);
  l.forward(123);
  l.doXfer({0, 0});
  EXPECT_EQ(l.value(), 123u);
  EXPECT_TRUE(l.hasValue());
}

TEST(GfsimComponentsTest, LinkResetClearsValue) {
  Link l("l", 1, nullptr);
  l.forward(99);
  l.doXfer({0, 0});
  l.reset();
  EXPECT_EQ(l.value(), 0u);
  EXPECT_FALSE(l.hasValue());
}

TEST(GfsimComponentsTest, ComputeResetClearsOutput) {
  Compute c("c", 1, nullptr);
  c.setFunction([](uint64_t x) { return x + 1; });
  c.setInput(5);
  c.doWork({0, 0});
  c.doXfer({0, 0});
  EXPECT_EQ(c.output(), 6u);
  c.reset();
  EXPECT_EQ(c.output(), 0u);
}

TEST(GfsimComponentsTest, SinkResetClearsAll) {
  Sink s("s", 1, nullptr);
  s.receive(1);
  s.receive(2);
  s.doXfer({0, 0});
  EXPECT_EQ(s.totalReceived(), 2u);
  s.reset();
  EXPECT_EQ(s.totalReceived(), 0u);
  EXPECT_TRUE(s.received().empty());
}

// ═══════════════════════════════════════════════════════════════════════
// SimSystem
// ═══════════════════════════════════════════════════════════════════════

class RecordingDispatchObject : public SimObject {
public:
  RecordingDispatchObject(ObjectId id, std::vector<std::string> &log)
      : SimObject(ObjectKind::Compute, "object", id), log_(log) {}

  void doWork(Epoch) override {
    log_.push_back("work:" + std::to_string(id()));
  }
  void doArbitrate(Epoch) override {
    log_.push_back("arbitrate:" + std::to_string(id()));
  }
  void doXfer(Epoch) override {
    log_.push_back("xfer:" + std::to_string(id()));
  }
  void reset() override { log_.push_back("reset:" + std::to_string(id())); }
  bool validate() const { return true; }

private:
  std::vector<std::string> &log_;
};

class SelfWakingDispatchObject : public SimObject {
public:
  SelfWakingDispatchObject(ObjectId id, SimSystem &system)
      : SimObject(ObjectKind::Process, "self", id), system_(system) {}

  void doWork(Epoch epoch) override {
    ++workCount;
    system_.scheduleWork(id(), epoch);
  }
  bool validate() const { return true; }

  uint64_t workCount = 0;

private:
  SimSystem &system_;
};

class CommittingDispatchObject : public SimObject {
public:
  CommittingDispatchObject(ObjectId id, std::vector<ObjectId> &workLog,
                           bool commitOnWork)
      : SimObject(ObjectKind::Compute, "committing", id), workLog_(workLog),
        commitOnWork_(commitOnWork) {}

  void doWork(Epoch) override {
    workLog_.push_back(id());
    pending_ = commitOnWork_;
  }
  void doXfer(Epoch) override {
    if (pending_)
      ++commitCount;
    pending_ = false;
  }
  bool hasPendingCommit() const override { return pending_; }
  bool validate() const { return true; }

  uint64_t commitCount = 0;

private:
  std::vector<ObjectId> &workLog_;
  bool commitOnWork_ = false;
  bool pending_ = false;
};

class SnapshotWriter : public SimObject {
public:
  SnapshotWriter(ObjectId id, uint64_t &state)
      : SimObject(ObjectKind::Memory, "writer", id), state_(state) {}
  void doWork(Epoch) override { pending_ = true; }
  void doXfer(Epoch) override {
    state_ = 1;
    pending_ = false;
  }
  bool hasPendingCommit() const override { return pending_; }
  bool validate() const { return true; }

private:
  uint64_t &state_;
  bool pending_ = false;
};

class SnapshotReader : public SimObject {
public:
  SnapshotReader(ObjectId id, const uint64_t &state)
      : SimObject(ObjectKind::Sink, "reader", id), state_(state) {}
  void doWork(Epoch) override { observed.push_back(state_); }
  bool validate() const { return true; }

  std::vector<uint64_t> observed;

private:
  const uint64_t &state_;
};

TEST(GfsimSystemTest, StaticDispatchUsesDenseStableOrderAndBarrierPhases) {
  for (unsigned seed = 0; seed < 32; ++seed) {
    SimSystem system("test");
    std::vector<std::string> log;
    RecordingDispatchObject first(0, log);
    RecordingDispatchObject second(1, log);
    std::array rows = {makeDispatchRow(&first), makeDispatchRow(&second)};
    ASSERT_TRUE(system.setDispatchTable(rows));

    std::array<ObjectId, 2> insertionOrder = {0, 1};
    std::mt19937 random(seed);
    std::shuffle(insertionOrder.begin(), insertionOrder.end(), random);
    for (ObjectId id : insertionOrder)
      EXPECT_TRUE(system.scheduleWork(id, {0, 0}));

    system.step();
    EXPECT_EQ(log,
              (std::vector<std::string>{"work:0", "work:1", "arbitrate:0",
                                        "arbitrate:1", "xfer:0", "xfer:1"}));
  }
}

TEST(GfsimSystemTest, StaticDispatchRejectsNonDenseRows) {
  SimSystem system("test");
  std::vector<std::string> log;
  RecordingDispatchObject object(1, log);
  std::array rows = {makeDispatchRow(&object)};
  EXPECT_FALSE(system.setDispatchTable(rows));
  EXPECT_EQ(system.terminationResult().classification,
            TerminationClass::Failed);
  EXPECT_EQ(system.terminationResult().diagnosticCode,
            "invalid_dispatch_table");
}

TEST(GfsimSystemTest, StaticDispatchRejectsMismatchedObjectKind) {
  SimSystem system("test");
  std::vector<std::string> log;
  RecordingDispatchObject object(0, log);
  std::array rows = {makeDispatchRow(&object)};
  rows.front().kind = ObjectKind::Sink;
  EXPECT_FALSE(system.setDispatchTable(rows));
  EXPECT_EQ(system.terminationResult().diagnosticCode,
            "invalid_dispatch_table");
}

TEST(GfsimSystemTest, StaticDispatchResetUsesDenseThunkOrder) {
  SimSystem system("test");
  std::vector<std::string> log;
  RecordingDispatchObject first(0, log);
  RecordingDispatchObject second(1, log);
  std::array rows = {makeDispatchRow(&first), makeDispatchRow(&second)};
  ASSERT_TRUE(system.setDispatchTable(rows));
  system.reset();
  EXPECT_EQ(log, (std::vector<std::string>{"reset:0", "reset:1"}));
}

TEST(GfsimSystemTest, ActivationAdjacencyWakesOnlyDeclaredTargetsNextTick) {
  SimSystem system("test");
  std::vector<ObjectId> workLog;
  CommittingDispatchObject source(0, workLog, true);
  CommittingDispatchObject target(1, workLog, false);
  CommittingDispatchObject inactive(2, workLog, false);
  std::array rows = {makeDispatchRow(&source), makeDispatchRow(&target),
                     makeDispatchRow(&inactive)};
  constexpr std::array<uint32_t, 4> offsets = {0, 1, 1, 1};
  constexpr std::array<ObjectId, 1> targets = {1};
  ASSERT_TRUE(system.setDispatchTable(rows));
  ASSERT_TRUE(system.setActivationPlan(offsets, targets));
  ASSERT_TRUE(system.scheduleWork(0, {0, 0}));

  EXPECT_TRUE(system.step());
  EXPECT_EQ(system.currentEpoch(), (Epoch{1, 0}));
  EXPECT_EQ(workLog, (std::vector<ObjectId>{0}));
  EXPECT_EQ(source.commitCount, 1u);

  EXPECT_FALSE(system.step());
  EXPECT_EQ(workLog, (std::vector<ObjectId>{0, 1}));
  EXPECT_EQ(std::count(workLog.begin(), workLog.end(), 2), 0);
}

TEST(GfsimSystemTest, ActivationPlanRejectsNonCanonicalTargets) {
  SimSystem system("test");
  std::vector<ObjectId> workLog;
  CommittingDispatchObject first(0, workLog, false);
  CommittingDispatchObject second(1, workLog, false);
  std::array rows = {makeDispatchRow(&first), makeDispatchRow(&second)};
  constexpr std::array<uint32_t, 3> offsets = {0, 2, 2};
  constexpr std::array<ObjectId, 2> targets = {1, 1};
  ASSERT_TRUE(system.setDispatchTable(rows));
  EXPECT_FALSE(system.setActivationPlan(offsets, targets));
  EXPECT_EQ(system.terminationResult().diagnosticCode,
            "invalid_activation_plan");
}

TEST(GfsimSystemTest, WorkPhaseReadsOneImmutableCommittedSnapshot) {
  SimSystem system("test");
  uint64_t committedState = 0;
  SnapshotWriter writer(0, committedState);
  SnapshotReader reader(1, committedState);
  std::array rows = {makeDispatchRow(&writer), makeDispatchRow(&reader)};
  ASSERT_TRUE(system.setDispatchTable(rows));
  ASSERT_TRUE(system.scheduleWork(0, {0, 0}));
  ASSERT_TRUE(system.scheduleWork(1, {0, 0}));

  system.step();
  EXPECT_EQ(reader.observed, (std::vector<uint64_t>{0}));
  EXPECT_EQ(committedState, 1u);
}

TEST(GfsimSystemTest, SameTimeFutureDeltaEventRemainsPending) {
  SimSystem system("test");
  std::vector<std::string> log;
  RecordingDispatchObject object(0, log);
  std::array rows = {makeDispatchRow(&object)};
  ASSERT_TRUE(system.setDispatchTable(rows));
  ASSERT_TRUE(system.scheduleEvent({{0, 2}, 0, 0, 0}));

  EXPECT_TRUE(system.step());
  EXPECT_EQ(system.currentEpoch(), (Epoch{0, 2}));
  ASSERT_TRUE(system.nextEvent());
  EXPECT_EQ(system.nextEvent()->readyTime, (Epoch{0, 2}));

  system.step();
  EXPECT_EQ(log.front(), "work:0");
  EXPECT_FALSE(system.nextEvent());
}

TEST(GfsimSystemTest, SchedulingBeforeCommittedEpochFails) {
  SimSystem system("test");
  ASSERT_TRUE(system.scheduleEvent({{1, 0}, kSystemObjectId, 0, 0}));
  ASSERT_TRUE(system.step());
  ASSERT_EQ(system.currentEpoch(), (Epoch{1, 0}));
  EXPECT_FALSE(system.scheduleEvent({{0, 0}, kSystemObjectId, 0, 0}));
  EXPECT_TRUE(system.isTerminated());
  EXPECT_EQ(system.terminationResult().classification,
            TerminationClass::Failed);
  EXPECT_EQ(system.terminationResult().diagnosticCode,
            "event_before_current_epoch");
}

TEST(GfsimSystemTest, DeltaCycleLimitFailsAtExactBoundary) {
  SimSystem system("test");
  SelfWakingDispatchObject object(0, system);
  std::array rows = {makeDispatchRow(&object)};
  ASSERT_TRUE(system.setDispatchTable(rows));
  ASSERT_TRUE(system.scheduleWork(0, {0, 0}));

  while (system.step()) {
  }
  EXPECT_EQ(system.terminationResult().classification,
            TerminationClass::Failed);
  EXPECT_EQ(system.terminationResult().diagnosticCode, "max_deltas_exceeded");
  EXPECT_EQ(system.currentEpoch().delta, kMaxDeltasPerTick - 1);
  EXPECT_EQ(object.workCount, kMaxDeltasPerTick);
}

TEST(GfsimSystemTest, SchedulingOutOfRangeDeltaFailsImmediately) {
  SimSystem system("test");
  EXPECT_FALSE(
      system.scheduleEvent({{1, kMaxDeltasPerTick}, kSystemObjectId, 0, 0}));
  EXPECT_EQ(system.terminationResult().classification,
            TerminationClass::Failed);
  EXPECT_EQ(system.terminationResult().diagnosticCode, "max_deltas_exceeded");
}

TEST(GfsimSystemTest, SchedulingUnknownEventTargetFailsImmediately) {
  SimSystem system("test");
  EXPECT_FALSE(system.scheduleEvent({{1, 0}, 999, 0, 0}));
  EXPECT_EQ(system.terminationResult().classification,
            TerminationClass::Failed);
  EXPECT_EQ(system.terminationResult().diagnosticCode, "unknown_event_target");
}

TEST(GfsimSystemTest, EmptySystemCompletesImmediately) {
  SimSystem sys("test");
  auto result = sys.run();
  EXPECT_EQ(result.classification, TerminationClass::Completed);
  EXPECT_EQ(result.finalEpoch.time, 0u);
  EXPECT_EQ(result.finalEpoch.delta, 0u);
}

TEST(GfsimSystemTest, MaxTicksCapTriggersIncomplete) {
  SimSystem sys("test");
  sys.setMaxTicks(3);
  sys.scheduleEvent({{100, 0}, kSystemObjectId, 0, 0});
  auto result = sys.run();
  EXPECT_EQ(result.classification, TerminationClass::Incomplete);
  EXPECT_EQ(result.diagnosticCode, "max_ticks_reached");
}

TEST(GfsimSystemTest, MaxEventsCapTriggersIncomplete) {
  SimSystem sys("test");
  sys.setMaxEvents(0);
  auto result = sys.run();
  EXPECT_EQ(result.classification, TerminationClass::Incomplete);
}

TEST(GfsimSystemTest, MaxEventsCapCannotBeExceededWithinOneEpoch) {
  SimSystem system("test");
  std::vector<std::string> log;
  RecordingDispatchObject object(0, log);
  std::array rows = {makeDispatchRow(&object)};
  ASSERT_TRUE(system.setDispatchTable(rows));
  ASSERT_TRUE(system.scheduleEvent({{0, 0}, 0, 0, 0}));
  ASSERT_TRUE(system.scheduleEvent({{0, 0}, 0, 0, 1}));
  system.setMaxEvents(1);

  EXPECT_FALSE(system.step());
  EXPECT_EQ(system.terminationResult().classification,
            TerminationClass::Incomplete);
  EXPECT_EQ(system.terminationResult().diagnosticCode, "max_events_reached");
  EXPECT_EQ(system.terminationResult().committedEventCount, 1u);
  EXPECT_TRUE(system.nextEvent());
}

TEST(GfsimSystemTest, ObjectRegistryLookup) {
  SimSystem sys("test");
  sys.registerObject(&sys.root());
  EXPECT_EQ(sys.lookup(kSystemObjectId), &sys);
  EXPECT_EQ(sys.lookup(999), nullptr);
}

TEST(GfsimSystemTest, DeterministicEmptyRuns) {
  SimSystem sys1("a"), sys2("a");
  auto r1 = sys1.run();
  auto r2 = sys2.run();
  EXPECT_EQ(r1.classification, r2.classification);
  EXPECT_EQ(r1.finalEpoch, r2.finalEpoch);
}

TEST(GfsimSystemTest, ScheduleEventAndCommitThenQuery) {
  SimSystem sys("test");
  sys.scheduleEvent({{5, 0}, kSystemObjectId, 0, 0});
  // Step once to commit the event into the event queue
  bool hasWork = sys.step();
  (void)hasWork;
  auto next = sys.nextEvent();
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(next->readyTime.time, 5u);
}

TEST(GfsimSystemTest, SystemCanBeReset) {
  SimSystem sys("test");
  sys.scheduleEvent({{5, 0}, kSystemObjectId, 0, 0});
  sys.run();
  EXPECT_TRUE(sys.isTerminated());
  sys.reset();
  EXPECT_FALSE(sys.isTerminated());
  EXPECT_EQ(sys.currentEpoch(), (Epoch{0, 0}));
  EXPECT_FALSE(sys.nextEvent());
}

// ═══════════════════════════════════════════════════════════════════════
// Integration
// ═══════════════════════════════════════════════════════════════════════

TEST(GfsimIntegrationTest, ComputeToSinkPipeline) {
  auto comp = std::make_unique<Compute>("adder", 10, nullptr);
  auto *cptr = comp.get();
  cptr->setFunction([](uint64_t x) { return x * 2; });

  auto snk = std::make_unique<Sink>("sink", 11, nullptr);
  auto *sptr = snk.get();

  cptr->setInput(21);
  cptr->doWork({0, 0});
  cptr->doXfer({0, 0});
  sptr->receive(cptr->output());
  sptr->doXfer({0, 0});

  ASSERT_EQ(sptr->received().size(), 1u);
  EXPECT_EQ(sptr->received()[0], 42u);
}

TEST(GfsimIntegrationTest, QueueProducerConsumer) {
  SimQueue<int> q("fifo", 1, nullptr, 5);
  q.proposePush(10);
  q.proposePush(20);
  q.doArbitrate({0, 0});
  q.doXfer({0, 0});
  auto v1 = q.proposePop();
  auto v2 = q.proposePop();
  auto v3 = q.proposePop();
  q.doArbitrate({0, 0});
  q.doXfer({0, 0});
  ASSERT_TRUE(v1.has_value());
  EXPECT_EQ(*v1, 10);
  ASSERT_TRUE(v2.has_value());
  EXPECT_EQ(*v2, 20);
  EXPECT_FALSE(v3.has_value());
  EXPECT_TRUE(q.isEmpty());
}

TEST(GfsimIntegrationTest, ResourceProducerConsumer) {
  Resource r("r", 1, nullptr, 3);
  EXPECT_TRUE(r.proposeReserve(100, 2, {0, 0}, 1));
  r.doArbitrate({0, 0});
  r.doXfer({0, 0});
  EXPECT_EQ(r.activeReservations(), 2u);
  EXPECT_TRUE(r.proposeReserve(200, 2, {0, 0}, 2));
  r.doArbitrate({0, 0});
  r.doXfer({0, 0});
  EXPECT_FALSE(r.hasReservation(2));
  EXPECT_EQ(r.rejectedTransactions(), (std::vector<uint64_t>{2}));
  r.proposeRelease(100, 1);
  r.doXfer({0, 0});
  EXPECT_EQ(r.activeReservations(), 1u);
}

TEST(GfsimIntegrationTest, MemoryToComputeToSinkChain) {
  Memory mem("mem", 1, nullptr, 4);
  Compute comp("comp", 2, nullptr);
  Sink snk("snk", 3, nullptr);

  mem.proposeWrite(0, 5);
  mem.proposeWrite(1, 7);
  mem.doXfer({0, 0});

  comp.setFunction([](uint64_t x) { return x + 3; });
  comp.setInput(mem.read(0));
  comp.doWork({0, 0});
  comp.doXfer({0, 0});

  snk.receive(comp.output());
  snk.doXfer({0, 0});

  EXPECT_EQ(snk.received()[0], 8u);
}

} // namespace
} // namespace gfsim
