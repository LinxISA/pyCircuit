#include "Analysis/ModelAnalysisInternal.h"
#include "Analysis/ProcessStatePlanInternal.h"
#include "ProcessStatePlanTestSupport.h"
#include "acir/InitAllDialects.h"
#include "acir/Transforms/Passes.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/Pass/PassInstrumentation.h"
#include "mlir/Pass/PassManager.h"
#include "gtest/gtest.h"

#include <string>
#include <vector>

namespace acir {
namespace {

class NormalizeTrace final : public mlir::PassInstrumentation {
public:
  void runBeforePass(mlir::Pass *pass, mlir::Operation *) override {
    events.push_back(("enter:" + pass->getArgument()).str());
  }
  void runAfterPass(mlir::Pass *pass, mlir::Operation *) override {
    events.push_back(("complete:" + pass->getArgument()).str());
  }
  void runAfterPassFailed(mlir::Pass *pass, mlir::Operation *) override {
    events.push_back(("fail:" + pass->getArgument()).str());
  }

  std::vector<std::string> events;
};

mlir::OwningOpRef<mlir::ModuleOp> buildRawDepthFixture(mlir::MLIRContext &ctx,
                                                       uint64_t depth,
                                                       bool malformed = false) {
  constexpr llvm::StringLiteral source = R"mlir(
    builtin.module {
      ac.module @M() parameters {} graph {
        ac.address_space @source width 8 unit "byte" id "source" path "source"
        ac.address_space @target width 8 unit "byte" id "target" path "target"
        ac.address_map @map source @source entries [
          {base = 8 : i64, size = 1 : i64, target = @target, offset = 0 : i64,
           permissions = ["write", "execute"], classes = []},
          {base = 0 : i64, size = 1 : i64, target = @target, offset = 0 : i64,
           permissions = ["read"], classes = []}
        ] default {kind = "unmapped"}
        ac.return
      }
    }
  )mlir";
  auto root = mlir::parseSourceString<mlir::ModuleOp>(source, &ctx);
  if (!root)
    return {};
  mlir::ModuleOp parent = *root;
  for (uint64_t index = 0; index < depth; ++index) {
    auto nested = mlir::ModuleOp::create(mlir::UnknownLoc::get(&ctx));
    parent.getBody()->push_back(nested);
    parent = nested;
  }
  if (malformed) {
    mlir::OperationState state(parent.getLoc(), "scf.yield");
    parent.getBody()->push_back(mlir::Operation::create(state));
  }
  return root;
}

struct NormalizeRun {
  mlir::LogicalResult result;
  std::vector<std::string> events;
  std::vector<std::string> diagnostics;
};

NormalizeRun runIsolatedNormalize(mlir::ModuleOp module) {
  std::vector<std::string> diagnostics;
  mlir::ScopedDiagnosticHandler handler(
      module.getContext(), [&](mlir::Diagnostic &diagnostic) {
        std::string text;
        llvm::raw_string_ostream(text) << diagnostic;
        diagnostics.push_back(std::move(text));
        return mlir::success();
      });
  auto trace = std::make_unique<NormalizeTrace>();
  NormalizeTrace *tracePtr = trace.get();
  mlir::PassManager manager(module.getContext());
  manager.enableVerifier(false);
  manager.addInstrumentation(std::move(trace));
  manager.addPass(createNormalizeACIRFilePass());
  mlir::LogicalResult result = manager.run(module);
  return {result, std::move(tracePtr->events), std::move(diagnostics)};
}

void loadDialects(mlir::MLIRContext &context) {
  mlir::DialectRegistry registry;
  registerAllDialects(registry);
  context.appendDialectRegistry(registry);
}

TEST(ProcessStatePlanNormalizeFactoryTest,
     Depth512ReachesAndSucceedsThroughIsolatedNormalizePass) {
  mlir::MLIRContext context;
  loadDialects(context);
  auto module = buildRawDepthFixture(context, 512);
  ASSERT_TRUE(module);

  NormalizeRun run = runIsolatedNormalize(*module);
  EXPECT_TRUE(mlir::succeeded(run.result));
  EXPECT_EQ(run.events,
            (std::vector<std::string>{"enter:normalize-ac-file",
                                      "complete:normalize-ac-file"}));
  EXPECT_TRUE(run.diagnostics.empty());
  std::string normalized = test::moduleText(*module);
  EXPECT_NE(normalized.find("permissions = [\"execute\", \"write\"]"),
            std::string::npos);
}

TEST(ProcessStatePlanPureCallTest,
     ExpandsNestedMultiResultCallsWithCallSiteQualifiedIdentity) {
  mlir::MLIRContext context;
  loadDialects(context);
  constexpr llvm::StringLiteral source = R"mlir(
    builtin.module attributes {ac.contract_epoch = "0.1"} {
      func.func @leaf(%arg : index) -> (index, index) {
        %one = arith.constant 1 : index
        %next = arith.addi %arg, %one : index
        return %next, %arg : index, index
      }
      func.func @middle(%arg : index) -> index {
        %next, %old = func.call @leaf(%arg) : (index) -> (index, index)
        return %next : index
      }
      ac.module @Top() parameters {} graph {
        ac.process @workload kind "workload" {
          %zero = arith.constant 0 : index
          %a = func.call @middle(%zero) : (index) -> index
          %b = func.call @middle(%a) : (index) -> index
          ac.yield_sim
        }
        ac.return
      }
    }
  )mlir";
  auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
  ASSERT_TRUE(module);
  ac::ProcessOp process;
  module->walk([&](ac::ProcessOp candidate) { process = candidate; });
  ASSERT_TRUE(process);

