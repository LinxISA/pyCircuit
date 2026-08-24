#include "acir/CodeGen/QueueGraphPlan.h"
#include "acir/CodeGen/QueueGraphGenerator.h"
#include "acir/CodeGen/QueueGraphPyc.h"

#include "acir/Dialect/ACIR/ACIRDialect.h"
#include "mlir/Dialect/DLTI/DLTI.h"
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

constexpr llvm::StringLiteral kStructuredTransform = R"mlir(
module attributes {ac.contract_epoch = "0.1", ac.system = "structured"} {
  ac.type_scope @types {
    ac.struct @Item fields [{name = "value", type = i64}]
  } {dlti.dl_spec = #dlti.dl_spec<!ac.struct<@types::@Item> = {abi_alignment = 8 : i64, endianness = "little", preferred_alignment = 8 : i64, size = 8 : i64}>}
  %input = ac.source depth 2 latency 1 {ac.name = "input"} : !ac.queue<!ac.struct<@types::@Item>>
  %output = ac.transform %input depths [2] latencies [1] {
  ^body(%item: !ac.var<!ac.struct<@types::@Item>>):
    %value = ac.var.get %item field "value" : !ac.var<!ac.struct<@types::@Item>> -> !ac.var<i64>
    %one = ac.var.constant 1 : i64 as !ac.var<i64>
    %sum = ac.var.add %value, %one : !ac.var<i64>
    %updated = ac.var.with %item, %sum field "value" : !ac.var<!ac.struct<@types::@Item>>, !ac.var<i64> -> !ac.var<!ac.struct<@types::@Item>>
    ac.transform.yield %updated : !ac.var<!ac.struct<@types::@Item>>
  } {ac.name = "output"} : (!ac.queue<!ac.struct<@types::@Item>>) -> !ac.queue<!ac.struct<@types::@Item>>
  ac.sink %output {ac.name = "sink_0"} : !ac.queue<!ac.struct<@types::@Item>>
}
)mlir";

constexpr llvm::StringLiteral kMultipleConsumers = R"mlir(
module attributes {ac.contract_epoch = "0.1", ac.system = "bad"} {
  %input = ac.source depth 2 latency 1 {ac.name = "input"} : !ac.queue<i64>
  ac.sink %input {ac.name = "left"} : !ac.queue<i64>
  ac.sink %input {ac.name = "right"} : !ac.queue<i64>
}
)mlir";

constexpr llvm::StringLiteral kObservationUse = R"mlir(
module attributes {ac.contract_epoch = "0.1", ac.system = "observed"} {
  %input = ac.source depth 2 latency 1 {ac.name = "input"} : !ac.queue<i64>
  ac.observe %input name "head" : !ac.queue<i64>
  ac.sink %input {ac.name = "sink_0"} : !ac.queue<i64>
}
)mlir";

