#include "acir/CodeGen/QueueGraphPlan.h"

#include "acir/Dialect/ACIR/ACIRDialect.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"

namespace acir::codegen {
namespace {

constexpr llvm::StringLiteral kQueueGraph = R"mlir(
module attributes {ac.contract_epoch = "0.1", ac.system = "pipeline"} {
  %input = ac.source depth 4 latency 1 {ac.name = "input"} : !ac.queue<i64>
  %left, %right = ac.route %input depths [2, 2] latencies [1, 1] {
  ^selector(%item: !ac.var<i64>):
    ac.route.yield %item : !ac.var<i64>
  } {ac.output_names = ["left", "right"]} : !ac.queue<i64> -> (!ac.queue<i64>, !ac.queue<i64>)
  %merged = ac.merge %left, %right policy "round_robin" depth 3 latency 1 {ac.name = "merged"} : (!ac.queue<i64>, !ac.queue<i64>) -> !ac.queue<i64>
  ac.sink %merged {ac.name = "sink_0"} : !ac.queue<i64>
}
)mlir";

TEST(QueueGraphPlanTest, ExtractsFrozenQueueIdentitiesAndTopology) {
  mlir::MLIRContext context;
  context.loadDialect<ac::ACIRDialect>();
  auto module = mlir::parseSourceString<mlir::ModuleOp>(kQueueGraph, &context);
  ASSERT_TRUE(module);
  auto plan = buildQueueGraphPlan(*module);
  ASSERT_TRUE(bool(plan)) << llvm::toString(plan.takeError());
  EXPECT_EQ(plan->system, "pipeline");
  ASSERT_EQ(plan->queues.size(), 4u);
  EXPECT_EQ(plan->queues[0].name, "input");
  EXPECT_EQ(plan->queues[1].name, "left");
  EXPECT_EQ(plan->queues[2].name, "right");
  EXPECT_EQ(plan->queues[3].name, "merged");
  ASSERT_EQ(plan->blocks.size(), 4u);
  EXPECT_EQ(plan->blocks[0].kind, "source");
  EXPECT_EQ(plan->blocks[1].kind, "route");
  EXPECT_EQ(plan->blocks[2].kind, "merge");
  EXPECT_EQ(plan->blocks[3].kind, "sink");
  EXPECT_EQ(plan->blocks[2].policy, "round_robin");
}

TEST(QueueGraphPlanTest, CanonicalJsonIsByteIdenticalAndClosed) {
  mlir::MLIRContext context;
  context.loadDialect<ac::ACIRDialect>();
  auto module = mlir::parseSourceString<mlir::ModuleOp>(kQueueGraph, &context);
  ASSERT_TRUE(module);
  auto plan = buildQueueGraphPlan(*module);
  ASSERT_TRUE(bool(plan)) << llvm::toString(plan.takeError());
  auto first = plan->canonicalJson();
  ASSERT_TRUE(bool(first)) << llvm::toString(first.takeError());
  auto second = plan->canonicalJson();
  ASSERT_TRUE(bool(second)) << llvm::toString(second.takeError());
  EXPECT_EQ(*first, *second);
  EXPECT_NE(first->find("\"schema\":\"agentic-circuit-queue-graph-plan\""),
            std::string::npos);
  EXPECT_NE(first->find("\"version\":\"0.2\""), std::string::npos);
  EXPECT_NE(first->find("\"name\":\"merged\""), std::string::npos);
}

} // namespace
} // namespace acir::codegen
