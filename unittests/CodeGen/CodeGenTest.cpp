#include "acir/CodeGen/Emitter.h"
#include "acir/CodeGen/Manifest.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace acir::codegen {
namespace {

// ── Fingerprint ───────────────────────────────────────────────────────

TEST(CodeGenManifestTest, ComputeFingerprintDeterministic) {
  auto fp1 = computeFingerprint("hello");
  auto fp2 = computeFingerprint("hello");
  EXPECT_EQ(fp1, fp2);
  EXPECT_EQ(fp1.size(), 64u); // SHA-256 = 64 hex chars
}

TEST(CodeGenManifestTest, DifferentContentDifferentFingerprint) {
  auto fp1 = computeFingerprint("hello");
  auto fp2 = computeFingerprint("world");
  EXPECT_NE(fp1, fp2);
}

TEST(CodeGenManifestTest, CompositeFingerprintIsDeterministic) {
  auto fp1 = compositeFingerprint({"aaa", "bbb"});
  auto fp2 = compositeFingerprint({"aaa", "bbb"});
  EXPECT_EQ(fp1, fp2);
}

TEST(CodeGenManifestTest, CompositeReordersChangeFingerprint) {
  auto fp1 = compositeFingerprint({"aaa", "bbb"});
  auto fp2 = compositeFingerprint({"bbb", "aaa"});
  EXPECT_NE(fp1, fp2);
}

TEST(CodeGenManifestTest, BuildManifestFinalize) {
  BuildManifest m;
  m.inputFingerprint = computeFingerprint("input");
  m.toolchainFingerprint = computeFingerprint("clang-20");
  SourceFile sf;
  sf.relativePath = "test.cpp";
  sf.content = "int main() {}";
  sf.fingerprint = computeFingerprint(sf.content);
  m.sources.push_back(sf);
  m.finalize();
  EXPECT_FALSE(m.outputFingerprint.empty());
}

TEST(CodeGenManifestTest, CacheKeyToString) {
  CacheKey key{"aaaa", "bbbb", "cccc", "dddd"};
  auto str = key.toString();
  EXPECT_FALSE(str.empty());
  EXPECT_NE(str.find("aaaa"), std::string::npos);
}

TEST(CodeGenManifestTest, CacheKeyFromManifest) {
  BuildManifest m;
  m.inputFingerprint = computeFingerprint("in");
  m.toolchainFingerprint = computeFingerprint("tc");
  m.profileFingerprint = computeFingerprint("pf");
  m.bindingFingerprint = computeFingerprint("bd");
  auto key = cacheKeyFromManifest(m);
  EXPECT_EQ(key.inputFingerprint, m.inputFingerprint);
  EXPECT_EQ(key.toolchainFingerprint, m.toolchainFingerprint);
}

// ── CppEmitter ────────────────────────────────────────────────────────

TEST(CodeGenEmitterTest, EmitsPragmaOnce) {
  std::ostringstream os;
  CppEmitter e(os);
  e.emitPragmaOnce();
  EXPECT_EQ(os.str(), "#pragma once\n\n");
}

TEST(CodeGenEmitterTest, EmitsInclude) {
  std::ostringstream os;
  CppEmitter e(os);
  e.emitInclude("string", true);
  e.emitInclude("local.h");
  EXPECT_NE(os.str().find("<string>"), std::string::npos);
  EXPECT_NE(os.str().find("\"local.h\""), std::string::npos);
}

TEST(CodeGenEmitterTest, EmitsNamespace) {
  std::ostringstream os;
  CppEmitter e(os);
  e.beginNamespace("test");
  e.emitComment("hello");
  e.endNamespace();
  EXPECT_NE(os.str().find("namespace test"), std::string::npos);
  EXPECT_NE(os.str().find("// hello"), std::string::npos);
}

TEST(CodeGenEmitterTest, EmitsClassWithBase) {
  std::ostringstream os;
  CppEmitter e(os);
  e.beginClass("Foo", "Bar");
  e.emitPublic();
  e.endClass();
  EXPECT_NE(os.str().find("class Foo : public Bar"), std::string::npos);
  EXPECT_NE(os.str().find("public:"), std::string::npos);
}

TEST(CodeGenEmitterTest, EmitsEnum) {
  std::ostringstream os;
  CppEmitter e(os);
  e.emitEnum("Color", {"Red", "Green", "Blue"});
  EXPECT_NE(os.str().find("enum class Color"), std::string::npos);
  EXPECT_NE(os.str().find("Red"), std::string::npos);
  EXPECT_NE(os.str().find("Green"), std::string::npos);
}

TEST(CodeGenEmitterTest, EmitsConstructor) {
  std::ostringstream os;
  CppEmitter e(os);
  e.emitConstructor("Foo", {{"int", "x"}, {"int", "y"}}, {"x_(x)", "y_(y)"},
                    "  init();\n");
  EXPECT_NE(os.str().find("Foo(int x, int y)"), std::string::npos);
  EXPECT_NE(os.str().find(": x_(x)"), std::string::npos);
  EXPECT_NE(os.str().find("init()"), std::string::npos);
}

TEST(CodeGenEmitterTest, EmitsMethod) {
  std::ostringstream os;
  CppEmitter e(os);
  e.emitMethod("void", "run", {}, true, true, true);
  auto s = os.str();
  EXPECT_NE(s.find("virtual void run() const override"), std::string::npos);
}

TEST(CodeGenEmitterTest, EmitsSwitch) {
  std::ostringstream os;
  CppEmitter e(os);
  e.emitSwitch("x");
  e.emitCase("1");
  e.emitBreak();
  e.emitDefault();
  e.emitBreak();
  e.endSwitch();
  EXPECT_NE(os.str().find("switch (x)"), std::string::npos);
  EXPECT_NE(os.str().find("case 1:"), std::string::npos);
  EXPECT_NE(os.str().find("default:"), std::string::npos);
}

// ── Deterministic code generation ─────────────────────────────────────

TEST(CodeGenGenTest, GenerateProcessHeader) {
  auto sf = generateProcessHeader("Top", "workload", {"entry", "pc00000001"},
                                  {"uint32_t"}, {"counter_"});
  EXPECT_FALSE(sf.content.empty());
  EXPECT_FALSE(sf.fingerprint.empty());
  EXPECT_EQ(sf.fingerprint.size(), 64u);
  EXPECT_NE(sf.content.find("Top_workload"), std::string::npos);
  EXPECT_NE(sf.content.find("class Top_workload"), std::string::npos);
  EXPECT_NE(sf.content.find("Pc"), std::string::npos);
  EXPECT_NE(sf.content.find("entry"), std::string::npos);
}

TEST(CodeGenGenTest, GenerateProcessHeaderIsDeterministic) {
  auto sf1 = generateProcessHeader("Top", "workload", {"entry"}, {}, {});
  auto sf2 = generateProcessHeader("Top", "workload", {"entry"}, {}, {});
  EXPECT_EQ(sf1.content, sf2.content);
  EXPECT_EQ(sf1.fingerprint, sf2.fingerprint);
}

TEST(CodeGenGenTest, GenerateProcessSource) {
  auto sf = generateProcessSource("Top", "workload", {"entry", "pc00000001"},
                                  {"uint32_t"}, {"counter_"});
  EXPECT_FALSE(sf.content.empty());
  EXPECT_NE(sf.content.find("Top_workload::doWork"), std::string::npos);
  EXPECT_NE(sf.content.find("switch (pc_)"), std::string::npos);
  EXPECT_NE(sf.content.find("Pc::entry"), std::string::npos);
}

TEST(CodeGenGenTest, GenerateModuleHeader) {
  auto sf = generateModuleHeader("Top", {"alu", "mem", "ctrl"});
  EXPECT_FALSE(sf.content.empty());
  EXPECT_NE(sf.content.find("TopModule"), std::string::npos);
  EXPECT_NE(sf.content.find("alu_"), std::string::npos);
  EXPECT_NE(sf.content.find("mem_"), std::string::npos);
}

TEST(CodeGenGenTest, GenerateModuleSource) {
  auto sf = generateModuleSource("Top", {"alu"});
  EXPECT_FALSE(sf.content.empty());
  EXPECT_NE(sf.content.find("TopModule::build"), std::string::npos);
  EXPECT_NE(sf.content.find("addChild"), std::string::npos);
}

TEST(CodeGenGenTest, GenerateDispatchHeaderIsDenseAndDeterministic) {
  std::vector<DispatchEntry> entries = {
      {1, "model.consumer"},
      {0, "model.producer"},
  };
  std::vector<ActivationEdge> edges = {{1, 1}, {0, 1}, {0, 0}, {0, 1}};
  auto first =
      generateDispatchHeader("generated::soc", "SocModel", entries, edges);
  std::reverse(entries.begin(), entries.end());
  std::reverse(edges.begin(), edges.end());
  auto second =
      generateDispatchHeader("generated::soc", "SocModel", entries, edges);

  constexpr std::string_view expected = R"(#pragma once

#include "gfsim/dispatch.h"

#include <array>
#include <cstdint>

namespace generated::soc {

inline std::array<gfsim::DispatchRow, 2>
makeDispatchTable(SocModel &model) {
  return {
      gfsim::makeDispatchRow(&model.producer),
      gfsim::makeDispatchRow(&model.consumer),
  };
}

inline constexpr std::array<uint32_t, 3> kActivationOffsets = {0, 2, 3};
inline constexpr std::array<gfsim::ObjectId, 3> kActivationTargets = {0, 1, 1};

} // namespace generated::soc
)";
  EXPECT_EQ(first.content, expected);
  EXPECT_EQ(first.content, second.content);
  EXPECT_EQ(first.fingerprint, second.fingerprint);
  EXPECT_NE(first.content.find("gfsim/dispatch.h"), std::string::npos);
  EXPECT_NE(first.content.find("std::array<gfsim::DispatchRow, 2>"),
            std::string::npos);
  EXPECT_NE(first.content.find("makeDispatchTable(SocModel &model)"),
            std::string::npos);
  EXPECT_EQ(first.relativePath, "include/generated/soc/dispatch.h");
  size_t producer = first.content.find("model.producer");
  size_t consumer = first.content.find("model.consumer");
  ASSERT_NE(producer, std::string::npos);
  ASSERT_NE(consumer, std::string::npos);
  EXPECT_LT(producer, consumer);
}

TEST(CodeGenGenTest, GenerateDispatchHeaderRejectsNonDenseIds) {
  EXPECT_THROW(generateDispatchHeader("generated::soc", "SocModel",
                                      {{1, "model.consumer"}}),
               std::invalid_argument);
}

TEST(CodeGenGenTest, GenerateDispatchHeaderRejectsInvalidActivationEdge) {
  EXPECT_THROW(generateDispatchHeader("generated::soc", "SocModel",
                                      {{0, "model.producer"}}, {{0, 1}}),
               std::invalid_argument);
}

} // namespace
} // namespace acir::codegen
