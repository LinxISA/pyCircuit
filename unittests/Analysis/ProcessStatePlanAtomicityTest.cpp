#include "Analysis/ProcessStatePlanInternal.h"
#include "Analysis/ProcessStatePlanTestHooks.h"
#include "ProcessStatePlanTestSupport.h"
#include "acir/Analysis/ProcessStatePlan.h"
#include "acir/InitAllDialects.h"

#include "gtest/gtest.h"

namespace acir {
namespace {

using PlanSetBuilder = detail::PlanSetBuilder;

TEST(ProcessStatePlanAtomicityTest, SerializationIsDeterministic) {
  mlir::DialectRegistry registry;
  registerAllDialects(registry);
  mlir::MLIRContext context(registry);
  auto module = test::parseAndFreezeYieldOnly(context);
  auto plans = PlanSetBuilder::buildYieldOnly(*module);
  ASSERT_TRUE(mlir::succeeded(plans));
  auto result1 = serializeProcessStatePlan(*plans);
  auto result2 = serializeProcessStatePlan(*plans);
  ASSERT_TRUE(static_cast<bool>(result1));
  ASSERT_TRUE(static_cast<bool>(result2));
  EXPECT_EQ(*result1, *result2);
}

TEST(ProcessStatePlanAtomicityTest, CloneWithMissingWakeCalleeFailsVerification) {
  mlir::DialectRegistry registry;
  registerAllDialects(registry);
  mlir::MLIRContext context(registry);
  auto module = test::parseAndFreezeYieldOnly(context);
  auto plans = PlanSetBuilder::buildYieldOnly(*module);
  ASSERT_TRUE(mlir::succeeded(plans));
  auto clone = PlanSetBuilder::cloneWithMissingWakeCallee(*plans);
  auto result = verifyProcessStatePlan(clone);
  EXPECT_TRUE(mlir::failed(result));
}

TEST(ProcessStatePlanAtomicityTest, CloneWithDanglingSuspendFailsVerification) {
  mlir::DialectRegistry registry;
  registerAllDialects(registry);
  mlir::MLIRContext context(registry);
  auto module = test::parseAndFreezeYieldOnly(context);
  auto plans = PlanSetBuilder::buildYieldOnly(*module);
  ASSERT_TRUE(mlir::succeeded(plans));
  auto clone = PlanSetBuilder::cloneWithDanglingSuspendTransition(*plans);
  auto result = verifyProcessStatePlan(clone);
  EXPECT_TRUE(mlir::failed(result));
}

} // namespace
} // namespace acir
