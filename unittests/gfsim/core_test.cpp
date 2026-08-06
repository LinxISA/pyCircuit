#include "gfsim/core.h"
#include "gfsim/object.h"
#include "gfsim/queue.h"
#include "gfsim/resource.h"
#include "gfsim/components.h"

#include "gtest/gtest.h"

namespace gfsim {
namespace {

// ── Core types ────────────────────────────────────────────────────────

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

TEST(GfsimCoreTest, EventOrdering) {
  Event e1{{0, 0}, 1, 0, 0};
  Event e2{{0, 1}, 1, 0, 0};
  Event e3{{1, 0}, 1, 0, 0};
  EXPECT_LT(e1, e2);
  EXPECT_LT(e2, e3);
}

// ── Object hierarchy ──────────────────────────────────────────────────

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
  EXPECT_EQ(count, 3); // root + a + b
}

// ── Queue ─────────────────────────────────────────────────────────────

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
  EXPECT_FALSE(q.proposePush(3)); // capacity exceeded
}

// ── EventQueue ────────────────────────────────────────────────────────

TEST(GfsimEventQueueTest, EventsOrderedByEpoch) {
  EventQueue eq("events", 1, nullptr);
  EXPECT_TRUE(eq.proposeSchedule({{2, 0}, 1, 0, 0}));
  EXPECT_TRUE(eq.proposeSchedule({{1, 0}, 1, 0, 0}));
  eq.doXfer({0, 0});

  auto e = eq.popNext();
  ASSERT_TRUE(e.has_value());
  EXPECT_EQ(e->readyTime.time, 1u);
}

// ── Resource ──────────────────────────────────────────────────────────

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

// ── Components ────────────────────────────────────────────────────────

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

// ── System ────────────────────────────────────────────────────────────

TEST(GfsimSystemTest, SystemRunsToCompletion) {
  SimSystem sys("test");
  auto result = sys.run();
  EXPECT_EQ(result.classification, TerminationClass::Completed);
  EXPECT_EQ(result.finalEpoch.time, 0u);
}

} // namespace
} // namespace gfsim
