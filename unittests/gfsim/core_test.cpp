#include "gfsim/components.h"
#include "gfsim/core.h"
#include "gfsim/dispatch.h"
#include "gfsim/object.h"
#include "gfsim/packet.h"
#include "gfsim/process.h"
#include "gfsim/queue.h"
#include "gfsim/resource.h"
#include "gfsim/statistics.h"
#include "gfsim/trace.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <memory>
#include <random>

namespace gfsim {

struct TestPacket {
  uint16_t opcode = 0;
  uint32_t payload = 0;
  auto operator<=>(const TestPacket &) const = default;
};

struct InvalidPacket {};

template <> struct PacketTraits<InvalidPacket> {
  static constexpr bool isPacket = true;
  static constexpr std::string_view schema = {};
  static constexpr size_t serializedSize = 1;
  static constexpr size_t maximumSerializedSize = serializedSize;
  static constexpr size_t alignment = 1;
  static constexpr PacketEndianness endianness = PacketEndianness::Little;
  static constexpr std::array<PacketField, 1> fields{{{"field", 1, 1}}};
  static constexpr std::optional<std::string_view> routingField = std::nullopt;
  static constexpr std::optional<std::string_view> correlationField =
      std::nullopt;
};

template <> struct PacketTraits<TestPacket> {
  static constexpr bool isPacket = true;
  static constexpr std::string_view schema = "test.Packet@0.1";
  static constexpr size_t serializedSize = 6;
  static constexpr size_t maximumSerializedSize = serializedSize;
  static constexpr size_t alignment = alignof(TestPacket);
  static constexpr PacketEndianness endianness = PacketEndianness::Little;
  static constexpr std::array<PacketField, 2> fields{{
      {"opcode", 0, 2},
      {"payload", 2, 4},
  }};
  static constexpr std::optional<std::string_view> routingField = std::nullopt;
  static constexpr std::optional<std::string_view> correlationField =
      std::nullopt;

  using Serialized = std::array<std::byte, serializedSize>;
  static Serialized serialize(const TestPacket &packet) {
    return {std::byte(packet.opcode),        std::byte(packet.opcode >> 8),
            std::byte(packet.payload),       std::byte(packet.payload >> 8),
            std::byte(packet.payload >> 16), std::byte(packet.payload >> 24)};
  }
  static std::optional<TestPacket>
  deserialize(std::span<const std::byte> bytes) {
    if (bytes.size() != serializedSize)
      return std::nullopt;
    return TestPacket{
        static_cast<uint16_t>(std::to_integer<uint16_t>(bytes[0]) |
                              (std::to_integer<uint16_t>(bytes[1]) << 8)),
        std::to_integer<uint32_t>(bytes[2]) |
            (std::to_integer<uint32_t>(bytes[3]) << 8) |
            (std::to_integer<uint32_t>(bytes[4]) << 16) |
            (std::to_integer<uint32_t>(bytes[5]) << 24),
    };
  }
};

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

class ResetTrackingObject final : public SimObject {
public:
  ResetTrackingObject(std::string name, ObjectId id)
      : SimObject(ObjectKind::Compute, std::move(name), id) {}

  void reset() override { ++resetCount; }

