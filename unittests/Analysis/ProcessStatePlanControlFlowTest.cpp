#include "Analysis/ProcessStatePlanInternal.h"
#include "Analysis/ProcessStatePlanTestHooks.h"
#include "ProcessStatePlanTestSupport.h"
#include "acir/Analysis/ProcessStatePlan.h"
#include "acir/InitAllDialects.h"

#include "gtest/gtest.h"

namespace acir {
namespace {

using PlanSetBuilder = detail::PlanSetBuilder;

static ProcessStatePlanSet getYieldOnlyPlan() {
  mlir::DialectRegistry registry;
  registerAllDialects(registry);
  mlir::MLIRContext context(registry);
  auto module = test::parseAndFreezeYieldOnly(context);
  auto built = PlanSetBuilder::buildYieldOnly(*module);
  assert(mlir::succeeded(built));
  return *built;
}

TEST(ProcessStatePlanControlFlowTest, YieldOnlyProducesEntryPcOneBlockOneWakeOneTransition) {
  auto plans = getYieldOnlyPlan();
  ASSERT_EQ(plans.processes().size(), 1u);
  const auto &process = plans.processes()[0];
  EXPECT_EQ(process.pcs().size(), 1u);
  EXPECT_EQ(process.pcs()[0].name(), "entry");
  EXPECT_EQ(process.pcs()[0].id().value(), 0u);
  EXPECT_EQ(process.blocks().size(), 1u);
  EXPECT_EQ(process.wakes().size(), 1u);
  EXPECT_EQ(process.wakes()[0].kind(), ProcessWakeKind::NextDelta);
  EXPECT_EQ(process.wakes()[0].typeKey(), "@acir_wake_next_delta");
  EXPECT_EQ(process.transitions().size(), 1u);
  EXPECT_EQ(process.transitions()[0].sourcePc().value(), 0u);
  EXPECT_EQ(process.transitions()[0].targetPc().value(), 0u);
  EXPECT_EQ(process.transitions()[0].wake().value(), 0u);
}

TEST(ProcessStatePlanControlFlowTest, EntryBlockHasSuspendEdge) {
  auto plans = getYieldOnlyPlan();
  const auto &process = plans.processes()[0];
  ASSERT_GE(process.blocks().size(), 1u);
  const auto &edge = process.blocks()[0].edge();
  EXPECT_EQ(edge.kind(), ProcessControlEdgeKind::Suspend);
}

TEST(ProcessStatePlanControlFlowTest, WakeHasCorrectKindAndTypeKey) {
  auto plans = getYieldOnlyPlan();
  const auto &process = plans.processes()[0];
  ASSERT_GE(process.wakes().size(), 1u);
  EXPECT_EQ(process.wakes()[0].typeKey(), "@acir_wake_next_delta");
  EXPECT_EQ(process.wakes()[0].kind(), ProcessWakeKind::NextDelta);
}

} // namespace
} // namespace acir
