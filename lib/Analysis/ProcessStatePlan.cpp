#include "ProcessStatePlanInternal.h"

#include "acir/Bindings/Binding.h"

#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/JSON.h"

namespace acir {
namespace {

thread_local std::string lastDiagnostic;

constexpr llvm::StringLiteral kWakeNextDeltaSpecialization =
    R"json({"contract_epoch":"0.1","effect":"stateful","inputs":[],"kind":"implementation","payload":{"wake_kind":"next_delta","wake_type":"@acir_wake_next_delta"},"results":["@acir_wake_next_delta"],"role":"wake_next_delta","schema":"acir-generated-implementation-0.1","source_paths":[]})json";
constexpr llvm::StringLiteral kWakeNextDeltaDigest =
    "63cacba5c3eb82976464804b4aeaa17d43b445733efaddfad7c7bec1ab650269";

mlir::LogicalResult reject(const ProcessStatePlanSet &plans,
                           llvm::StringRef diagnostic) {
  lastDiagnostic = diagnostic.str();
  if (!plans.processes().empty() && plans.processes().front().process())
    plans.processes().front().process().emitError(diagnostic);
  return mlir::failure();
}

bool validDefinitionKey(llvm::StringRef key) {
  size_t separator = key.find("::");
  return separator > 1 && separator + 3 < key.size() && key.front() == '@' &&
         key[separator + 2] == '@' && key.find("::", separator + 2) == key.npos;
}

} // namespace

ProcessStatePlanSet detail::PlanSetBuilder::buildEmpty(mlir::ModuleOp) {
  return ProcessStatePlanSet(std::make_shared<ProcessStatePlanSet::Impl>());
}

ProcessStatePlanSet
detail::PlanSetBuilder::buildYieldOnly(mlir::ModuleOp module) {
  ac::ProcessOp process;
  module.walk([&](ac::ProcessOp candidate) {
    if (!process)
      process = candidate;
  });

  std::string moduleName = "Top";
  if (process) {
    if (auto owner = process->getParentOfType<ac::ModuleOp>())
      if (auto name = mlir::SymbolTable::getSymbolName(owner))
        moduleName = name.str();
  }
  std::string processName = "workload";
  if (process)
    if (auto name = mlir::SymbolTable::getSymbolName(process))
      processName = name.str();
  std::string definitionKey = "@" + moduleName + "::@" + processName;
  std::string operationPath = definitionKey + "/r0/b0/o0";

  mlir::Operation *yield = nullptr;
  if (process)
    process.walk([&](ac::YieldSimOp candidate) {
      if (!yield)
        yield = candidate;
    });

  auto originalImpl = std::make_shared<ProcessOriginalOccurrence::Impl>();
  originalImpl->operation = yield;
  originalImpl->operationPath = operationPath;
  ProcessOriginalOccurrence original(originalImpl);
  auto occurrenceImpl = std::make_shared<ProcessOccurrenceId::Impl>();
  occurrenceImpl->kind = ProcessOccurrenceKind::Original;
  occurrenceImpl->original = original;
  ProcessOccurrenceId occurrence(occurrenceImpl);

  auto wakePayloadImpl = std::make_shared<ProcessWakeNextDeltaPayload::Impl>();
  wakePayloadImpl->wakeKind = ProcessWakeKind::NextDelta;
  wakePayloadImpl->wakeType = "@acir_wake_next_delta";
  ProcessWakeNextDeltaPayload wakePayload(wakePayloadImpl);
  auto payloadImpl = std::make_shared<ProcessGeneratedCalleePayload::Impl>();
  payloadImpl->role = ProcessHelperRole::WakeNextDelta;
  payloadImpl->wakeNextDelta = wakePayload;
  ProcessGeneratedCalleePayload payload(payloadImpl);

  auto calleeImpl = std::make_shared<ProcessGeneratedCalleePlan::Impl>();
  calleeImpl->id = ProcessCalleeId(0);
  calleeImpl->symbol =
      "@acir_impl_wake_next_delta_" + kWakeNextDeltaDigest.str();
  calleeImpl->cpp =
      "acir::generated::impl_wake_next_delta_" + kWakeNextDeltaDigest.str();
  calleeImpl->fingerprint = "sha256:" + kWakeNextDeltaDigest.str();
  calleeImpl->effect = ProcessEffectKind::Stateful;
  calleeImpl->resultTypeKeyStorage = {"@acir_wake_next_delta"};
  for (const std::string &key : calleeImpl->resultTypeKeyStorage)
    calleeImpl->resultTypeKeys.push_back(key);
  calleeImpl->role = ProcessHelperRole::WakeNextDelta;
  calleeImpl->payload = payload;
  calleeImpl->specializationBytes = kWakeNextDeltaSpecialization.str();
  llvm::json::Object descriptor;
  descriptor["cpp"] = calleeImpl->cpp;
  descriptor["effect"] = "stateful";
  descriptor["fingerprint"] = calleeImpl->fingerprint;
  descriptor["inputs"] = llvm::json::Array();
  descriptor["kind"] = "implementation";
  descriptor["ordinal"] = 0;
  llvm::json::Object descriptorPayload;
  descriptorPayload["wake_kind"] = "next_delta";
  descriptorPayload["wake_type"] = "@acir_wake_next_delta";
  descriptor["payload"] = std::move(descriptorPayload);
  llvm::json::Array results;
  results.push_back("@acir_wake_next_delta");
  descriptor["results"] = std::move(results);
  descriptor["role"] = "wake_next_delta";
  descriptor["source_paths"] = llvm::json::Array();
  descriptor["symbol"] = calleeImpl->symbol;
  if (auto canonical =
          bindings::canonicalizeJson(llvm::json::Value(std::move(descriptor))))
    calleeImpl->descriptorBytes = std::move(*canonical);
  ProcessGeneratedCalleePlan callee(calleeImpl);

  auto wakeImpl = std::make_shared<ProcessWakePlan::Impl>();
  wakeImpl->id = ProcessWakeId(0);
  wakeImpl->kind = ProcessWakeKind::NextDelta;
  wakeImpl->operation = yield;
  wakeImpl->callee = ProcessCalleeId(0);
  wakeImpl->typeKey = "@acir_wake_next_delta";
  wakeImpl->operationPath = operationPath;
  wakeImpl->target = "";
  wakeImpl->occurrence = occurrence;
  ProcessWakePlan wake(wakeImpl);

  auto transitionImpl = std::make_shared<ProcessTransitionPlan::Impl>();
  transitionImpl->id = ProcessTransitionId(0);
  transitionImpl->sourcePc = ProcessPcId(0);
  transitionImpl->targetPc = ProcessPcId(0);
  transitionImpl->wake = ProcessWakeId(0);
  ProcessTransitionPlan transition(transitionImpl);

  auto edgeImpl = std::make_shared<ProcessControlEdgePlan::Impl>();
  edgeImpl->kind = ProcessControlEdgeKind::Suspend;
  edgeImpl->transition = ProcessTransitionId(0);
  ProcessControlEdgePlan edge(edgeImpl);

  auto blockImpl = std::make_shared<ProcessBlockPlan::Impl>();
  blockImpl->id = ProcessBlockId(0);
  blockImpl->pc = ProcessPcId(0);
  if (process) {
    blockImpl->originRegion = &process.getBody();
    blockImpl->originBlock = &process.getBody().front();
  }
  blockImpl->path = definitionKey + "/plan/pc/entry/b00000000";
  blockImpl->edge = edge;
  blockImpl->cost = 2;
  ProcessBlockPlan block(blockImpl);

  auto pcImpl = std::make_shared<ProcessPcPlan::Impl>();
  pcImpl->id = ProcessPcId(0);
  pcImpl->name = "entry";
  pcImpl->entryPath = blockImpl->path;
  pcImpl->blocks.push_back(ProcessBlockId(0));
  ProcessPcPlan pc(pcImpl);

  auto processImpl = std::make_shared<ProcessStatePlan::Impl>();
  processImpl->definitionKey = definitionKey;
  processImpl->process = process;
  processImpl->entryPc = ProcessPcId(0);
  processImpl->pcs.push_back(pc);
  processImpl->blocks.push_back(block);
  processImpl->wakes.push_back(wake);
  processImpl->transitions.push_back(transition);
  processImpl->pcBitWidth = 1;
  processImpl->fairnessWork = 2;
  ProcessStatePlan state(processImpl);

  auto setImpl = std::make_shared<ProcessStatePlanSet::Impl>();
  setImpl->processes.push_back(state);
  setImpl->callees.push_back(callee);
  return ProcessStatePlanSet(setImpl);
}

ProcessStatePlanSet detail::PlanSetBuilder::cloneWithCorruption(
    const ProcessStatePlanSet &plans,
    ProcessStatePlanCorruptionForTest corruption) {
  auto impl = std::make_shared<ProcessStatePlanSet::Impl>(*plans.impl_);
  auto cloneProcess = [&]() {
    auto value = std::make_shared<ProcessStatePlan::Impl>(
        *impl->processes.front().impl_);
    impl->processes.front() = ProcessStatePlan(value);
    return value;
  };
  auto cloneBlock = [&]() {
    auto process = cloneProcess();
    auto value = std::make_shared<ProcessBlockPlan::Impl>(
        *process->blocks.front().impl_);
    process->blocks.front() = ProcessBlockPlan(value);
    return value;
  };
  auto cloneWake = [&]() {
    auto process = cloneProcess();
    auto value =
        std::make_shared<ProcessWakePlan::Impl>(*process->wakes.front().impl_);
    process->wakes.front() = ProcessWakePlan(value);
    return value;
  };
  auto cloneTransition = [&]() {
    auto process = cloneProcess();
    auto value = std::make_shared<ProcessTransitionPlan::Impl>(
        *process->transitions.front().impl_);
    process->transitions.front() = ProcessTransitionPlan(value);
    return value;
  };
  auto cloneCallee = [&]() {
    auto value = std::make_shared<ProcessGeneratedCalleePlan::Impl>(
        *impl->callees.front().impl_);
    impl->callees.front() = ProcessGeneratedCalleePlan(value);
    return value;
  };
  switch (corruption) {
  case ProcessStatePlanCorruptionForTest::DuplicateOrdinal:
    impl->callees.push_back(impl->callees.front());
    break;
  case ProcessStatePlanCorruptionForTest::NonDenseOrdinal:
    cloneCallee()->id = ProcessCalleeId(1);
    break;
  case ProcessStatePlanCorruptionForTest::DanglingReference:
    cloneTransition()->targetPc = ProcessPcId(99);
    break;
  case ProcessStatePlanCorruptionForTest::DuplicateIdentity: {
    auto duplicate = std::make_shared<ProcessGeneratedCalleePlan::Impl>(
        *impl->callees.front().impl_);
    duplicate->id = ProcessCalleeId(1);
    impl->callees.push_back(ProcessGeneratedCalleePlan(duplicate));
    break;
  }
  case ProcessStatePlanCorruptionForTest::UnsortedCanonicalOrder: {
    auto duplicate = std::make_shared<ProcessStatePlan::Impl>(
        *impl->processes.front().impl_);
    duplicate->definitionKey = "@A::@a";
    impl->processes.push_back(ProcessStatePlan(duplicate));
    break;
  }
  case ProcessStatePlanCorruptionForTest::CostMismatch:
    ++cloneBlock()->cost;
    break;
  case ProcessStatePlanCorruptionForTest::DefinitionKeyMismatch:
    cloneProcess()->definitionKey = "workload";
    break;
  case ProcessStatePlanCorruptionForTest::CalleeSpecializationMismatch:
    cloneCallee()->specializationBytes.push_back(' ');
    break;
  case ProcessStatePlanCorruptionForTest::ValueTypeSpecializationMismatch: {
    auto payloadImpl = std::make_shared<ProcessStorageValuePayload::Impl>();
    payloadImpl->encoding = "i1";
    payloadImpl->widthBits = 1;
    ProcessStorageValuePayload storage(payloadImpl);
    auto unionImpl = std::make_shared<ProcessValueTypePayload::Impl>();
    unionImpl->kind = ProcessValueTypeKind::Value;
    unionImpl->value = storage;
    ProcessValueTypePayload payload(unionImpl);
    auto typeImpl = std::make_shared<ProcessValueTypePlan::Impl>();
    typeImpl->id = ProcessValueTypeId(0);
    typeImpl->kind = ProcessValueTypeKind::Value;
    typeImpl->fingerprint = "sha256:invalid";
    typeImpl->payload = payload;
    impl->valueTypes.push_back(ProcessValueTypePlan(typeImpl));
    break;
  }
  case ProcessStatePlanCorruptionForTest::EffectMismatch:
    cloneCallee()->effect = ProcessEffectKind::Pure;
    break;
  case ProcessStatePlanCorruptionForTest::IdKindMismatch: {
    auto process = cloneProcess();
    auto pc =
        std::make_shared<ProcessPcPlan::Impl>(*process->pcs.front().impl_);
    pc->blocks.front() = ProcessBlockId(1);
    process->pcs.front() = ProcessPcPlan(pc);
    break;
  }
  case ProcessStatePlanCorruptionForTest::WrongTypeKey:
    cloneWake()->typeKey = "mlir:!acsim.wake";
    break;
  case ProcessStatePlanCorruptionForTest::InvalidFramePhase: {
    auto frameImpl = std::make_shared<ProcessControlFramePlan::Impl>();
    frameImpl->kind = ProcessFrameKind::ScfIf;
    frameImpl->phase = ProcessFramePhase::Entry;
    cloneBlock()->frames.push_back(ProcessControlFramePlan(frameImpl));
    break;
  }
  case ProcessStatePlanCorruptionForTest::InvalidEdgeBinding: {
    auto edgeImpl = std::make_shared<ProcessControlEdgePlan::Impl>();
    edgeImpl->kind = ProcessControlEdgeKind::Branch;
    edgeImpl->trueBlock = ProcessBlockId(0);
    edgeImpl->falseBlock = ProcessBlockId(0);
    cloneBlock()->edge = ProcessControlEdgePlan(edgeImpl);
    break;
  }
  case ProcessStatePlanCorruptionForTest::InvalidWakeCallee:
    cloneWake()->callee = ProcessCalleeId(99);
    break;
  }
  return ProcessStatePlanSet(impl);
}

llvm::StringRef detail::PlanSetBuilder::specializationBytes(
    const ProcessGeneratedCalleePlan &callee) {
  return callee.impl_->specializationBytes;
}

llvm::StringRef detail::PlanSetBuilder::descriptorBytes(
    const ProcessGeneratedCalleePlan &callee) {
  return callee.impl_->descriptorBytes;
}

bool detail::PlanSetBuilder::validEdgeShape(
    const ProcessControlEdgePlan &edge) {
  switch (edge.impl_->kind) {
  case ProcessControlEdgeKind::Branch:
    return edge.impl_->condition && edge.impl_->trueBlock &&
           edge.impl_->falseBlock;
  case ProcessControlEdgeKind::LocalContinue:
    return edge.impl_->targetBlock.has_value();
  case ProcessControlEdgeKind::Suspend:
    return edge.impl_->transition.has_value();
  case ProcessControlEdgeKind::Terminate:
    return true;
  }
  return false;
}

ProcessStatePlanSet cloneProcessStatePlanWithCorruptionForTest(
    const ProcessStatePlanSet &plan,
    ProcessStatePlanCorruptionForTest corruption) {
  return detail::PlanSetBuilder::cloneWithCorruption(plan, corruption);
}

llvm::StringRef detail::generatedCalleeSpecializationBytes(
    const ProcessGeneratedCalleePlan &callee) {
  return PlanSetBuilder::specializationBytes(callee);
}

llvm::StringRef detail::generatedCalleeDescriptorBytes(
    const ProcessGeneratedCalleePlan &callee) {
  return PlanSetBuilder::descriptorBytes(callee);
}

llvm::StringRef detail::processStatePlanCorruptionDiagnostic(
    ProcessStatePlanCorruptionForTest corruption) {
  static constexpr llvm::StringLiteral diagnostics[] = {
      "process-state plan invariant violated: duplicate ordinal",
      "process-state plan invariant violated: non-dense ordinal",
      "process-state plan invariant violated: dangling reference",
      "process-state plan invariant violated: duplicate identity",
      "process-state plan invariant violated: unsorted canonical order",
      "process-state plan invariant violated: cost mismatch",
      "process-state plan invariant violated: definition key mismatch",
      "process-state plan invariant violated: callee specialization mismatch",
      "process-state plan invariant violated: value-type specialization "
      "mismatch",
      "process-state plan invariant violated: effect mismatch",
      "process-state plan invariant violated: ID kind mismatch",
      "process-state plan invariant violated: wrong type key",
      "process-state plan invariant violated: invalid frame phase",
      "process-state plan invariant violated: invalid edge binding",
      "process-state plan invariant violated: invalid wake callee",
  };
  return diagnostics[static_cast<unsigned>(corruption)];
}

llvm::StringRef detail::lastProcessStatePlanDiagnosticForTest() {
  return lastDiagnostic;
}

std::vector<std::string> detail::canonicalDefinitionKeyOrderForTest(
    llvm::ArrayRef<llvm::StringRef> keys) {
  std::vector<std::string> ordered;
  ordered.reserve(keys.size());
  for (llvm::StringRef key : keys)
    ordered.push_back(key.str());
  llvm::sort(ordered);
  return ordered;
}

mlir::LogicalResult verifyProcessStatePlan(const ProcessStatePlanSet &plans,
                                           const ProcessStateLimits &limits) {
  lastDiagnostic.clear();
  if (plans.processes().size() > limits.maxProcesses)
    return reject(plans, "process-state plan capability maxProcesses exceeded");
  if (plans.callees().size() > limits.maxCalleeDescriptors)
    return reject(
        plans, "process-state plan capability maxCalleeDescriptors exceeded");
  llvm::StringRef previousKey;
  uint64_t pcs = 0, slots = 0, wakes = 0, transitions = 0, actions = 0;
  for (const ProcessStatePlan &plan : plans.processes()) {
    if (!validDefinitionKey(plan.definitionKey()))
      return reject(
          plans,
          "process-state plan invariant violated: definition key mismatch");
    if (!previousKey.empty() && previousKey.compare(plan.definitionKey()) >= 0)
      return reject(
          plans,
          "process-state plan invariant violated: unsorted canonical order");
    previousKey = plan.definitionKey();
    if (plan.pcs().empty() || plan.entryPc().value() != 0 ||
        plan.pcs().front().id().value() != 0 ||
        plan.pcs().front().name() != "entry")
      return reject(plans,
                    "process-state plan invariant violated: non-dense ordinal");
    uint32_t expectedWidth = 1;
    uint32_t largestPc = static_cast<uint32_t>(plan.pcs().size() - 1);
    while (largestPc >>= 1)
      ++expectedWidth;
    if (plan.pcBitWidth() != expectedWidth)
      return reject(plans, "process-state plan invariant violated: PC width");
    for (auto [index, pc] : llvm::enumerate(plan.pcs()))
      if (pc.id().value() != index)
        return reject(
            plans, "process-state plan invariant violated: non-dense ordinal");
    for (const ProcessPcPlan &pc : plan.pcs())
      for (ProcessBlockId block : pc.blocks())
        if (block.value() >= plan.blocks().size() ||
            plan.blocks()[block.value()].pc() != pc.id())
          return reject(
              plans, "process-state plan invariant violated: ID kind mismatch");
    for (auto [index, block] : llvm::enumerate(plan.blocks())) {
      if (block.id().value() != index ||
          block.pc().value() >= plan.pcs().size())
        return reject(
            plans, "process-state plan invariant violated: dangling reference");
      actions += block.actions().size();
      for (const ProcessControlFramePlan &frame : block.frames()) {
        bool legal = (frame.kind() == ProcessFrameKind::Entry &&
                      frame.phase() == ProcessFramePhase::Entry) ||
                     (frame.kind() == ProcessFrameKind::ScfIf &&
                      (frame.phase() == ProcessFramePhase::Then ||
                       frame.phase() == ProcessFramePhase::Else ||
                       frame.phase() == ProcessFramePhase::Merge)) ||
                     (frame.kind() == ProcessFrameKind::ScfFor &&
                      (frame.phase() == ProcessFramePhase::Header ||
                       frame.phase() == ProcessFramePhase::Body ||
                       frame.phase() == ProcessFramePhase::Exit)) ||
                     (frame.kind() == ProcessFrameKind::ScfWhile &&
                      (frame.phase() == ProcessFramePhase::Before ||
                       frame.phase() == ProcessFramePhase::After ||
                       frame.phase() == ProcessFramePhase::Exit));
        if (!legal)
          return reject(
              plans,
              "process-state plan invariant violated: invalid frame phase");
      }
      if (!detail::PlanSetBuilder::validEdgeShape(block.edge()))
        return reject(
            plans,
            "process-state plan invariant violated: invalid edge binding");
      uint64_t expectedCost = block.loads().size();
      for (const ProcessActionPlan &action : block.actions())
        expectedCost += action.cost();
      if (block.edge().kind() == ProcessControlEdgeKind::Suspend) {
        const ProcessTransitionPlan &transition =
            plan.transitions()[block.edge().transition().value()];
        expectedCost += transition.stores().size() + 2;
      } else {
        ++expectedCost;
      }
      if (block.cost() != expectedCost)
        return reject(plans,
                      "process-state plan invariant violated: cost mismatch");
    }
    for (auto [index, wake] : llvm::enumerate(plan.wakes())) {
      if (wake.id().value() != index ||
          wake.callee().value() >= plans.callees().size())
        return reject(
            plans,
            "process-state plan invariant violated: invalid wake callee");
      if (wake.kind() == ProcessWakeKind::NextDelta &&
          wake.typeKey() != "@acir_wake_next_delta")
        return reject(plans,
                      "process-state plan invariant violated: wrong type key");
    }
    for (auto [index, transition] : llvm::enumerate(plan.transitions()))
      if (transition.id().value() != index ||
          transition.sourcePc().value() >= plan.pcs().size() ||
          transition.targetPc().value() >= plan.pcs().size() ||
          transition.wake().value() >= plan.wakes().size())
        return reject(
            plans, "process-state plan invariant violated: dangling reference");
    if (plan.fairnessWork() == 0 ||
        plan.fairnessWork() > limits.maxFairnessWork)
      return reject(plans,
                    "process-state plan capability maxFairnessWork exceeded");
    pcs += plan.pcs().size();
    slots += plan.liveSlots().size();
    wakes += plan.wakes().size();
    transitions += plan.transitions().size();
  }
  if (pcs > limits.maxProgramCounters)
    return reject(plans,
                  "process-state plan capability maxProgramCounters exceeded");
  if (slots > limits.maxLiveSlots)
    return reject(plans, "process-state plan capability maxLiveSlots exceeded");
  if (wakes > limits.maxWakeRecords)
    return reject(plans,
                  "process-state plan capability maxWakeRecords exceeded");
  if (transitions > limits.maxTransitions)
    return reject(plans,
                  "process-state plan capability maxTransitions exceeded");
  if (actions > limits.maxPlannedOperations)
    return reject(
        plans, "process-state plan capability maxPlannedOperations exceeded");
  for (size_t index = 0; index < plans.callees().size(); ++index)
    for (size_t prior = 0; prior < index; ++prior)
      if (plans.callees()[index].id() == plans.callees()[prior].id())
        return reject(
            plans, "process-state plan invariant violated: duplicate ordinal");
  llvm::StringRef previousSpecialization;
  for (auto [index, callee] : llvm::enumerate(plans.callees())) {
    if (callee.id().value() != index)
      return reject(plans,
                    "process-state plan invariant violated: non-dense ordinal");
    if (!previousSpecialization.empty() &&
        previousSpecialization ==
            detail::generatedCalleeSpecializationBytes(callee))
      return reject(
          plans, "process-state plan invariant violated: duplicate identity");
    previousSpecialization = detail::generatedCalleeSpecializationBytes(callee);
    if (callee.role() == ProcessHelperRole::WakeNextDelta &&
        callee.effect() != ProcessEffectKind::Stateful)
      return reject(plans,
                    "process-state plan invariant violated: effect mismatch");
    if (callee.role() == ProcessHelperRole::WakeNextDelta &&
        (detail::generatedCalleeSpecializationBytes(callee) !=
             kWakeNextDeltaSpecialization ||
         bindings::sha256Fingerprint(detail::generatedCalleeSpecializationBytes(
             callee)) != callee.fingerprint()))
      return reject(plans, "process-state plan invariant violated: callee "
                           "specialization mismatch");
  }
  for (auto [index, type] : llvm::enumerate(plans.valueTypes())) {
    if (type.id().value() != index)
      return reject(plans,
                    "process-state plan invariant violated: non-dense ordinal");
    if (!type.fingerprint().starts_with("sha256:") ||
        type.fingerprint().size() != 71 || type.kind() != type.payload().kind())
      return reject(
          plans,
          "process-state plan invariant violated: value-type specialization "
          "mismatch");
  }
  return mlir::success();
}

} // namespace acir