  unsigned resetCount = 0;
};

TEST(GfsimObjectTest, AttachedByValueChildSharesWalkPathAndResetBehavior) {
  Module root("top", 1);
  root.setPath("/top");
  ResetTrackingObject child("worker", 2);

  ASSERT_TRUE(root.attachChild(child));
  EXPECT_EQ(child.parent(), &root);
  EXPECT_EQ(child.path(), "/top/worker");

  std::vector<ObjectId> visited;
  root.walk([&](SimObject &object) { visited.push_back(object.id()); });
  EXPECT_EQ(visited, (std::vector<ObjectId>{1, 2}));

  root.reset();
  EXPECT_EQ(child.resetCount, 1u);
}

TEST(GfsimObjectTest, DuplicateAndCrossParentAttachmentAreRejected) {
  Module first("first", 1);
  Module second("second", 2);
  ResetTrackingObject child("worker", 3);

  ASSERT_TRUE(first.attachChild(child));
  EXPECT_FALSE(first.attachChild(child));
  EXPECT_FALSE(second.attachChild(child));
  EXPECT_EQ(child.parent(), &first);
  EXPECT_EQ(first.children(), (std::vector<SimObject *>{&child}));
  EXPECT_TRUE(second.children().empty());
}

TEST(GfsimObjectTest, ReparentingAttachedModuleRefreshesNestedPaths) {
  Module root("top", 1);
  root.setPath("/top");
  Module nested("nested", 2);
  ResetTrackingObject leaf("leaf", 3);

  ASSERT_TRUE(nested.attachChild(leaf));
  ASSERT_TRUE(root.attachChild(nested));
  EXPECT_EQ(nested.path(), "/top/nested");
  EXPECT_EQ(leaf.path(), "/top/nested/leaf");
}

TEST(GfsimObjectTest, OwnedAndAttachedChildrenShareInsertionOrder) {
  Module root("top", 1);
  root.addChild(
      std::make_unique<SimObject>(ObjectKind::Compute, "owned_first", 2));
  ResetTrackingObject attached("attached", 3);
  ASSERT_TRUE(root.attachChild(attached));
  root.addChild(std::make_unique<SimObject>(ObjectKind::Sink, "owned_last", 4));

  std::vector<ObjectId> visited;
  root.walk([&](SimObject &object) { visited.push_back(object.id()); });
  EXPECT_EQ(visited, (std::vector<ObjectId>{1, 2, 3, 4}));
}

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

TEST(GfsimObjectTest, WalkUsesModuleCapabilityRatherThanKindTag) {
  Module root("root", 1);
  root.addChild(std::make_unique<SimObject>(ObjectKind::Module, "tagged", 2));

  std::vector<ObjectId> visited;
  root.walk([&](SimObject &object) { visited.push_back(object.id()); });
  EXPECT_EQ(visited, (std::vector<ObjectId>{1, 2}));
}

TEST(GfsimObjectTest, GeneratedModuleWrappersNeedPathsButNoRuntimeObjectIds) {
  SimSystem system("system");
  Module generated("generated", kInvalidObjectId);
  ASSERT_TRUE(system.root().attachChild(generated));

  const TerminationResult result = system.run();
  EXPECT_EQ(result.classification, TerminationClass::Completed);
  EXPECT_FALSE(generated.path().empty());
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

TEST(GfsimObjectTest, ReparentingRefreshesAllNestedCanonicalPaths) {
  Module root("top", 1);
  root.setPath("/top");
  auto mid = std::make_unique<Module>("mid", 2);
  mid->addChild(
      std::make_unique<SimObject>(ObjectKind::Sink, "leaf", 3, nullptr));
  root.addChild(std::move(mid));

  ASSERT_EQ(root.children()[0]->children().size(), 1u);
  EXPECT_EQ(root.children()[0]->path(), "/top/mid");
  EXPECT_EQ(root.children()[0]->children()[0]->path(), "/top/mid/leaf");
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

TEST(GfsimResourceTest, ArbitrationKeepsRejectedStatePrivateUntilXfer) {
  Resource resource("resource", 1, nullptr, 1);
  ASSERT_TRUE(resource.proposeReserve(1, 1, {0, 0}, 10));
  ASSERT_TRUE(resource.proposeReserve(2, 1, {0, 0}, 20));

  resource.doArbitrate({0, 0});
  EXPECT_TRUE(resource.rejectedTransactions().empty());
  EXPECT_TRUE(resource.hasPendingCommit());
  resource.doXfer({0, 0});
  EXPECT_EQ(resource.rejectedTransactions(), (std::vector<uint64_t>{20}));
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

struct DoublePolicy {
  uint64_t operator()(uint64_t value) const { return value * 2; }
};

struct IncrementPolicy {
  uint64_t operator()(uint64_t value) const { return value + 1; }
};

struct AddThreePolicy {
  uint64_t operator()(uint64_t value) const { return value + 3; }
};

struct StringSizePolicy {
  size_t operator()(const std::string &value) const { return value.size(); }
};

TEST(GfsimComponentsTest, ComputeTransformsInput) {
  Compute<uint64_t, uint64_t, DoublePolicy> c("c", 1, nullptr);
  c.setInput(21);
  c.doWork({0, 0});
  c.doXfer({0, 0});
  EXPECT_EQ(c.output(), 42u);
}

TEST(GfsimComponentsTest, SinkAccumulatesValues) {
  Sink<> s("s", 1, nullptr);
  s.receive(10);
  s.receive(20);
  s.doXfer({0, 0});
  EXPECT_EQ(s.totalReceived(), 2u);
  ASSERT_EQ(s.received().size(), 2u);
  EXPECT_EQ(s.received()[0], 10u);
  EXPECT_EQ(s.received()[1], 20u);
}

TEST(GfsimComponentsTest, MemoryReadWrite) {
  Memory<> m("m", 1, nullptr, 4);
  EXPECT_TRUE(m.proposeWrite(0, 42));
  EXPECT_TRUE(m.proposeWrite(2, 99));
  m.doXfer({0, 0});
  EXPECT_EQ(m.read(0), 42u);
  EXPECT_EQ(m.read(1), 0u);
  EXPECT_EQ(m.read(2), 99u);
}

TEST(GfsimComponentsTest, MemoryRejectsOutOfBoundsWrite) {
  Memory<> m("m", 1, nullptr, 2);
  EXPECT_TRUE(m.proposeWrite(0, 42));
  EXPECT_TRUE(m.proposeWrite(1, 43));
  EXPECT_FALSE(m.proposeWrite(2, 44));
}

TEST(GfsimComponentsTest, MemoryReadOutOfBoundsReturnsZero) {
  Memory<> m("m", 1, nullptr, 4);
  EXPECT_EQ(m.read(100), 0u);
}

TEST(GfsimComponentsTest, LinkForwardsValue) {
  Link<> l("l", 1, nullptr);
  l.forward(123);
  l.doXfer({0, 0});
  EXPECT_EQ(l.value(), 123u);
  EXPECT_TRUE(l.hasValue());
}

TEST(GfsimComponentsTest, LinkResetClearsValue) {
  Link<> l("l", 1, nullptr);
  l.forward(99);
  l.doXfer({0, 0});
  l.reset();
  EXPECT_EQ(l.value(), 0u);
  EXPECT_FALSE(l.hasValue());
}

TEST(GfsimComponentsTest, ComputeResetClearsOutput) {
  Compute<uint64_t, uint64_t, IncrementPolicy> c("c", 1, nullptr);
  c.setInput(5);
  c.doWork({0, 0});
  c.doXfer({0, 0});
  EXPECT_EQ(c.output(), 6u);
  c.reset();
  EXPECT_EQ(c.output(), 0u);
}

TEST(GfsimComponentsTest, SinkResetClearsAll) {
  Sink<> s("s", 1, nullptr);
  s.receive(1);
  s.receive(2);
  s.doXfer({0, 0});
  EXPECT_EQ(s.totalReceived(), 2u);
  s.reset();
  EXPECT_EQ(s.totalReceived(), 0u);
  EXPECT_TRUE(s.received().empty());
}

static_assert(Component<TraceSource<>>);
static_assert(Component<Queue<uint64_t>>);
static_assert(Component<Scheduler<uint64_t>>);
static_assert(Component<Compute<>>);
static_assert(Component<Compute<std::string, size_t, StringSizePolicy>>);
static_assert(Component<Link<>>);
static_assert(Component<Link<std::string>>);
static_assert(Component<Memory<>>);
static_assert(Component<Memory<std::string>>);
static_assert(Component<Sink<>>);
static_assert(Component<Sink<std::string>>);
static_assert(Component<ReadyValid<uint64_t>>);
static_assert(Component<RequestResponse<uint64_t, uint64_t>>);

TEST(GfsimComponentsTest, BaselineTemplatesExposeCanonicalObjectKinds) {
  TraceSource<> trace("trace", 0, nullptr);
  Queue<uint64_t> queue("queue", 1, nullptr, 2);
  Scheduler<uint64_t> scheduler("scheduler", 2, nullptr, 2);
  Compute<> compute("compute", 3, nullptr);
  Link<> link("link", 4, nullptr);
  Memory<> memory("memory", 5, nullptr, 2);
  Sink<> sink("sink", 6, nullptr);

  EXPECT_EQ(trace.kind(), ObjectKind::TraceSource);
  EXPECT_EQ(queue.kind(), ObjectKind::Queue);
  EXPECT_EQ(scheduler.kind(), ObjectKind::Scheduler);
  EXPECT_EQ(compute.kind(), ObjectKind::Compute);
  EXPECT_EQ(link.kind(), ObjectKind::Link);
  EXPECT_EQ(memory.kind(), ObjectKind::Memory);
  EXPECT_EQ(sink.kind(), ObjectKind::Sink);
  EXPECT_EQ(TraceSource<>::contractName, "ac.std.TraceSource");
  EXPECT_EQ(Queue<uint64_t>::contractName, "ac.std.Queue");
  EXPECT_EQ(Scheduler<uint64_t>::contractName, "ac.std.Scheduler");
  EXPECT_EQ(Compute<>::contractName, "ac.std.Compute");
  EXPECT_EQ(Link<>::contractName, "ac.std.Link");
  EXPECT_EQ(Memory<>::contractName, "ac.std.Memory");
  EXPECT_EQ(Sink<>::contractName, "ac.std.Sink");
  EXPECT_EQ(ReadyValid<uint64_t>::contractName, "ac.std.ready_valid");
  EXPECT_EQ((RequestResponse<uint64_t, uint64_t>::contractName),
            "ac.std.request_response");
}

TEST(GfsimComponentsTest, QueueCommitsThroughBarrierAndTracksStatistics) {
  Queue<int> queue("queue", 1, nullptr, 2);
  EXPECT_TRUE(queue.proposePush(10));
  EXPECT_TRUE(queue.proposePush(20));
  EXPECT_FALSE(queue.proposePush(30));
  EXPECT_TRUE(queue.hasPendingCommit());

  queue.doArbitrate({0, 0});
  queue.doXfer({0, 0});
  EXPECT_EQ(queue.committedSize(), 2u);
  EXPECT_EQ(queue.totalPushes(), 2u);
  EXPECT_EQ(queue.highWatermark(), 2u);

  EXPECT_EQ(queue.proposePop(), 10);
  queue.doArbitrate({1, 0});
  queue.doXfer({1, 0});
  EXPECT_EQ(queue.totalPops(), 1u);
  ASSERT_NE(queue.peek(), nullptr);
  EXPECT_EQ(*queue.peek(), 20);
}

TEST(GfsimComponentsTest, QueueEnforcesStaticPacketByteCapacity) {
  Queue<uint32_t> queue("queue", 1, nullptr, 4, 2 * sizeof(uint32_t));
  EXPECT_TRUE(queue.proposePush(10));
  EXPECT_TRUE(queue.proposePush(20));
  EXPECT_FALSE(queue.proposePush(30));
  queue.doXfer({0, 0});

  EXPECT_EQ(queue.committedSize(), 2u);
  EXPECT_EQ(queue.committedBytes(), 2 * sizeof(uint32_t));
  EXPECT_TRUE(queue.isFull());
}

TEST(GfsimComponentsTest, LinkTreatsZeroAsACommittedValue) {
  Link<> link("link", 1, nullptr);
  link.forward(0);
  link.doXfer({0, 0});
  EXPECT_TRUE(link.hasValue());
  EXPECT_EQ(link.value(), 0u);
}

struct ScheduledValue {
  uint64_t value = 0;
  auto operator<=>(const ScheduledValue &) const = default;
};

TEST(GfsimComponentsTest, SchedulerOrderIsIndependentOfProposalInsertion) {
  struct Candidate {
    ScheduledValue value;
    uint32_t priority;
    uint32_t port;
    uint32_t instance;
    ObjectId owner;
    uint64_t transaction;
  };
  const std::array<Candidate, 4> candidates{{
      {{10}, 1, 0, 0, 3, 10},
      {{20}, 0, 1, 0, 2, 20},
      {{30}, 0, 0, 1, 4, 30},
      {{40}, 0, 0, 0, 5, 40},
  }};

  auto schedule = [&](std::array<size_t, 4> order) {
    Scheduler<ScheduledValue> scheduler("scheduler", 1, nullptr, 4);
    for (size_t index : order) {
      const Candidate &candidate = candidates[index];
      EXPECT_TRUE(scheduler.proposeSchedule(
          candidate.value, candidate.priority, candidate.port,
          candidate.instance, candidate.owner, candidate.transaction));
    }
    scheduler.doArbitrate({0, 0});
    scheduler.doXfer({0, 0});

    std::vector<uint64_t> result;
    while (auto value = scheduler.proposePop())
      result.push_back(value->value);
    scheduler.doXfer({1, 0});
    return result;
  };

  EXPECT_EQ(schedule({0, 1, 2, 3}), (std::vector<uint64_t>{40, 30, 20, 10}));
  EXPECT_EQ(schedule({2, 0, 3, 1}), (std::vector<uint64_t>{40, 30, 20, 10}));
}

TEST(GfsimComponentsTest, SchedulerAppliesFiniteCapacityAfterArbitration) {
  Scheduler<uint64_t> scheduler("scheduler", 1, nullptr, 2);
  EXPECT_TRUE(scheduler.proposeSchedule(30, 2, 0, 0, 3, 30));
  EXPECT_TRUE(scheduler.proposeSchedule(10, 0, 0, 0, 1, 10));
  EXPECT_TRUE(scheduler.proposeSchedule(20, 1, 0, 0, 2, 20));
  scheduler.doArbitrate({0, 0});
  EXPECT_TRUE(scheduler.hasPendingCommit());
  scheduler.doXfer({0, 0});

  EXPECT_EQ(scheduler.size(), 2u);
  EXPECT_EQ(scheduler.rejectedTransactions(), (std::vector<uint64_t>{30}));
  EXPECT_EQ(scheduler.proposePop(), 10u);
  EXPECT_EQ(scheduler.proposePop(), 20u);
  EXPECT_EQ(scheduler.proposePop(), std::nullopt);
}

TEST(GfsimComponentsTest, SchedulerPreservesFifoAcrossCommittedEpochs) {
  Scheduler<uint64_t> scheduler("scheduler", 1, nullptr, 2);
  EXPECT_TRUE(scheduler.proposeSchedule(90, 0, 0, 0, 9, 90));
  scheduler.doArbitrate({0, 0});
  scheduler.doXfer({0, 0});

  EXPECT_TRUE(scheduler.proposeSchedule(10, 0, 0, 0, 1, 10));
  scheduler.doArbitrate({1, 0});
  scheduler.doXfer({1, 0});

  EXPECT_EQ(scheduler.proposePop(), 90u);
  EXPECT_EQ(scheduler.proposePop(), 10u);
}

TEST(GfsimComponentsTest, SchedulerRejectsDuplicateIdentityAndResets) {
  Scheduler<uint64_t> scheduler("scheduler", 1, nullptr, 2);
  EXPECT_TRUE(scheduler.proposeSchedule(10, 0, 0, 0, 7, 99));
  EXPECT_FALSE(scheduler.proposeSchedule(20, 0, 0, 0, 7, 99));
  scheduler.doArbitrate({0, 0});
  scheduler.doXfer({0, 0});
  EXPECT_EQ(scheduler.size(), 1u);

  scheduler.reset();
  EXPECT_EQ(scheduler.size(), 0u);
  EXPECT_TRUE(scheduler.rejectedTransactions().empty());
  EXPECT_TRUE(scheduler.proposeSchedule(20, 0, 0, 0, 7, 99));
}

// ═══════════════════════════════════════════════════════════════════════
// Protocols and packets
// ═══════════════════════════════════════════════════════════════════════

static_assert(Packet<TestPacket>);
static_assert(!Packet<uint64_t>);
static_assert(!Packet<InvalidPacket>);

TEST(GfsimPacketTest, FixedPacketRoundTripsWithStableReflection) {
  const TestPacket packet{0x1234, 0x89abcdef};
  const auto bytes = serializePacket(packet);
  EXPECT_EQ(bytes, (PacketTraits<TestPacket>::Serialized{
                       std::byte{0x34}, std::byte{0x12}, std::byte{0xef},
                       std::byte{0xcd}, std::byte{0xab}, std::byte{0x89}}));
  EXPECT_EQ(deserializePacket<TestPacket>(bytes), packet);
  EXPECT_EQ(PacketTraits<TestPacket>::maximumSerializedSize, bytes.size());
  EXPECT_EQ(PacketTraits<TestPacket>::endianness, PacketEndianness::Little);
  ASSERT_EQ(PacketTraits<TestPacket>::fields.size(), 2u);
  EXPECT_EQ(PacketTraits<TestPacket>::fields[0], (PacketField{"opcode", 0, 2}));
  EXPECT_EQ(PacketTraits<TestPacket>::fields[1],
            (PacketField{"payload", 2, 4}));
}

TEST(GfsimPacketTest, DeserializationRejectsWrongSerializedSize) {
  const std::array<std::byte, 5> shortBytes{};
  EXPECT_EQ(deserializePacket<TestPacket>(shortBytes), std::nullopt);
}

TEST(GfsimPacketTest, QueueUsesSerializedPacketSizeInsteadOfNativeLayout) {
  static_assert(sizeof(TestPacket) > PacketTraits<TestPacket>::serializedSize);
  Queue<TestPacket> queue("packets", 1, nullptr, 2,
                          PacketTraits<TestPacket>::serializedSize);
  EXPECT_TRUE(queue.proposePush({1, 2}));
  EXPECT_FALSE(queue.proposePush({3, 4}));
  queue.doXfer({0, 0});
  EXPECT_EQ(queue.committedBytes(), PacketTraits<TestPacket>::serializedSize);
}

TEST(GfsimProtocolTest, ReadyValidRetainsOfferAndTransfersExactlyOnce) {
  ReadyValid<uint64_t> channel("channel", 1, nullptr);
  EXPECT_TRUE(channel.proposeOffer(7));
  EXPECT_FALSE(channel.proposeOffer(8));
  channel.doXfer({0, 0});

  ASSERT_NE(channel.peekOffer(), nullptr);
  EXPECT_EQ(*channel.peekOffer(), 7u);
  EXPECT_EQ(channel.transferCount(), 0u);

  channel.proposeReady(true);
  channel.doXfer({1, 0});
  EXPECT_EQ(channel.peekOffer(), nullptr);
  EXPECT_EQ(channel.lastTransferred(), 7u);
  EXPECT_EQ(channel.transferCount(), 1u);

  channel.proposeReady(true);
  channel.doXfer({2, 0});
  EXPECT_EQ(channel.transferCount(), 1u);
  EXPECT_TRUE(channel.validate());
}

TEST(GfsimProtocolTest, RequestResponseEnforcesBoundsAndCorrelation) {
  RequestResponse<uint64_t, uint64_t> channel("channel", 1, nullptr, 2);
  EXPECT_TRUE(channel.proposeRequest(11, 101));
  EXPECT_TRUE(channel.proposeRequest(22, 102));
  EXPECT_FALSE(channel.proposeRequest(33, 103));
  EXPECT_FALSE(channel.proposeRequest(44, 101));
  channel.doXfer({0, 0});
  EXPECT_EQ(channel.inFlight(), 2u);

  auto first = channel.proposePopRequest();
  auto second = channel.proposePopRequest();
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->payload, 11u);
  EXPECT_EQ(first->correlationId, 101u);
  EXPECT_EQ(second->payload, 22u);
  EXPECT_EQ(second->correlationId, 102u);
  channel.doXfer({1, 0});

  EXPECT_FALSE(channel.proposeResponse(999, 999));
  EXPECT_TRUE(channel.proposeResponse(111, 101));
  EXPECT_FALSE(channel.proposeResponse(222, 101));
  channel.doXfer({2, 0});
  EXPECT_EQ(channel.inFlight(), 1u);
  EXPECT_EQ(channel.totalCompleted(), 1u);

  auto response = channel.proposePopResponse();
  ASSERT_TRUE(response.has_value());
  EXPECT_EQ(response->payload, 111u);
  EXPECT_EQ(response->correlationId, 101u);
  channel.doXfer({3, 0});
  EXPECT_FALSE(channel.proposePopResponse().has_value());
  EXPECT_TRUE(channel.validate());
}

TEST(GfsimProtocolTest, ProtocolStatePreservesCreditAndPhaseInvariants) {
  ProtocolState state(2);
  EXPECT_TRUE(state.validate());
  EXPECT_TRUE(state.startRequest());
  EXPECT_TRUE(state.startRequest());
  EXPECT_FALSE(state.startRequest());
  EXPECT_EQ(state.credits(), 0u);
  EXPECT_EQ(state.inFlight(), 2u);

  EXPECT_TRUE(state.beginResponse());
  EXPECT_TRUE(state.completeResponse());
  EXPECT_EQ(state.phase(), ProtocolPhase::Transfer);
  EXPECT_EQ(state.credits(), 1u);
  EXPECT_EQ(state.inFlight(), 1u);

  EXPECT_TRUE(state.setBackpressure(true));
  EXPECT_EQ(state.phase(), ProtocolPhase::Backpressure);
  EXPECT_FALSE(state.startRequest());
  EXPECT_TRUE(state.setBackpressure(false));
  EXPECT_EQ(state.phase(), ProtocolPhase::Transfer);

  EXPECT_TRUE(state.beginResponse());
  EXPECT_TRUE(state.completeResponse());
  EXPECT_EQ(state.phase(), ProtocolPhase::Idle);
  EXPECT_EQ(state.credits(), 2u);
  EXPECT_EQ(state.inFlight(), 0u);
  EXPECT_FALSE(state.completeResponse());
  EXPECT_TRUE(state.validate());
}

// ═══════════════════════════════════════════════════════════════════════
// PTO trace streaming
// ═══════════════════════════════════════════════════════════════════════

constexpr std::string_view ValidPtoTrace = R"json({
  "schema": "pto-trace",
  "version": "0.1",
  "contract_epoch": "0.1",
  "metadata": {
    "producer": "gfsim-test",
    "address_spaces": ["global"],
    "record_count": 2
  },
  "records": [
    {
      "sequence_id": 10,
      "opcode": "pto.load",
      "operands": [{"kind": "buffer", "id": "A"}],
      "dependencies": [],
      "attributes": {"width": 32},
      "issue_time": 2
    },
    {
      "sequence_id": 20,
      "opcode": "pto.store",
      "operands": [
        {"kind": "address", "space": "global", "value": 64},
        {"kind": "record_result", "sequence_id": 10, "result_index": 0}
      ],
      "dependencies": [10],
      "attributes": {"ordered": true}
    }
  ]
})json";

TEST(GfsimTraceTest, ParsesClosedTypedPtoTraceDocument) {
  TraceLoadResult result = parsePtoTrace(ValidPtoTrace);
  ASSERT_TRUE(result.succeeded()) << result.primaryDiagnostic();
  ASSERT_TRUE(result.document.has_value());
  EXPECT_EQ(result.document->metadata.producer, "gfsim-test");
  ASSERT_EQ(result.document->records.size(), 2u);
  EXPECT_EQ(result.document->records[0].sequenceId, 10u);
  EXPECT_EQ(result.document->records[0].opcode, "pto.load");
  EXPECT_EQ(result.document->records[0].issueTime, 2u);
  ASSERT_EQ(result.document->records[1].operands.size(), 2u);
  EXPECT_EQ(result.document->records[1].operands[0].kind,
            PtoOperandKind::Address);
  EXPECT_EQ(result.document->records[1].dependencies,
            (std::vector<uint64_t>{10}));
}

TEST(GfsimTraceTest, StreamingAndBufferedParsingProduceIdenticalDocument) {
  TraceLoadResult buffered = parsePtoTrace(ValidPtoTrace);
  ASSERT_TRUE(buffered.succeeded());

  PtoTraceStream stream;
  for (size_t offset = 0; offset < ValidPtoTrace.size(); offset += 17)
    ASSERT_TRUE(stream.append(ValidPtoTrace.substr(
        offset, std::min<size_t>(17, ValidPtoTrace.size() - offset))));
  TraceLoadResult streamed = stream.finish();
  ASSERT_TRUE(streamed.succeeded()) << streamed.primaryDiagnostic();
  EXPECT_EQ(streamed.document, buffered.document);
}

TEST(GfsimTraceTest, RejectsForwardDependencyWithStableDiagnostic) {
  constexpr std::string_view invalid = R"json({
    "schema":"pto-trace","version":"0.1","contract_epoch":"0.1",
    "metadata":{},"records":[
      {"sequence_id":1,"opcode":"pto.a","operands":[],
       "dependencies":[2],"attributes":{}},
      {"sequence_id":2,"opcode":"pto.b","operands":[],
       "dependencies":[],"attributes":{}}
    ]})json";
  TraceLoadResult result = parsePtoTrace(invalid);
  ASSERT_FALSE(result.succeeded());
  ASSERT_FALSE(result.diagnostics.empty());
  EXPECT_EQ(result.diagnostics.front().code, "ACTRACE-DEPENDENCY");
  EXPECT_EQ(result.diagnostics.front().jsonPointer,
            "/records/0/dependencies/0");
  EXPECT_EQ(result.diagnostics.front().sequenceId, 1u);
}

