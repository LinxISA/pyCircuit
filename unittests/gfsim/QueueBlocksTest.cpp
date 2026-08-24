#include "gfsim/queue_blocks.h"

#include "gtest/gtest.h"

#include <cstdint>
#include <limits>

namespace gfsim {
namespace {

struct Increment {
  int operator()(const int &value) const { return value + 1; }
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

} // namespace
} // namespace gfsim
