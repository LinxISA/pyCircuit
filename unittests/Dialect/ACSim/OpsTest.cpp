#include "Dialect/ACSim/ACSimOpsTestHooks.h"
#include "acir/Dialect/ACSim/ACSimDialect.h"
#include "acir/Dialect/ACSim/ACSimOps.h"
#include "acir/Dialect/ACSim/ACSimTypes.h"

#include "mlir/AsmParser/AsmParser.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Index/IR/IndexDialect.h"
#include "mlir/IR/AttrTypeSubElements.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Parser/Parser.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

#include <array>
#include <string>
#include <vector>

namespace acir::acsim {
namespace {

void loadTestDialects(mlir::MLIRContext &context) {
  context
      .loadDialect<ACSimDialect, mlir::arith::ArithDialect,
                   mlir::cf::ControlFlowDialect, mlir::index::IndexDialect>();
}

void replaceDictionaryField(BindingOp binding, llvm::StringRef name,
                            mlir::Attribute value) {
  mlir::NamedAttrList fields(binding.getRecord());
  fields.set(name, value);
  binding.setRecordAttr(fields.getDictionary(binding.getContext()));
}

std::string scalableModel(unsigned extraTypes) {
  std::string source;
  llvm::raw_string_ostream os(source);
  os << R"mlir(
builtin.module attributes {ac.contract_epoch = "0.1"} {
  acsim.model @scale epoch "0.1" root @Top construction [] destruction []
      fingerprints {
        frozen_acir = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
        binding_lock = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
        provider = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
        profile = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
        toolchain = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
        schema_set = "sha256:0000000000000000000000000000000000000000000000000000000000000000"
      } {
    acsim.type @impl cpp "Impl" kind "implementation" fingerprint "sha256:0000000000000000000000000000000000000000000000000000000000000000"
    acsim.type @provider cpp "Provider" kind "provider" fingerprint "sha256:0000000000000000000000000000000000000000000000000000000000000000"
    acsim.type @schema cpp "schema" kind "schema" fingerprint "sha256:0000000000000000000000000000000000000000000000000000000000000000"
    acsim.type @value cpp "bool" kind "value" fingerprint "sha256:0000000000000000000000000000000000000000000000000000000000000000"
)mlir";
  for (unsigned index = 0; index != extraTypes; ++index)
    os << "    acsim.type @x" << llvm::format_hex_no_prefix(index, 8)
       << " cpp \"bool\" kind \"value\" fingerprint \"sha256:"
          "0000000000000000000000000000000000000000000000000000000000000000\""
          "\n";
  os << R"mlir(    acsim.binding @top record {
      activation_sources = [], availability = "available", binding = "top",
      binding_schema = "acsim-binding-0.1", component_schema = @schema,
      component_schema_fingerprint = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      construction = {arguments = [], kind = "constructor"}, contract_epoch = "0.1",
      cpp = {concept = "Model", entry_points = {pure = "", reset = "reset", validate = "validate", work = "work", xfer = "xfer"}, header = "model.hpp", symbol = "Model", target = "model"},
      cpp_type = @value, effect = "stateful", fingerprint = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      implementation = @impl, ownership = {kind = "unique", placement = "root"},
      parameters = [], ports = [], provider = @provider,
      provider_implementation_fingerprint = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      resources = [], results = []
    }
    acsim.module @Top binding @top path "Top" static [] specialization "sha256:0000000000000000000000000000000000000000000000000000000000000000" exports [] {
      acsim.return
    }
  }
}
)mlir";
  return source;
}

mlir::OwningOpRef<mlir::ModuleOp> parseValidModel(mlir::MLIRContext &context) {
  return mlir::parseSourceFile<mlir::ModuleOp>(ACSIM_VALID_TEST_FILE, &context);
}

std::string expectVerificationFailure(mlir::ModuleOp file) {
  std::string diagnostic;
  mlir::ScopedDiagnosticHandler handler(
      file.getContext(), [&](mlir::Diagnostic &value) {
        llvm::raw_string_ostream(diagnostic) << value;
        return mlir::success();
      });
  EXPECT_TRUE(mlir::failed(mlir::verify(file)));
  return diagnostic;
}