TEST(GfsimTraceTest, RejectsUnknownFieldsAndRepresentationCaps) {
  constexpr std::string_view unknown = R"json({
    "schema":"pto-trace","version":"0.1","contract_epoch":"0.1",
    "metadata":{"extension":true},"records":[]})json";
  TraceLoadResult closed = parsePtoTrace(unknown);
  ASSERT_FALSE(closed.succeeded());
  EXPECT_EQ(closed.diagnostics.front().code, "ACTRACE-SCHEMA");
  EXPECT_EQ(closed.diagnostics.front().jsonPointer, "/metadata/extension");

  TraceValidationLimits limits;
  limits.maxDocumentBytes = 32;
  TraceLoadResult capped = parsePtoTrace(ValidPtoTrace, limits);
  ASSERT_FALSE(capped.succeeded());
  EXPECT_EQ(capped.diagnostics.front().code, "ACTRACE-LIMIT");
}

TEST(GfsimTraceTest, ValidatesDuplicateKeysRecordCountAndContentHash) {
  constexpr std::string_view validEmpty = R"json({
    "schema":"pto-trace","version":"0.1","contract_epoch":"0.1",
    "metadata":{
      "record_count":0,
      "content_hash":"sha256:4f53cda18c2baa0c0354bb5f9a3ecbe5ed12ab4d8e11ba873c2f11161202b945"
    },"records":[]})json";
  EXPECT_TRUE(parsePtoTrace(validEmpty).succeeded());

  constexpr std::string_view wrongCount = R"json({
    "schema":"pto-trace","version":"0.1","contract_epoch":"0.1",
    "metadata":{"record_count":1},"records":[]})json";
  TraceLoadResult count = parsePtoTrace(wrongCount);
  ASSERT_FALSE(count.succeeded());
  EXPECT_EQ(count.diagnostics.front().code, "ACTRACE-METADATA");

  constexpr std::string_view duplicateKey = R"json({
    "schema":"pto-trace","schema":"pto-trace","version":"0.1",
    "contract_epoch":"0.1","metadata":{},"records":[]})json";
  TraceLoadResult duplicate = parsePtoTrace(duplicateKey);
  ASSERT_FALSE(duplicate.succeeded());
  EXPECT_EQ(duplicate.diagnostics.front().code, "ACTRACE-JSON");
}