  auto expanded = detail::expandProcessForPlanning(process);
  ASSERT_TRUE(mlir::succeeded(expanded));
  unsigned leafAdds = 0;
  std::vector<std::string> outerCallPaths;
  std::vector<std::string> occurrenceJson;
  std::vector<std::string> occurrenceHashes;
  for (const detail::ExpandedAction &action : expanded->actions) {
    ASSERT_NE(action.operation->getName().getStringRef(), "func.call");
    ASSERT_NE(action.operation->getName().getStringRef(), "func.return");
    if (action.operation->getName().getStringRef() != "arith.addi")
      continue;
    ++leafAdds;
    ASSERT_EQ(action.callSites.size(), 2u);
    EXPECT_EQ(action.operationPath, "@Top::@workload/func/@leaf/r0/b0/o1");
    EXPECT_EQ(action.callSites.back().operationPath(),
              "@Top::@workload/func/@middle/r0/b0/o0");
    auto serialized =
        detail::canonicalProcessOccurrenceJSON(*action.occurrence);
    ASSERT_TRUE(static_cast<bool>(serialized));
    occurrenceJson.push_back(*serialized);
    auto hash = detail::hashProcessOccurrence(*action.occurrence);
    ASSERT_TRUE(static_cast<bool>(hash));
    occurrenceHashes.push_back(*hash);
    outerCallPaths.push_back(action.callSites.front().operationPath().str());
    ASSERT_FALSE(action.results.empty());
    EXPECT_EQ(action.results.front()
                  .original()
                  .occurrence()
                  .original()
                  .callSites()
                  .size(),
              2u);
  }
  EXPECT_EQ(leafAdds, 2u);
  ASSERT_EQ(outerCallPaths.size(), 2u);
  EXPECT_NE(outerCallPaths[0], outerCallPaths[1]);
  ASSERT_EQ(occurrenceJson.size(), 2u);
  EXPECT_NE(occurrenceJson[0], occurrenceJson[1]);
  EXPECT_NE(occurrenceHashes[0], occurrenceHashes[1]);
  EXPECT_EQ(occurrenceHashes[0].size(), 64u);
  EXPECT_EQ(occurrenceHashes[1].size(), 64u);
  EXPECT_NE(
      occurrenceJson[0].find("\"operation_path\":\"@Top::@workload/r0/b0/o1\""),
      std::string::npos);
  EXPECT_NE(
      occurrenceJson[1].find("\"operation_path\":\"@Top::@workload/r0/b0/o2\""),
      std::string::npos);
  EXPECT_GE(expanded->forwarding.size(), 8u);
}

