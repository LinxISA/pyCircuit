#include "ProcessStatePlanInternal.h"

#include "acir/Bindings/Binding.h"

#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"

#include <cassert>
#include <functional>
#include <limits>

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

llvm::StringRef helperRoleSpelling(ProcessHelperRole role) {
  static constexpr llvm::StringLiteral names[] = {"record_create",
                                                  "record_get",
                                                  "record_with",
                                                  "packet_serialize",
                                                  "packet_deserialize",
                                                  "trace_decode",
                                                  "queue_try_send",
                                                  "queue_try_recv",
                                                  "event_schedule",
                                                  "trace_open",
                                                  "trace_next",
                                                  "trace_eof",
                                                  "trace_position",
                                                  "contract_require",
                                                  "contract_ensure",
                                                  "contract_assert",
                                                  "probe",
                                                  "stat_add",
                                                  "wake_condition",
                                                  "wake_resource",
                                                  "wake_event_queue",
                                                  "wake_next_delta",
                                                  "scalar_wrap",
                                                  "scalar_unwrap"};
  return names[static_cast<unsigned>(role)];
}

llvm::StringRef wakeTypeKey(ProcessWakeKind kind) {
  static constexpr llvm::StringLiteral keys[] = {
      "@acir_wake_condition", "@acir_wake_resource", "@acir_wake_event_queue",
      "@acir_wake_next_delta"};
  return keys[static_cast<unsigned>(kind)];
}