struct DecodedTraceTransaction {
  uint64_t sequenceId;
  std::string opcode;
  auto operator<=>(const DecodedTraceTransaction &) const = default;
};

struct CountingTraceDecoder {
  size_t *decodeCount;
  std::optional<DecodedTraceTransaction>
  operator()(const PtoTraceRecord &record) const {
    ++*decodeCount;
    return DecodedTraceTransaction{record.sequenceId, record.opcode};
  }
};

TEST(GfsimTraceTest, CursorAdvancesOnlyOnAcceptedXferAndHonorsDependencies) {
  TraceLoadResult loaded = parsePtoTrace(ValidPtoTrace);
  ASSERT_TRUE(loaded.succeeded());
  size_t decodeCount = 0;
  TraceSource<DecodedTraceTransaction, CountingTraceDecoder> source(
      "trace", 1, nullptr, std::move(*loaded.document), {&decodeCount});

  source.doWork({2, 0});
  source.doXfer({2, 0});
  ASSERT_NE(source.peekOffer(), nullptr);
  EXPECT_EQ(source.peekOffer()->sequenceId, 10u);
  EXPECT_EQ(source.position().nextRecordIndex, 0u);
  EXPECT_FALSE(source.position().lastCommittedSequenceId.has_value());
  EXPECT_EQ(decodeCount, 1u);

  source.doWork({3, 0});
  source.doWork({3, 1});
  EXPECT_EQ(source.peekOffer()->sequenceId, 10u);
  EXPECT_EQ(decodeCount, 1u);
  ASSERT_TRUE(source.proposeAccept());
  source.doXfer({3, 1});
  EXPECT_EQ(source.position().nextRecordIndex, 1u);
  EXPECT_EQ(source.position().lastCommittedSequenceId, 10u);
  EXPECT_FALSE(source.eof());

  source.doWork({4, 0});
  source.doXfer({4, 0});
  EXPECT_EQ(source.peekOffer(), nullptr);
  EXPECT_EQ(decodeCount, 1u);
  EXPECT_TRUE(source.markDependencyComplete(10));
  source.doWork({4, 1});
  source.doXfer({4, 1});
  ASSERT_NE(source.peekOffer(), nullptr);
  EXPECT_EQ(source.peekOffer()->sequenceId, 20u);
  EXPECT_EQ(decodeCount, 2u);

  ASSERT_TRUE(source.proposeAccept());
  source.doXfer({5, 0});
  EXPECT_TRUE(source.eof());
  EXPECT_EQ(source.position().nextRecordIndex, 2u);
  EXPECT_EQ(source.position().lastCommittedSequenceId, 20u);
  EXPECT_TRUE(source.validate());
}