TEST(ProcessStatePlanPureCallTest,
     ExpandsEveryStaticLoopIterationAndNestedCallSiteVector) {
  mlir::MLIRContext context;
  loadDialects(context);
  constexpr llvm::StringLiteral source = R"mlir(
    builtin.module attributes {ac.contract_epoch = "0.1"} {
      func.func @leaf(%arg : index) -> index {
        %one = arith.constant 1 : index
        %next = arith.addi %arg, %one : index
        return %next : index
      }
      ac.module @Top() parameters {} graph {
        ac.process @workload kind "workload" {
          %lb = arith.constant 0 : index
          %ub = arith.constant 2 : index
          %step = arith.constant 1 : index
          scf.for %i = %lb to %ub step %step {
            %value = func.call @leaf(%i) : (index) -> index
            scf.yield
          }
          ac.yield_sim
        }
        ac.return
      }
    }
  )mlir";
  auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
  ASSERT_TRUE(module);
  ac::ProcessOp process;
  module->walk([&](ac::ProcessOp candidate) { process = candidate; });
  auto expanded = detail::expandProcessForPlanning(process);
  ASSERT_TRUE(mlir::succeeded(expanded));

  std::vector<uint64_t> iterations;
  std::vector<uint32_t> constantOrdinals;
  for (const detail::ExpandedAction &action : expanded->actions) {
    if (action.kind == ProcessActionKind::Constant) {
      ASSERT_TRUE(action.occurrence.has_value());
      ASSERT_EQ(action.occurrence->kind(),
                ProcessOccurrenceKind::SyntheticConstant);
      constantOrdinals.push_back(
          action.occurrence->syntheticConstant().constant());
      EXPECT_EQ(action.occurrence->syntheticConstant()
                    .anchor()
                    .original()
                    .operationPath(),
                "@Top::@workload/r0/b0/o3");
      continue;
    }
    if (action.operation->getName().getStringRef() != "arith.addi")
      continue;
    ASSERT_EQ(action.iterationVector.size(), 1u);
    ASSERT_EQ(action.callSites.size(), 1u);
    EXPECT_TRUE(llvm::equal(action.callSites.front().iterationVector(),
                            action.iterationVector));
    iterations.push_back(action.iterationVector.front());
  }
  EXPECT_EQ(iterations, (std::vector<uint64_t>{0, 1}));
  EXPECT_EQ(constantOrdinals, (std::vector<uint32_t>{0, 1}));
}

TEST(ProcessStatePlanIdentityTest,
     LoopInvariantDefinitionKeepsOuterOccurrenceAcrossIterations) {
  mlir::MLIRContext context;
  loadDialects(context);
  constexpr llvm::StringLiteral source = R"mlir(
    builtin.module attributes {ac.contract_epoch = "0.1"} {
      ac.module @Top() parameters {} graph {
        ac.process @workload kind "workload" {
          %lb = arith.constant 0 : index
          %ub = arith.constant 2 : index
          %step = arith.constant 1 : index
          %invariant = arith.constant 7 : index
          scf.for %i = %lb to %ub step %step {
            %sum = arith.addi %i, %invariant : index
            scf.yield
          }
          ac.yield_sim
        }
        ac.return
      }
    }
  )mlir";
  auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
  ASSERT_TRUE(module);
  ac::ProcessOp process;
  module->walk([&](ac::ProcessOp candidate) { process = candidate; });
  auto expanded = detail::expandProcessForPlanning(process);
  ASSERT_TRUE(mlir::succeeded(expanded));

  std::vector<std::string> invariantOccurrences;
  std::vector<std::string> resultOccurrences;
  for (const detail::ExpandedAction &action : expanded->actions) {
    if (!action.operation ||
        action.operation->getName().getStringRef() != "arith.addi")
      continue;
    ASSERT_EQ(action.operands.size(), 2u);
    ASSERT_EQ(action.results.size(), 1u);
    auto invariant = detail::canonicalProcessOccurrenceJSON(
        action.operands[1].original().occurrence());
    auto result = detail::canonicalProcessOccurrenceJSON(
        action.results[0].original().occurrence());
    ASSERT_TRUE(static_cast<bool>(invariant));
    ASSERT_TRUE(static_cast<bool>(result));
    invariantOccurrences.push_back(*invariant);
    resultOccurrences.push_back(*result);
  }
  ASSERT_EQ(invariantOccurrences.size(), 2u);
  EXPECT_EQ(invariantOccurrences[0], invariantOccurrences[1]);
  EXPECT_NE(invariantOccurrences[0].find("\"iteration_vector\":[]"),
            std::string::npos);
  EXPECT_NE(resultOccurrences[0], resultOccurrences[1]);
}

