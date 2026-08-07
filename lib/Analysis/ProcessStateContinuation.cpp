#include "ProcessStatePlanInternal.h"

#include "acir/Dialect/ACIR/ACIROps.h"

#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Diagnostics.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ErrorHandling.h"

#include <iomanip>
#include <memory>
#include <sstream>

using namespace mlir;

namespace acir::detail {
namespace {

static bool isSuspensionOp(Operation *op) {
  return isa<ac::WaitUntilOp>(op) || isa<ac::WaitForOp>(op) ||
         isa<ac::AwaitEventOp>(op) || isa<ac::YieldSimOp>(op);
}

static bool isYieldSim(Operation *op) { return isa<ac::YieldSimOp>(op); }

static ProcessWakeKind wakeKindForOp(Operation *op) {
  if (isa<ac::WaitUntilOp>(op))
    return ProcessWakeKind::Condition;
  if (isa<ac::WaitForOp>(op))
    return ProcessWakeKind::Resource;
  if (isa<ac::AwaitEventOp>(op))
    return ProcessWakeKind::EventQueue;
  if (isa<ac::YieldSimOp>(op))
    return ProcessWakeKind::NextDelta;
  llvm_unreachable("unknown suspension op");
}

static std::string wakeTypeKeyForOp(Operation *op) {
  if (isa<ac::WaitUntilOp>(op))
    return "@acir_wake_condition";
  if (isa<ac::WaitForOp>(op))
    return "@acir_wake_resource";
  if (isa<ac::AwaitEventOp>(op))
    return "@acir_wake_event_queue";
  if (isa<ac::YieldSimOp>(op))
    return "@acir_wake_next_delta";
  llvm_unreachable("unknown suspension op");
}

static std::string pcName(uint32_t index) {
  if (index == 0)
    return "entry";
  std::ostringstream s;
  s << "pc" << std::setfill('0') << std::setw(8) << index;
  return s.str();
}

static std::string blockPath(const std::string &defKey,
                             const std::string &pcNameStr, uint32_t blockIdx) {
  std::ostringstream s;
  s << defKey << "/plan/pc/" << pcNameStr << "/b" << std::setfill('0')
    << std::setw(8) << blockIdx;
  return s.str();
}

} // namespace

FailureOr<std::unique_ptr<PlanSetBuilder::ControlPlan>>
PlanSetBuilder::planProcessContinuation(const ExpandedProcess &expanded,
                                        const ProcessStateLimits &limits) {
  auto plan = std::make_unique<ControlPlan>();

  if (expanded.actions.empty())
    return mlir::failure();

  uint32_t nextPcId = 0;
  uint32_t nextBlockId = 0;
  uint32_t nextWakeId = 0;
  uint32_t nextTransitionId = 0;

  // Entry PC
  auto entryPc = std::make_shared<ProcessPcPlan::Impl>();
  entryPc->id = ProcessPcId(nextPcId++);
  entryPc->name = pcName(0);
  plan->pcs.push_back(entryPc);

  struct Susp {
    size_t idx;
    ProcessWakeKind kind;
    std::string typeKey;
    Operation *op;
  };
  SmallVector<Susp> suspensions;
  for (auto [i, a] : llvm::enumerate(expanded.actions)) {
    if (isSuspensionOp(a.operation))
      suspensions.push_back({i, wakeKindForOp(a.operation),
                             wakeTypeKeyForOp(a.operation), a.operation});
  }

  // Resume PCs
  uint32_t resumeIdx = 1;
  SmallVector<uint32_t> pcMap(expanded.actions.size(), 0);
  for (const auto &s : suspensions) {
    if (!isYieldSim(s.op)) {
      auto rpc = std::make_shared<ProcessPcPlan::Impl>();
      rpc->id = ProcessPcId(nextPcId);
      rpc->name = pcName(resumeIdx);
      plan->pcs.push_back(rpc);
      pcMap[s.idx] = nextPcId;
      ++nextPcId;
      ++resumeIdx;
    } else {
      pcMap[s.idx] = 0; // yield_sim resumes at entry
    }
  }

  // Segment starts
  SmallVector<size_t> starts;
  starts.push_back(0);
  for (const auto &s : suspensions)
    starts.push_back(s.idx + 1);

  for (size_t seg = 0; seg < suspensions.size(); ++seg) {
    size_t start = starts[seg];
    size_t end = suspensions[seg].idx;
    uint32_t pcId = (seg == 0) ? 0 : pcMap[suspensions[seg - 1].idx];
    const auto &susp = suspensions[seg];

    auto block = std::make_shared<ProcessBlockPlan::Impl>();
    block->id = ProcessBlockId(nextBlockId);
    block->pc = ProcessPcId(pcId);
    block->path =
        blockPath(expanded.definitionKey, plan->pcs[pcId]->name, nextBlockId);
    plan->pcs[pcId]->blocks.push_back(ProcessBlockId(nextBlockId));
    if (plan->pcs[pcId]->entryPath.empty())
      plan->pcs[pcId]->entryPath = block->path;

    // Actions in segment
    for (size_t i = start; i <= end; ++i) {
      auto act = std::make_shared<ProcessActionPlan::Impl>();
      act->id = static_cast<uint32_t>(i - start);
      act->kind = expanded.actions[i].kind;
      act->sourceOperation = expanded.actions[i].operation;
      act->iterationVector = expanded.actions[i].iterationVector;
      block->actions.push_back(ProcessActionPlan(act));
    }

    // Suspension edge
    auto edge = std::make_shared<ProcessControlEdgePlan::Impl>();
    edge->kind = ProcessControlEdgeKind::Suspend;

    // Wake
    auto wake = std::make_shared<ProcessWakePlan::Impl>();
    wake->id = ProcessWakeId(nextWakeId);
    wake->kind = susp.kind;
    wake->typeKey = susp.typeKey;
    wake->operation = susp.op;
    wake->operationPath = expanded.actions[susp.idx].operationPath;
    wake->target = "";
    wake->occurrence = expanded.actions[susp.idx].occurrence;
    wake->iterationVector = expanded.actions[susp.idx].iterationVector;

    // Subscription sources
    for (const auto &opd : expanded.actions[susp.idx].operands) {
      auto src = std::make_shared<ProcessSubscriptionSourcePlan::Impl>();
      if (opd.kind() == ProcessPlannedValueKind::Original) {
        src->kind = ProcessSubscriptionSourceKind::Value;
        src->value = opd.original().value();
        src->owner = opd.original().occurrence().original().operation();
        src->path = opd.original().path().str();
      } else if (opd.kind() == ProcessPlannedValueKind::Capture) {
        src->kind = ProcessSubscriptionSourceKind::Capture;
        src->capture = opd.capture().capture();
      } else {
        src->kind = ProcessSubscriptionSourceKind::Value;
      }
      wake->sources.push_back(ProcessSubscriptionSourcePlan(src));
    }

    // Transition
    auto tr = std::make_shared<ProcessTransitionPlan::Impl>();
    tr->id = ProcessTransitionId(nextTransitionId);
    tr->sourcePc = ProcessPcId(pcId);
    tr->targetPc = ProcessPcId(pcMap[susp.idx]);
    tr->wake = ProcessWakeId(nextWakeId);

    edge->transition = ProcessTransitionId(nextTransitionId);
    block->edge = ProcessControlEdgePlan(edge);

    plan->blocks.push_back(block);
    plan->wakes.push_back(wake);
    plan->transitions.push_back(tr);

    ++nextWakeId;
    ++nextTransitionId;
    ++nextBlockId;
  }

  return plan;
}

FailureOr<std::unique_ptr<PlanSetBuilder::ControlPlan>>
PlanSetBuilder::planProcessWakes(std::unique_ptr<ControlPlan> control,
                                 const ProcessStateLimits &limits) {
  return control;
}

} // namespace acir::detail
