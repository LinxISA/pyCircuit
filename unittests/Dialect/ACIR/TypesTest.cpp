#include "acir/Dialect/ACIR/ACIRDialect.h"
#include "acir/Dialect/ACIR/ACIRTypes.h"

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/MLIRContext.h"
#include "gtest/gtest.h"

namespace acir::ac {
namespace {

TEST(ACIRTypesTest, TypesAreUniquedByTheirParameters) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();

  auto payload = mlir::IntegerType::get(&context, 32);
  EXPECT_EQ(OptionalType::get(&context, payload),
            OptionalType::get(&context, payload));
  EXPECT_NE(VectorType::get(&context, int64_t{4}, mlir::Type(payload)),
            VectorType::get(&context, int64_t{8}, mlir::Type(payload)));
}

TEST(ACIRTypesTest, NamedTypesPreserveTheirIdentity) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();

  auto lhs = mlir::FlatSymbolRefAttr::get(&context, "Left");
  auto rhs = mlir::FlatSymbolRefAttr::get(&context, "Right");
  EXPECT_NE(StructType::get(&context, lhs), StructType::get(&context, rhs));
}

TEST(ACIRTypesTest, CheckedBuildersRejectInvalidParameters) {
  mlir::MLIRContext context;
  context.loadDialect<ACIRDialect>();
  auto payload = mlir::IntegerType::get(&context, 8);
  mlir::ScopedDiagnosticHandler suppressExpectedDiagnostics(
      &context, [](mlir::Diagnostic &) { return mlir::success(); });

  auto location = mlir::UnknownLoc::get(&context);
  auto emitError = [location] { return mlir::emitError(location); };
  EXPECT_FALSE(VectorType::getChecked(emitError, &context, int64_t{0},
                                      mlir::Type(payload)));
  EXPECT_FALSE(DurationType::getChecked(emitError, &context, Unit::Bytes));
  EXPECT_FALSE(
      RateType::getChecked(emitError, &context, Unit::Cycles, Unit::Cycles));
}

} // namespace
} // namespace acir::ac