TEST(ACSimOpsTest, RegistryIsExactlyTheAuthoritativeTwentyTwoOperationTable) {
  mlir::MLIRContext context;
  context.loadDialect<ACSimDialect>();

  std::vector<std::string> actual;
  for (mlir::RegisteredOperationName operation :
       context.getRegisteredOperationsByDialect("acsim"))
    actual.push_back(operation.getStringRef().str());
  llvm::sort(actual);

  std::vector<std::string> expected = {
      "acsim.activate",   "acsim.array",    "acsim.bind",    "acsim.binding",
      "acsim.continue",   "acsim.dispatch", "acsim.element", "acsim.export",
      "acsim.inline",     "acsim.instance", "acsim.invoke",  "acsim.live.load",
      "acsim.live.store", "acsim.model",    "acsim.module",  "acsim.port",
      "acsim.process",    "acsim.resource", "acsim.return",  "acsim.suspend",
      "acsim.terminate",  "acsim.type",
  };
  llvm::sort(expected);
  EXPECT_EQ(actual, expected);
}

TEST(ACSimOpsTest, EveryPublicOperationHasItsTypedCppClass) {
  mlir::MLIRContext context;
  context.loadDialect<ACSimDialect>();

#define EXPECT_REGISTERED(OP)                                                  \
  EXPECT_TRUE(                                                                 \
      mlir::OperationName(OP::getOperationName(), &context).isRegistered())    \
      << OP::getOperationName().str()
  EXPECT_REGISTERED(ModelOp);
  EXPECT_REGISTERED(TypeOp);
  EXPECT_REGISTERED(BindingOp);
  EXPECT_REGISTERED(ModuleOp);
  EXPECT_REGISTERED(InstanceOp);
  EXPECT_REGISTERED(ArrayOp);
  EXPECT_REGISTERED(ElementOp);
  EXPECT_REGISTERED(PortOp);
  EXPECT_REGISTERED(ResourceOp);
  EXPECT_REGISTERED(BindOp);
  EXPECT_REGISTERED(InlineOp);
  EXPECT_REGISTERED(ProcessOp);
  EXPECT_REGISTERED(LiveLoadOp);
  EXPECT_REGISTERED(LiveStoreOp);
  EXPECT_REGISTERED(InvokeOp);
  EXPECT_REGISTERED(ContinueOp);
  EXPECT_REGISTERED(SuspendOp);
  EXPECT_REGISTERED(TerminateOp);
  EXPECT_REGISTERED(ExportOp);
  EXPECT_REGISTERED(DispatchOp);
  EXPECT_REGISTERED(ActivateOp);
  EXPECT_REGISTERED(ReturnOp);
#undef EXPECT_REGISTERED
}

TEST(ACSimOpsTest, IndexedWholeModelVerificationHasExactLinearWorkDelta) {
  mlir::MLIRContext context;
  loadTestDialects(context);

  auto measure = [&](unsigned extraTypes) {
    auto file = mlir::parseSourceString<mlir::ModuleOp>(
        scalableModel(extraTypes), &context);
    EXPECT_TRUE(file);
    detail::ModelVerificationWork work;
    if (file) {
      detail::ScopedModelVerificationWorkCollector collector(work);
      EXPECT_TRUE(mlir::succeeded(mlir::verify(*file)));
    }
    return work;
  };

  constexpr unsigned smallSize = 128;
  constexpr unsigned largeSize = 512;
  detail::ModelVerificationWork small = measure(smallSize);
  detail::ModelVerificationWork large = measure(largeSize);
  RecordProperty("small_declarations", smallSize);
  RecordProperty("large_declarations", largeSize);
  RecordProperty("small_work_units", small.total());
  RecordProperty("large_work_units", large.total());
  EXPECT_EQ(large.total() - small.total(),
            uint64_t{11} * (largeSize - smallSize));
}