TEST(ProcessStatePlanPureCallTest,
     SharedGraphAuthorityEnforcesExactFunctionEdgeAndDepthBounds) {
  mlir::MLIRContext context;
  loadDialects(context);
  constexpr llvm::StringLiteral source = R"mlir(
    builtin.module attributes {ac.contract_epoch = "0.1"} {
      func.func @leaf() {
        return
      }
      func.func @middle() {
        func.call @leaf() : () -> ()
        return
      }
      func.func @top() {
        func.call @middle() : () -> ()
        return
      }
      ac.module @Top() parameters {} graph {
        ac.process @workload kind "workload" {
          func.call @top() : () -> ()
          ac.yield_sim
        }
        ac.return
      }
    }
  )mlir";
  auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
  ASSERT_TRUE(module);
  mlir::ScopedDiagnosticHandler suppress(
      &context, [](mlir::Diagnostic &) { return mlir::success(); });
  EXPECT_TRUE(mlir::succeeded(detail::validatePureProcessCallGraph(
      *module, {}, detail::PureCallGraphLimits{3, 2, 3})));
  EXPECT_TRUE(mlir::failed(detail::validatePureProcessCallGraph(
      *module, {}, detail::PureCallGraphLimits{2, 2, 3})));
  EXPECT_TRUE(mlir::failed(detail::validatePureProcessCallGraph(
      *module, {}, detail::PureCallGraphLimits{3, 1, 3})));
  EXPECT_TRUE(mlir::failed(detail::validatePureProcessCallGraph(
      *module, {}, detail::PureCallGraphLimits{3, 2, 2})));
}

TEST(ProcessStatePlanPureCallTest,
     SharedGraphAuthorityRejectsEffectfulReachableCallee) {
  mlir::MLIRContext context;
  loadDialects(context);
  constexpr llvm::StringLiteral source = R"mlir(
    builtin.module attributes {ac.contract_epoch = "0.1"} {
      func.func @bad() {
        %condition = arith.constant true
        return
      }
      ac.module @Top() parameters {} graph {
        ac.process @workload kind "workload" {
          func.call @bad() : () -> ()
          ac.yield_sim
        }
        ac.return
      }
    }
  )mlir";
  auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
  ASSERT_TRUE(module);
  mlir::func::FuncOp function;
  module->walk([&](mlir::func::FuncOp candidate) { function = candidate; });
  mlir::Value condition = function.getBody().front().front().getResult(0);
  mlir::OpBuilder builder(function.getContext());
  builder.setInsertionPoint(function.getBody().front().getTerminator());
  ac::WaitUntilOp::create(builder, function.getLoc(), condition);
  std::vector<std::string> diagnostics;
  mlir::ScopedDiagnosticHandler capture(
      &context, [&](mlir::Diagnostic &diagnostic) {
        std::string text;
        llvm::raw_string_ostream(text) << diagnostic;
        diagnostics.push_back(std::move(text));
        return mlir::success();
      });
  EXPECT_TRUE(mlir::failed(detail::validatePureProcessCallGraph(*module)));
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_NE(diagnostics.front().find(
                "function reachable from ac.process is not effect-free"),
            std::string::npos);
}

TEST(ProcessStatePlanLowerabilityTest,
     HostileNestedSuspensionShapeHitsDepthLimitBeforeSummaryAnalysis) {
  mlir::MLIRContext context;
  loadDialects(context);
  constexpr llvm::StringLiteral source = R"mlir(
    builtin.module attributes {ac.contract_epoch = "0.1"} {
      ac.module @Top(index, index, index, i1) parameters {} graph {
      ^bb0(%l : index, %u : index, %s : index, %condition : i1):
        ac.process @workload kind "workload"
            captures(%l, %u, %s, %condition : index, index, index, i1) {
        ^bb0(%pl : index, %pu : index, %ps : index, %pc : i1):
          scf.for %i = %pl to %pu step %ps {
            ac.wait_until %pc
            scf.yield
          }
          ac.yield_sim
        }
        ac.return
      }
    }
  )mlir";
  auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
  ASSERT_TRUE(module);
  mlir::scf::ForOp loop;
  module->walk([&](mlir::scf::ForOp candidate) { loop = candidate; });
  ASSERT_TRUE(loop);
  mlir::Value condition = loop.getBody()->front().getOperand(0);
  mlir::Block *insertionBlock = loop.getBody();
  for (unsigned depth = 0; depth < 10000; ++depth) {
    mlir::OpBuilder builder(&context);
    builder.setInsertionPoint(insertionBlock->getTerminator());
    auto branch = mlir::scf::IfOp::create(builder, loop.getLoc(),
                                          mlir::TypeRange{}, condition, false);
    insertionBlock = &branch.getThenRegion().front();
  }
  std::vector<std::string> diagnostics;
  mlir::ScopedDiagnosticHandler capture(
      &context, [&](mlir::Diagnostic &diagnostic) {
        std::string text;
        llvm::raw_string_ostream(text) << diagnostic;
        diagnostics.push_back(std::move(text));
        return mlir::success();
      });
  EXPECT_TRUE(mlir::failed(
      ac::verifyProcessLowerability(loop->getParentOfType<ac::ProcessOp>())));
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_NE(diagnostics.front().find(
                "whole-model region nesting exceeds ACIR v0.1 capability "
                "limit 512"),
            std::string::npos);
}