bool validTypeKey(llvm::StringRef key) {
  auto validDigest = [](llvm::StringRef value) {
    return value.size() == 64 && llvm::all_of(value, [](char c) {
             return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
  };
  if (key.starts_with("mlir:"))
    return key.size() > 5;
  if (key.consume_front("storage:value:") ||
      key.consume_front("storage:packet:"))
    return validDigest(key);
  return key == "@acir_wake_condition" || key == "@acir_wake_resource" ||
         key == "@acir_wake_event_queue" || key == "@acir_wake_next_delta";
}

bool validCalleeSemantics(const ProcessGeneratedCalleePlan &callee) {
  auto inputs = callee.inputTypeKeys();
  auto results = callee.resultTypeKeys();
  const ProcessGeneratedCalleePayload &payload = callee.payload();
  switch (callee.role()) {
  case ProcessHelperRole::RecordCreate: {
    auto fields = payload.recordCreate().fields();
    if (inputs.size() != fields.size() || results.size() != 1 ||
        results[0] != payload.recordCreate().recordType())
      return false;
    llvm::SmallVector<llvm::StringRef> names;
    for (auto [input, field] : llvm::zip_equal(inputs, fields)) {
      if (input != field.typeKey() || field.name().empty() ||
          llvm::is_contained(names, field.name()))
        return false;
      names.push_back(field.name());
    }
    return true;
  }
  case ProcessHelperRole::RecordGet:
    return inputs.size() == 1 && results.size() == 1 &&
           inputs[0] == payload.recordGet().record() &&
           results[0] == payload.recordGet().result();
  case ProcessHelperRole::RecordWith:
    return inputs.size() == 2 && results.size() == 1 &&
           inputs[0] == payload.recordWith().record() &&
           inputs[1] == payload.recordWith().value() &&
           results[0] == payload.recordWith().record();
  case ProcessHelperRole::PacketSerialize:
    return inputs.size() == 1 && results.size() == 1 &&
           inputs[0] == payload.packetSerialize().packetType();
  case ProcessHelperRole::PacketDeserialize:
    return inputs.size() == 1 && results.size() == 1 &&
           results[0] == payload.packetDeserialize().packetType();
  case ProcessHelperRole::TraceDecode:
    return inputs.size() == 1 && results.size() == 1 &&
           inputs[0] == payload.traceDecode().entry() &&
           results[0] == payload.traceDecode().result();
  case ProcessHelperRole::QueueTrySend:
    return inputs.size() == 1 && results.size() == 1 &&
           inputs[0] == payload.queueTrySend().element();
  case ProcessHelperRole::QueueTryRecv:
    return inputs.empty() && results.size() == 2 &&
           results[0] == payload.queueTryRecv().element();
  case ProcessHelperRole::EventSchedule:
    return inputs.size() == 2 && results.empty() &&
           inputs[0] == payload.eventSchedule().value() &&
           inputs[1] == payload.eventSchedule().delay();
  case ProcessHelperRole::TraceOpen:
    return inputs.empty() && results.size() == 1;
  case ProcessHelperRole::TraceNext:
    return inputs.size() == 1 && results.size() == 3 &&
           inputs[0] == results[0] && results[1] == payload.traceNext().entry();
  case ProcessHelperRole::TraceEof:
  case ProcessHelperRole::TracePosition:
    return inputs.size() == 1 && results.size() == 1;
  case ProcessHelperRole::ContractRequire:
  case ProcessHelperRole::ContractEnsure:
  case ProcessHelperRole::ContractAssert:
    return inputs.size() == 1 && results.empty();
  case ProcessHelperRole::Probe:
    return inputs.empty() && results.size() == 1 &&
           results[0] == payload.probe().result();
  case ProcessHelperRole::StatAdd:
    return inputs.size() == 1 && results.empty() &&
           inputs[0] == payload.statAdd().valueType();
  case ProcessHelperRole::WakeCondition:
    return inputs.empty() && results.size() == 1 &&
           payload.wakeCondition().wakeKind() == ProcessWakeKind::Condition &&
           payload.wakeCondition().wakeType() ==
               wakeTypeKey(ProcessWakeKind::Condition) &&
           results[0] == payload.wakeCondition().wakeType();
  case ProcessHelperRole::WakeResource:
    return inputs.empty() && results.size() == 1 &&
           payload.wakeResource().wakeKind() == ProcessWakeKind::Resource &&
           payload.wakeResource().wakeType() ==
               wakeTypeKey(ProcessWakeKind::Resource) &&
           results[0] == payload.wakeResource().wakeType();
  case ProcessHelperRole::WakeEventQueue:
    return inputs.empty() && results.size() == 1 &&
           payload.wakeEventQueue().wakeKind() == ProcessWakeKind::EventQueue &&
           payload.wakeEventQueue().wakeType() ==
               wakeTypeKey(ProcessWakeKind::EventQueue) &&
           results[0] == payload.wakeEventQueue().wakeType();
  case ProcessHelperRole::WakeNextDelta:
    return inputs.empty() && results.size() == 1 &&
           payload.wakeNextDelta().wakeKind() == ProcessWakeKind::NextDelta &&
           payload.wakeNextDelta().wakeType() ==
               wakeTypeKey(ProcessWakeKind::NextDelta) &&
           results[0] == payload.wakeNextDelta().wakeType();
  case ProcessHelperRole::ScalarWrap:
    return inputs.size() == 1 && results.size() == 1 &&
           payload.scalarWrap().direction() == ProcessWrapperDirection::Wrap &&
           inputs[0] == payload.scalarWrap().scalar() &&
           results[0] == payload.scalarWrap().valueType();
  case ProcessHelperRole::ScalarUnwrap:
    return inputs.size() == 1 && results.size() == 1 &&
           payload.scalarUnwrap().direction() ==
               ProcessWrapperDirection::Unwrap &&
           inputs[0] == payload.scalarUnwrap().valueType() &&
           results[0] == payload.scalarUnwrap().scalar();
  }
  return false;
}

} // namespace

mlir::FailureOr<ProcessStatePlanSet>
detail::PlanSetBuilder::buildEmpty(mlir::ModuleOp module) {
  bool hasProcess = false;
  module.walk([&](ac::ProcessOp) { hasProcess = true; });
  if (hasProcess) {
    module.emitError(
        "empty process-state fixture requires zero ac.process operations");
    return mlir::failure();
  }
  auto epoch = module->getAttrOfType<mlir::StringAttr>("ac.contract_epoch");
  if (!epoch || epoch.getValue() != "0.1") {
    module.emitError("empty process-state fixture requires contract epoch 0.1");
    return mlir::failure();
  }
  return ProcessStatePlanSet(std::make_shared<ProcessStatePlanSet::Impl>());
}

mlir::FailureOr<ProcessStatePlanSet>
detail::PlanSetBuilder::buildYieldOnly(mlir::ModuleOp module) {
  auto frozenEpoch = module->getAttrOfType<mlir::StringAttr>("ac.freeze_epoch");
  if (!frozenEpoch || frozenEpoch.getValue() != "0.1") {
    module.emitError(
        "yield-only process-state fixture requires a frozen v0.1 model");
    return mlir::failure();
  }

  struct ProcessFixture {
    std::string definitionKey;
    ac::ProcessOp process;
    ac::YieldSimOp yield;
  };
  llvm::SmallVector<ProcessFixture> fixtures;
  module.walk([&](ac::ProcessOp process) {
    auto owner = process->getParentOfType<ac::ModuleOp>();
    auto moduleName = owner ? mlir::SymbolTable::getSymbolName(owner) : nullptr;
    auto processName = mlir::SymbolTable::getSymbolName(process);
    ac::YieldSimOp yield;
    unsigned yieldCount = 0;
    process.walk([&](ac::YieldSimOp candidate) {
      yield = candidate;
      ++yieldCount;
    });
    if (!owner || !moduleName || !processName || yieldCount != 1 ||
        !process.getBody().hasOneBlock() ||
        !llvm::hasSingleElement(process.getBody().front())) {
      fixtures.push_back({{}, process, {}});
      return;
    }
    fixtures.push_back(
        {("@" + moduleName.str() + "::@" + processName.str()), process, yield});
  });
  if (fixtures.empty()) {
    module.emitError(
        "yield-only process-state fixture requires at least one ac.process");
    return mlir::failure();
  }
  if (llvm::any_of(fixtures, [](const ProcessFixture &fixture) {
        return fixture.definitionKey.empty() || !fixture.yield;
      })) {
    module.emitError("yield-only process-state fixture requires exactly one "
                     "ac.yield_sim per process and no other operations");
    return mlir::failure();
  }
  llvm::sort(fixtures,
             [](const ProcessFixture &lhs, const ProcessFixture &rhs) {
               return lhs.definitionKey < rhs.definitionKey;
             });
  for (auto [index, fixture] : llvm::enumerate(fixtures))
    if (index && fixtures[index - 1].definitionKey == fixture.definitionKey) {
      module.emitError(
          "yield-only process-state fixture has duplicate definition key");
      return mlir::failure();
    }

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

  auto setImpl = std::make_shared<ProcessStatePlanSet::Impl>();
  setImpl->callees.push_back(callee);
  for (ProcessFixture &fixture : fixtures) {
    std::string operationPath = fixture.definitionKey + "/r0/b0/o0";
    auto originalImpl = std::make_shared<ProcessOriginalOccurrence::Impl>();
    originalImpl->operation = fixture.yield;
    originalImpl->operationPath = operationPath;
    ProcessOriginalOccurrence original(originalImpl);
    auto occurrenceImpl = std::make_shared<ProcessOccurrenceId::Impl>();
    occurrenceImpl->kind = ProcessOccurrenceKind::Original;
    occurrenceImpl->original = original;
    ProcessOccurrenceId occurrence(occurrenceImpl);

    auto wakeImpl = std::make_shared<ProcessWakePlan::Impl>();
    wakeImpl->id = ProcessWakeId(0);
    wakeImpl->kind = ProcessWakeKind::NextDelta;
    wakeImpl->operation = fixture.yield;
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
    blockImpl->originRegion = &fixture.process.getBody();
    blockImpl->originBlock = &fixture.process.getBody().front();
    blockImpl->path = fixture.definitionKey + "/plan/pc/entry/b00000000";
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
    processImpl->definitionKey = fixture.definitionKey;
    processImpl->process = fixture.process;
    processImpl->entryPc = ProcessPcId(0);
    processImpl->pcs.push_back(pc);
    processImpl->blocks.push_back(block);
    processImpl->wakes.push_back(wake);
    processImpl->transitions.push_back(transition);
    processImpl->pcBitWidth = 1;
    processImpl->fairnessWork = 2;
    setImpl->processes.push_back(ProcessStatePlan(processImpl));
  }
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
    assert(impl->processes.size() > 1 &&
           "permutation fixture must contain multiple processes");
    std::swap(impl->processes[0], impl->processes[1]);
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
    ProcessStatePlanSet seeded = cloneWithUnpairedLiveSlotCallee(plans);
    impl = std::make_shared<ProcessStatePlanSet::Impl>(*seeded.impl_);
    auto process = std::make_shared<ProcessStatePlan::Impl>(
        *impl->processes.front().impl_);
    process->liveSlots.clear();
    impl->processes.front() = ProcessStatePlan(process);
    auto type = std::make_shared<ProcessValueTypePlan::Impl>(
        *impl->valueTypes.front().impl_);
    type->fingerprint =
        "sha256:"
        "0000000000000000000000000000000000000000000000000000000000000000";
    impl->valueTypes.front() = ProcessValueTypePlan(type);
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

ProcessStatePlanSet detail::PlanSetBuilder::cloneWithMissingWakeCallee(
    const ProcessStatePlanSet &plans) {
  auto impl = std::make_shared<ProcessStatePlanSet::Impl>(*plans.impl_);
  auto process =
      std::make_shared<ProcessStatePlan::Impl>(*impl->processes.front().impl_);
  auto wake =
      std::make_shared<ProcessWakePlan::Impl>(*process->wakes.front().impl_);
  wake->callee.reset();
  process->wakes.front() = ProcessWakePlan(wake);
  impl->processes.front() = ProcessStatePlan(process);
  return ProcessStatePlanSet(impl);
}

ProcessStatePlanSet detail::PlanSetBuilder::cloneWithDanglingSuspendTransition(
    const ProcessStatePlanSet &plans) {
  auto impl = std::make_shared<ProcessStatePlanSet::Impl>(*plans.impl_);
  auto process =
      std::make_shared<ProcessStatePlan::Impl>(*impl->processes.front().impl_);
  auto block =
      std::make_shared<ProcessBlockPlan::Impl>(*process->blocks.front().impl_);
  auto edge =
      std::make_shared<ProcessControlEdgePlan::Impl>(*block->edge->impl_);
  edge->transition = ProcessTransitionId(99);
  block->edge = ProcessControlEdgePlan(edge);
  process->blocks.front() = ProcessBlockPlan(block);
  impl->processes.front() = ProcessStatePlan(process);
  return ProcessStatePlanSet(impl);
}

ProcessStatePlanSet detail::PlanSetBuilder::cloneWithUnpairedLiveSlotCallee(
    const ProcessStatePlanSet &plans) {
  auto impl = std::make_shared<ProcessStatePlanSet::Impl>(*plans.impl_);
  mlir::MLIRContext *context = plans.processes().front().process().getContext();
  mlir::Type i32 = mlir::IntegerType::get(context, 32);

  auto storageImpl = std::make_shared<ProcessStorageValuePayload::Impl>();
  storageImpl->widthBits = 32;
  storageImpl->encoding = "i32";
  ProcessStorageValuePayload storage(storageImpl);
  auto payloadImpl = std::make_shared<ProcessValueTypePayload::Impl>();
  payloadImpl->kind = ProcessValueTypeKind::Value;
  payloadImpl->value = storage;
  ProcessValueTypePayload payload(payloadImpl);

  llvm::json::Object specialization;
  specialization["acir_type"] = "i32";
  specialization["contract_epoch"] = "0.1";
  specialization["kind"] = "value";
  llvm::json::Object payloadObject;
  payloadObject["encoding"] = "i32";
  payloadObject["members"] = llvm::json::Array();
  payloadObject["width_bits"] = 32;
  specialization["payload"] = std::move(payloadObject);
  specialization["schema"] = "acir-generated-value-type-0.1";
  auto canonical =
      bindings::canonicalizeJson(llvm::json::Value(std::move(specialization)));
  assert(canonical && "literal value-type specialization must canonicalize");
  std::string fingerprint = bindings::sha256Fingerprint(*canonical);
  llvm::StringRef digest = llvm::StringRef(fingerprint).drop_front(7);

  auto typeImpl = std::make_shared<ProcessValueTypePlan::Impl>();
  typeImpl->id = ProcessValueTypeId(0);
  typeImpl->symbol = ("@acir_value_" + digest).str();
  typeImpl->cpp = ("acir::generated::value_" + digest).str();
  typeImpl->kind = ProcessValueTypeKind::Value;
  typeImpl->fingerprint = fingerprint;
  typeImpl->acirType = i32;
  typeImpl->payload = payload;
  typeImpl->specializationBytes = std::move(*canonical);
  impl->valueTypes.push_back(ProcessValueTypePlan(typeImpl));

  auto process =
      std::make_shared<ProcessStatePlan::Impl>(*impl->processes.front().impl_);
  auto slotImpl = std::make_shared<ProcessLiveSlotPlan::Impl>();
  slotImpl->id = ProcessLiveSlotId(0);
  slotImpl->name = "live00000000";
  slotImpl->type = i32;
  slotImpl->storageType = ProcessValueTypeId(0);
  slotImpl->wrapCallee = ProcessCalleeId(0);
  process->liveSlots.push_back(ProcessLiveSlotPlan(slotImpl));
  impl->processes.front() = ProcessStatePlan(process);
  return ProcessStatePlanSet(impl);
}

ProcessStatePlanSet detail::PlanSetBuilder::cloneWithMissingValueTypePayload(
    const ProcessStatePlanSet &plans) {
  ProcessStatePlanSet result = cloneWithUnpairedLiveSlotCallee(plans);
  auto impl = std::make_shared<ProcessStatePlanSet::Impl>(*result.impl_);
  auto type = std::make_shared<ProcessValueTypePlan::Impl>(
      *impl->valueTypes.front().impl_);
  type->payload.reset();
  impl->valueTypes.front() = ProcessValueTypePlan(type);
  auto process =
      std::make_shared<ProcessStatePlan::Impl>(*impl->processes.front().impl_);
  process->liveSlots.clear();
  impl->processes.front() = ProcessStatePlan(process);
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

llvm::StringRef
detail::PlanSetBuilder::specializationBytes(const ProcessValueTypePlan &type) {
  return type.impl_->specializationBytes;
}

bool detail::PlanSetBuilder::validEdgeShape(
    const ProcessControlEdgePlan &edge) {
  switch (edge.impl_->kind) {
  case ProcessControlEdgeKind::Branch:
    return edge.impl_->condition && edge.impl_->trueBlock &&
           edge.impl_->falseBlock && !edge.impl_->targetBlock &&
           !edge.impl_->transition;
  case ProcessControlEdgeKind::LocalContinue:
    return edge.impl_->targetBlock && !edge.impl_->condition &&
           !edge.impl_->trueBlock && !edge.impl_->falseBlock &&
           !edge.impl_->transition;
  case ProcessControlEdgeKind::Suspend:
    return edge.impl_->transition && !edge.impl_->condition &&
           !edge.impl_->trueBlock && !edge.impl_->falseBlock &&
           !edge.impl_->targetBlock;
  case ProcessControlEdgeKind::Terminate:
    return !edge.impl_->condition && !edge.impl_->trueBlock &&
           !edge.impl_->falseBlock && !edge.impl_->targetBlock &&
           !edge.impl_->transition;
  }
  return false;
}

llvm::StringRef
detail::PlanSetBuilder::structuralError(const ProcessStatePlanSet &plans) {
  if (!plans.impl_)
    return "process-state plan invariant violated: missing plan-set storage";

  auto validOccurrence = [](const ProcessOccurrenceId &root) {
    llvm::SmallVector<const ProcessOccurrenceId *> worklist{&root};
    llvm::SmallPtrSet<const ProcessOccurrenceId::Impl *, 16> visited;
    while (!worklist.empty()) {
      const ProcessOccurrenceId *occurrence = worklist.pop_back_val();
      if (!occurrence->impl_)
        return false;
      if (!visited.insert(occurrence->impl_.get()).second)
        return false;
      auto &impl = *occurrence->impl_;
      unsigned active =
          static_cast<unsigned>(impl.original.has_value()) +
          static_cast<unsigned>(impl.syntheticLoop.has_value()) +
          static_cast<unsigned>(impl.syntheticWrapper.has_value()) +
          static_cast<unsigned>(impl.syntheticConstant.has_value());
      if (active != 1)
        return false;
      switch (impl.kind) {
      case ProcessOccurrenceKind::Original:
        if (!impl.original || !impl.original->impl_)
          return false;
        for (const ProcessCallSitePlan &site : impl.original->impl_->callSites)
          if (!site.impl_)
            return false;
        break;
      case ProcessOccurrenceKind::SyntheticLoop:
        if (!impl.syntheticLoop || !impl.syntheticLoop->impl_ ||
            !impl.syntheticLoop->impl_->anchor)
          return false;
        worklist.push_back(&*impl.syntheticLoop->impl_->anchor);
        break;
      case ProcessOccurrenceKind::SyntheticWrapper:
        if (!impl.syntheticWrapper || !impl.syntheticWrapper->impl_ ||
            !impl.syntheticWrapper->impl_->anchor ||
            !impl.syntheticWrapper->impl_->transition ||
            !impl.syntheticWrapper->impl_->slot)
          return false;
        worklist.push_back(&*impl.syntheticWrapper->impl_->anchor);
        break;
      case ProcessOccurrenceKind::SyntheticConstant:
        if (!impl.syntheticConstant || !impl.syntheticConstant->impl_ ||
            !impl.syntheticConstant->impl_->anchor)
          return false;
        worklist.push_back(&*impl.syntheticConstant->impl_->anchor);
        break;
      }
    }
    return true;
  };

  auto validPlannedValue = [&](const ProcessPlannedValue &value) {
    if (!value.impl_ || !value.impl_->type)
      return false;
    unsigned active =
        static_cast<unsigned>(value.impl_->original.has_value()) +
        static_cast<unsigned>(value.impl_->capture.has_value()) +
        static_cast<unsigned>(value.impl_->liveSlot.has_value()) +
        static_cast<unsigned>(value.impl_->synthetic.has_value()) +
        static_cast<unsigned>(value.impl_->constant.has_value());
    if (active != 1)
      return false;
    switch (value.impl_->kind) {
    case ProcessPlannedValueKind::Original:
      return value.impl_->original && value.impl_->original->impl_ &&
             value.impl_->original->impl_->occurrence &&
             value.impl_->original->impl_->coordinate &&
             value.impl_->original->impl_->coordinate->impl_ &&
             validOccurrence(*value.impl_->original->impl_->occurrence);
    case ProcessPlannedValueKind::Capture:
      return value.impl_->capture && value.impl_->capture->impl_ &&
             value.impl_->capture->impl_->capture;
    case ProcessPlannedValueKind::LiveSlot:
      return value.impl_->liveSlot && value.impl_->liveSlot->impl_ &&
             value.impl_->liveSlot->impl_->slot;
    case ProcessPlannedValueKind::Synthetic:
      return value.impl_->synthetic && value.impl_->synthetic->impl_ &&
             value.impl_->synthetic->impl_->occurrence &&
             value.impl_->synthetic->impl_->coordinate &&
             value.impl_->synthetic->impl_->coordinate->impl_ &&
             validOccurrence(*value.impl_->synthetic->impl_->occurrence);
    case ProcessPlannedValueKind::Constant:
      return value.impl_->constant && value.impl_->constant->impl_;
    }
    return false;
  };

  auto validPayloadArm = [](const ProcessGeneratedCalleePayload &payload) {
    if (!payload.impl_)
      return false;
    auto &p = *payload.impl_;
    unsigned active = static_cast<unsigned>(p.recordCreate.has_value()) +
                      static_cast<unsigned>(p.recordGet.has_value()) +
                      static_cast<unsigned>(p.recordWith.has_value()) +
                      static_cast<unsigned>(p.packetSerialize.has_value()) +
                      static_cast<unsigned>(p.packetDeserialize.has_value()) +
                      static_cast<unsigned>(p.traceDecode.has_value()) +
                      static_cast<unsigned>(p.queueTrySend.has_value()) +
                      static_cast<unsigned>(p.queueTryRecv.has_value()) +
                      static_cast<unsigned>(p.eventSchedule.has_value()) +
                      static_cast<unsigned>(p.traceOpen.has_value()) +
                      static_cast<unsigned>(p.traceNext.has_value()) +
                      static_cast<unsigned>(p.traceEof.has_value()) +
                      static_cast<unsigned>(p.tracePosition.has_value()) +
                      static_cast<unsigned>(p.contractRequire.has_value()) +
                      static_cast<unsigned>(p.contractEnsure.has_value()) +
                      static_cast<unsigned>(p.contractAssert.has_value()) +
                      static_cast<unsigned>(p.probe.has_value()) +
                      static_cast<unsigned>(p.statAdd.has_value()) +
                      static_cast<unsigned>(p.wakeCondition.has_value()) +
                      static_cast<unsigned>(p.wakeResource.has_value()) +
                      static_cast<unsigned>(p.wakeEventQueue.has_value()) +
                      static_cast<unsigned>(p.wakeNextDelta.has_value()) +
                      static_cast<unsigned>(p.scalarWrap.has_value()) +
                      static_cast<unsigned>(p.scalarUnwrap.has_value());
    if (active != 1)
      return false;
    auto present = [](const auto &arm) { return arm && arm->impl_; };
    switch (p.role) {
    case ProcessHelperRole::RecordCreate:
      return present(p.recordCreate) &&
             llvm::all_of(p.recordCreate->impl_->fields,
                          [](const ProcessRecordFieldDescriptor &field) {
                            return static_cast<bool>(field.impl_);
                          });
    case ProcessHelperRole::RecordGet:
      return present(p.recordGet);
    case ProcessHelperRole::RecordWith:
      return present(p.recordWith);
    case ProcessHelperRole::PacketSerialize:
      return present(p.packetSerialize);
    case ProcessHelperRole::PacketDeserialize:
      return present(p.packetDeserialize);
    case ProcessHelperRole::TraceDecode:
      return present(p.traceDecode);
    case ProcessHelperRole::QueueTrySend:
      return present(p.queueTrySend);
    case ProcessHelperRole::QueueTryRecv:
      return present(p.queueTryRecv);
    case ProcessHelperRole::EventSchedule:
      return present(p.eventSchedule);
    case ProcessHelperRole::TraceOpen:
      return present(p.traceOpen);
    case ProcessHelperRole::TraceNext:
      return present(p.traceNext);
    case ProcessHelperRole::TraceEof:
      return present(p.traceEof);
    case ProcessHelperRole::TracePosition:
      return present(p.tracePosition);
    case ProcessHelperRole::ContractRequire:
      return present(p.contractRequire);
    case ProcessHelperRole::ContractEnsure:
      return present(p.contractEnsure);
    case ProcessHelperRole::ContractAssert:
      return present(p.contractAssert);
    case ProcessHelperRole::Probe:
      return present(p.probe);
    case ProcessHelperRole::StatAdd:
      return present(p.statAdd);
    case ProcessHelperRole::WakeCondition:
      return present(p.wakeCondition);
    case ProcessHelperRole::WakeResource:
      return present(p.wakeResource);
    case ProcessHelperRole::WakeEventQueue:
      return present(p.wakeEventQueue);
    case ProcessHelperRole::WakeNextDelta:
      return present(p.wakeNextDelta);
    case ProcessHelperRole::ScalarWrap:
      return present(p.scalarWrap);
    case ProcessHelperRole::ScalarUnwrap:
      return present(p.scalarUnwrap);
    }
    return false;
  };

  for (const ProcessGeneratedCalleePlan &callee : plans.impl_->callees)
    if (!callee.impl_ || !callee.impl_->id || !callee.impl_->payload ||
        !validPayloadArm(*callee.impl_->payload))
      return "process-state plan invariant violated: callee specialization "
             "mismatch";
  for (const ProcessValueTypePlan &type : plans.impl_->valueTypes) {
    if (!type.impl_ || !type.impl_->id || !type.impl_->payload)
      return "process-state plan invariant violated: value-type specialization "
             "mismatch";
    if (!type.impl_->payload->impl_)
      return "process-state plan invariant violated: value-type specialization "
             "mismatch";
    auto &payload = *type.impl_->payload->impl_;
    if ((payload.kind == ProcessValueTypeKind::Value &&
         (!payload.value || !payload.value->impl_)) ||
        (payload.kind == ProcessValueTypeKind::Packet &&
         (!payload.packet || !payload.packet->impl_)))
      return "process-state plan invariant violated: value-type specialization "
             "mismatch";
    auto members = payload.kind == ProcessValueTypeKind::Value
                       ? llvm::ArrayRef(payload.value->impl_->members)
                       : llvm::ArrayRef(payload.packet->impl_->members);
    if (llvm::any_of(members, [](const ProcessValueTypeMemberPlan &member) {
          return !member.impl_;
        }))
      return "process-state plan invariant violated: value-type specialization "
             "mismatch";
  }

  for (const ProcessStatePlan &plan : plans.impl_->processes) {
    if (!plan.impl_ || !plan.impl_->process || !plan.impl_->entryPc ||
        plan.impl_->entryPc->value() >= plan.impl_->pcs.size())
      return "process-state plan invariant violated: definition key mismatch";
    for (const ProcessCapturePlan &capture : plan.impl_->captures)
      if (!capture.impl_ || !capture.impl_->id || !capture.impl_->type ||
          !capture.impl_->operand || !capture.impl_->entryArgument)
        return "process-state plan invariant violated: dangling reference";
    for (const ProcessPcPlan &pc : plan.impl_->pcs) {
      if (!pc.impl_ || !pc.impl_->id)
        return "process-state plan invariant violated: non-dense ordinal";
      if (llvm::any_of(pc.impl_->blocks, [&](ProcessBlockId block) {
            return block.value() >= plan.impl_->blocks.size();
          }))
        return "process-state plan invariant violated: ID kind mismatch";
    }
    for (const ProcessBlockPlan &block : plan.impl_->blocks) {
      if (!block.impl_ || !block.impl_->id || !block.impl_->pc ||
          block.impl_->pc->value() >= plan.impl_->pcs.size() ||
          !block.impl_->edge)
        return "process-state plan invariant violated: dangling reference";
      if (!validEdgeShape(*block.impl_->edge))
        return "process-state plan invariant violated: invalid edge binding";
      auto &edge = *block.impl_->edge->impl_;
      auto validBinding = [&](const ProcessForwardingBindingPlan &binding) {
        return binding.impl_ && binding.impl_->from && binding.impl_->to &&
               validPlannedValue(*binding.impl_->from) &&
               validPlannedValue(*binding.impl_->to);
      };
      if ((edge.condition && !validPlannedValue(*edge.condition)) ||
          llvm::any_of(
              edge.trueBindings,
              [&](const auto &binding) { return !validBinding(binding); }) ||
          llvm::any_of(
              edge.falseBindings,
              [&](const auto &binding) { return !validBinding(binding); }) ||
          llvm::any_of(edge.bindings, [&](const auto &binding) {
            return !validBinding(binding);
          }))
        return "process-state plan invariant violated: invalid edge binding";
      if (edge.kind == ProcessControlEdgeKind::Suspend &&
          edge.transition->value() >= plan.impl_->transitions.size())
        return "process-state plan invariant violated: dangling reference";
      if ((edge.kind == ProcessControlEdgeKind::Branch &&
           (edge.trueBlock->value() >= plan.impl_->blocks.size() ||
            edge.falseBlock->value() >= plan.impl_->blocks.size())) ||
          (edge.kind == ProcessControlEdgeKind::LocalContinue &&
           edge.targetBlock->value() >= plan.impl_->blocks.size()))
        return "process-state plan invariant violated: dangling reference";
      for (const ProcessActionPlan &action : block.impl_->actions) {
        if (!action.impl_ || !action.impl_->occurrence ||
            !validOccurrence(*action.impl_->occurrence))
          return "process-state plan invariant violated: invalid action arm";
        bool calleeRequired =
            action.impl_->emission == ProcessEmissionClass::Inline ||
            action.impl_->emission == ProcessEmissionClass::Invoke ||
            action.impl_->emission == ProcessEmissionClass::Wrap ||
            action.impl_->emission == ProcessEmissionClass::Unwrap;
        bool scalarRequired =
            action.impl_->emission == ProcessEmissionClass::CopyScalar;
        if (action.impl_->callee.has_value() != calleeRequired ||
            action.impl_->scalarOp.has_value() != scalarRequired ||
            (action.impl_->callee &&
             action.impl_->callee->value() >= plans.impl_->callees.size()) ||
            (action.impl_->scalarOp && !action.impl_->scalarOp->impl_))
          return "process-state plan invariant violated: invalid action arm";
        for (const ProcessPlannedValue &value : action.impl_->operands)
          if (!validPlannedValue(value))
            return "process-state plan invariant violated: invalid planned "
                   "value arm";
        for (const ProcessPlannedValue &value : action.impl_->results)
          if (!validPlannedValue(value))
            return "process-state plan invariant violated: invalid planned "
                   "value arm";
      }
      for (const ProcessTransitionLoadPlan &load : block.impl_->loads) {
        if (!load.impl_ || !load.impl_->slot ||
            load.impl_->slot->value() >= plan.impl_->liveSlots.size())
          return "process-state plan invariant violated: dangling reference";
        for (const ProcessPlannedValue &value : load.impl_->replacements)
          if (!validPlannedValue(value))
            return "process-state plan invariant violated: invalid planned "
                   "value arm";
      }
      for (const ProcessControlFramePlan &frame : block.impl_->frames) {
        if (!frame.impl_)
          return "process-state plan invariant violated: invalid frame phase";
        for (const ProcessForwardingBindingPlan &binding :
             frame.impl_->bindings)
          if (!binding.impl_ || !binding.impl_->from || !binding.impl_->to ||
              !validPlannedValue(*binding.impl_->from) ||
              !validPlannedValue(*binding.impl_->to))
            return "process-state plan invariant violated: dangling reference";
      }
    }
    for (const ProcessLiveSlotPlan &slot : plan.impl_->liveSlots) {
      if (!slot.impl_ || !slot.impl_->id || !slot.impl_->storageType ||
          slot.impl_->storageType->value() >= plans.impl_->valueTypes.size() ||
          (slot.impl_->wrapCallee &&
           slot.impl_->wrapCallee->value() >= plans.impl_->callees.size()) ||
          (slot.impl_->unwrapCallee &&
           slot.impl_->unwrapCallee->value() >= plans.impl_->callees.size()))
        return "process-state plan invariant violated: dangling reference";
      if (slot.impl_->wrapCallee.has_value() !=
          slot.impl_->unwrapCallee.has_value())
        return "process-state plan invariant violated: invalid live-slot "
               "wrapper pair";
      for (const ProcessPlannedValue &value : slot.impl_->memberValues)
        if (!validPlannedValue(value))
          return "process-state plan invariant violated: invalid planned value "
                 "arm";
    }
    for (const ProcessWakePlan &wake : plan.impl_->wakes) {
      if (!wake.impl_ || !wake.impl_->id || !wake.impl_->callee ||
          wake.impl_->callee->value() >= plans.impl_->callees.size() ||
          !wake.impl_->occurrence || !validOccurrence(*wake.impl_->occurrence))
        return "process-state plan invariant violated: invalid wake callee";
      for (const ProcessSubscriptionSourcePlan &source : wake.impl_->sources) {
        if (!source.impl_)
          return "process-state plan invariant violated: dangling reference";
        switch (source.impl_->kind) {
        case ProcessSubscriptionSourceKind::Capture:
          if (!source.impl_->capture || source.impl_->value ||
              source.impl_->declaration || !source.impl_->symbol.empty() ||
              !source.impl_->ownerPath.empty())
            return "process-state plan invariant violated: dangling reference";
          break;
        case ProcessSubscriptionSourceKind::Value:
          if (!source.impl_->value || source.impl_->capture ||
              source.impl_->declaration || !source.impl_->symbol.empty())
            return "process-state plan invariant violated: dangling reference";
          break;
        case ProcessSubscriptionSourceKind::Symbol:
          if (!source.impl_->declaration || source.impl_->capture ||
              source.impl_->value || source.impl_->symbol.empty())
            return "process-state plan invariant violated: dangling reference";
          break;
        }
      }
    }
    for (const ProcessTransitionPlan &transition : plan.impl_->transitions) {
      if (!transition.impl_ || !transition.impl_->id ||
          !transition.impl_->sourcePc || !transition.impl_->targetPc ||
          !transition.impl_->wake ||
          transition.impl_->sourcePc->value() >= plan.impl_->pcs.size() ||
          transition.impl_->targetPc->value() >= plan.impl_->pcs.size() ||
          transition.impl_->wake->value() >= plan.impl_->wakes.size())
        return "process-state plan invariant violated: dangling reference";
      for (const ProcessTransitionStorePlan &store : transition.impl_->stores)
        if (!store.impl_ || !store.impl_->slot ||
            store.impl_->slot->value() >= plan.impl_->liveSlots.size() ||
            !store.impl_->source || !validPlannedValue(*store.impl_->source))
          return "process-state plan invariant violated: dangling reference";
      for (const ProcessTransitionLoadPlan &load : transition.impl_->loads)
        if (!load.impl_ || !load.impl_->slot ||
            load.impl_->slot->value() >= plan.impl_->liveSlots.size() ||
            llvm::any_of(load.impl_->replacements,
                         [&](const ProcessPlannedValue &value) {
                           return !validPlannedValue(value);
                         }))
          return "process-state plan invariant violated: dangling reference";
    }
  }
  return {};
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

llvm::StringRef detail::lastProcessStatePlanDiagnosticForTest() {
  return lastDiagnostic;
}

mlir::LogicalResult verifyProcessStatePlan(const ProcessStatePlanSet &plans,
                                           const ProcessStateLimits &limits) {
  lastDiagnostic.clear();
  if (llvm::StringRef error = detail::PlanSetBuilder::structuralError(plans);
      !error.empty())
    return reject(plans, error);
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
    ac::ModuleOp owner = plan.process()->getParentOfType<ac::ModuleOp>();
    auto ownerName = owner ? mlir::SymbolTable::getSymbolName(owner) : nullptr;
    auto processName = mlir::SymbolTable::getSymbolName(plan.process());
    if (!ownerName || !processName ||
        plan.definitionKey() !=
            ("@" + ownerName.str() + "::@" + processName.str()))
      return reject(
          plans,
          "process-state plan invariant violated: definition key mismatch");
    if (!previousKey.empty() && previousKey.compare(plan.definitionKey()) >= 0)
      return reject(
          plans,
          "process-state plan invariant violated: unsorted canonical order");
    previousKey = plan.definitionKey();
    auto occurrenceReferencesClose = [&](const ProcessOccurrenceId &root) {
      llvm::SmallVector<const ProcessOccurrenceId *> worklist{&root};
      while (!worklist.empty()) {
        const ProcessOccurrenceId &occurrence = *worklist.pop_back_val();
        switch (occurrence.kind()) {
        case ProcessOccurrenceKind::Original:
          break;
        case ProcessOccurrenceKind::SyntheticLoop:
          worklist.push_back(&occurrence.syntheticLoop().anchor());
          break;
        case ProcessOccurrenceKind::SyntheticWrapper:
          if (occurrence.syntheticWrapper().transition().value() >=
                  plan.transitions().size() ||
              occurrence.syntheticWrapper().slot().value() >=
                  plan.liveSlots().size())
            return false;
          worklist.push_back(&occurrence.syntheticWrapper().anchor());
          break;
        case ProcessOccurrenceKind::SyntheticConstant:
          worklist.push_back(&occurrence.syntheticConstant().anchor());
          break;
        }
      }
      return true;
    };
    auto plannedReferencesClose = [&](const ProcessPlannedValue &value) {
      switch (value.kind()) {
      case ProcessPlannedValueKind::Original:
        return occurrenceReferencesClose(value.original().occurrence());
      case ProcessPlannedValueKind::Capture:
        return value.capture().capture().value() < plan.captures().size();
      case ProcessPlannedValueKind::LiveSlot:
        return value.liveSlot().slot().value() < plan.liveSlots().size();
      case ProcessPlannedValueKind::Synthetic:
        return occurrenceReferencesClose(value.synthetic().occurrence());
      case ProcessPlannedValueKind::Constant:
        return true;
      }
      return false;
    };
    auto bindingsClose =
        [&](llvm::ArrayRef<ProcessForwardingBindingPlan> bindings) {
          return llvm::all_of(
              bindings, [&](const ProcessForwardingBindingPlan &binding) {
                return plannedReferencesClose(binding.from()) &&
                       plannedReferencesClose(binding.to()) &&
                       binding.from().type() == binding.to().type();
              });
        };
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
      if (pc.id().value() != index ||
          pc.name() !=
              (index == 0 ? "entry" : llvm::formatv("pc{0:D8}", index).str()))
        return reject(
            plans, "process-state plan invariant violated: non-dense ordinal");
    llvm::SmallVector<bool> listedBlocks(plan.blocks().size());
    for (const ProcessPcPlan &pc : plan.pcs()) {
      std::optional<uint32_t> previousBlock;
      for (ProcessBlockId block : pc.blocks()) {
        if (block.value() >= plan.blocks().size() ||
            plan.blocks()[block.value()].pc() != pc.id() ||
            listedBlocks[block.value()] ||
            (previousBlock && *previousBlock >= block.value()))
          return reject(
              plans, "process-state plan invariant violated: ID kind mismatch");
        listedBlocks[block.value()] = true;
        previousBlock = block.value();
      }
    }
    if (llvm::is_contained(listedBlocks, false))
      return reject(plans,
                    "process-state plan invariant violated: ID kind mismatch");
    for (auto [index, capture] : llvm::enumerate(plan.captures()))
      if (capture.id().value() != index || !capture.type() ||
          capture.name() != llvm::formatv("capture{0:D8}", index).str() ||
          capture.operand().getType() != capture.type() ||
          capture.entryArgument().getType() != capture.type())
        return reject(
            plans, "process-state plan invariant violated: non-dense ordinal");
    for (auto [index, slot] : llvm::enumerate(plan.liveSlots())) {
      if (slot.id().value() != index ||
          slot.storageType().value() >= plans.valueTypes().size() ||
          !slot.type() ||
          slot.name() != llvm::formatv("live{0:D8}", index).str())
        return reject(
            plans, "process-state plan invariant violated: dangling reference");
      if (slot.wrapCallee()) {
        if (slot.wrapCallee()->value() >= plans.callees().size() ||
            slot.unwrapCallee()->value() >= plans.callees().size() ||
            plans.callees()[slot.wrapCallee()->value()].role() !=
                ProcessHelperRole::ScalarWrap ||
            plans.callees()[slot.unwrapCallee()->value()].role() !=
                ProcessHelperRole::ScalarUnwrap)
          return reject(plans, "process-state plan invariant violated: invalid "
                               "live-slot wrapper pair");
      }
      if (llvm::any_of(slot.memberValues(), [&](const auto &value) {
            return !plannedReferencesClose(value);
          }))
        return reject(
            plans, "process-state plan invariant violated: dangling reference");
    }
    for (auto [index, block] : llvm::enumerate(plan.blocks())) {
      std::string expectedBlockPath =
          (plan.definitionKey() + "/plan/pc/" +
           plan.pcs()[block.pc().value()].name() + "/" +
           llvm::formatv("b{0:D8}", index).str())
              .str();
      if (block.id().value() != index ||
          block.pc().value() >= plan.pcs().size() ||
          block.path() != expectedBlockPath)
        return reject(
            plans, "process-state plan invariant violated: dangling reference");
      actions += block.actions().size();
      for (auto [actionIndex, action] : llvm::enumerate(block.actions())) {
        if (action.id() != actionIndex)
          return reject(
              plans,
              "process-state plan invariant violated: non-dense ordinal");
        uint32_t expectedActionCost =
            action.emission() == ProcessEmissionClass::ForwardOnly ? 0 : 1;
        if (action.cost() != expectedActionCost)
          return reject(plans,
                        "process-state plan invariant violated: cost mismatch");
        if (action.callee() &&
            action.callee()->value() >= plans.callees().size())
          return reject(
              plans,
              "process-state plan invariant violated: dangling reference");
        if (action.callee()) {
          ProcessHelperRole role =
              plans.callees()[action.callee()->value()].role();
          ProcessEmissionClass expectedEmission =
              role <= ProcessHelperRole::TraceDecode
                  ? ProcessEmissionClass::Inline
              : role == ProcessHelperRole::ScalarWrap
                  ? ProcessEmissionClass::Wrap
              : role == ProcessHelperRole::ScalarUnwrap
                  ? ProcessEmissionClass::Unwrap
                  : ProcessEmissionClass::Invoke;
          if ((role >= ProcessHelperRole::WakeCondition &&
               role <= ProcessHelperRole::WakeNextDelta) ||
              action.emission() != expectedEmission)
            return reject(
                plans,
                "process-state plan invariant violated: invalid action arm");
        }
        if ((action.kind() == ProcessActionKind::ScalarWrap) !=
                (action.emission() == ProcessEmissionClass::Wrap) ||
            (action.kind() == ProcessActionKind::ScalarUnwrap) !=
                (action.emission() == ProcessEmissionClass::Unwrap) ||
            (action.kind() == ProcessActionKind::ForInitialize &&
             action.emission() != ProcessEmissionClass::ForwardOnly))
          return reject(
              plans,
              "process-state plan invariant violated: invalid action arm");
        if (action.resultTypes().size() != action.results().size())
          return reject(
              plans, "process-state plan invariant violated: wrong type key");
        for (auto [type, result] :
             llvm::zip_equal(action.resultTypes(), action.results()))
          if (type != result.type())
            return reject(
                plans, "process-state plan invariant violated: wrong type key");
        if (llvm::any_of(action.operands(),
                         [&](const auto &value) {
                           return !plannedReferencesClose(value);
                         }) ||
            llvm::any_of(action.results(), [&](const auto &value) {
              return !plannedReferencesClose(value);
            }))
          return reject(
              plans,
              "process-state plan invariant violated: dangling reference");
      }
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
        if (!bindingsClose(frame.bindings()))
          return reject(
              plans,
              "process-state plan invariant violated: dangling reference");
      }
      if (!detail::PlanSetBuilder::validEdgeShape(block.edge()))
        return reject(
            plans,
            "process-state plan invariant violated: invalid edge binding");
      const ProcessControlEdgePlan &edge = block.edge();
      if ((edge.kind() == ProcessControlEdgeKind::Branch &&
           (edge.trueBlock().value() >= plan.blocks().size() ||
            edge.falseBlock().value() >= plan.blocks().size() ||
            !plannedReferencesClose(edge.condition()))) ||
          (edge.kind() == ProcessControlEdgeKind::LocalContinue &&
           edge.targetBlock().value() >= plan.blocks().size()))
        return reject(
            plans, "process-state plan invariant violated: dangling reference");
      if (!bindingsClose(edge.trueBindings()) ||
          !bindingsClose(edge.falseBindings()) ||
          !bindingsClose(edge.bindings()))
        return reject(
            plans, "process-state plan invariant violated: dangling reference");
      for (const ProcessTransitionLoadPlan &load : block.loads())
        if (load.slot().value() >= plan.liveSlots().size() ||
            llvm::any_of(load.replacements(), [&](const auto &value) {
              return !plannedReferencesClose(value);
            }))
          return reject(
              plans,
              "process-state plan invariant violated: dangling reference");
      for (size_t i = 1; i < block.loads().size(); ++i)
        if (block.loads()[i - 1].slot().value() >=
            block.loads()[i].slot().value())
          return reject(plans, "process-state plan invariant violated: "
                               "unsorted canonical order");
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
      if (wake.typeKey() != wakeTypeKey(wake.kind()))
        return reject(plans,
                      "process-state plan invariant violated: wrong type key");
      if (!occurrenceReferencesClose(wake.occurrence()) ||
          llvm::any_of(wake.sources(), [&](const auto &source) {
            return source.capture() &&
                   source.capture()->value() >= plan.captures().size();
          }))
        return reject(
            plans, "process-state plan invariant violated: dangling reference");
      bool rawHandlesMatch = false;
      switch (wake.kind()) {
      case ProcessWakeKind::Condition:
        rawHandlesMatch = static_cast<bool>(wake.triggeringValue()) &&
                          wake.declaration() == nullptr &&
                          !wake.target().empty();
        break;
      case ProcessWakeKind::Resource:
      case ProcessWakeKind::EventQueue:
        rawHandlesMatch = !wake.triggeringValue() &&
                          wake.declaration() != nullptr &&
                          !wake.target().empty();
        break;
      case ProcessWakeKind::NextDelta:
        rawHandlesMatch = !wake.triggeringValue() &&
                          wake.declaration() == nullptr &&
                          wake.target().empty();
        break;
      }
      if (!rawHandlesMatch)
        return reject(plans,
                      "process-state plan invariant violated: wrong type key");
      const ProcessGeneratedCalleePlan &callee =
          plans.callees()[wake.callee().value()];
      ProcessHelperRole expectedRole = static_cast<ProcessHelperRole>(
          static_cast<unsigned>(ProcessHelperRole::WakeCondition) +
          static_cast<unsigned>(wake.kind()));
      if (callee.role() != expectedRole || !callee.inputTypeKeys().empty() ||
          callee.resultTypeKeys().size() != 1 ||
          callee.resultTypeKeys().front() != wake.typeKey())
        return reject(
            plans,
            "process-state plan invariant violated: invalid wake callee");
    }
    for (auto [index, transition] : llvm::enumerate(plan.transitions())) {
      if (transition.id().value() != index ||
          transition.sourcePc().value() >= plan.pcs().size() ||
          transition.targetPc().value() >= plan.pcs().size() ||
          transition.wake().value() >= plan.wakes().size())
        return reject(
            plans, "process-state plan invariant violated: dangling reference");
      for (const ProcessTransitionStorePlan &store : transition.stores())
        if (store.slot().value() >= plan.liveSlots().size() ||
            !plannedReferencesClose(store.source()))
          return reject(
              plans,
              "process-state plan invariant violated: dangling reference");
      for (const ProcessTransitionLoadPlan &load : transition.loads())
        if (load.slot().value() >= plan.liveSlots().size() ||
            llvm::any_of(load.replacements(), [&](const auto &value) {
              return !plannedReferencesClose(value);
            }))
          return reject(
              plans,
              "process-state plan invariant violated: dangling reference");
      for (size_t i = 1; i < transition.stores().size(); ++i)
        if (transition.stores()[i - 1].slot().value() >=
            transition.stores()[i].slot().value())
          return reject(plans, "process-state plan invariant violated: "
                               "unsorted canonical order");
      for (size_t i = 1; i < transition.loads().size(); ++i)
        if (transition.loads()[i - 1].slot().value() >=
            transition.loads()[i].slot().value())
          return reject(plans, "process-state plan invariant violated: "
                               "unsorted canonical order");
    }
    uint64_t expectedFairness = 0;
    for (const ProcessPcPlan &pc : plan.pcs()) {
      if (pc.blocks().empty() ||
          plan.blocks()[pc.blocks().front().value()].path() != pc.entryPath())
        return reject(
            plans, "process-state plan invariant violated: dangling reference");
      llvm::SmallVector<uint8_t> color(plan.blocks().size());
      llvm::SmallVector<uint64_t> memo(plan.blocks().size());
      bool invalidGraph = false;
      std::function<uint64_t(ProcessBlockId)> longest = [&](ProcessBlockId id) {
        size_t index = id.value();
        if (color[index] == 1) {
          invalidGraph = true;
          return uint64_t{0};
        }
        if (color[index] == 2)
          return memo[index];
        color[index] = 1;
        const ProcessBlockPlan &block = plan.blocks()[index];
        if (block.pc() != pc.id()) {
          invalidGraph = true;
          return uint64_t{0};
        }
        uint64_t suffix = 0;
        if (block.edge().kind() == ProcessControlEdgeKind::Branch) {
          suffix = std::max(longest(block.edge().trueBlock()),
                            longest(block.edge().falseBlock()));
        } else if (block.edge().kind() ==
                   ProcessControlEdgeKind::LocalContinue) {
          suffix = longest(block.edge().targetBlock());
        }
        if (block.cost() > std::numeric_limits<uint64_t>::max() - suffix) {
          invalidGraph = true;
          return uint64_t{0};
        }
        color[index] = 2;
        return memo[index] = block.cost() + suffix;
      };
      uint64_t pcWork = longest(pc.blocks().front());
      for (ProcessBlockId id : pc.blocks())
        if (color[id.value()] == 0)
          invalidGraph = true;
      if (invalidGraph)
        return reject(plans,
                      "process-state plan invariant violated: cost mismatch");
      expectedFairness = std::max(expectedFairness, pcWork);
    }
    if (plan.fairnessWork() != expectedFairness || expectedFairness == 0)
      return reject(plans,
                    "process-state plan invariant violated: cost mismatch");
    if (plan.fairnessWork() > limits.maxFairnessWork)
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
  llvm::StringRef previousSpecialization;
  for (auto [index, callee] : llvm::enumerate(plans.callees())) {
    if (callee.id().value() < index)
      return reject(plans,
                    "process-state plan invariant violated: duplicate ordinal");
    if (callee.id().value() > index)
      return reject(plans,
                    "process-state plan invariant violated: non-dense ordinal");
    if (!previousSpecialization.empty() &&
        previousSpecialization.compare(
            detail::generatedCalleeSpecializationBytes(callee)) >= 0)
      return reject(
          plans,
          previousSpecialization ==
                  detail::generatedCalleeSpecializationBytes(callee)
              ? "process-state plan invariant violated: duplicate identity"
              : "process-state plan invariant violated: unsorted canonical "
                "order");
    previousSpecialization = detail::generatedCalleeSpecializationBytes(callee);
    ProcessEffectKind expectedEffect =
        callee.role() <= ProcessHelperRole::TraceDecode ||
                callee.role() >= ProcessHelperRole::ScalarWrap
            ? ProcessEffectKind::Pure
            : ProcessEffectKind::Stateful;
    if (callee.effect() != expectedEffect)
      return reject(plans,
                    "process-state plan invariant violated: effect mismatch");
    if (callee.kind() != "implementation" || !validCalleeSemantics(callee) ||
        llvm::any_of(callee.inputTypeKeys(),
                     [](llvm::StringRef key) { return !validTypeKey(key); }) ||
        llvm::any_of(callee.resultTypeKeys(),
                     [](llvm::StringRef key) { return !validTypeKey(key); }) ||
        !llvm::is_sorted(callee.sourcePaths()) ||
        std::adjacent_find(callee.sourcePaths().begin(),
                           callee.sourcePaths().end()) !=
            callee.sourcePaths().end() ||
        callee.sourceOperations().size() != callee.sourcePaths().size())
      return reject(plans, "process-state plan invariant violated: callee "
                           "specialization mismatch");
    bool ownsDeclaration =
        callee.role() <= ProcessHelperRole::PacketDeserialize ||
        callee.role() == ProcessHelperRole::QueueTrySend ||
        callee.role() == ProcessHelperRole::QueueTryRecv ||
        callee.role() == ProcessHelperRole::EventSchedule ||
        callee.role() == ProcessHelperRole::Probe ||
        callee.role() == ProcessHelperRole::StatAdd;
    llvm::SmallPtrSet<mlir::Operation *, 8> uniqueDeclarations;
    if ((ownsDeclaration && callee.declarations().empty()) ||
        (!ownsDeclaration && !callee.declarations().empty()) ||
        llvm::any_of(callee.sourceOperations(),
                     [](mlir::Operation *op) { return op == nullptr; }) ||
        llvm::any_of(callee.declarations(), [&](mlir::Operation *op) {
          return op == nullptr || !uniqueDeclarations.insert(op).second;
        }))
      return reject(plans, "process-state plan invariant violated: callee "
                           "specialization mismatch");
    auto canonical = detail::canonicalGeneratedCalleeSpecialization(callee);
    if (!canonical) {
      llvm::consumeError(canonical.takeError());
      return reject(plans, "process-state plan invariant violated: callee "
                           "specialization mismatch");
    }
    std::string fingerprint = bindings::sha256Fingerprint(*canonical);
    llvm::StringRef digest = llvm::StringRef(fingerprint).drop_front(7);
    std::string expectedSymbol =
        ("@acir_impl_" + helperRoleSpelling(callee.role()) + "_" + digest)
            .str();
    std::string expectedCpp = ("acir::generated::impl_" +
                               helperRoleSpelling(callee.role()) + "_" + digest)
                                  .str();
    if (*canonical != detail::generatedCalleeSpecializationBytes(callee) ||
        fingerprint != callee.fingerprint() ||
        callee.symbol() != expectedSymbol || callee.cpp() != expectedCpp)
      return reject(plans, "process-state plan invariant violated: callee "
                           "specialization mismatch");
  }
  previousSpecialization = {};
  llvm::StringSet<> generatedTypeKeys;
  for (auto [index, type] : llvm::enumerate(plans.valueTypes())) {
    if (type.id().value() != index)
      return reject(plans,
                    "process-state plan invariant violated: non-dense ordinal");
    llvm::StringRef specialization =
        detail::PlanSetBuilder::specializationBytes(type);
    if (!previousSpecialization.empty() &&
        previousSpecialization.compare(specialization) >= 0)
      return reject(
          plans,
          previousSpecialization == specialization
              ? "process-state plan invariant violated: duplicate identity"
              : "process-state plan invariant violated: unsorted canonical "
                "order");
    previousSpecialization = specialization;
    auto canonical = detail::canonicalValueTypeSpecialization(type);
    if (!canonical) {
      llvm::consumeError(canonical.takeError());
      return reject(plans, "process-state plan invariant violated: value-type "
                           "specialization mismatch");
    }
    std::string fingerprint = bindings::sha256Fingerprint(*canonical);
    llvm::StringRef digest = llvm::StringRef(fingerprint).drop_front(7);
    llvm::StringRef stem =
        type.kind() == ProcessValueTypeKind::Value ? "value" : "packet";
    std::string expectedSymbol = ("@acir_" + stem + "_" + digest).str();
    std::string expectedCpp = ("acir::generated::" + stem + "_" + digest).str();
    if (type.kind() != type.payload().kind() || !type.acirType() ||
        *canonical != specialization || fingerprint != type.fingerprint() ||
        type.symbol() != expectedSymbol || type.cpp() != expectedCpp)
      return reject(
          plans,
          "process-state plan invariant violated: value-type specialization "
          "mismatch");
    generatedTypeKeys.insert(("storage:" + stem + ":" + digest).str());
    uint64_t width = type.kind() == ProcessValueTypeKind::Value
                         ? type.payload().value().widthBits()
                         : type.payload().packet().widthBits();
    auto members = type.kind() == ProcessValueTypeKind::Value
                       ? type.payload().value().members()
                       : type.payload().packet().members();
    if ((type.kind() == ProcessValueTypeKind::Packet &&
         (type.payload().packet().bytes() > UINT64_MAX / 8 ||
          type.payload().packet().bytes() * 8 != width)) ||
        llvm::any_of(members, [&](const ProcessValueTypeMemberPlan &member) {
          bool shape =
              member.kind() == ProcessValueTypeMemberKind::Field
                  ? !member.name().empty() && !member.index()
                  : member.name().empty() && member.index().has_value();
          return !shape || member.offsetBits() > width ||
                 member.widthBits() > width - member.offsetBits() ||
                 member.encoding().empty() || !validTypeKey(member.typeKey());
        }))
      return reject(plans, "process-state plan invariant violated: value-type "
                           "specialization mismatch");
  }
  auto referenceCloses = [&](llvm::StringRef key) {
    return !key.starts_with("storage:") || generatedTypeKeys.contains(key);
  };
  for (const ProcessGeneratedCalleePlan &callee : plans.callees())
    if (llvm::any_of(
            callee.inputTypeKeys(),
            [&](llvm::StringRef key) { return !referenceCloses(key); }) ||
        llvm::any_of(callee.resultTypeKeys(), [&](llvm::StringRef key) {
          return !referenceCloses(key);
        }))
      return reject(
          plans, "process-state plan invariant violated: dangling reference");
  for (const ProcessValueTypePlan &type : plans.valueTypes()) {
    auto members = type.kind() == ProcessValueTypeKind::Value
                       ? type.payload().value().members()
                       : type.payload().packet().members();
    if (llvm::any_of(members, [&](const ProcessValueTypeMemberPlan &member) {
          return !referenceCloses(member.typeKey());
        }))
      return reject(
          plans, "process-state plan invariant violated: dangling reference");
  }
  return mlir::success();
}

} // namespace acir