TEST(ACSimOpsTest, CanonicalFixtureCoversInventoryEffectsAndPerPcRoundTrip) {
  mlir::MLIRContext context;
  loadTestDialects(context);
  auto file = parseValidModel(context);
  ASSERT_TRUE(file);
  ASSERT_TRUE(mlir::succeeded(mlir::verify(*file)));

  llvm::StringMap<unsigned> counts;
  file->walk([&](mlir::Operation *operation) {
    if (operation->getName().getDialectNamespace() == "acsim")
      ++counts[operation->getName().getStringRef()];
  });
  for (mlir::RegisteredOperationName operation :
       context.getRegisteredOperationsByDialect("acsim"))
    EXPECT_GT(counts.lookup(operation.getStringRef()), 0u)
        << operation.getStringRef().str();
  EXPECT_EQ(counts.lookup("acsim.model"), 1u);

  ProcessOp process;
  file->walk([&](ProcessOp candidate) { process = candidate; });
  ASSERT_TRUE(process);
  EXPECT_EQ(process.getPcs().size(), 3u);
  EXPECT_EQ(process.getStates().size(), process.getPcs().size());
  for (mlir::Region &state : process.getStates())
    EXPECT_FALSE(state.empty());

  const std::array<llvm::StringLiteral, 7> pureOperations = {
      "acsim.type",     "acsim.binding", "acsim.element", "acsim.port",
      "acsim.resource", "acsim.inline",  "acsim.export"};
  file->walk([&](mlir::Operation *operation) {
    if (llvm::is_contained(pureOperations, operation->getName().getStringRef()))
      EXPECT_TRUE(mlir::isMemoryEffectFree(operation))
          << operation->getName().getStringRef().str();
  });
  file->walk([&](mlir::Operation *operation) {
    if (mlir::isa<BindOp, DispatchOp, ActivateOp>(operation))
      EXPECT_FALSE(mlir::isMemoryEffectFree(operation))
          << operation->getName().getStringRef().str();
  });

  std::string printed;
  llvm::raw_string_ostream(printed) << *file;
  auto reparsed = mlir::parseSourceString<mlir::ModuleOp>(printed, &context);
  ASSERT_TRUE(reparsed);
  ASSERT_TRUE(mlir::succeeded(mlir::verify(*reparsed)));
  std::string reprinted;
  llvm::raw_string_ostream(reprinted) << *reparsed;
  EXPECT_EQ(reprinted, printed);
}

