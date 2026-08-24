#include "gfsim/queue_blocks.h"

#include "gtest/gtest.h"

#include <cstdint>
#include <limits>

namespace gfsim {
namespace {

struct Increment {
  int operator()(const int &value) const { return value + 1; }
};

struct SelectParity {
  size_t operator()(const int &value) const {
    return static_cast<size_t>(value & 1);
  }
};

struct Positive {
  bool operator()(const int &value) const { return value > 0; }
};

struct Decrement {
  int operator()(const int &value) const { return value - 1; }
};

struct IncrementAndDouble {
  std::tuple<int, int> operator()(const int &left, const int &right) const {
    return {left + 1, right * 2};
  }
};

struct SumToWide {
  std::tuple<int64_t> operator()(const int &left, const int64_t &right) const {
    return {static_cast<int64_t>(left) + right};
  }
};

struct SequencedValue {
  uint64_t sequence = 0;
  int value = 0;

  bool operator==(const SequencedValue &) const = default;
};

struct SequenceKey {
  uint64_t operator()(const SequencedValue &value) const {
    return value.sequence;
  }
};

struct SignedSequencedValue {
  int64_t sequence = 0;
};

struct SignedSequenceKey {
  int64_t operator()(const SignedSequencedValue &value) const {
    return value.sequence;
  }
};

struct DependencyValue {
  uint64_t sequence = 0;
  uint64_t predecessor = 255;
  uint64_t resource = 0;
  uint64_t cycles = 1;