TEST(GfsimTraceTest, IssueTimeSchedulesAnExactWakeInsteadOfPolling) {
  TraceLoadResult loaded = parsePtoTrace(ValidPtoTrace);
  ASSERT_TRUE(loaded.succeeded());
  SimSystem system;
  auto source = std::make_unique<TraceSource<>>(
      "trace", 0, nullptr, std::move(*loaded.document),
      IdentityTraceDecoder<PtoTraceRecord>{}, &system);
  TraceSource<> *sourcePtr = source.get();
  system.registerObject(sourcePtr);
  system.root().addChild(std::move(source));

  TerminationResult result = system.run();
  EXPECT_EQ(result.finalEpoch, (Epoch{2, 0}));
  EXPECT_EQ(result.classification, TerminationClass::Failed);
  EXPECT_EQ(result.diagnosticCode, "no_progress");
  ASSERT_NE(sourcePtr->peekOffer(), nullptr);
  EXPECT_EQ(sourcePtr->peekOffer()->sequenceId, 10u);
  EXPECT_EQ(sourcePtr->position().nextRecordIndex, 0u);
}

// ═══════════════════════════════════════════════════════════════════════
// Statistics, diagnostics, and termination
// ═══════════════════════════════════════════════════════════════════════

TEST(GfsimStatisticsTest, CounterGaugeAndHistogramCommitDeterministically) {
  Statistic counter("accepted", 1, nullptr, StatisticKind::Counter);
  EXPECT_TRUE(counter.proposeAdd(2));
  EXPECT_TRUE(counter.proposeAdd(3));
  counter.doXfer({4, 0});
  StatSnapshot counterSnapshot = counter.snapshot();
  EXPECT_EQ(counterSnapshot.value, 5u);
  EXPECT_EQ(counterSnapshot.lastUpdate, (Epoch{4, 0}));

  Statistic gauge("occupancy", 2, nullptr, StatisticKind::Gauge);
  EXPECT_TRUE(gauge.proposeSet(7));
  EXPECT_FALSE(gauge.proposeSet(8));
  gauge.doXfer({4, 0});
  EXPECT_EQ(gauge.snapshot().value, 7u);

  Statistic histogram("latency", 3, nullptr, {10, 20});
  EXPECT_TRUE(histogram.proposeObserve(25));
  EXPECT_TRUE(histogram.proposeObserve(5));
  EXPECT_TRUE(histogram.proposeObserve(15));
  histogram.doXfer({5, 0});
  StatSnapshot histogramSnapshot = histogram.snapshot();
  EXPECT_EQ(histogramSnapshot.count, 3u);
  EXPECT_EQ(histogramSnapshot.sum, 45u);
  EXPECT_EQ(histogramSnapshot.minimum, 5u);
  EXPECT_EQ(histogramSnapshot.maximum, 25u);
  EXPECT_EQ(histogramSnapshot.buckets,
            (std::vector<HistogramBucket>{{10, 1}, {20, 1}, {UINT64_MAX, 1}}));
}