TEST(ACSimOpsTest, ClosedSchemaSourceMapActivationAndFairnessRegressions) {
  mlir::MLIRContext context;
  loadTestDialects(context);

  auto missingBindingField = parseValidModel(context);
  ASSERT_TRUE(missingBindingField);
  BindingOp binding;
  missingBindingField->walk([&](BindingOp candidate) {
    if (!binding)
      binding = candidate;
  });
  mlir::NamedAttrList fields(binding.getRecord());
  fields.erase("availability");
  binding.setRecordAttr(fields.getDictionary(&context));
  EXPECT_TRUE(llvm::StringRef(expectVerificationFailure(*missingBindingField))
                  .contains("binding lock must contain exactly"));

  auto sourceMapFile = parseValidModel(context);
  ASSERT_TRUE(sourceMapFile);
  ModelOp model;
  sourceMapFile->walk([&](ModelOp candidate) { model = candidate; });
  model->setAttr("acsim.source_map", mlir::StringAttr::get(&context, "bad"));
  EXPECT_TRUE(llvm::StringRef(expectVerificationFailure(*sourceMapFile))
                  .contains("acsim.source_map must be an array"));

  auto validSourceMapFile = parseValidModel(context);
  ASSERT_TRUE(validSourceMapFile);
  validSourceMapFile->walk([&](ModelOp candidate) { model = candidate; });
  mlir::NamedAttrList sourceRecord;
  sourceRecord.set("file", mlir::StringAttr::get(&context, "model.acir"));
  for (llvm::StringRef field : {"line", "column", "end_line", "end_column"})
    sourceRecord.set(
        field, mlir::IntegerAttr::get(mlir::IntegerType::get(&context, 64),
                                      field == "end_column" ? 2 : 1));
  model->setAttr(
      "acsim.source_map",
      mlir::ArrayAttr::get(&context, {sourceRecord.getDictionary(&context)}));
  EXPECT_TRUE(mlir::succeeded(mlir::verify(*validSourceMapFile)));

  auto unknownAttrFile = parseValidModel(context);
  ASSERT_TRUE(unknownAttrFile);
  unknownAttrFile->walk([&](ModelOp candidate) { model = candidate; });
  model->setAttr("acsim.unknown", mlir::UnitAttr::get(&context));
  EXPECT_TRUE(llvm::StringRef(expectVerificationFailure(*unknownAttrFile))
                  .contains("unknown public attribute 'acsim.unknown'"));

  auto fileAttrFile = parseValidModel(context);
  ASSERT_TRUE(fileAttrFile);
  (*fileAttrFile)->setAttr("ac.extra", mlir::UnitAttr::get(&context));
  std::string fileAttrDiagnostic;
  mlir::ScopedDiagnosticHandler fileAttrHandler(
      &context, [&](mlir::Diagnostic &value) {
        llvm::raw_string_ostream(fileAttrDiagnostic) << value;
        return mlir::success();
      });
  EXPECT_TRUE(mlir::failed(verifyCanonicalACSimFile(*fileAttrFile)));
  EXPECT_TRUE(llvm::StringRef(fileAttrDiagnostic)
                  .contains("canonical ACSim file attributes must be exactly"));

  auto activationFile = parseValidModel(context);
  ASSERT_TRUE(activationFile);
  ActivateOp lastActivation;
  activationFile->walk(
      [&](ActivateOp candidate) { lastActivation = candidate; });
  lastActivation.erase();
  EXPECT_TRUE(llvm::StringRef(expectVerificationFailure(*activationFile))
                  .contains("activation edges must exactly equal computed"));

  auto fairnessFile = parseValidModel(context);
  ASSERT_TRUE(fairnessFile);
  ProcessOp process;
  fairnessFile->walk([&](ProcessOp candidate) { process = candidate; });
  process.setFairnessCap(5);
  EXPECT_TRUE(llvm::StringRef(expectVerificationFailure(*fairnessFile))
                  .contains("below maximum local execution path 6"));
  process.setFairnessCap(6);
  EXPECT_TRUE(mlir::succeeded(mlir::verify(*fairnessFile)));

  auto captureFile = parseValidModel(context);
  ASSERT_TRUE(captureFile);
  captureFile->walk([&](ProcessOp candidate) { process = candidate; });
  process.setCaptureNamesAttr(mlir::ArrayAttr::get(&context, {}));
  EXPECT_TRUE(llvm::StringRef(expectVerificationFailure(*captureFile))
                  .contains("one exact ordered name per operand"));

  auto typeClosureFile = parseValidModel(context);
  ASSERT_TRUE(typeClosureFile);
  typeClosureFile->walk([&](ProcessOp candidate) { process = candidate; });
  process.getStates().front().front().getArgument(0).setType(
      mlir::MemRefType::get({1}, mlir::IntegerType::get(&context, 8)));
  EXPECT_TRUE(llvm::StringRef(expectVerificationFailure(*typeClosureFile))
                  .contains("is not legal in canonical ACSim"));

  auto specializationFile = parseValidModel(context);
  ASSERT_TRUE(specializationFile);
  ArrayOp lanes;
  specializationFile->walk([&](ArrayOp candidate) { lanes = candidate; });
  lanes.setSpecializationFingerprint(
      "sha256:"
      "2400000000000000000000000000000000000000000000000000000000000000");
  EXPECT_TRUE(
      llvm::StringRef(expectVerificationFailure(*specializationFile))
          .contains("identical target and static arguments require one"));
}

