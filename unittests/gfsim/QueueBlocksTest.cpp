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

struct SelectIndex {
  size_t operator()(const int &value) const {
    return static_cast<size_t>(value);
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

struct MemoryRequest {
  uint8_t address = 0;
  bool write = false;
  uint16_t data = 0;

  bool operator==(const MemoryRequest &) const = default;
};

struct MemoryAddress {
  uint8_t operator()(const MemoryRequest &request) const {
    return request.address;
  }
};

struct MemoryWrite {
  bool operator()(const MemoryRequest &request) const { return request.write; }
};

struct MemoryWriteData {
  uint16_t operator()(const MemoryRequest &request) const {
    return request.data;
  }
};

struct MemoryResponse {
  MemoryRequest operator()(const MemoryRequest &request,
                           const uint16_t &oldData) const {
    MemoryRequest response = request;
    response.data = oldData;
    return response;
  }
};

struct SharedMemoryAddress {
  uint8_t operator()(size_t, const MemoryRequest &request) const {
    return request.address;
  }
};
struct SharedMemoryWrite {
  bool operator()(size_t, const MemoryRequest &request) const {
    return request.write;
  }
};
struct SharedMemoryWriteData {
  uint16_t operator()(size_t, const MemoryRequest &request) const {
    return request.data;
  }
};
struct SharedMemoryResponse {
  MemoryRequest operator()(size_t, const MemoryRequest &request,
                           const uint16_t &oldData) const {
    MemoryRequest response = request;
    response.data = oldData;
    return response;
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

TEST(QueueBlocksTest, BarrierTransfersHeterogeneousQueuesAtomically) {
  SimQueue<int> left("left", 1, nullptr, 1);
  SimQueue<int64_t> right("right", 2, nullptr, 1);
  SimQueue<int> leftOutput("left_output", 3, nullptr, 1);
  SimQueue<int64_t> rightOutput("right_output", 4, nullptr, 1);
  QueueBarrier<std::tuple<int, int64_t>> barrier(
      "barrier", 5, nullptr, {&left, &right}, {&leftOutput, &rightOutput});
  ASSERT_TRUE(left.proposePush(4));
  ASSERT_TRUE(right.proposePush(8));
  ASSERT_TRUE(rightOutput.proposePush(99));
  left.doXfer({0, 0});
  right.doXfer({0, 0});
  rightOutput.doXfer({0, 0});

  barrier.doWork({1, 0});
  EXPECT_FALSE(barrier.hasPendingCommit());
  EXPECT_EQ(left.committedSize(), 1u);
  EXPECT_EQ(right.committedSize(), 1u);
  EXPECT_TRUE(leftOutput.isEmpty());

  rightOutput.proposePop();
  rightOutput.doXfer({1, 0});
  barrier.doWork({2, 0});
  ASSERT_TRUE(barrier.hasPendingCommit());
  left.doXfer({2, 0});
  right.doXfer({2, 0});
  leftOutput.doXfer({2, 0});
  rightOutput.doXfer({2, 0});
  barrier.doXfer({2, 0});
  EXPECT_TRUE(left.isEmpty());
  EXPECT_TRUE(right.isEmpty());
  ASSERT_NE(leftOutput.peek(), nullptr);
  ASSERT_NE(rightOutput.peek(), nullptr);
  EXPECT_EQ(*leftOutput.peek(), 4);
  EXPECT_EQ(*rightOutput.peek(), 8);
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

TEST(QueueBlocksTest, CreditWindowCompletesParallelTokensAndReturnsSlots) {
  SimQueue<DependencyValue> input("input", 1, nullptr, 4);
  SimQueue<DependencyValue> output("output", 2, nullptr, 4);
  QueueCredit<DependencyValue, DependencyCost> credit("credit", 3, nullptr,
                                                      input, output, 2);
  QueueSink<DependencyValue> sink("sink", 4, nullptr, output);
  ASSERT_TRUE(input.proposePush({0, 255, 0, 4}));
  ASSERT_TRUE(input.proposePush({1, 255, 0, 1}));
  ASSERT_TRUE(input.proposePush({2, 255, 0, 1}));
  input.doXfer({0, 0});

  for (uint64_t tick = 1; tick < 12; ++tick) {
    const Epoch epoch{tick, 0};
    credit.doWork(epoch);
    sink.doWork(epoch);
    input.doXfer(epoch);
    output.doXfer(epoch);
    credit.doXfer(epoch);
    sink.doXfer(epoch);
  }

  ASSERT_EQ(sink.received().size(), 3u);
  EXPECT_EQ(sink.received()[0].sequence, 1u);
  EXPECT_EQ(sink.received()[1].sequence, 0u);
  EXPECT_EQ(sink.received()[2].sequence, 2u);
  EXPECT_EQ(credit.active(), 0u);
}

TEST(QueueBlocksTest, CreditRejectsZeroCostWithoutConsumingInput) {
  SimQueue<DependencyValue> input("input", 1, nullptr, 1);
  SimQueue<DependencyValue> output("output", 2, nullptr, 1);
  QueueCredit<DependencyValue, DependencyCost> credit("credit", 3, nullptr,
                                                      input, output, 1);
  ASSERT_TRUE(input.proposePush({0, 255, 0, 0}));
  input.doXfer({0, 0});
  credit.doWork({1, 0});
  EXPECT_EQ(credit.runtimeFailureCode(), "credit_nonpositive_cost");
  EXPECT_FALSE(credit.hasPendingCommit());
  EXPECT_EQ(input.committedSize(), 1u);
  EXPECT_TRUE(output.isEmpty());
}

TEST(QueueBlocksTest, MemoryReturnsOldDataAndCommitsWriteAtXfer) {
  SimQueue<MemoryRequest> input("input", 1, nullptr, 2);
  SimQueue<MemoryRequest> output("output", 2, nullptr, 2);
  QueueMemory<MemoryRequest, uint16_t, MemoryAddress, MemoryWrite,
              MemoryWriteData, MemoryResponse>
      memory("memory", 3, nullptr, input, output, 16);
  QueueSink<MemoryRequest> sink("sink", 4, nullptr, output);
  ASSERT_TRUE(input.proposePush({3, true, 42}));
  ASSERT_TRUE(input.proposePush({3, false, 0}));
  input.doXfer({0, 0});

  for (uint64_t tick = 1; tick < 8; ++tick) {
    const Epoch epoch{tick, 0};
    memory.doWork(epoch);
    sink.doWork(epoch);
    input.doXfer(epoch);
    output.doXfer(epoch);
    memory.doXfer(epoch);
    sink.doXfer(epoch);
  }

  ASSERT_EQ(sink.received().size(), 2u);
  EXPECT_EQ(sink.received()[0].data, 0u);
  EXPECT_EQ(sink.received()[1].data, 42u);
  EXPECT_EQ(memory.at(3), 42u);
}

TEST(QueueBlocksTest, MemoryRejectsOutOfRangeAddress) {
  SimQueue<MemoryRequest> input("input", 1, nullptr, 1);
  SimQueue<MemoryRequest> output("output", 2, nullptr, 1);
  QueueMemory<MemoryRequest, uint16_t, MemoryAddress, MemoryWrite,
              MemoryWriteData, MemoryResponse>
      memory("memory", 3, nullptr, input, output, 4);
  ASSERT_TRUE(input.proposePush({4, false, 0}));
  input.doXfer({0, 0});
  memory.doWork({1, 0});
  EXPECT_EQ(memory.runtimeFailureCode(), "memory_address_out_of_range");
  EXPECT_FALSE(memory.hasPendingCommit());
  EXPECT_TRUE(output.isEmpty());
}

TEST(QueueBlocksTest, SharedMemoryUsesPriorityAndBlocksUntilResponseAccepted) {
  SimQueue<MemoryRequest> input0("input0", 1, nullptr, 2);
  SimQueue<MemoryRequest> input1("input1", 2, nullptr, 2);
  SimQueue<MemoryRequest> output0("output0", 3, nullptr, 1);
  SimQueue<MemoryRequest> output1("output1", 4, nullptr, 1);
  QueueMemoryArbiter<MemoryRequest, uint16_t, 2, SharedMemoryAddress,
                     SharedMemoryWrite, SharedMemoryWriteData,
                     SharedMemoryResponse>
      memory("memory", 5, nullptr, {&input0, &input1}, {&output0, &output1},
             16);
  ASSERT_TRUE(input0.proposePush({3, true, 42}));
  ASSERT_TRUE(input1.proposePush({3, false, 0}));
  ASSERT_TRUE(output0.proposePush({0, false, 99}));
  input0.doXfer({0, 0});
  input1.doXfer({0, 0});
  output0.doXfer({0, 0});

  memory.doWork({1, 0});
  input0.doXfer({1, 0});
  input1.doXfer({1, 0});
  memory.doXfer({1, 0});
  EXPECT_TRUE(memory.busy());
  EXPECT_EQ(memory.selectedEndpoint(), 0u);
  EXPECT_EQ(input0.committedSize(), 0u);
  EXPECT_EQ(input1.committedSize(), 1u);
  EXPECT_EQ(memory.at(3), 42u);

  memory.doWork({2, 0});
  EXPECT_FALSE(memory.hasPendingCommit());
  ASSERT_TRUE(output0.proposePop());
  output0.doXfer({2, 0});
  memory.doWork({2, 1});
  output0.doXfer({2, 1});
  memory.doXfer({2, 1});
  EXPECT_FALSE(memory.busy());
  memory.doWork({2, 1});
  EXPECT_EQ(input1.committedSize(), 1u);

  memory.doWork({3, 0});
  input1.doXfer({3, 0});
  memory.doXfer({3, 0});
  EXPECT_EQ(input1.committedSize(), 0u);
}

TEST(QueueBlocksTest,
     SharedMemoryLatencyDelaysResponseAndBackpressuresRequests) {
  SimQueue<MemoryRequest> input("input", 1, nullptr, 2);
  SimQueue<MemoryRequest> output("output", 2, nullptr, 1);
  QueueMemoryArbiter<MemoryRequest, uint16_t, 1, SharedMemoryAddress,
                     SharedMemoryWrite, SharedMemoryWriteData,
                     SharedMemoryResponse>
      memory("memory", 3, nullptr, {&input}, {&output}, 16, 0, 3);
  EXPECT_EQ(memory.latency(), 3u);
  ASSERT_TRUE(input.proposePush({3, false, 0}));
  input.doXfer({0, 0});

  memory.doWork({1, 0});
  input.doXfer({1, 0});
  memory.doXfer({1, 0});
  ASSERT_TRUE(memory.busy());
  ASSERT_TRUE(input.proposePush({7, false, 0}));
  input.doXfer({2, 0});

  for (uint64_t tick : {2, 3}) {
    memory.doWork({tick, 0});
    EXPECT_TRUE(memory.hasPendingCommit());
    EXPECT_EQ(input.committedSize(), 1u);
    EXPECT_TRUE(output.isEmpty());
    memory.doXfer({tick, 0});
  }

  memory.doWork({4, 0});
  EXPECT_TRUE(memory.hasPendingCommit());
  output.doXfer({4, 0});
  memory.doXfer({4, 0});
  EXPECT_FALSE(memory.busy());
  EXPECT_EQ(input.committedSize(), 1u);
  EXPECT_EQ(output.committedSize(), 1u);

  memory.doWork({4, 0});
  EXPECT_FALSE(memory.hasPendingCommit());
  memory.doWork({5, 0});
  input.doXfer({5, 0});
  memory.doXfer({5, 0});
  EXPECT_TRUE(memory.busy());
  EXPECT_EQ(input.committedSize(), 0u);
}

TEST(QueueBlocksTest, SharedMemoryRejectsZeroLatency) {
  SimQueue<MemoryRequest> input("input", 1, nullptr, 1);
  SimQueue<MemoryRequest> output("output", 2, nullptr, 1);
  EXPECT_THROW((QueueMemoryArbiter<MemoryRequest, uint16_t, 1,
                                   SharedMemoryAddress, SharedMemoryWrite,
                                   SharedMemoryWriteData, SharedMemoryResponse>(
                   "memory", 3, nullptr, {&input}, {&output}, 16, 0, 0)),
               std::invalid_argument);
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

TEST(QueueBlocksTest, ExpectChecksHeadWithoutConsumingIt) {
  SimQueue<int> input("input", 1, nullptr, 2);
  QueueExpect<int, Positive> expect("expect", 2, nullptr, input,
                                    "must be positive");
  ASSERT_TRUE(input.proposePush(7));
  input.doXfer({0, 0});
  expect.doWork({1, 0});
  EXPECT_TRUE(expect.hasPendingCommit());
  expect.doXfer({1, 0});
  EXPECT_EQ(input.committedSize(), 1u);
  EXPECT_TRUE(expect.runtimeFailureCode().empty());
  EXPECT_EQ(expect.message(), "must be positive");
}

TEST(QueueBlocksTest, ExpectReportsPredicateFailure) {
  SimQueue<int> input("input", 1, nullptr, 1);
  QueueExpect<int, Positive> expect("expect", 2, nullptr, input,
                                    "must be positive");
  ASSERT_TRUE(input.proposePush(-1));
  input.doXfer({0, 0});
  expect.doWork({1, 0});
  EXPECT_EQ(expect.runtimeFailureCode(), "expectation_failed");
  EXPECT_FALSE(expect.hasPendingCommit());
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

TEST(QueueBlocksTest, SelectConsumesControlAndChosenInputOnly) {
  SimQueue<int> control("control", 1, nullptr, 1);
  SimQueue<int> left("left", 2, nullptr, 1);
  SimQueue<int> right("right", 3, nullptr, 1);
  SimQueue<int> output("output", 4, nullptr, 1);
  QueueSelect<int, int, 2, SelectIndex> select("select", 5, nullptr, control,
                                               {&left, &right}, output);
  ASSERT_TRUE(control.proposePush(1));
  ASSERT_TRUE(left.proposePush(10));
  ASSERT_TRUE(right.proposePush(20));
  control.doXfer({0, 0});
  left.doXfer({0, 0});
  right.doXfer({0, 0});
  select.doWork({1, 0});
  control.doXfer({1, 0});
  left.doXfer({1, 0});
  right.doXfer({1, 0});
  output.doXfer({1, 0});
  select.doXfer({1, 0});
  EXPECT_TRUE(control.isEmpty());
  ASSERT_NE(left.peek(), nullptr);
  EXPECT_EQ(*left.peek(), 10);
  EXPECT_TRUE(right.isEmpty());
  ASSERT_NE(output.peek(), nullptr);
  EXPECT_EQ(*output.peek(), 20);
}

TEST(QueueBlocksTest, SelectRejectsOutOfRangeSelector) {
  SimQueue<int> control("control", 1, nullptr, 1);
  SimQueue<int> left("left", 2, nullptr, 1);
  SimQueue<int> right("right", 3, nullptr, 1);
  SimQueue<int> output("output", 4, nullptr, 1);
  QueueSelect<int, int, 2, SelectIndex> select("select", 5, nullptr, control,
                                               {&left, &right}, output);
  ASSERT_TRUE(control.proposePush(2));
  control.doXfer({0, 0});
  select.doWork({1, 0});
  EXPECT_EQ(select.runtimeFailureCode(), "select_selector_out_of_range");
  EXPECT_FALSE(select.hasPendingCommit());
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
