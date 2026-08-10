#include "acir/CodeGen/Generator.h"
#include "acir/Dialect/ACSim/ACSimDialect.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Index/IR/IndexDialect.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace acir::codegen {
namespace {

bool hasError(llvm::Error error) {
  if (!error)
    return false;
  llvm::consumeError(std::move(error));
  return true;
}

llvm::Expected<ModelPlan> fixturePlan(mlir::MLIRContext &context) {
  context
      .loadDialect<acsim::ACSimDialect, mlir::arith::ArithDialect,
                   mlir::cf::ControlFlowDialect, mlir::index::IndexDialect>();
  auto file =
      mlir::parseSourceFile<mlir::ModuleOp>(ACSIM_VALID_TEST_FILE, &context);
  if (!file)
    return llvm::createStringError("failed to parse canonical ACSim fixture");
  return buildModelPlan(*file);
}

const GeneratedFile *findFile(const SourceBundle &bundle,
                              llvm::StringRef path) {
  auto found = std::find_if(
      bundle.files.begin(), bundle.files.end(),
      [&](const GeneratedFile &file) { return file.relativePath == path; });
  return found == bundle.files.end() ? nullptr : &*found;
}

TEST(GeneratorTest, EmitsExactOrderedFileSetAndTypedOwnership) {
  mlir::MLIRContext context;
  auto plan = fixturePlan(context);
  ASSERT_TRUE(static_cast<bool>(plan));
  auto bundle = generateModelSources(*plan);
  if (!bundle) {
    ADD_FAILURE() << llvm::toString(bundle.takeError());
    return;
  }

  std::vector<std::string> paths;
  for (const GeneratedFile &file : bundle->files)
    paths.push_back(file.relativePath);
  const std::vector<std::string> expected = {
      "include/generated/dispatch.h",
      "include/generated/model.h",
      "include/generated/modules/Top_s2100000000000000.h",
      "include/generated/processes/tick_s2300000000000000.h",
      "src/generated/main.cpp",
      "src/generated/model.cpp",
      "src/generated/modules/Top_s2100000000000000.cpp",
      "src/generated/processes/tick_s2300000000000000.cpp"};
  EXPECT_EQ(paths, expected);

  const GeneratedFile *header =
      findFile(*bundle, "include/generated/modules/Top_s2100000000000000.h");
  const GeneratedFile *source =
      findFile(*bundle, "src/generated/modules/Top_s2100000000000000.cpp");
  ASSERT_NE(header, nullptr);
  ASSERT_NE(source, nullptr);
  EXPECT_NE(header->content.find(
                "class Top_s2100000000000000 final : public gfsim::Module"),
            std::string::npos);
  EXPECT_NE(header->content.find("gfsim::Fifo fifo_;"), std::string::npos);
  EXPECT_NE(header->content.find("std::array<gfsim::Fifo, 2> lanes_;"),
            std::string::npos);
  EXPECT_NE(source->content.find("attachChild(fifo_)"), std::string::npos);
  EXPECT_FALSE(hasError(validateSourceBundle(*plan, *bundle)));
}

TEST(GeneratorTest, RepeatedGenerationIsByteIdentical) {
  mlir::MLIRContext context;
  auto plan = fixturePlan(context);
  ASSERT_TRUE(static_cast<bool>(plan));
  auto first = generateModelSources(*plan);
  auto second = generateModelSources(*plan);
  ASSERT_TRUE(static_cast<bool>(first));
  ASSERT_TRUE(static_cast<bool>(second));
  ASSERT_EQ(first->files.size(), second->files.size());
  for (size_t index = 0; index < first->files.size(); ++index) {
    EXPECT_EQ(first->files[index].relativePath,
              second->files[index].relativePath);
    EXPECT_EQ(first->files[index].content, second->files[index].content);
    EXPECT_EQ(first->files[index].fingerprint,
              second->files[index].fingerprint);
  }
}

TEST(GeneratorTest, RejectsExecutableCppInStructuredMetadata) {
  mlir::MLIRContext context;
  auto plan = fixturePlan(context);
  ASSERT_TRUE(static_cast<bool>(plan));
  plan->bindings[1].cppSymbol = "gfsim::Fifo; system(\"bad\")";

  auto bundle = generateModelSources(*plan);
  ASSERT_FALSE(bundle);
  EXPECT_TRUE(hasError(bundle.takeError()));
}

TEST(GeneratorTest, RejectsExecutableThunkInStructuredMetadata) {
  mlir::MLIRContext context;
  auto plan = fixturePlan(context);
  ASSERT_TRUE(static_cast<bool>(plan));
  plan->bindings[1].entryPoints.work = "fifo_work; inject()";

  auto bundle = generateModelSources(*plan);
  ASSERT_FALSE(bundle);
  EXPECT_TRUE(hasError(bundle.takeError()));
}

} // namespace
} // namespace acir::codegen