TEST(ProcessStatePlanLimitsTest,
     ExpansionAcceptsExactNodeAndEdgeBudgetsAndRejectsOneLess) {
  mlir::MLIRContext context;
  loadDialects(context);
  constexpr llvm::StringLiteral source = R"mlir(
    builtin.module attributes {ac.contract_epoch = "0.1"} {
      func.func @leaf(%arg : index) -> index { return %arg : index }
      ac.module @Top() parameters {} graph {
        ac.process @workload kind "workload" {
          %zero = arith.constant 0 : index
          %value = func.call @leaf(%zero) : (index) -> index
          ac.yield_sim
        }
        ac.return
      }
    }
  )mlir";
  auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
  ASSERT_TRUE(module);
  ac::ProcessOp process;
  module->walk([&](ac::ProcessOp candidate) { process = candidate; });
  auto baseline = detail::expandProcessForPlanning(process);
  ASSERT_TRUE(mlir::succeeded(baseline));
  ac::RawModelStructureLimits exact;
  exact.maxNodes = baseline->expandedNodes;
  exact.maxEdges = baseline->expandedEdges;
  ASSERT_TRUE(
      mlir::succeeded(detail::expandProcessForPlanning(process, exact)));
  mlir::ScopedDiagnosticHandler suppress(
      &context, [](mlir::Diagnostic &) { return mlir::success(); });
  ac::RawModelStructureLimits oneLessNode = exact;
  --oneLessNode.maxNodes;
  EXPECT_TRUE(
      mlir::failed(detail::expandProcessForPlanning(process, oneLessNode)));
  ac::RawModelStructureLimits oneLessEdge = exact;
  --oneLessEdge.maxEdges;
  EXPECT_TRUE(
      mlir::failed(detail::expandProcessForPlanning(process, oneLessEdge)));
}

