#include "Analysis/ProcessStatePlanInternal.h"
#include "Analysis/ProcessStatePlanTestHooks.h"
#include "ProcessStatePlanTestSupport.h"
#include "acir/Analysis/ProcessStatePlan.h"

#include "gtest/gtest.h"

#include <concepts>
#include <type_traits>

namespace acir {
namespace {

template <typename T>
concept HasId = requires(const T &value) { value.id(); };
template <typename T>
concept HasOrdinal = requires(const T &value) { value.ordinal(); };
template <typename T>
concept HasSetter = requires(T &value) { value.setId(value.entryPc()); };
template <typename T>
concept HasMutableProcesses = requires(T &value) {
  {
    value.processes()
  } -> std::same_as<llvm::MutableArrayRef<ProcessStatePlan>>;
};
template <typename T>
concept HasComponentLookup =
    requires(const T &value) { value.lookupByComponentName("workload"); };
template <typename T>
concept HasHierarchyLookup =
    requires(const T &value) { value.lookupByHierarchy("Top.workload"); };
template <typename T>
concept HasFallbackLookup =
    requires(const T &value) { value.lookup("workload"); };

template <typename = void>
concept CompleteProcessStatePlanApi = requires(
    const ProcessCallSitePlan &callSite,
    const ProcessOriginalOccurrence &originalOccurrence,
    const ProcessSyntheticLoopOccurrence &loopOccurrence,
    const ProcessSyntheticWrapperOccurrence &wrapperOccurrence,
    const ProcessSyntheticConstantOccurrence &constantOccurrence,
    const ProcessOccurrenceId &occurrence,
    const ProcessValueCoordinate &coordinate,
    const ProcessOriginalPlannedValue &originalValue,
    const ProcessCapturePlannedValue &captureValue,
    const ProcessLiveSlotPlannedValue &slotValue,
    const ProcessSyntheticPlannedValue &syntheticValue,
    const ProcessConstantPlannedValue &constantValue,
    const ProcessPlannedValue &plannedValue,
    const ProcessScalarAttribute &scalarAttribute,
    const ProcessScalarOperationPlan &scalarOp,
    const ProcessCapturePlan &capture, const ProcessActionPlan &action,
    const ProcessLiveSlotPlan &slot,
    const ProcessSubscriptionSourcePlan &source, const ProcessWakePlan &wake,
    const ProcessTransitionStorePlan &store,
    const ProcessTransitionLoadPlan &load,
    const ProcessTransitionPlan &transition,
    const ProcessForwardingBindingPlan &binding,
    const ProcessControlFramePlan &frame, const ProcessControlEdgePlan &edge,
    const ProcessBlockPlan &block, const ProcessPcPlan &pc,
    const ProcessStatePlan &plan, const ProcessRecordFieldDescriptor &field,
    const ProcessRecordCreatePayload &recordCreate,
    const ProcessRecordGetPayload &recordGet,
    const ProcessRecordWithPayload &recordWith,
    const ProcessPacketSerializePayload &packetSerialize,
    const ProcessPacketDeserializePayload &packetDeserialize,
    const ProcessTraceDecodePayload &traceDecode,
    const ProcessQueueTrySendPayload &queueSend,
    const ProcessQueueTryRecvPayload &queueRecv,
    const ProcessEventSchedulePayload &eventSchedule,
    const ProcessTraceOpenPayload &traceOpen,
    const ProcessTraceNextPayload &traceNext,
    const ProcessTraceEofPayload &traceEof,
    const ProcessTracePositionPayload &tracePosition,
    const ProcessContractRequirePayload &requirePayload,
    const ProcessContractEnsurePayload &ensurePayload,
    const ProcessContractAssertPayload &assertPayload,
    const ProcessProbePayload &probe, const ProcessStatAddPayload &stat,
    const ProcessWakeConditionPayload &wakeCondition,
    const ProcessWakeResourcePayload &wakeResource,
    const ProcessWakeEventQueuePayload &wakeEvent,
    const ProcessWakeNextDeltaPayload &wakeNext,
    const ProcessScalarWrapPayload &wrap,
    const ProcessScalarUnwrapPayload &unwrap,
    const ProcessGeneratedCalleePayload &calleePayload,
    const ProcessValueTypeMemberPlan &member,
    const ProcessStorageValuePayload &storageValue,
    const ProcessStoragePacketPayload &storagePacket,
    const ProcessValueTypePayload &valuePayload,
    const ProcessGeneratedCalleePlan &callee,
    const ProcessValueTypePlan &valueType, const ProcessStatePlanSet &set) {
  callSite.operation();
  callSite.operationPath();
  callSite.iterationVector();
  originalOccurrence.operation();
  originalOccurrence.operationPath();
  originalOccurrence.callSites();
  originalOccurrence.iterationVector();
  loopOccurrence.anchor();
  loopOccurrence.phase();
  wrapperOccurrence.anchor();
  wrapperOccurrence.transition();
  wrapperOccurrence.slot();
  wrapperOccurrence.direction();
  constantOccurrence.anchor();
  constantOccurrence.constant();
  occurrence.kind();
  occurrence.original();
  occurrence.syntheticLoop();
  occurrence.syntheticWrapper();
  occurrence.syntheticConstant();
  coordinate.kind();
  coordinate.ownerPath();
  coordinate.index();
  originalValue.value();
  originalValue.occurrence();
  originalValue.coordinate();
  originalValue.path();
  captureValue.capture();
  slotValue.slot();
  syntheticValue.occurrence();
  syntheticValue.coordinate();
  constantValue.value();
  plannedValue.kind();
  plannedValue.type();
  plannedValue.original();
  plannedValue.capture();
  plannedValue.liveSlot();
  plannedValue.synthetic();
  plannedValue.constant();
  scalarAttribute.name();
  scalarAttribute.value();
  scalarOp.name();
  scalarOp.attributes();
  scalarOp.properties();
  capture.id();
  capture.name();
  capture.operand();
  capture.entryArgument();
  capture.type();
  capture.operandPath();
  capture.argumentPath();
  action.id();
  action.kind();
  action.emission();
  action.occurrence();
  action.sourceOperation();
  action.iterationVector();
  action.operands();
  action.results();
  action.cost();
  action.resultTypes();
  action.callee();
  action.scalarOp();
  slot.id();
  slot.name();
  slot.type();
  slot.storageType();
  slot.memberValues();
  slot.wrapCallee();
  slot.unwrapCallee();
  source.kind();
  source.value();
  source.owner();
  source.declaration();
  source.capture();
  source.symbol();
  source.path();
  source.ownerPath();
  wake.id();
  wake.kind();
  wake.operation();
  wake.triggeringValue();
  wake.declaration();
  wake.callee();
  wake.typeKey();
  wake.operationPath();
  wake.target();
  wake.occurrence();
  wake.iterationVector();
  wake.sources();
  store.slot();
  store.source();
  store.sourceValue();
  load.slot();
  load.replacements();
  transition.id();
  transition.sourcePc();
  transition.targetPc();
  transition.wake();
  transition.iterationVector();
  transition.stores();
  transition.loads();
  binding.from();
  binding.to();
  frame.kind();
  frame.phase();
  frame.operation();
  frame.operationPath();
  frame.bindings();
  edge.kind();
  edge.condition();
  edge.trueBlock();
  edge.falseBlock();
  edge.trueBindings();
  edge.falseBindings();
  edge.targetBlock();
  edge.bindings();
  edge.transition();
  edge.status();
  block.id();
  block.pc();
  block.originRegion();
  block.originBlock();
  block.path();
  block.frames();
  block.loads();
  block.actions();
  block.edge();
  block.cost();
  pc.id();
  pc.name();
  pc.entryPath();
  pc.blocks();
  plan.definitionKey();
  plan.process();
  plan.captures();
  plan.entryPc();
  plan.pcs();
  plan.blocks();
  plan.liveSlots();
  plan.wakes();
  plan.transitions();
  plan.pcBitWidth();
  plan.fairnessWork();
  field.name();
  field.typeKey();
  recordCreate.fields();
  recordCreate.recordType();
  recordGet.field();
  recordGet.record();
  recordGet.result();
  recordWith.field();
  recordWith.record();
  recordWith.value();
  packetSerialize.bytes();
  packetSerialize.packet();
  packetSerialize.packetType();
  packetDeserialize.bytes();
  packetDeserialize.packet();
  packetDeserialize.packetType();
  traceDecode.entry();
  traceDecode.result();
  traceDecode.source();
  queueSend.element();
  queueSend.queue();
  queueRecv.element();
  queueRecv.queue();
  eventSchedule.delay();
  eventSchedule.target();
  eventSchedule.value();
  traceOpen.source();
  traceNext.entry();
  traceNext.source();
  traceEof.source();
  tracePosition.source();
  requirePayload.message();
  ensurePayload.message();
  assertPayload.message();
  probe.kind();
  probe.result();
  probe.target();
  stat.stat();
  stat.valueType();
  wakeCondition.wakeKind();
  wakeCondition.wakeType();
  wakeResource.wakeKind();
  wakeResource.wakeType();
  wakeEvent.wakeKind();
  wakeEvent.wakeType();
  wakeNext.wakeKind();
  wakeNext.wakeType();
  wrap.direction();
  wrap.scalar();
  wrap.valueType();
  unwrap.direction();
  unwrap.scalar();
  unwrap.valueType();
  calleePayload.role();
  calleePayload.recordCreate();
  calleePayload.recordGet();
  calleePayload.recordWith();
  calleePayload.packetSerialize();
  calleePayload.packetDeserialize();
  calleePayload.traceDecode();
  calleePayload.queueTrySend();
  calleePayload.queueTryRecv();
  calleePayload.eventSchedule();
  calleePayload.traceOpen();
  calleePayload.traceNext();
  calleePayload.traceEof();
  calleePayload.tracePosition();
  calleePayload.contractRequire();
  calleePayload.contractEnsure();
  calleePayload.contractAssert();
  calleePayload.probe();
  calleePayload.statAdd();
  calleePayload.wakeCondition();
  calleePayload.wakeResource();
  calleePayload.wakeEventQueue();
  calleePayload.wakeNextDelta();
  calleePayload.scalarWrap();
  calleePayload.scalarUnwrap();
  member.kind();
  member.name();
  member.index();
  member.offsetBits();
  member.widthBits();
  member.signedness();
  member.encoding();
  member.typeKey();
  storageValue.members();
  storageValue.widthBits();
  storageValue.encoding();
  storagePacket.members();
  storagePacket.widthBits();
  storagePacket.bytes();
  storagePacket.encoding();
  valuePayload.kind();
  valuePayload.value();
  valuePayload.packet();
  callee.id();
  callee.symbol();
  callee.cpp();
  callee.kind();
  callee.fingerprint();
  callee.effect();
  callee.inputTypeKeys();
  callee.resultTypeKeys();
  callee.role();
  callee.payload();
  callee.sourceOperations();
  callee.declarations();
  callee.sourcePaths();
  valueType.id();
  valueType.symbol();
  valueType.cpp();
  valueType.kind();
  valueType.fingerprint();
  valueType.acirType();
  valueType.payload();
  set.processes();
  set.callees();
  set.valueTypes();
  set.lookupByDefinitionKey("@Top::@workload");
};

static_assert(HasId<ProcessGeneratedCalleePlan>);
static_assert(HasId<ProcessValueTypePlan>);
static_assert(HasId<ProcessCapturePlan>);
static_assert(HasId<ProcessPcPlan>);
static_assert(HasId<ProcessBlockPlan>);
static_assert(HasId<ProcessLiveSlotPlan>);
static_assert(HasId<ProcessWakePlan>);
static_assert(HasId<ProcessTransitionPlan>);
static_assert(CompleteProcessStatePlanApi<>);
static_assert(!HasOrdinal<ProcessGeneratedCalleePlan>);
static_assert(!HasSetter<ProcessStatePlan>);
static_assert(!HasMutableProcesses<ProcessStatePlanSet>);
static_assert(!HasComponentLookup<ProcessStatePlanSet>);
static_assert(!HasHierarchyLookup<ProcessStatePlanSet>);
static_assert(!HasFallbackLookup<ProcessStatePlanSet>);
static_assert(!std::default_initializable<ProcessCalleeId>);
static_assert(!std::constructible_from<ProcessCalleeId, uint32_t>);

constexpr llvm::StringLiteral kEmptyBytes =
    R"json({"callees":[],"contract_epoch":"0.1","processes":[],"schema":"acir-process-state-plan-0.1","value_types":[]})json";
constexpr llvm::StringLiteral kSpecialization =
    R"json({"contract_epoch":"0.1","effect":"stateful","inputs":[],"kind":"implementation","payload":{"wake_kind":"next_delta","wake_type":"@acir_wake_next_delta"},"results":["@acir_wake_next_delta"],"role":"wake_next_delta","schema":"acir-generated-implementation-0.1","source_paths":[]})json";
constexpr llvm::StringLiteral kDescriptor =
    R"json({"cpp":"acir::generated::impl_wake_next_delta_63cacba5c3eb82976464804b4aeaa17d43b445733efaddfad7c7bec1ab650269","effect":"stateful","fingerprint":"sha256:63cacba5c3eb82976464804b4aeaa17d43b445733efaddfad7c7bec1ab650269","inputs":[],"kind":"implementation","ordinal":0,"payload":{"wake_kind":"next_delta","wake_type":"@acir_wake_next_delta"},"results":["@acir_wake_next_delta"],"role":"wake_next_delta","source_paths":[],"symbol":"@acir_impl_wake_next_delta_63cacba5c3eb82976464804b4aeaa17d43b445733efaddfad7c7bec1ab650269"})json";

TEST(ProcessStatePlanApiTest, EmptyFrozenModelHasLiteralCanonicalBytes) {
  mlir::DialectRegistry registry;
  registerAllDialects(registry);
  mlir::MLIRContext context(registry);
  auto module = test::parseEmptyModel(context);
  ASSERT_TRUE(module);
  ProcessStatePlanSet plans = detail::PlanSetBuilder::buildEmpty(*module);
  ASSERT_TRUE(mlir::succeeded(verifyProcessStatePlan(plans)));
  auto bytes = serializeProcessStatePlan(plans);
  ASSERT_TRUE(static_cast<bool>(bytes)) << test::takeError(bytes.takeError());
  EXPECT_EQ(*bytes, kEmptyBytes);
}

TEST(ProcessStatePlanBasicTest, YieldOnlyBaselineIsExactAndImmutable) {
  mlir::DialectRegistry registry;
  registerAllDialects(registry);
  mlir::MLIRContext context(registry);
  auto module = test::parseAndFreezeYieldOnly(context);
  ASSERT_TRUE(module);
  ProcessStatePlanSet plans = detail::PlanSetBuilder::buildYieldOnly(*module);
  ASSERT_TRUE(mlir::succeeded(verifyProcessStatePlan(plans)));
  ASSERT_EQ(plans.processes().size(), 1U);
  ASSERT_EQ(plans.callees().size(), 1U);
  EXPECT_TRUE(plans.valueTypes().empty());

  const ProcessStatePlan *plan = plans.lookupByDefinitionKey("@Top::@workload");
  ASSERT_NE(plan, nullptr);
  EXPECT_EQ(plans.lookupByDefinitionKey("@workload"), nullptr);
  EXPECT_EQ(plans.lookupByDefinitionKey("workload"), nullptr);
  EXPECT_EQ(plans.lookupByDefinitionKey("Top.workload"), nullptr);
  EXPECT_EQ(plan->entryPc().value(), 0U);
  EXPECT_EQ(plan->pcBitWidth(), 1U);
  EXPECT_EQ(plan->pcs().size(), 1U);
  EXPECT_EQ(plan->pcs()[0].name(), "entry");
  EXPECT_EQ(plan->pcs()[0].id().value(), 0U);
  EXPECT_TRUE(plan->liveSlots().empty());
  ASSERT_EQ(plan->wakes().size(), 1U);
  EXPECT_EQ(plan->wakes()[0].kind(), ProcessWakeKind::NextDelta);
  EXPECT_EQ(plan->wakes()[0].typeKey(), "@acir_wake_next_delta");
  EXPECT_EQ(plan->wakes()[0].callee().value(), 0U);
  ASSERT_EQ(plan->transitions().size(), 1U);
  EXPECT_EQ(plan->transitions()[0].targetPc().value(), 0U);

  const auto &callee = plans.callees()[0];
  EXPECT_EQ(callee.id().value(), 0U);
  EXPECT_EQ(callee.kind(), "implementation");
  EXPECT_EQ(callee.effect(), ProcessEffectKind::Stateful);
  EXPECT_EQ(callee.role(), ProcessHelperRole::WakeNextDelta);
  EXPECT_EQ(callee.payload().wakeNextDelta().wakeKind(),
            ProcessWakeKind::NextDelta);
  EXPECT_EQ(callee.payload().wakeNextDelta().wakeType(),
            "@acir_wake_next_delta");
  EXPECT_EQ(detail::generatedCalleeSpecializationBytes(callee),
            kSpecialization);
  EXPECT_EQ(detail::generatedCalleeDescriptorBytes(callee), kDescriptor);
}

TEST(ProcessStatePlanBasicTest, FrozenDeclarationPermutationsAreByteIdentical) {
  mlir::DialectRegistry registry;
  registerAllDialects(registry);
  mlir::MLIRContext context(registry);
  auto first = test::parseAndFreezeYieldOnly(context, false);
  auto second = test::parseAndFreezeYieldOnly(context, true);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  auto firstPlan = detail::PlanSetBuilder::buildYieldOnly(*first);
  auto secondPlan = detail::PlanSetBuilder::buildYieldOnly(*second);
  auto firstBytes = serializeProcessStatePlan(firstPlan);
  auto secondBytes = serializeProcessStatePlan(secondPlan);
  ASSERT_TRUE(static_cast<bool>(firstBytes));
  ASSERT_TRUE(static_cast<bool>(secondBytes));
  EXPECT_EQ(*firstBytes, *secondBytes);
  EXPECT_EQ(firstPlan.processes()[0].definitionKey(),
            secondPlan.processes()[0].definitionKey());
  EXPECT_EQ(detail::generatedCalleeDescriptorBytes(firstPlan.callees()[0]),
            detail::generatedCalleeDescriptorBytes(secondPlan.callees()[0]));
  EXPECT_EQ(detail::canonicalDefinitionKeyOrderForTest(
                {"@Top::@zeta", "@Top::@alpha"}),
            (std::vector<std::string>{"@Top::@alpha", "@Top::@zeta"}));
}

TEST(ProcessStatePlanBasicTest, EveryFrozenSemanticCorruptionIsRejected) {
  mlir::DialectRegistry registry;
  registerAllDialects(registry);
  mlir::MLIRContext context(registry);
  auto module = test::parseAndFreezeYieldOnly(context);
  ASSERT_TRUE(module);
  auto plan = detail::PlanSetBuilder::buildYieldOnly(*module);
  mlir::ScopedDiagnosticHandler handler(
      &context, [](mlir::Diagnostic &) { return mlir::success(); });
  const ProcessStatePlanCorruptionForTest corruptions[] = {
      ProcessStatePlanCorruptionForTest::DuplicateOrdinal,
      ProcessStatePlanCorruptionForTest::NonDenseOrdinal,
      ProcessStatePlanCorruptionForTest::DanglingReference,
      ProcessStatePlanCorruptionForTest::DuplicateIdentity,
      ProcessStatePlanCorruptionForTest::UnsortedCanonicalOrder,
      ProcessStatePlanCorruptionForTest::CostMismatch,
      ProcessStatePlanCorruptionForTest::DefinitionKeyMismatch,
      ProcessStatePlanCorruptionForTest::CalleeSpecializationMismatch,
      ProcessStatePlanCorruptionForTest::ValueTypeSpecializationMismatch,
      ProcessStatePlanCorruptionForTest::EffectMismatch,
      ProcessStatePlanCorruptionForTest::IdKindMismatch,
      ProcessStatePlanCorruptionForTest::WrongTypeKey,
      ProcessStatePlanCorruptionForTest::InvalidFramePhase,
      ProcessStatePlanCorruptionForTest::InvalidEdgeBinding,
      ProcessStatePlanCorruptionForTest::InvalidWakeCallee,
  };
  for (auto corruption : corruptions) {
    SCOPED_TRACE(static_cast<int>(corruption));
    auto corrupted =
        cloneProcessStatePlanWithCorruptionForTest(plan, corruption);
    EXPECT_TRUE(mlir::failed(verifyProcessStatePlan(corrupted)));
    EXPECT_EQ(detail::processStatePlanCorruptionDiagnostic(corruption),
              detail::lastProcessStatePlanDiagnosticForTest());
  }
}

} // namespace
} // namespace acir