  bool operator==(const DependencyValue &) const = default;
};

struct DependencyKey {
  uint64_t operator()(const DependencyValue &value) const {
    return value.sequence;
  }
};

struct DependencyPredecessor {
  uint64_t operator()(const DependencyValue &value) const {
    return value.predecessor;
  }
};

struct DependencyCost {
  uint64_t operator()(const DependencyValue &value) const {
    return value.cycles;
  }
};

struct DependencyResource {
  uint64_t operator()(const DependencyValue &value) const {
    return value.resource;
  }
};

TEST(QueueBlocksTest, TransformCommitsOnlyAcrossTheQueueBarrier) {
  SimQueue<int> input("input", 1, nullptr, 2);
  SimQueue<int> output("output", 2, nullptr, 2);
  QueueTransform<int, int, Increment> transform("transform", 3, nullptr, input,
                                                output);

  ASSERT_TRUE(input.proposePush(41));
  input.doXfer({0, 0});
  transform.doWork({1, 0});

  ASSERT_NE(input.peek(), nullptr);
  EXPECT_EQ(*input.peek(), 41);
  EXPECT_TRUE(output.isEmpty());

  input.doXfer({1, 0});
  output.doXfer({1, 0});
  transform.doXfer({1, 0});
  EXPECT_TRUE(input.isEmpty());
  ASSERT_NE(output.peek(), nullptr);
  EXPECT_EQ(*output.peek(), 42);
}

TEST(QueueBlocksTest, TransformDoesNotConsumeWhenOutputIsBackpressured) {
  SimQueue<int> input("input", 1, nullptr, 1);
  SimQueue<int> output("output", 2, nullptr, 1);
  QueueTransform<int, int, Increment> transform("transform", 3, nullptr, input,
                                                output);
  ASSERT_TRUE(input.proposePush(7));
  ASSERT_TRUE(output.proposePush(99));
  input.doXfer({0, 0});
  output.doXfer({0, 0});

  transform.doWork({1, 0});
  input.doXfer({1, 0});
  output.doXfer({1, 0});
  ASSERT_NE(input.peek(), nullptr);
  EXPECT_EQ(*input.peek(), 7);
  ASSERT_NE(output.peek(), nullptr);
  EXPECT_EQ(*output.peek(), 99);
}

TEST(QueueBlocksTest, AtomicTransformCommitsAllQueuesTogether) {
  SimQueue<int> left("left", 1, nullptr, 1);
  SimQueue<int> right("right", 2, nullptr, 1);
  SimQueue<int> leftOutput("left_output", 3, nullptr, 1);
  SimQueue<int> rightOutput("right_output", 4, nullptr, 1);
  QueueAtomicTransform<IncrementAndDouble, std::tuple<int, int>,
                       std::tuple<int, int>>
      atomic("atomic", 5, nullptr, {&left, &right},
             {&leftOutput, &rightOutput});
  ASSERT_TRUE(left.proposePush(4));
  ASSERT_TRUE(right.proposePush(7));
  left.doXfer({0, 0});
  right.doXfer({0, 0});
  atomic.doWork({1, 0});
  EXPECT_EQ(left.committedSize(), 1u);
  EXPECT_EQ(right.committedSize(), 1u);
  EXPECT_TRUE(leftOutput.isEmpty());
  EXPECT_TRUE(rightOutput.isEmpty());
  left.doXfer({1, 0});
  right.doXfer({1, 0});
  leftOutput.doXfer({1, 0});
  rightOutput.doXfer({1, 0});
  atomic.doXfer({1, 0});
  EXPECT_TRUE(left.isEmpty());
  EXPECT_TRUE(right.isEmpty());
  ASSERT_NE(leftOutput.peek(), nullptr);
  ASSERT_NE(rightOutput.peek(), nullptr);
  EXPECT_EQ(*leftOutput.peek(), 5);
  EXPECT_EQ(*rightOutput.peek(), 14);
}

TEST(QueueBlocksTest, AtomicTransformSupportsIndependentInputOutputArity) {
  SimQueue<int> left("left", 1, nullptr, 1);
  SimQueue<int64_t> right("right", 2, nullptr, 1);
  SimQueue<int64_t> output("output", 3, nullptr, 1);
  QueueAtomicTransform<SumToWide, std::tuple<int, int64_t>, std::tuple<int64_t>>
      atomic("atomic", 4, nullptr, {&left, &right}, {&output});

  ASSERT_TRUE(left.proposePush(4));
  ASSERT_TRUE(right.proposePush(8));
  left.doXfer({0, 0});
  right.doXfer({0, 0});
  atomic.doWork({1, 0});

  EXPECT_EQ(left.committedSize(), 1u);
  EXPECT_EQ(right.committedSize(), 1u);
  EXPECT_TRUE(output.isEmpty());
  left.doXfer({1, 0});
  right.doXfer({1, 0});
  output.doXfer({1, 0});
  atomic.doXfer({1, 0});
  EXPECT_TRUE(left.isEmpty());
  EXPECT_TRUE(right.isEmpty());
  ASSERT_NE(output.peek(), nullptr);
  EXPECT_EQ(*output.peek(), 12);
}

TEST(QueueBlocksTest, ReorderRetiresOutOfOrderArrivalsBySequenceKey) {
  SimQueue<SequencedValue> input("input", 1, nullptr, 4);
  SimQueue<SequencedValue> output("output", 2, nullptr, 4);
  QueueReorder<SequencedValue, SequenceKey> reorder("reorder", 3, nullptr,
                                                    input, output, 4, 0);
  QueueSink<SequencedValue> sink("sink", 4, nullptr, output);
  ASSERT_TRUE(input.proposePush({2, 20}));
  ASSERT_TRUE(input.proposePush({0, 0}));
  ASSERT_TRUE(input.proposePush({1, 10}));
  input.doXfer({0, 0});

  for (uint64_t tick = 1; tick < 12; ++tick) {
    const Epoch epoch{tick, 0};
    reorder.doWork(epoch);
    sink.doWork(epoch);
    input.doXfer(epoch);
    output.doXfer(epoch);
    reorder.doXfer(epoch);
    sink.doXfer(epoch);
  }

  ASSERT_EQ(sink.received().size(), 3u);
  EXPECT_EQ(sink.received()[0], (SequencedValue{0, 0}));
  EXPECT_EQ(sink.received()[1], (SequencedValue{1, 10}));
  EXPECT_EQ(sink.received()[2], (SequencedValue{2, 20}));
}

TEST(QueueBlocksTest, ReorderRejectsDuplicateSequenceKey) {
  SimQueue<SequencedValue> input("input", 1, nullptr, 2);
  SimQueue<SequencedValue> output("output", 2, nullptr, 2);
  QueueReorder<SequencedValue, SequenceKey> reorder("reorder", 3, nullptr,
                                                    input, output, 2, 0);
  ASSERT_TRUE(input.proposePush({0, 1}));
  ASSERT_TRUE(input.proposePush({0, 2}));
  input.doXfer({0, 0});
  reorder.doWork({1, 0});
  input.doXfer({1, 0});
  output.doXfer({1, 0});
  reorder.doXfer({1, 0});
  reorder.doWork({2, 0});
  EXPECT_EQ(reorder.runtimeFailureCode(), "reorder_duplicate_key");
  EXPECT_FALSE(reorder.hasPendingCommit());
  EXPECT_TRUE(output.isEmpty());
}

TEST(QueueBlocksTest, ReorderRejectsNegativeSequenceKey) {
  SimQueue<SignedSequencedValue> input("input", 1, nullptr, 1);
  SimQueue<SignedSequencedValue> output("output", 2, nullptr, 1);
  QueueReorder<SignedSequencedValue, SignedSequenceKey> reorder(
      "reorder", 3, nullptr, input, output, 1, 0);
  ASSERT_TRUE(input.proposePush({-1}));
  input.doXfer({0, 0});
  reorder.doWork({1, 0});
  EXPECT_EQ(reorder.runtimeFailureCode(), "reorder_negative_key");
  EXPECT_FALSE(reorder.hasPendingCommit());
  EXPECT_TRUE(output.isEmpty());
}

TEST(QueueBlocksTest, DependencyCompletesReadyTokensOutOfOrder) {
  SimQueue<DependencyValue> input("input", 1, nullptr, 4);
  SimQueue<DependencyValue> output("output", 2, nullptr, 4);
  QueueDependency<DependencyValue, DependencyKey, DependencyPredecessor,
                  DependencyResource, DependencyCost>
      dependency("dependency", 3, nullptr, input, output, 4, 2, 255);
  QueueSink<DependencyValue> sink("sink", 4, nullptr, output);
  ASSERT_TRUE(input.proposePush({0, 255, 0, 4}));
  ASSERT_TRUE(input.proposePush({1, 255, 0, 1}));
  ASSERT_TRUE(input.proposePush({2, 255, 1, 1}));
  ASSERT_TRUE(input.proposePush({3, 0, 1, 1}));
  input.doXfer({0, 0});

  for (uint64_t tick = 1; tick < 16; ++tick) {
    const Epoch epoch{tick, 0};
    dependency.doWork(epoch);
    sink.doWork(epoch);
    input.doXfer(epoch);
    output.doXfer(epoch);
    dependency.doXfer(epoch);
    sink.doXfer(epoch);
  }

  ASSERT_EQ(sink.received().size(), 4u);
  EXPECT_EQ(sink.received()[0].sequence, 2u);
  EXPECT_EQ(sink.received()[1].sequence, 0u);
  EXPECT_EQ(sink.received()[2].sequence, 1u);
  EXPECT_EQ(sink.received()[3].sequence, 3u);
}

TEST(QueueBlocksTest, DependencyRejectsZeroExecutionCost) {
  SimQueue<DependencyValue> input("input", 1, nullptr, 1);
  SimQueue<DependencyValue> output("output", 2, nullptr, 1);
  QueueDependency<DependencyValue, DependencyKey, DependencyPredecessor,
                  DependencyResource, DependencyCost>
      dependency("dependency", 3, nullptr, input, output, 1, 1, 255);
  ASSERT_TRUE(input.proposePush({0, 255, 0, 0}));
  input.doXfer({0, 0});
  dependency.doWork({1, 0});
  EXPECT_EQ(dependency.runtimeFailureCode(), "dependency_nonpositive_cost");
  EXPECT_FALSE(dependency.hasPendingCommit());
  EXPECT_TRUE(output.isEmpty());
}

TEST(QueueBlocksTest, DependencyRejectsOutOfRangeResource) {
  SimQueue<DependencyValue> input("input", 1, nullptr, 1);
  SimQueue<DependencyValue> output("output", 2, nullptr, 1);
  QueueDependency<DependencyValue, DependencyKey, DependencyPredecessor,
                  DependencyResource, DependencyCost>
      dependency("dependency", 3, nullptr, input, output, 1, 1, 255);
  ASSERT_TRUE(input.proposePush({0, 255, 1, 1}));
  input.doXfer({0, 0});
  dependency.doWork({1, 0});
  EXPECT_EQ(dependency.runtimeFailureCode(),
            "dependency_resource_out_of_range");
  EXPECT_FALSE(dependency.hasPendingCommit());
  EXPECT_TRUE(output.isEmpty());
}

TEST(QueueBlocksTest, QueueLatencyDelaysVisibilityButReservesCapacity) {
  SimQueue<int> queue("queue", 1, nullptr, 1,
                      std::numeric_limits<size_t>::max(), nullptr, 3);
  EXPECT_EQ(queue.latency(), 3u);
  ASSERT_TRUE(queue.proposePush(5));
  queue.doXfer({0, 0});
  EXPECT_TRUE(queue.isEmpty());
  EXPECT_TRUE(queue.isFull());
  queue.doXfer({1, 0});
  EXPECT_TRUE(queue.isEmpty());
  queue.doXfer({2, 0});
  ASSERT_NE(queue.peek(), nullptr);
  EXPECT_EQ(*queue.peek(), 5);
}

TEST(QueueBlocksTest, SinkConsumesAtWorkAndPublishesAtXfer) {
  SimQueue<int> input("input", 1, nullptr, 1);
  QueueSink<int> sink("sink", 2, nullptr, input);
  ASSERT_TRUE(input.proposePush(13));
  input.doXfer({0, 0});

  sink.doWork({1, 0});
  EXPECT_TRUE(sink.received().empty());
  input.doXfer({1, 0});
  sink.doXfer({1, 0});
  ASSERT_EQ(sink.received().size(), 1u);
  EXPECT_EQ(sink.received().front(), 13);
}

TEST(QueueBlocksTest, ObserveCommitsWithoutConsumingOrBackpressure) {
  SimQueue<int> input("input", 1, nullptr, 2);
  QueueObserve<int> observe("observe", 2, nullptr, input);
  ASSERT_TRUE(input.proposePush(13));
  input.doXfer({0, 0});
  observe.doWork({1, 0});
  EXPECT_EQ(input.committedSize(), 1u);
  EXPECT_TRUE(observe.observed().empty());
  observe.doXfer({1, 0});
  ASSERT_EQ(observe.observed().size(), 1u);
  EXPECT_EQ(observe.observed().front(), 13);
  observe.doWork({2, 0});
  observe.doXfer({2, 0});
  EXPECT_EQ(observe.observed().size(), 1u);
  EXPECT_EQ(input.committedSize(), 1u);
}

TEST(QueueBlocksTest, BroadcastWaitsForEveryOutput) {
  SimQueue<int> input("input", 1, nullptr, 1);
  SimQueue<int> left("left", 2, nullptr, 1);
  SimQueue<int> right("right", 3, nullptr, 1);
  QueueBroadcast<int, 2> broadcast("broadcast", 4, nullptr, input,
                                   {&left, &right});
  ASSERT_TRUE(input.proposePush(9));
  ASSERT_TRUE(right.proposePush(4));
  input.doXfer({0, 0});
  right.doXfer({0, 0});
  broadcast.doWork({1, 0});
  input.doXfer({1, 0});
  left.doXfer({1, 0});
  ASSERT_NE(input.peek(), nullptr);
  EXPECT_TRUE(left.isEmpty());
}

TEST(QueueBlocksTest, ForkDeliversOutputsIndependentlyBeforeInputPop) {
  SimQueue<int> input("input", 1, nullptr, 1);
  SimQueue<int> left("left", 2, nullptr, 1);
  SimQueue<int> right("right", 3, nullptr, 1);
  QueueFork<int, 2> fork("fork", 4, nullptr, input, {&left, &right});
  ASSERT_TRUE(input.proposePush(9));
  ASSERT_TRUE(right.proposePush(4));
  input.doXfer({0, 0});
  right.doXfer({0, 0});

  fork.doWork({1, 0});
  input.doXfer({1, 0});
  left.doXfer({1, 0});
  right.doXfer({1, 0});
  fork.doXfer({1, 0});
  ASSERT_NE(input.peek(), nullptr);
  ASSERT_NE(left.peek(), nullptr);
  EXPECT_EQ(*left.peek(), 9);
  EXPECT_EQ(*right.peek(), 4);

  right.proposePop();
  right.doXfer({2, 0});
  fork.doWork({2, 0});
  input.doXfer({2, 0});
  left.doXfer({2, 0});
  right.doXfer({2, 0});
  fork.doXfer({2, 0});
  EXPECT_TRUE(input.isEmpty());
  EXPECT_EQ(left.committedSize(), 1u);
  ASSERT_NE(right.peek(), nullptr);
  EXPECT_EQ(*right.peek(), 9);
}

TEST(QueueBlocksTest, RouteSelectsExactlyOneOutput) {
  SimQueue<int> input("input", 1, nullptr, 1);
  SimQueue<int> even("even", 2, nullptr, 1);
  SimQueue<int> odd("odd", 3, nullptr, 1);
  QueueRoute<int, 2, SelectParity> route("route", 4, nullptr, input,
                                         {&even, &odd});
  ASSERT_TRUE(input.proposePush(7));
  input.doXfer({0, 0});
  route.doWork({1, 0});
  input.doXfer({1, 0});
  even.doXfer({1, 0});
  odd.doXfer({1, 0});
  EXPECT_TRUE(even.isEmpty());
  ASSERT_NE(odd.peek(), nullptr);
  EXPECT_EQ(*odd.peek(), 7);
}

TEST(QueueBlocksTest, MergeRoundRobinIgnoresWorkInsertionOrder) {
  SimQueue<int> left("left", 1, nullptr, 2);
  SimQueue<int> right("right", 2, nullptr, 2);
  SimQueue<int> output("output", 3, nullptr, 2);
  QueueMerge<int, 2> merge("merge", 4, nullptr, {&left, &right}, output);
  ASSERT_TRUE(left.proposePush(10));
  ASSERT_TRUE(right.proposePush(20));
  left.doXfer({0, 0});
  right.doXfer({0, 0});
  merge.doWork({1, 0});
  left.doXfer({1, 0});
  right.doXfer({1, 0});
  output.doXfer({1, 0});
  merge.doXfer({1, 0});
  ASSERT_NE(output.peek(), nullptr);
  EXPECT_EQ(*output.peek(), 10);
  output.proposePop();
  output.doXfer({2, 0});
  merge.doWork({2, 0});
  right.doXfer({2, 0});
  output.doXfer({2, 0});
  ASSERT_NE(output.peek(), nullptr);
  EXPECT_EQ(*output.peek(), 20);
}

TEST(QueueBlocksTest, FeedbackUsesParentOwnedStateQueue) {
  using State = FeedbackToken<int>;
  SimQueue<int> input("input", 1, nullptr, 1);
  SimQueue<State> feedback("feedback", 2, nullptr, 1);
  SimQueue<int> output("output", 3, nullptr, 1);
  QueueFeedback<int, Decrement, Positive> loop("feedback_block", 4, nullptr,
                                               input, feedback, output, 8);
  ASSERT_TRUE(input.proposePush(3));
  input.doXfer({0, 0});
  for (uint64_t tick = 1; tick <= 4; ++tick) {
    loop.doWork({tick, 0});
    input.doXfer({tick, 0});
    feedback.doXfer({tick, 0});
    output.doXfer({tick, 0});
    loop.doXfer({tick, 0});
  }
  ASSERT_NE(output.peek(), nullptr);
  EXPECT_EQ(*output.peek(), 0);
}

} // namespace
} // namespace gfsim
