#include "ProcessStatePlanInternal.h"

#include "acir/Analysis/ModelAnalysis.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

using namespace mlir;

namespace acir::detail {
namespace {

struct ExpansionContext {
  std::vector<ProcessCallSitePlan> callSites;
  std::vector<uint64_t> iterations;
};

struct ExpansionTask {
  Operation *operation = nullptr;
  ExpansionContext context;
};

std::string processDefinitionKey(ac::ProcessOp process) {
  ac::ModuleOp owner = process->getParentOfType<ac::ModuleOp>();
  return ("@" + owner.getSymName() + "::@" + process.getSymName()).str();
}

void indexOperationTree(Operation *root, StringRef rootPath,
                        DenseMap<Operation *, std::string> &paths,
                        DenseMap<Block *, std::string> &blockPaths) {
  struct Task {
    Operation *operation;
    std::string path;
  };
  SmallVector<Task> pending{{root, rootPath.str()}};
  while (!pending.empty()) {
    Task task = std::move(pending.back());
    pending.pop_back();
    paths[task.operation] = task.path;
    SmallVector<Task> children;
    for (auto [regionIndex, region] :
         llvm::enumerate(task.operation->getRegions()))
      for (auto [blockIndex, block] : llvm::enumerate(region)) {
        std::string blockPath = (task.path + "/r" + llvm::Twine(regionIndex) +
                                 "/b" + llvm::Twine(blockIndex))
                                    .str();
        blockPaths[&block] = blockPath;
        for (auto [operationIndex, child] : llvm::enumerate(block))
          children.push_back(
              {&child, (blockPath + "/o" + llvm::Twine(operationIndex)).str()});
      }
    for (Task &child : llvm::reverse(children))
      pending.push_back(std::move(child));
  }
}

} // namespace

FailureOr<ExpandedProcess>
PlanSetBuilder::expandProcess(ac::ProcessOp process,
                              const ac::RawModelStructureLimits &limits) {
  if (failed(ac::verifyProcessLowerability(process, limits)))
    return failure();

  ExpandedProcess expanded;
  expanded.process = process;
  expanded.definitionKey = processDefinitionKey(process);

  ModuleOp file = process->getParentOfType<ModuleOp>();
  DenseMap<StringAttr, func::FuncOp> functions;
  for (func::FuncOp function : file.getOps<func::FuncOp>())
    functions[function.getSymNameAttr()] = function;
  if (functions.size() > kMaxPureCallFunctions) {
    file.emitError() << "pure func.call expansion exceeds ACIR v0.1 function "
                        "limit "
                     << kMaxPureCallFunctions;
    return failure();
  }

  DenseMap<Operation *, std::string> operationPaths;
  DenseMap<Block *, std::string> blockPaths;
  indexOperationTree(process, expanded.definitionKey, operationPaths,
                     blockPaths);
  for (auto &[name, function] : functions)
    indexOperationTree(
        function, (expanded.definitionKey + "/func/@" + name.getValue()).str(),
        operationPaths, blockPaths);

  auto makeCallSite = [&](Operation *operation,
                          const ExpansionContext &context) {
    auto impl = std::make_shared<ProcessCallSitePlan::Impl>();
    impl->operation = operation;
    impl->operationPath = operationPaths.lookup(operation);
    impl->iterationVector = context.iterations;
    return ProcessCallSitePlan(impl);
  };
  auto makeOriginalOccurrence = [&](Operation *operation,
                                    const ExpansionContext &context) {
    auto original = std::make_shared<ProcessOriginalOccurrence::Impl>();
    original->operation = operation;
    original->operationPath = operationPaths.lookup(operation);
    original->callSites = context.callSites;
    original->iterationVector = context.iterations;
    auto occurrence = std::make_shared<ProcessOccurrenceId::Impl>();
    occurrence->kind = ProcessOccurrenceKind::Original;
    occurrence->original = ProcessOriginalOccurrence(original);
    return ProcessOccurrenceId(occurrence);
  };
  auto makeSyntheticLoopOccurrence = [&](scf::ForOp loop,
                                         const ExpansionContext &context,
                                         ProcessLoopPhase phase) {
    ProcessOccurrenceId anchor =
        makeOriginalOccurrence(loop.getOperation(), context);
    auto loopImpl = std::make_shared<ProcessSyntheticLoopOccurrence::Impl>();
    loopImpl->anchor = anchor;
    loopImpl->phase = phase;
    auto occurrence = std::make_shared<ProcessOccurrenceId::Impl>();
    occurrence->kind = ProcessOccurrenceKind::SyntheticLoop;
    occurrence->syntheticLoop = ProcessSyntheticLoopOccurrence(loopImpl);
    return ProcessOccurrenceId(occurrence);
  };
  auto makeSyntheticConstantOccurrence =
      [&](scf::ForOp loop, const ExpansionContext &context, uint32_t ordinal) {
        ProcessOccurrenceId anchor =
            makeOriginalOccurrence(loop.getOperation(), context);
        auto constant =
            std::make_shared<ProcessSyntheticConstantOccurrence::Impl>();
        constant->anchor = anchor;
        constant->constant = ordinal;
        auto occurrence = std::make_shared<ProcessOccurrenceId::Impl>();
        occurrence->kind = ProcessOccurrenceKind::SyntheticConstant;
        occurrence->syntheticConstant =
            ProcessSyntheticConstantOccurrence(constant);
        return ProcessOccurrenceId(occurrence);
      };
  auto makeValue = [&](Value value, const ExpansionContext &context) {
    Operation *owner = value.getDefiningOp();
    auto coordinateImpl = std::make_shared<ProcessValueCoordinate::Impl>();
    std::string path;
    if (auto argument = dyn_cast<BlockArgument>(value)) {
      owner = argument.getOwner()->getParentOp();
      coordinateImpl->kind = ProcessValueCoordinateKind::BlockArgument;
      coordinateImpl->ownerPath = blockPaths.lookup(argument.getOwner());
      coordinateImpl->index = argument.getArgNumber();
      path = coordinateImpl->ownerPath + "/a" +
             std::to_string(argument.getArgNumber());
    } else {
      coordinateImpl->kind = ProcessValueCoordinateKind::Result;
      coordinateImpl->ownerPath = operationPaths.lookup(owner);
      coordinateImpl->index = cast<OpResult>(value).getResultNumber();
      path = coordinateImpl->ownerPath + "/v" +
             std::to_string(coordinateImpl->index);
    }
    ProcessValueCoordinate coordinate(coordinateImpl);
    auto originalImpl = std::make_shared<ProcessOriginalPlannedValue::Impl>();
    originalImpl->value = value;
    originalImpl->occurrence = makeOriginalOccurrence(owner, context);
    originalImpl->coordinate = coordinate;
    originalImpl->path = std::move(path);
    auto planned = std::make_shared<ProcessPlannedValue::Impl>();
    planned->kind = ProcessPlannedValueKind::Original;
    planned->type = value.getType();
    planned->original = ProcessOriginalPlannedValue(originalImpl);
    return ProcessPlannedValue(planned);
  };
  auto makeSyntheticValue = [&](scf::ForOp loop,
                                const ExpansionContext &context,
                                uint32_t ordinal) {
    auto coordinateImpl = std::make_shared<ProcessValueCoordinate::Impl>();
    coordinateImpl->kind = ProcessValueCoordinateKind::Result;
    coordinateImpl->ownerPath = operationPaths.lookup(loop);
    coordinateImpl->index = 0;
    auto syntheticImpl = std::make_shared<ProcessSyntheticPlannedValue::Impl>();
    syntheticImpl->occurrence =
        makeSyntheticConstantOccurrence(loop, context, ordinal);
    syntheticImpl->coordinate = ProcessValueCoordinate(coordinateImpl);
    auto planned = std::make_shared<ProcessPlannedValue::Impl>();
    planned->kind = ProcessPlannedValueKind::Synthetic;
    planned->type = loop.getInductionVar().getType();
    planned->synthetic = ProcessSyntheticPlannedValue(syntheticImpl);
    return ProcessPlannedValue(planned);
  };
  auto addForwarding = [&](Value from, const ExpansionContext &fromContext,
                           Value to, const ExpansionContext &toContext) {
    expanded.forwarding.push_back(
        {makeValue(from, fromContext), makeValue(to, toContext)});
  };
  auto addOriginalAction = [&](Operation *operation,
                               const ExpansionContext &context) {
    ExpandedAction action;
    action.operation = operation;
    action.operationPath = operationPaths.lookup(operation);
    action.callSites = context.callSites;
    action.iterationVector = context.iterations;
    action.occurrence = makeOriginalOccurrence(operation, context);
    for (Value operand : operation->getOperands())
      action.operands.push_back(makeValue(operand, context));
    for (Value result : operation->getResults())
      action.results.push_back(makeValue(result, context));
    expanded.actions.push_back(std::move(action));
  };

  SmallVector<ExpansionTask> pending;
  auto pushBlock = [&](Block &block, const ExpansionContext &context) {
    SmallVector<Operation *> operations;
    for (Operation &operation : block)
      operations.push_back(&operation);
    for (Operation *operation : llvm::reverse(operations))
      pending.push_back({operation, context});
  };
  pushBlock(process.getBody().front(), {});
  uint64_t expansionEdges = 0;
  uint64_t callEdges = 0;
  while (!pending.empty()) {
    ExpansionTask task = std::move(pending.back());
    pending.pop_back();
    Operation *operation = task.operation;
    if (expanded.actions.size() >= limits.maxNodes ||
        expansionEdges > limits.maxEdges - operation->getNumOperands()) {
      operation->emitOpError(
          "pure process expansion exceeds ACIR v0.1 node/edge limits");
      return failure();
    }
    expansionEdges += operation->getNumOperands();

    if (auto call = dyn_cast<func::CallOp>(operation)) {
      if (++callEdges > kMaxPureCallEdges) {
        call.emitOpError()
            << "pure func.call expansion exceeds ACIR v0.1 edge limit "
            << kMaxPureCallEdges;
        return failure();
      }
      auto found = functions.find(
          StringAttr::get(process.getContext(), call.getCallee()));
      if (found == functions.end() || found->second.isExternal()) {
        call.emitOpError() << "process func.call callee '@" << call.getCallee()
                           << "' is unresolved or external";
        return failure();
      }
      LogicalResult pure = success();
      if (failed(ac::walkStructuredOperationsIterative(
              found->second,
              [&](Operation *candidate) -> LogicalResult {
                if (candidate == found->second.getOperation() ||
                    isa<func::CallOp, func::ReturnOp>(candidate))
                  return success();
                if (candidate->getName().getStringRef().starts_with("cf."))
                  return candidate->emitOpError(
                      "function reachable from ac.process contains "
                      "unsupported control-flow operation");
                if (!isMemoryEffectFree(candidate))
                  return candidate->emitOpError(
                      "function reachable from ac.process is not "
                      "effect-free");
                return success();
              },
              limits)))
        pure = failure();
      if (failed(pure))
        return failure();
      ExpansionContext calleeContext = task.context;
      calleeContext.callSites.push_back(makeCallSite(operation, task.context));
      if (calleeContext.callSites.size() > kMaxPureCallDepth) {
        call.emitOpError() << "pure func.call expansion exceeds ACIR v0.1 "
                              "depth limit "
                           << kMaxPureCallDepth;
        return failure();
      }
      Block &entry = found->second.getBody().front();
      for (auto [operand, argument] :
           llvm::zip(call.getOperands(), entry.getArguments()))
        addForwarding(operand, task.context, argument, calleeContext);
      for (Operation &calleeOperation : entry) {
        if (auto returnOp = dyn_cast<func::ReturnOp>(calleeOperation))
          for (auto [returned, result] :
               llvm::zip(returnOp.getOperands(), call.getResults()))
            addForwarding(returned, calleeContext, result, task.context);
      }
      pushBlock(entry, calleeContext);
      continue;
    }
    if (isa<func::ReturnOp, scf::YieldOp, scf::ConditionOp>(operation))
      continue;
    if (auto forOp = dyn_cast<scf::ForOp>(operation)) {
      FailureOr<ac::StaticForTripCount> staticTrip =
          ac::analyzeStaticFor(forOp);
      if (succeeded(staticTrip)) {
        auto yield = cast<scf::YieldOp>(forOp.getBody()->getTerminator());
        if (staticTrip->tripCount == 0)
          for (auto [init, result] :
               llvm::zip(forOp.getInitArgs(), forOp.getResults()))
            addForwarding(init, task.context, result, task.context);
        for (uint64_t iteration = 0; iteration < staticTrip->tripCount;
             ++iteration) {
          ExpansionContext bodyContext = task.context;
          bodyContext.iterations.push_back(iteration);
          ProcessPlannedValue induction = makeSyntheticValue(
              forOp, bodyContext, static_cast<uint32_t>(iteration));
          ExpandedAction constant;
          constant.kind = ProcessActionKind::Constant;
          constant.operation = operation;
          constant.operationPath = operationPaths.lookup(operation);
          constant.callSites = task.context.callSites;
          constant.iterationVector = bodyContext.iterations;
          constant.occurrence = induction.synthetic().occurrence();
          constant.results.push_back(induction);
          expanded.actions.push_back(std::move(constant));
          expanded.forwarding.push_back(
              {induction, makeValue(forOp.getInductionVar(), bodyContext)});
          if (iteration == 0)
            for (auto [init, argument] :
                 llvm::zip(forOp.getInitArgs(), forOp.getRegionIterArgs()))
              addForwarding(init, task.context, argument, bodyContext);
          if (iteration + 1 < staticTrip->tripCount) {
            ExpansionContext nextContext = task.context;
            nextContext.iterations.push_back(iteration + 1);
            for (auto [yielded, argument] :
                 llvm::zip(yield.getOperands(), forOp.getRegionIterArgs()))
              addForwarding(yielded, bodyContext, argument, nextContext);
          } else {
            for (auto [yielded, result] :
                 llvm::zip(yield.getOperands(), forOp.getResults()))
              addForwarding(yielded, bodyContext, result, task.context);
          }
        }
        for (uint64_t iteration = staticTrip->tripCount; iteration > 0;
             --iteration) {
          ExpansionContext bodyContext = task.context;
          bodyContext.iterations.push_back(iteration - 1);
          pushBlock(*forOp.getBody(), bodyContext);
        }
        continue;
      }

      for (auto [kind, phase] : {std::pair{ProcessActionKind::ForInitialize,
                                           ProcessLoopPhase::Initialize},
                                 std::pair{ProcessActionKind::ForCondition,
                                           ProcessLoopPhase::Condition},
                                 std::pair{ProcessActionKind::ForIncrement,
                                           ProcessLoopPhase::Increment}}) {
        ExpandedAction action;
        action.kind = kind;
        action.operation = operation;
        action.operationPath = operationPaths.lookup(operation);
        action.callSites = task.context.callSites;
        action.iterationVector = task.context.iterations;
        action.occurrence =
            makeSyntheticLoopOccurrence(forOp, task.context, phase);
        if (kind == ProcessActionKind::ForCondition) {
          action.scalarOperation = "arith.cmpi";
          action.scalarPredicate = "slt";
        } else if (kind == ProcessActionKind::ForIncrement) {
          action.scalarOperation = "arith.addi";
        }
        expanded.actions.push_back(std::move(action));
      }
      pushBlock(*forOp.getBody(), task.context);
      continue;
    }
    if (auto ifOp = dyn_cast<scf::IfOp>(operation)) {
      pushBlock(ifOp.getThenRegion().front(), task.context);
      if (!ifOp.getElseRegion().empty())
        pushBlock(ifOp.getElseRegion().front(), task.context);
      continue;
    }
    if (auto whileOp = dyn_cast<scf::WhileOp>(operation)) {
      pushBlock(whileOp.getBefore().front(), task.context);
      pushBlock(whileOp.getAfter().front(), task.context);
      continue;
    }
    addOriginalAction(operation, task.context);
  }

  return expanded;
}

FailureOr<ExpandedProcess>
expandProcessForPlanning(ac::ProcessOp process,
                         const ac::RawModelStructureLimits &limits) {
  return PlanSetBuilder::expandProcess(process, limits);
}

} // namespace acir::detail