TEST(GfsimStatisticsTest, SystemSnapshotsAreStableByPathAndName) {
  SimSystem system;
  Statistic second("zeta", 2, nullptr, StatisticKind::Counter);
  Statistic first("alpha", 1, nullptr, StatisticKind::Gauge);
  second.setPath("/system/zeta");
  first.setPath("/system/alpha");
  ASSERT_TRUE(second.proposeAdd(2));
  ASSERT_TRUE(first.proposeSet(1));
  second.doXfer({1, 0});
  first.doXfer({1, 0});
  system.registerObject(&second);
  system.registerObject(&first);

  std::vector<StatSnapshot> snapshots = system.statistics();
  ASSERT_EQ(snapshots.size(), 2u);
  EXPECT_EQ(snapshots[0].objectPath, "/system/alpha");
  EXPECT_EQ(snapshots[0].name, "alpha");
  EXPECT_EQ(snapshots[1].objectPath, "/system/zeta");
  EXPECT_EQ(snapshots[1].name, "zeta");
}

TEST(GfsimStatisticsTest, QueueSnapshotsUseFrozenNamesAndKinds) {
  Queue<uint64_t> queue("queue", 1, nullptr, 2);
  queue.setPath("/system/queue");
  ASSERT_TRUE(queue.proposePush(7));
  queue.doXfer({3, 0});
  std::vector<StatSnapshot> snapshots;
  queue.collectStatistics(snapshots);
  ASSERT_EQ(snapshots.size(), 4u);

  auto find = [&](std::string_view name) -> const StatSnapshot * {
    auto position = std::ranges::find(snapshots, name, &StatSnapshot::name);
    return position == snapshots.end() ? nullptr : &*position;
  };
  ASSERT_NE(find("queue_occupancy"), nullptr);
  EXPECT_EQ(find("queue_occupancy")->kind, StatisticKind::Gauge);
  ASSERT_NE(find("queue_occupancy_peak"), nullptr);
  EXPECT_EQ(find("queue_occupancy_peak")->kind, StatisticKind::Gauge);
  ASSERT_NE(find("accepted_transactions"), nullptr);
  EXPECT_EQ(find("accepted_transactions")->kind, StatisticKind::Counter);
  ASSERT_NE(find("completed_transactions"), nullptr);
  EXPECT_EQ(find("completed_transactions")->kind, StatisticKind::Counter);
}

TEST(GfsimSystemTest, NonEmptyQueueWithoutWakeFailsWithNoProgressReport) {
  SimSystem system;
  Queue<uint64_t> queue("queue", 1, nullptr, 2);
  queue.setPath("/system/queue");
  ASSERT_TRUE(queue.proposePush(7));
  queue.doXfer({0, 0});
  system.registerObject(&queue);

  TerminationResult result = system.run();
  EXPECT_EQ(result.classification, TerminationClass::Failed);
  EXPECT_EQ(result.diagnosticCode, "no_progress");
  NoProgressReport report = system.noProgressReport();
  ASSERT_EQ(report.blockedObjects.size(), 1u);
  EXPECT_EQ(report.blockedObjects[0].id, 1u);
  EXPECT_EQ(report.blockedObjects[0].reason, "queue_not_empty");
  EXPECT_EQ(report.queueOccupancy, 1u);
}

TEST(GfsimSystemTest, TraceLimitIsIncompleteAndReportsExactPosition) {
  TraceLoadResult loaded = parsePtoTrace(ValidPtoTrace);
  ASSERT_TRUE(loaded.succeeded());
  SimSystem system;
  TraceSource<> source("trace", 1, nullptr, std::move(*loaded.document));
  source.setPath("/system/trace");
  system.registerObject(&source);
  system.setMaxTraceRecords(0);

  TerminationResult result = system.run();
  EXPECT_EQ(result.classification, TerminationClass::Incomplete);
  EXPECT_EQ(result.diagnosticCode, "max_trace_records_reached");
  EXPECT_EQ(result.tracePosition, 0u);
  EXPECT_EQ(result.terminationCap, 0u);
}