TEST(QueueGraphPlanTest, ExtractsFrozenQueueIdentitiesAndTopology) {
  mlir::MLIRContext context;
  context.loadDialect<ac::ACIRDialect, mlir::DLTIDialect>();
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
  context.loadDialect<ac::ACIRDialect, mlir::DLTIDialect>();
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

TEST(QueueGraphPlanTest, ExtractsPayloadAndImmutableVarDag) {
  mlir::MLIRContext context;
  context.loadDialect<ac::ACIRDialect, mlir::DLTIDialect>();
  auto module =
      mlir::parseSourceString<mlir::ModuleOp>(kStructuredTransform, &context);
  ASSERT_TRUE(module);
  auto plan = buildQueueGraphPlan(*module);
  ASSERT_TRUE(bool(plan)) << llvm::toString(plan.takeError());
  ASSERT_EQ(plan->payloads.size(), 1u);
  EXPECT_EQ(plan->payloads[0].name, "Item");
  ASSERT_EQ(plan->payloads[0].fields.size(), 1u);
  EXPECT_EQ(plan->payloads[0].fields[0].name, "value");
  ASSERT_EQ(plan->blocks.size(), 3u);
  const QueueBlockPlan &transform = plan->blocks[1];
  ASSERT_EQ(transform.expressions.size(), 4u);
  EXPECT_EQ(transform.expressions[0].kind, "get");
  EXPECT_EQ(transform.expressions[1].kind, "constant");
  EXPECT_EQ(transform.expressions[2].kind, "add");
  EXPECT_EQ(transform.expressions[3].kind, "with");
  ASSERT_EQ(transform.yields.size(), 1u);
  EXPECT_EQ(transform.yields[0], "v3");
}

TEST(QueueGraphPlanTest, NativeGeneratorConsumesOnlyExtractedPlan) {
  mlir::MLIRContext context;
  context.loadDialect<ac::ACIRDialect, mlir::DLTIDialect>();
  auto module = mlir::parseSourceString<mlir::ModuleOp>(kQueueGraph, &context);
  ASSERT_TRUE(module);
  auto plan = buildQueueGraphPlan(*module);
  ASSERT_TRUE(bool(plan)) << llvm::toString(plan.takeError());
  auto source = generateQueueGraphCpp(*plan);
  ASSERT_TRUE(bool(source)) << llvm::toString(source.takeError());
  EXPECT_NE(source->find("gfsim::QueueRoute<std::int64_t, 2"),
            std::string::npos);
  EXPECT_NE(source->find("gfsim::QueueMerge<std::int64_t, 2>"),
            std::string::npos);
  EXPECT_NE(source->find("gfsim::QueueSink<std::int64_t>"), std::string::npos);
}

TEST(QueueGraphPlanTest, EmitsCanonicalScalarQueuePyc) {
  QueueGraphPlan plan;
  plan.system = "scalar_pipeline";
  plan.queues = {{"input", "i64", "/", 2, 1}, {"output", "i64", "/", 2, 1}};
  plan.blocks.push_back({"source", "input", "/", {}, {"input"}, {2}, {1}});
  QueueBlockPlan transform{"transform", "output", "/", {"input"},
                           {"output"},  {2},      {1}};
  transform.expressions = {{"v0", "constant", "i64", {}, "", "", "1 : i64"},
                           {"v1", "add", "i64", {"item", "v0"}, "", "", ""}};
  transform.yields = {"v1"};
  plan.blocks.push_back(std::move(transform));
  plan.blocks.push_back({"sink", "sink_0", "/", {"output"}, {}});
  auto pyc = generateQueueGraphPyc(plan);
  ASSERT_TRUE(bool(pyc)) << llvm::toString(pyc.takeError());
  EXPECT_EQ(std::count(pyc->begin(), pyc->end(), '\n') > 5, true);
  EXPECT_NE(pyc->find("pyc.fifo"), std::string::npos);
  EXPECT_NE(pyc->find("pyc.add"), std::string::npos);
  EXPECT_NE(pyc->find("pyc.frontend.contract = \"pycircuit\""),
            std::string::npos);
}

TEST(QueueGraphPlanTest, RejectsImplicitMultipleConsumers) {
  mlir::MLIRContext context;
  context.loadDialect<ac::ACIRDialect, mlir::DLTIDialect>();
  auto module =
      mlir::parseSourceString<mlir::ModuleOp>(kMultipleConsumers, &context);
  ASSERT_TRUE(module);
  auto plan = buildQueueGraphPlan(*module);
  ASSERT_FALSE(bool(plan));
  EXPECT_NE(llvm::toString(plan.takeError()).find("insert ac.broadcast"),
            std::string::npos);
}

TEST(QueueGraphPlanTest, ObservationDoesNotConsumeQueue) {
  mlir::MLIRContext context;
  context.loadDialect<ac::ACIRDialect, mlir::DLTIDialect>();
  auto module =
      mlir::parseSourceString<mlir::ModuleOp>(kObservationUse, &context);
  ASSERT_TRUE(module);
  auto plan = buildQueueGraphPlan(*module);
  ASSERT_TRUE(bool(plan)) << llvm::toString(plan.takeError());
  ASSERT_EQ(plan->blocks.size(), 3u);
  EXPECT_EQ(plan->blocks[1].kind, "observe");
}

} // namespace
} // namespace acir::codegen
