#include "acir/CodeGen/ModelPlan.h"
#include "acir/Dialect/ACSim/ACSimDialect.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Index/IR/IndexDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"

#include <string>
#include <utility>
#include <vector>

namespace acir::codegen {
namespace {

void loadACSimDialects(mlir::MLIRContext &context) {
  context
      .loadDialect<acsim::ACSimDialect, mlir::arith::ArithDialect,
                   mlir::cf::ControlFlowDialect, mlir::index::IndexDialect>();
}

bool hasError(llvm::Error error) {
  if (!error)
    return false;
  llvm::consumeError(std::move(error));
  return true;
}

TEST(ModelPlanTest, ExtractsClosedIdentitiesTypesAndDenseRuntimePlan) {
  mlir::MLIRContext context;
  loadACSimDialects(context);
  auto file =
      mlir::parseSourceFile<mlir::ModuleOp>(ACSIM_VALID_TEST_FILE, &context);
  ASSERT_TRUE(file);

  auto plan = buildModelPlan(*file);
  if (!plan) {
    ADD_FAILURE() << llvm::toString(plan.takeError());
    return;
  }

  EXPECT_EQ(plan->modelSymbol, "demo");
  EXPECT_EQ(plan->rootSymbol, "Top");
  EXPECT_EQ(plan->contractEpoch, "0.1");
  EXPECT_EQ(plan->frozenAcirFingerprint,
            "sha256:"
            "0000000000000000000000000000000000000000000000000000000000000001");
  EXPECT_EQ(plan->bindingLockFingerprint,
            "sha256:"
            "0000000000000000000000000000000000000000000000000000000000000002");
  EXPECT_EQ(plan->providerFingerprint,
            "sha256:"
            "0000000000000000000000000000000000000000000000000000000000000003");
  EXPECT_EQ(plan->profileFingerprint,
            "sha256:"
            "0000000000000000000000000000000000000000000000000000000000000004");
  EXPECT_EQ(plan->toolchainFingerprint,
            "sha256:"
            "0000000000000000000000000000000000000000000000000000000000000005");
  EXPECT_EQ(plan->schemaSetFingerprint,
            "sha256:"
            "0000000000000000000000000000000000000000000000000000000000000006");

  ASSERT_EQ(plan->types.size(), 24u);
  EXPECT_EQ(plan->types.front().symbol, "comb_domain");
  EXPECT_EQ(plan->types.front().kind, TypeKind::TimeDomain);
  EXPECT_EQ(plan->types.front().cppType, "gfsim::CombinationalDomain");
  EXPECT_EQ(plan->types.back().symbol, "target");
  EXPECT_EQ(plan->types.back().kind, TypeKind::Role);

  ASSERT_EQ(plan->runtimeObjects.size(), 4u);
  for (size_t index = 0; index < plan->runtimeObjects.size(); ++index) {
    EXPECT_EQ(plan->runtimeObjects[index].objectId, index);
    EXPECT_EQ(plan->runtimeObjects[index].activationId, index);
  }
  EXPECT_EQ(plan->runtimeObjects[0].targetSymbol, "Top::fifo");
  EXPECT_EQ(plan->runtimeObjects[0].hierarchyPath, "Top.fifo");
  EXPECT_EQ(plan->runtimeObjects[1].indices, (std::vector<uint64_t>{0}));
  EXPECT_EQ(plan->runtimeObjects[2].indices, (std::vector<uint64_t>{1}));
  EXPECT_EQ(plan->runtimeObjects[3].objectKind, RuntimeObjectKind::Process);

  const std::vector<ActivationEdgePlan> expectedEdges = {
      {0, 0}, {1, 1}, {1, 2}, {1, 3}, {2, 2}, {3, 3}};
  EXPECT_EQ(plan->activationEdges, expectedEdges);
  EXPECT_FALSE(hasError(validateModelPlan(*plan)));
}

TEST(ModelPlanTest, RejectsInputWithoutOneCanonicalACSimModel) {
  mlir::MLIRContext context;
  loadACSimDialects(context);
  auto file =
      mlir::parseSourceString<mlir::ModuleOp>("builtin.module {}", &context);
  ASSERT_TRUE(file);

  auto plan = buildModelPlan(*file);
  ASSERT_FALSE(plan);
  EXPECT_TRUE(hasError(plan.takeError()));
}

TEST(ModelPlanTest, ValidationRejectsDestructionThatIsNotReverseConstruction) {
  mlir::MLIRContext context;
  loadACSimDialects(context);
  auto file =
      mlir::parseSourceFile<mlir::ModuleOp>(ACSIM_VALID_TEST_FILE, &context);
  ASSERT_TRUE(file);
  auto plan = buildModelPlan(*file);
  if (!plan) {
    ADD_FAILURE() << llvm::toString(plan.takeError());
    return;
  }
  ASSERT_GE(plan->destructionOrder.size(), 2u);

  std::swap(plan->destructionOrder[0], plan->destructionOrder[1]);
  EXPECT_TRUE(hasError(validateModelPlan(*plan)));
}

} // namespace
} // namespace acir::codegen