TEST(GfsimSystemTest, ExhaustedTraceCompletesWithCommittedCursorIdentity) {
  constexpr std::string_view oneRecord = R"json({
    "schema":"pto-trace","version":"0.1","contract_epoch":"0.1",
    "metadata":{"record_count":1},"records":[{
      "sequence_id":9,"opcode":"pto.done","operands":[],
      "dependencies":[],"attributes":{}}]})json";
  TraceLoadResult loaded = parsePtoTrace(oneRecord);
  ASSERT_TRUE(loaded.succeeded());
  TraceSource<> source("trace", 1, nullptr, std::move(*loaded.document));
  source.doWork({0, 0});
  source.doXfer({0, 0});
  ASSERT_TRUE(source.proposeAccept());
  source.doXfer({1, 0});
  ASSERT_TRUE(source.eof());

  SimSystem system;
  source.setPath("/system/trace");
  system.registerObject(&source);
  TerminationResult result = system.run();
  EXPECT_EQ(result.classification, TerminationClass::Completed);
  EXPECT_EQ(result.tracePosition, 1u);
  EXPECT_EQ(result.traceLastCommittedSequenceId, 9u);
  EXPECT_TRUE(system.noProgressReport().empty());
}

TEST(GfsimSystemTest, MultipleTraceCursorOwnersFailDeterministically) {
  TraceLoadResult firstDocument = parsePtoTrace(ValidPtoTrace);
  TraceLoadResult secondDocument = parsePtoTrace(ValidPtoTrace);
  ASSERT_TRUE(firstDocument.succeeded());
  ASSERT_TRUE(secondDocument.succeeded());
  TraceSource<> first("first", 1, nullptr, std::move(*firstDocument.document));
  TraceSource<> second("second", 2, nullptr,
                       std::move(*secondDocument.document));
  SimSystem system;
  system.registerObject(&first);
  system.registerObject(&second);

  TerminationResult result = system.run();
  EXPECT_EQ(result.classification, TerminationClass::Failed);
  EXPECT_EQ(result.diagnosticCode, "multiple_trace_owners");
}