TEST(ACSimOpsTest, SemanticMutationsProduceDeterministicDiagnostics) {
  mlir::MLIRContext context;
  loadTestDialects(context);

  auto destructionFile = parseValidModel(context);
  ASSERT_TRUE(destructionFile);
  ModelOp destructionModel = *destructionFile->getOps<ModelOp>().begin();
  destructionModel.setDestructionOrderAttr(
      destructionModel.getConstructionOrderAttr());
  EXPECT_TRUE(llvm::StringRef(expectVerificationFailure(*destructionFile))
                  .contains("destruction order must be the exact reverse"));

  auto exportFile = parseValidModel(context);
  ASSERT_TRUE(exportFile);
  ModuleOp module;
  exportFile->walk([&](ModuleOp candidate) { module = candidate; });
  module.setExportsAttr(mlir::ArrayAttr::get(&context, {}));
  EXPECT_TRUE(llvm::StringRef(expectVerificationFailure(*exportFile))
                  .contains("exports metadata must exactly cover"));

  auto wakeFile = parseValidModel(context);
  ASSERT_TRUE(wakeFile);
  SuspendOp suspend;
  wakeFile->walk([&](SuspendOp candidate) { suspend = candidate; });
  ASSERT_TRUE(suspend);
  suspend.getWake().setType(ValueType::get(
      &context, mlir::FlatSymbolRefAttr::get(&context, "cpp_bool")));
  std::string wakeDiagnostic = expectVerificationFailure(*wakeFile);
  EXPECT_TRUE(
      llvm::StringRef(wakeDiagnostic).contains("requires one exact typed wake"))
      << wakeDiagnostic;

  auto castFile = parseValidModel(context);
  ASSERT_TRUE(castFile);
  TerminateOp terminate;
  castFile->walk([&](TerminateOp candidate) { terminate = candidate; });
  ASSERT_TRUE(terminate);
  mlir::OpBuilder builder(terminate);
  auto constant = mlir::arith::ConstantOp::create(builder, terminate.getLoc(),
                                                  builder.getI32IntegerAttr(0));
  mlir::UnrealizedConversionCastOp::create(
      builder, terminate.getLoc(), mlir::TypeRange{builder.getI64Type()},
      mlir::ValueRange{constant});
  std::string castDiagnostic = expectVerificationFailure(*castFile);
  EXPECT_TRUE(llvm::StringRef(castDiagnostic)
                  .contains("conversion placeholders are not legal"))
      << castDiagnostic;

  auto processShapeFile = parseValidModel(context);
  ASSERT_TRUE(processShapeFile);
  ProcessOp malformedProcess;
  processShapeFile->walk(
      [&](ProcessOp candidate) { malformedProcess = candidate; });
  malformedProcess.setPcsAttr(mlir::ArrayAttr::get(
      &context, {mlir::FlatSymbolRefAttr::get(&context, "entry")}));
  std::string processDiagnostic = expectVerificationFailure(*processShapeFile);
  EXPECT_TRUE(llvm::StringRef(processDiagnostic)
                  .contains("exactly one ordered state region per PC"))
      << processDiagnostic;

  auto thunkFile = parseValidModel(context);
  ASSERT_TRUE(thunkFile);
  DispatchOp dispatch;
  thunkFile->walk([&](DispatchOp candidate) {
    if (!dispatch)
      dispatch = candidate;
  });
  dispatch.setWorkAttr(mlir::StringAttr::get(&context, "work();"));
  std::string thunkDiagnostic = expectVerificationFailure(*thunkFile);
  EXPECT_TRUE(llvm::StringRef(thunkDiagnostic)
                  .contains("dispatch thunks must exactly match"))
      << thunkDiagnostic;

  auto objectOrderFile = parseValidModel(context);
  ASSERT_TRUE(objectOrderFile);
  ArrayOp array;
  objectOrderFile->walk([&](ArrayOp candidate) { array = candidate; });
  array.setObjectIdsAttr(mlir::DenseI64ArrayAttr::get(&context, {2, 1}));
  std::string objectOrderDiagnostic =
      expectVerificationFailure(*objectOrderFile);
  EXPECT_TRUE(
      llvm::StringRef(objectOrderDiagnostic)
          .contains(
              "runtime object IDs must equal canonical ownership preorder"))
      << objectOrderDiagnostic;

  auto disconnectedFile = parseValidModel(context);
  ASSERT_TRUE(disconnectedFile);
  ModuleOp top;
  InstanceOp fifo;
  disconnectedFile->walk([&](ModuleOp candidate) { top = candidate; });
  disconnectedFile->walk([&](InstanceOp candidate) { fifo = candidate; });
  mlir::OpBuilder hierarchyBuilder(top);
  auto other = ModuleOp::create(
      hierarchyBuilder, top.getLoc(), "Other", "top", "Other",
      mlir::ArrayAttr::get(&context, {}),
      "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      mlir::ArrayAttr::get(&context, {}));
  mlir::Block *otherBody = &other.getBody().emplaceBlock();
  hierarchyBuilder.setInsertionPointToEnd(otherBody);
  ReturnOp::create(hierarchyBuilder, other.getLoc(), mlir::ValueRange{});
  fifo.setOwnerAttr(mlir::FlatSymbolRefAttr::get(&context, "Other"));
  fifo.setPathAttr(mlir::StringAttr::get(&context, "Other.fifo"));
  std::string disconnectedDiagnostic =
      expectVerificationFailure(*disconnectedFile);
  EXPECT_TRUE(llvm::StringRef(disconnectedDiagnostic)
                  .contains("ownership chain must descend from the model root"))
      << disconnectedDiagnostic;

  auto bindingKindFile = parseValidModel(context);
  ASSERT_TRUE(bindingKindFile);
  BindingOp binding;
  bindingKindFile->walk([&](BindingOp candidate) {
    if (!binding)
      binding = candidate;
  });
  replaceDictionaryField(binding, "component_schema",
                         mlir::FlatSymbolRefAttr::get(&context, "gfsim"));
  std::string bindingKindDiagnostic =
      expectVerificationFailure(*bindingKindFile);
  EXPECT_TRUE(llvm::StringRef(bindingKindDiagnostic)
                  .contains("schema reference '@gfsim' has incompatible"))
      << bindingKindDiagnostic;

  auto captureBoundaryFile = parseValidModel(context);
  ASSERT_TRUE(captureBoundaryFile);
  ModelOp captureModel;
  ProcessOp capturedProcess;
  captureBoundaryFile->walk(
      [&](ModelOp candidate) { captureModel = candidate; });
  captureBoundaryFile->walk(
      [&](ProcessOp candidate) { capturedProcess = candidate; });
  mlir::ArrayAttr originalConstruction = captureModel.getConstructionOrder();
  captureModel.setConstructionOrderAttr(mlir::ArrayAttr::get(
      &context, {originalConstruction[0], originalConstruction[2],
                 originalConstruction[1]}));
  captureModel.setDestructionOrderAttr(mlir::ArrayAttr::get(
      &context, {originalConstruction[1], originalConstruction[2],
                 originalConstruction[0]}));
  capturedProcess.setOwnerAttr(mlir::FlatSymbolRefAttr::get(&context, "fifo"));
  capturedProcess.setPathAttr(mlir::StringAttr::get(&context, "Top.fifo.tick"));
  std::string captureBoundaryDiagnostic =
      expectVerificationFailure(*captureBoundaryFile);
  EXPECT_TRUE(
      llvm::StringRef(captureBoundaryDiagnostic)
          .contains("must remain within the process ownership boundary"))
      << captureBoundaryDiagnostic;

  auto invokeEffectFile = parseValidModel(context);
  ASSERT_TRUE(invokeEffectFile);
  InvokeOp invoke;
  invokeEffectFile->walk([&](InvokeOp candidate) {
    if (!invoke)
      invoke = candidate;
  });
  invoke.setBindingAttr(mlir::FlatSymbolRefAttr::get(&context, "pure"));
  std::string invokeEffectDiagnostic =
      expectVerificationFailure(*invokeEffectFile);
  EXPECT_TRUE(llvm::StringRef(invokeEffectDiagnostic)
                  .contains("invoke requires a stateful binding"))
      << invokeEffectDiagnostic;
}

TEST(ACSimOpsTest, CapabilityPreflightUsesExactPrivateLimits) {
  mlir::MLIRContext context;
  loadTestDialects(context);
  auto checkLimit = [&](detail::ModelVerificationLimits limits,
                        llvm::StringRef expected) {
    auto file = parseValidModel(context);
    ASSERT_TRUE(file);
    detail::ScopedModelVerificationLimits scopedLimits(limits);
    std::string diagnostic = expectVerificationFailure(*file);
    EXPECT_TRUE(llvm::StringRef(diagnostic).contains(expected)) << diagnostic;
  };

  detail::ModelVerificationLimits nodeLimits;
  nodeLimits.maxNodes = 8;
  checkLimit(nodeLimits, "model node count exceeds ACSim v0.1 capability 8");

  detail::ModelVerificationLimits depthLimits;
  depthLimits.maxRegionDepth = 1;
  checkLimit(depthLimits, "region nesting exceeds ACSim v0.1 capability 1");

  detail::ModelVerificationLimits expansionLimits;
  expansionLimits.maxExpandedObjects = 1;
  checkLimit(expansionLimits,
             "expanded array volume exceeds ACSim v0.1 capability 1");

  detail::ModelVerificationLimits edgeLimits;
  edgeLimits.maxEdges = 0;
  checkLimit(edgeLimits, "model edge count exceeds ACSim v0.1 capability 0");

  detail::ModelVerificationLimits attributeLimits;
  attributeLimits.maxAttributeElements = 8;
  checkLimit(attributeLimits,
             "attribute element count exceeds ACSim v0.1 capability");

  detail::ModelVerificationLimits stringLimits;
  stringLimits.maxAttributeStringBytes = 8;
  checkLimit(stringLimits,
             "attribute string bytes exceed ACSim v0.1 capability");
}

} // namespace
} // namespace acir::acsim