TEST(ProcessStatePlanVerifierTest,
     DynamicSuspendingLoopExpandsExactLoopPhaseActions) {
  mlir::MLIRContext context;
  loadDialects(context);
  constexpr llvm::StringLiteral source = R"mlir(
    builtin.module attributes {ac.contract_epoch = "0.1"} {
      ac.module @Top(index, index, index) parameters {} graph {
      ^bb0(%lb : index, %ub : index, %step : index):
        ac.process @workload kind "workload"
            captures(%lb, %ub, %step : index, index, index) {
        ^bb0(%l : index, %u : index, %s : index):
          %true = arith.constant true
          scf.for %i = %l to %u step %s {
            ac.wait_until %true
            scf.yield
          }
          ac.yield_sim
        }
        ac.return
      }
    }
  )mlir";
  auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
  ASSERT_TRUE(module);
  ac::ProcessOp process;
  module->walk([&](ac::ProcessOp candidate) { process = candidate; });
  auto expanded = detail::expandProcessForPlanning(process);
  ASSERT_TRUE(mlir::succeeded(expanded));

  std::vector<ProcessActionKind> kinds;
  for (const detail::ExpandedAction &action : expanded->actions) {
    if (action.kind != ProcessActionKind::ForInitialize &&
        action.kind != ProcessActionKind::ForCondition &&
        action.kind != ProcessActionKind::ForIncrement)
      continue;
    kinds.push_back(action.kind);
    ASSERT_TRUE(action.occurrence.has_value());
    EXPECT_EQ(action.occurrence->kind(), ProcessOccurrenceKind::SyntheticLoop);
    ASSERT_FALSE(action.results.empty());
    for (const ProcessPlannedValue &operand : action.operands)
      EXPECT_TRUE(operand.type().isIndex());
    for (const ProcessPlannedValue &result : action.results)
      EXPECT_EQ(result.kind(), ProcessPlannedValueKind::Synthetic);
    if (action.kind == ProcessActionKind::ForCondition) {
      EXPECT_EQ(action.occurrence->syntheticLoop().phase(),
                ProcessLoopPhase::Condition);
      ASSERT_EQ(action.operands.size(), 2u);
      ASSERT_EQ(action.results.size(), 1u);
      EXPECT_TRUE(action.results.front().type().isInteger(1));
      ASSERT_TRUE(action.scalarOperation.has_value());
      EXPECT_EQ(action.scalarOperation->name(), "arith.cmpi");
      EXPECT_EQ(action.scalarOperation->properties(), "{}");
      ASSERT_EQ(action.scalarOperation->attributes().size(), 1u);
      EXPECT_EQ(action.scalarOperation->attributes().front().name(),
                "predicate");
      EXPECT_EQ(action.scalarOperation->attributes().front().value(),
                "2 : i64");
    }
    if (action.kind == ProcessActionKind::ForIncrement) {
      EXPECT_EQ(action.occurrence->syntheticLoop().phase(),
                ProcessLoopPhase::Increment);
      ASSERT_EQ(action.operands.size(), 2u);
      ASSERT_EQ(action.results.size(), 1u);
      EXPECT_TRUE(action.results.front().type().isIndex());
      ASSERT_TRUE(action.scalarOperation.has_value());
      EXPECT_EQ(action.scalarOperation->name(), "arith.addi");
      EXPECT_EQ(action.scalarOperation->properties(), "{}");
      EXPECT_TRUE(action.scalarOperation->attributes().empty());
    }
    if (action.kind == ProcessActionKind::ForInitialize) {
      EXPECT_EQ(action.occurrence->syntheticLoop().phase(),
                ProcessLoopPhase::Initialize);
      ASSERT_EQ(action.operands.size(), 1u);
      ASSERT_EQ(action.results.size(), 1u);
      EXPECT_TRUE(action.results.front().type().isIndex());
      EXPECT_FALSE(action.scalarOperation.has_value());
    }
  }
  EXPECT_EQ(kinds,
            (std::vector<ProcessActionKind>{ProcessActionKind::ForInitialize,
                                            ProcessActionKind::ForCondition,
                                            ProcessActionKind::ForIncrement}));
}

TEST(ProcessStatePlanNormalizeFactoryTest,
     Depth513FailsRawStructuralPreflightBeforeNormalizeRecursion) {
  mlir::MLIRContext context;
  loadDialects(context);
  auto module = buildRawDepthFixture(context, 513);
  ASSERT_TRUE(module);

  NormalizeRun run = runIsolatedNormalize(*module);
  EXPECT_TRUE(mlir::failed(run.result));
  EXPECT_EQ(run.events, (std::vector<std::string>{"enter:normalize-ac-file",
                                                  "fail:normalize-ac-file"}));
  ASSERT_EQ(run.diagnostics.size(), 1u);
  EXPECT_NE(run.diagnostics.front().find(
                "whole-model region nesting exceeds ACIR v0.1 capability "
                "limit 512"),
            std::string::npos);
}

TEST(ProcessStatePlanNormalizeFactoryTest,
     VeryDeepMalformedFailsRawStructuralPreflightWithoutRecursion) {
  mlir::MLIRContext context;
  loadDialects(context);
  auto module = buildRawDepthFixture(context, 10000, /*malformed=*/true);
  ASSERT_TRUE(module);

  NormalizeRun run = runIsolatedNormalize(*module);
  EXPECT_TRUE(mlir::failed(run.result));
  EXPECT_EQ(run.events, (std::vector<std::string>{"enter:normalize-ac-file",
                                                  "fail:normalize-ac-file"}));
  ASSERT_EQ(run.diagnostics.size(), 1u);
  EXPECT_NE(run.diagnostics.front().find(
                "whole-model region nesting exceeds ACIR v0.1 capability "
                "limit 512"),
            std::string::npos);
}

} // namespace
} // namespace acir