TEST(GfsimSystemTest,
     NoProgressReportIncludesReservationsProtocolsAndCorrelations) {
  SimSystem system;
  Resource resource("resource", 1, nullptr, 1);
  ReadyValid<uint64_t> readyValid("ready_valid", 2, nullptr);
  RequestResponse<uint64_t, uint64_t> requestResponse("request_response", 3,
                                                      nullptr, 1);
  resource.setPath("/system/resource");
  readyValid.setPath("/system/ready_valid");
  requestResponse.setPath("/system/request_response");

  ASSERT_TRUE(resource.proposeReserve(9, 1, {0, 0}, 101));
  resource.doArbitrate({0, 0});
  resource.doXfer({0, 0});
  ASSERT_TRUE(readyValid.proposeOffer(5));
  readyValid.doXfer({0, 0});
  ASSERT_TRUE(requestResponse.proposeRequest(6, 202));
  requestResponse.doXfer({0, 0});
  system.registerObject(&resource);
  system.registerObject(&readyValid);
  system.registerObject(&requestResponse);

  TerminationResult result = system.run();
  EXPECT_EQ(result.diagnosticCode, "no_progress");
  NoProgressReport report = system.noProgressReport();
  ASSERT_EQ(report.blockedObjects.size(), 3u);
  EXPECT_EQ(report.activeReservations, 1u);
  EXPECT_EQ(report.pendingOffers, 2u);
  EXPECT_EQ(report.blockedObjects[0].reason, "resource_reservation_live");
  EXPECT_EQ(report.blockedObjects[1].protocolState, "backpressure");
  EXPECT_EQ(report.blockedObjects[2].correlationChain,
            (std::vector<uint64_t>{202}));
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

class RepeatedCommitDispatchObject : public SimObject {
public:
  RepeatedCommitDispatchObject(ObjectId id, SimSystem &system)
      : SimObject(ObjectKind::Memory, "repeated", id), system_(system) {}

  void doWork(Epoch epoch) override {
    pending_ = true;
    system_.scheduleWork(id(), epoch);
  }
  void doXfer(Epoch) override { pending_ = false; }
  bool hasPendingCommit() const override { return pending_; }
  bool validate() const { return true; }

private:
  SimSystem &system_;
  bool pending_ = false;
};

class SameTickEventChainObject : public SimObject {
public:
  SameTickEventChainObject(ObjectId id, SimSystem &system)
      : SimObject(ObjectKind::Process, "event_chain", id), system_(system) {}

  void doWork(Epoch epoch) override {
    system_.scheduleEvent({epoch, id(), 1, eventCount++});
  }
  bool validate() const { return true; }

private:
  SimSystem &system_;
  uint64_t eventCount = 0;
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

TEST(GfsimSystemTest, RegistryAndDispatchCannotShadowOneStableObjectId) {
  SimSystem system("test");
  std::vector<std::string> log;
  RecordingDispatchObject registered(0, log);
  RecordingDispatchObject dispatched(0, log);
  system.registerObject(&registered);
  std::array rows = {makeDispatchRow(&dispatched)};
  ASSERT_TRUE(system.setDispatchTable(rows));

  TerminationResult result = system.run();
  EXPECT_EQ(result.classification, TerminationClass::Failed);
  EXPECT_EQ(result.diagnosticCode, "duplicate_object_identity");
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

TEST(GfsimSystemTest, StatefulObjectCannotCommitTwiceInOneTick) {
  SimSystem system("test");
  RepeatedCommitDispatchObject object(0, system);
  std::array rows = {makeDispatchRow(&object)};
  ASSERT_TRUE(system.setDispatchTable(rows));
  ASSERT_TRUE(system.scheduleWork(0, {0, 0}));

  EXPECT_TRUE(system.step());
  EXPECT_EQ(system.currentEpoch(), (Epoch{0, 1}));
  EXPECT_FALSE(system.step());
  EXPECT_EQ(system.terminationResult().classification,
            TerminationClass::Failed);
  EXPECT_EQ(system.terminationResult().diagnosticCode,
            "multiple_stateful_commits");
}

TEST(GfsimSystemTest, EventQueueCannotCommitTwiceInOneTick) {
  SimSystem system("test");
  SameTickEventChainObject object(0, system);
  std::array rows = {makeDispatchRow(&object)};
  ASSERT_TRUE(system.setDispatchTable(rows));
  ASSERT_TRUE(system.scheduleWork(0, {0, 0}));

  EXPECT_TRUE(system.step());
  EXPECT_EQ(system.currentEpoch(), (Epoch{0, 1}));
  EXPECT_FALSE(system.step());
  EXPECT_EQ(system.terminationResult().diagnosticCode,
            "multiple_stateful_commits");
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

TEST(GfsimSystemTest, MaxTicksCapDoesNotJumpToAFutureEvent) {
  SimSystem system("test");
  system.setMaxTicks(3);
  ASSERT_TRUE(system.scheduleEvent({{100, 0}, kSystemObjectId, 0, 0}));

  TerminationResult result = system.run();
  EXPECT_EQ(result.classification, TerminationClass::Incomplete);
  EXPECT_EQ(result.finalEpoch, (Epoch{3, 0}));
  EXPECT_EQ(result.terminationCap, 3u);
  ASSERT_TRUE(system.nextEvent());
  EXPECT_EQ(system.nextEvent()->readyTime, (Epoch{100, 0}));
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

TEST(GfsimSystemTest, ResetIncludesRegisteredObjectsOutsideTheRootTree) {
  SimSystem system("test");
  Statistic statistic("counter", 1, nullptr, StatisticKind::Counter);
  ASSERT_TRUE(statistic.proposeAdd(4));
  statistic.doXfer({0, 0});
  system.registerObject(&statistic);
  ASSERT_EQ(statistic.snapshot().value, 4u);

  system.reset();
  EXPECT_EQ(statistic.snapshot().value, 0u);
}

// ═══════════════════════════════════════════════════════════════════════
// Process runtime
// ═══════════════════════════════════════════════════════════════════════

class SuspendingProcess final : public ProcessRuntime<SuspendingProcess> {
public:
  SuspendingProcess() : ProcessRuntime("process", 0, nullptr, 0, 4) {}

  ProcessStep executeProcessStep(uint32_t pc, Epoch) {
    ++stepCount;
    switch (pc) {
    case 0:
      return ProcessStep::continueAt(1);
    case 1:
      return ProcessStep::suspendAt(
          2, {.kind = ProcessWakeKind::EventQueue, .id = 7}, 42);
    default:
      return ProcessStep::terminate();
    }
  }

  uint64_t stepCount = 0;
};

class UnboundedProcess final : public ProcessRuntime<UnboundedProcess> {
public:
  UnboundedProcess() : ProcessRuntime("unbounded", 0, nullptr, 0, 3) {}

  ProcessStep executeProcessStep(uint32_t pc, Epoch) {
    ++stepCount;
    return ProcessStep::continueAt(pc);
  }

  uint64_t stepCount = 0;
};

class InvalidContinuationProcess final
    : public ProcessRuntime<InvalidContinuationProcess> {
public:
  InvalidContinuationProcess() : ProcessRuntime("invalid", 0, nullptr, 0, 1) {}

  ProcessStep executeProcessStep(uint32_t, Epoch) {
    return ProcessStep::suspendAt(
        1, {.kind = ProcessWakeKind::Condition, .id = 1}, 0);
  }
};

TEST(GfsimProcessTest, SuspensionCommitsContinuationAndRequiresExactWake) {
  SuspendingProcess process;
  EXPECT_TRUE(process.isRunnable({0, 0}));
  process.doWork({0, 0});
  EXPECT_EQ(process.stepCount, 2u);
  EXPECT_EQ(process.pc(), 0u);
  EXPECT_TRUE(process.hasPendingCommit());
  process.doXfer({0, 0});

  EXPECT_EQ(process.status(), ProcessStatus::Suspended);
  EXPECT_EQ(process.pc(), 2u);
  EXPECT_EQ(process.continuationId(), 42u);
  EXPECT_FALSE(process.wake({ProcessWakeKind::EventQueue, 8}, 42));
  EXPECT_FALSE(process.wake({ProcessWakeKind::EventQueue, 7}, 41));
  EXPECT_TRUE(process.wake({ProcessWakeKind::EventQueue, 7}, 42));
  EXPECT_TRUE(process.isRunnable({1, 0}));

  process.doWork({1, 0});
  process.doXfer({1, 0});
  EXPECT_EQ(process.status(), ProcessStatus::Terminated);
}

TEST(GfsimProcessTest, FairnessCapProducesDeterministicFailure) {
  UnboundedProcess process;
  process.doWork({0, 0});
  EXPECT_EQ(process.stepCount, 3u);
  EXPECT_TRUE(process.hasPendingCommit());
  process.doXfer({0, 0});
  EXPECT_EQ(process.status(), ProcessStatus::Failed);
  EXPECT_EQ(process.diagnosticCode(), "process_fairness_exceeded");
  EXPECT_FALSE(process.isRunnable({1, 0}));
}

TEST(GfsimProcessTest, ResetRestoresEntryState) {
  SuspendingProcess process;
  process.doWork({0, 0});
  process.doXfer({0, 0});
  ASSERT_EQ(process.status(), ProcessStatus::Suspended);
  process.reset();
  EXPECT_EQ(process.status(), ProcessStatus::Runnable);
  EXPECT_EQ(process.pc(), 0u);
  EXPECT_EQ(process.continuationId(), 0u);
  EXPECT_FALSE(process.hasPendingCommit());
}

TEST(GfsimProcessTest, InvalidContinuationFailsAtCommitBarrier) {
  InvalidContinuationProcess process;
  process.doWork({0, 0});
  process.doXfer({0, 0});
  EXPECT_EQ(process.status(), ProcessStatus::Failed);
  EXPECT_EQ(process.diagnosticCode(), "invalid_process_continuation");
}

TEST(GfsimProcessTest, ProcessFailureTerminatesTheSystem) {
  SimSystem system("test");
  UnboundedProcess process;
  std::array rows = {makeDispatchRow(&process)};
  ASSERT_TRUE(system.setDispatchTable(rows));
  ASSERT_TRUE(system.scheduleWork(0, {0, 0}));

  EXPECT_FALSE(system.step());
  EXPECT_EQ(system.terminationResult().classification,
            TerminationClass::Failed);
  EXPECT_EQ(system.terminationResult().diagnosticCode,
            "process_fairness_exceeded");
}

TEST(GfsimProcessTest, SuspendedProcessProducesExactNoProgressSubscription) {
  SimSystem system("test");
  SuspendingProcess process;
  std::array rows = {makeDispatchRow(&process)};
  ASSERT_TRUE(system.setDispatchTable(rows));
  ASSERT_TRUE(system.scheduleWork(0, {0, 0}));

  EXPECT_FALSE(system.step());
  EXPECT_EQ(system.terminationResult().diagnosticCode, "no_progress");
  NoProgressReport report = system.noProgressReport();
  ASSERT_EQ(report.blockedObjects.size(), 1u);
  EXPECT_EQ(report.blockedObjects[0].reason, "process_suspended");
  EXPECT_EQ(report.blockedObjects[0].subscriptions,
            (std::vector<std::string>{"event_queue:7"}));
}

// ═══════════════════════════════════════════════════════════════════════
// Integration
// ═══════════════════════════════════════════════════════════════════════

TEST(GfsimIntegrationTest, ComputeToSinkPipeline) {
  auto comp = std::make_unique<Compute<uint64_t, uint64_t, DoublePolicy>>(
      "adder", 10, nullptr);
  auto *cptr = comp.get();

  auto snk = std::make_unique<Sink<>>("sink", 11, nullptr);
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
  Memory<> mem("mem", 1, nullptr, 4);
  Compute<uint64_t, uint64_t, AddThreePolicy> comp("comp", 2, nullptr);
  Sink<> snk("snk", 3, nullptr);

  mem.proposeWrite(0, 5);
  mem.proposeWrite(1, 7);
  mem.doXfer({0, 0});

  comp.setInput(mem.read(0));
  comp.doWork({0, 0});
  comp.doXfer({0, 0});

  snk.receive(comp.output());
  snk.doXfer({0, 0});

  EXPECT_EQ(snk.received()[0], 8u);
}

} // namespace
} // namespace gfsim
