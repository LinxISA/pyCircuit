#include "acir/CodeGen/Build.h"

#include "gtest/gtest.h"

#include <string>

namespace acir::codegen {
namespace {

constexpr llvm::StringLiteral kFingerprint =
    "sha256:0000000000000000000000000000000000000000000000000000000000000000";

bool hasError(llvm::Error error) {
  if (!error)
    return false;
  llvm::consumeError(std::move(error));
  return true;
}

llvm::Expected<BuildRequest> makeBuildRequest() {
  auto toolchain = identifyToolchain(ACIR_TEST_CXX_COMPILER, "libc++",
                                     "default", "mach-o", {"-std=c++20"});
  if (!toolchain)
    return toolchain.takeError();
  BuildRequest request;
  request.project = {"project", "project.example"};
  request.system = {"system", "system.example"};
  request.profile = "fast";
  request.toolchain = std::move(*toolchain);
  request.includeRoots = {"vendor/include", "include"};
  request.definitions = {"ZETA=1", "ALPHA=1"};
  request.compilerFlags = {"-Wall"};
  request.linkerFlags = {"-pthread"};
  request.linkInputs = {"lib/runtime.a"};
  request.outputRoot = "out";
  return request;
}

SourceBundle makeSourceBundle() {
  SourceBundle bundle;
  bundle.buildFingerprint = kFingerprint.str();
  bundle.files = {
      {.relativePath = "include/generated/model.h",
       .content = "#pragma once\n",
       .fingerprint = computeFingerprint("#pragma once\n")},
      {.relativePath = "src/generated/main.cpp",
       .content = "int main() { return 0; }\n",
       .fingerprint = computeFingerprint("int main() { return 0; }\n")}};
  return bundle;
}

TEST(BuildTest, CompilePlanIsClosedCanonicalAndArgumentVectorBased) {
  auto request = makeBuildRequest();
  ASSERT_TRUE(static_cast<bool>(request));
  auto first = createCompilePlan(*request, makeSourceBundle());
  auto second = createCompilePlan(*request, makeSourceBundle());
  ASSERT_TRUE(static_cast<bool>(first));
  ASSERT_TRUE(static_cast<bool>(second));
  EXPECT_EQ(first->schema, "acsim-compile-plan-0.1");
  EXPECT_TRUE(isValidFingerprint(first->fingerprint));
  EXPECT_EQ(first->fingerprint, second->fingerprint);
  EXPECT_EQ(first->includeRoots,
            (std::vector<std::string>{"include", "vendor/include"}));
  EXPECT_EQ(first->definitions,
            (std::vector<std::string>{"ALPHA=1", "ZETA=1"}));
  ASSERT_EQ(first->compileCommands.size(), 1u);
  EXPECT_EQ(first->compileCommands[0].arguments.front(),
            request->toolchain.compilerPath);
  EXPECT_EQ(first->compileCommands[0].arguments.back(),
            first->compileCommands[0].output);
  EXPECT_FALSE(first->linkCommand.arguments.empty());
  auto firstJson = first->canonicalJson();
  auto secondJson = second->canonicalJson();
  ASSERT_TRUE(static_cast<bool>(firstJson));
  ASSERT_TRUE(static_cast<bool>(secondJson));
  EXPECT_EQ(*firstJson, *secondJson);
}

TEST(BuildTest, RejectsToolchainOrPrebuiltProvenanceMismatch) {
  auto request = makeBuildRequest();
  ASSERT_TRUE(static_cast<bool>(request));
  request->prebuiltInputs.push_back(
      {.path = "lib/provider.o",
       .kind = "provider",
       .provenance = {.compilerBuildId = request->toolchain.compilerBuildId,
                      .targetTriple = request->toolchain.targetTriple,
                      .standardLibrary = request->toolchain.standardLibrary,
                      .abiMode = request->toolchain.abiMode,
                      .objectFormat = request->toolchain.objectFormat,
                      .contractEpoch = "0.1",
                      .contractFlags = request->toolchain.contractFlags,
                      .toolchainFingerprint = request->toolchain.fingerprint,
                      .sourceFingerprint = kFingerprint.str()}});
  EXPECT_FALSE(hasError(preflightBuildRequest(*request)));

  request->prebuiltInputs.front().provenance.compilerBuildId = "different";
  EXPECT_TRUE(hasError(preflightBuildRequest(*request)));

  request->prebuiltInputs.front().sourceAvailable = true;
  EXPECT_FALSE(hasError(preflightBuildRequest(*request)));
  auto plan = createCompilePlan(*request, makeSourceBundle());
  ASSERT_TRUE(static_cast<bool>(plan));
  EXPECT_TRUE(plan->prebuiltInputs.empty());
}

TEST(BuildTest, RejectsProfileAndToolchainIdentityMismatch) {
  auto request = makeBuildRequest();
  ASSERT_TRUE(static_cast<bool>(request));
  request->profile = "debug";
  EXPECT_TRUE(hasError(preflightBuildRequest(*request)));

  request = makeBuildRequest();
  ASSERT_TRUE(static_cast<bool>(request));
  request->toolchain.targetTriple = "different-target";
  EXPECT_TRUE(hasError(preflightBuildRequest(*request)));
}

TEST(BuildTest, RejectsNonCanonicalPathsAndSourceFingerprintMismatch) {
  auto request = makeBuildRequest();
  ASSERT_TRUE(static_cast<bool>(request));
  request->includeRoots = {"../escape"};
  auto plan = createCompilePlan(*request, makeSourceBundle());
  EXPECT_FALSE(plan);
  llvm::consumeError(plan.takeError());

  request = makeBuildRequest();
  ASSERT_TRUE(static_cast<bool>(request));
  SourceBundle bundle = makeSourceBundle();
  bundle.files.back().fingerprint = kFingerprint.str();
  plan = createCompilePlan(*request, bundle);
  EXPECT_FALSE(plan);
  llvm::consumeError(plan.takeError());
}

} // namespace
} // namespace acir::codegen
