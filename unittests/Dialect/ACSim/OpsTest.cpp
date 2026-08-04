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
#include <functional>
#include <string>
#include <vector>

namespace acir::acsim {
namespace {

void loadTestDialects(mlir::MLIRContext &context) {
  context
      .loadDialect<ACSimDialect, mlir::arith::ArithDialect,
                   mlir::cf::ControlFlowDialect, mlir::index::IndexDialect>();
}

template <typename Op> Op firstOp(mlir::ModuleOp file) {
  Op result;
  file.walk([&](Op candidate) {
    if (!result)
      result = candidate;
  });
  return result;
}

void replaceDictionaryField(BindingOp binding, llvm::StringRef name,
                            mlir::Attribute value) {
  mlir::NamedAttrList fields(binding.getRecord());
  fields.set(name, value);
  binding.setRecordAttr(fields.getDictionary(binding.getContext()));
}

void replaceNestedDictionaryField(BindingOp binding, llvm::StringRef record,
                                  llvm::StringRef name, mlir::Attribute value) {
  mlir::NamedAttrList fields(binding.getRecord());
  mlir::NamedAttrList nested(
      mlir::dyn_cast<mlir::DictionaryAttr>(fields.get(record)));
  nested.set(name, value);
  fields.set(record, nested.getDictionary(binding.getContext()));
  binding.setRecordAttr(fields.getDictionary(binding.getContext()));
}

void replaceRecordArrayField(BindingOp binding, llvm::StringRef records,
                             unsigned ordinal, llvm::StringRef name,
                             mlir::Attribute value) {
  mlir::NamedAttrList fields(binding.getRecord());
  auto array = mlir::cast<mlir::ArrayAttr>(fields.get(records));
  llvm::SmallVector<mlir::Attribute> elements(array.begin(), array.end());
  mlir::NamedAttrList nested(
      mlir::cast<mlir::DictionaryAttr>(elements[ordinal]));
  nested.set(name, value);
  elements[ordinal] = nested.getDictionary(binding.getContext());
  fields.set(records, mlir::ArrayAttr::get(binding.getContext(), elements));
  binding.setRecordAttr(fields.getDictionary(binding.getContext()));
}

template <typename Callable>
std::string expectDirectVerificationFailure(mlir::MLIRContext &context,
                                            Callable &&verify) {
  std::string diagnostic;
  mlir::ScopedDiagnosticHandler handler(&context, [&](mlir::Diagnostic &value) {
    llvm::raw_string_ostream(diagnostic) << value;
    return mlir::success();
  });
  EXPECT_TRUE(mlir::failed(verify()));
  return diagnostic;
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
    acsim.module @Top binding @top static [] specialization "sha256:0000000000000000000000000000000000000000000000000000000000000000" exports [] {
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

mlir::OwningOpRef<mlir::ModuleOp>
parseReusableModel(mlir::MLIRContext &context) {
  return mlir::parseSourceFile<mlir::ModuleOp>(ACSIM_REUSABLE_TEST_FILE,
                                               &context);
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
            uint64_t{9} * (largeSize - smallSize));
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
  const std::array<std::pair<llvm::StringLiteral, unsigned>, 22> exact = {{
      {"acsim.model", 1},     {"acsim.type", 21},      {"acsim.binding", 3},
      {"acsim.module", 1},    {"acsim.instance", 1},   {"acsim.array", 1},
      {"acsim.element", 2},   {"acsim.port", 2},       {"acsim.resource", 2},
      {"acsim.bind", 3},      {"acsim.inline", 2},     {"acsim.process", 1},
      {"acsim.live.load", 1}, {"acsim.live.store", 1}, {"acsim.invoke", 2},
      {"acsim.continue", 1},  {"acsim.suspend", 1},    {"acsim.terminate", 1},
      {"acsim.export", 1},    {"acsim.dispatch", 4},   {"acsim.activate", 6},
      {"acsim.return", 1},
  }};
  for (auto [name, expected] : exact)
    EXPECT_EQ(counts.lookup(name), expected) << name.str();

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
  DispatchOp wrongId;
  objectOrderFile->walk([&](DispatchOp candidate) {
    if (candidate.getObjectId() == 0)
      wrongId = candidate;
  });
  wrongId.setObjectId(2);
  std::string objectOrderDiagnostic =
      expectVerificationFailure(*objectOrderFile);
  EXPECT_TRUE(
      llvm::StringRef(objectOrderDiagnostic)
          .contains("target, path, indices, and IDs must jointly match"))
      << objectOrderDiagnostic;

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

  auto implementationFingerprintFile = parseValidModel(context);
  ASSERT_TRUE(implementationFingerprintFile);
  implementationFingerprintFile->walk([&](BindingOp candidate) {
    if (candidate.getSymName() == "pure")
      binding = candidate;
  });
  replaceDictionaryField(
      binding, "provider_implementation_fingerprint",
      mlir::StringAttr::get(
          &context,
          "sha256:"
          "3000000000000000000000000000000000000000000000000000000000000000"));
  std::string implementationFingerprintDiagnostic =
      expectVerificationFailure(*implementationFingerprintFile);
  EXPECT_TRUE(llvm::StringRef(implementationFingerprintDiagnostic)
                  .contains("implementation fingerprint must exactly match"))
      << implementationFingerprintDiagnostic;

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

TEST(ACSimOpsTest, EveryOperationHasATableDrivenBehavioralNegative) {
  mlir::MLIRContext context;
  loadTestDialects(context);
  struct OperationCase {
    llvm::StringLiteral operation;
    std::function<void(mlir::ModuleOp)> mutate;
    llvm::StringLiteral expected;
  };
  const std::array<OperationCase, 22> cases = {{
      {"model",
       [&](mlir::ModuleOp file) {
         ModelOp op = firstOp<ModelOp>(file);
         op.setDestructionOrderAttr(op.getConstructionOrderAttr());
       },
       "destruction order must be the exact reverse"},
      {"type",
       [&](mlir::ModuleOp file) {
         firstOp<TypeOp>(file).setKind("runtime_descriptor");
       },
       "runtime_descriptor"},
      {"binding",
       [&](mlir::ModuleOp file) {
         BindingOp op = firstOp<BindingOp>(file);
         mlir::NamedAttrList fields(op.getRecord());
         fields.erase("availability");
         op.setRecordAttr(fields.getDictionary(&context));
       },
       "binding lock must contain exactly"},
      {"module",
       [&](mlir::ModuleOp file) {
         firstOp<ModuleOp>(file).setExportsAttr(
             mlir::ArrayAttr::get(&context, {}));
       },
       "exports metadata must exactly cover"},
      {"instance",
       [&](mlir::ModuleOp file) {
         firstOp<InstanceOp>(file).getResult().setType(OwnerType::get(
             &context, mlir::FlatSymbolRefAttr::get(&context, "pure")));
       },
       "owner/ref type requires a stateful binding"},
      {"array",
       [&](mlir::ModuleOp file) {
         ArrayOp op = firstOp<ArrayOp>(file);
         op.getResult().setType(ArrayType::get(
             &context, mlir::DenseI64ArrayAttr::get(&context, {3}),
             OwnerType::get(&context, mlir::FlatSymbolRefAttr::get(
                                          &context, "stateful"))));
       },
       "array result shape and owning element binding must be exact"},
      {"element",
       [&](mlir::ModuleOp file) {
         firstOp<ElementOp>(file).setIndicesAttr(
             mlir::DenseI64ArrayAttr::get(&context, {2}));
       },
       "element index is out of static bounds"},
      {"port",
       [&](mlir::ModuleOp file) {
         PortOp op = firstOp<PortOp>(file);
         PortType type = mlir::cast<PortType>(op.getResult().getType());
         op.getResult().setType(
             PortType::get(&context, type.getInterface(),
                           mlir::FlatSymbolRefAttr::get(&context, "initiator"),
                           type.getPayload(), type.getProtocol()));
       },
       "port projection must exactly match"},
      {"resource",
       [&](mlir::ModuleOp file) {
         ResourceOp op = firstOp<ResourceOp>(file);
         ResourceType type = mlir::cast<ResourceType>(op.getResult().getType());
         op.getResult().setType(ResourceType::get(
             &context, type.getResource(),
             mlir::FlatSymbolRefAttr::get(&context, "consumer")));
       },
       "resource projection must exactly match"},
      {"bind",
       [&](mlir::ModuleOp file) { firstOp<BindOp>(file).setKind("resource"); },
       "resource binding must connect exact initiator and target"},
      {"inline",
       [&](mlir::ModuleOp file) {
         firstOp<InlineOp>(file).setBinding("stateful");
       },
       "inline requires a pure binding"},
      {"process",
       [&](mlir::ModuleOp file) {
         firstOp<ProcessOp>(file).setCaptureNamesAttr(
             mlir::ArrayAttr::get(&context, {}));
       },
       "one exact ordered name per operand"},
      {"live.load",
       [&](mlir::ModuleOp file) {
         firstOp<LiveLoadOp>(file).setSlot("missing");
       },
       "live load must resolve to an exact typed slot"},
      {"live.store",
       [&](mlir::ModuleOp file) {
         firstOp<LiveStoreOp>(file).setSlot("missing");
       },
       "live store must resolve to an exact typed slot"},
      {"invoke",
       [&](mlir::ModuleOp file) { firstOp<InvokeOp>(file).setBinding("pure"); },
       "invoke requires a stateful binding"},
      {"continue",
       [&](mlir::ModuleOp file) {
         firstOp<ContinueOp>(file).setTargetPc("missing");
       },
       "target PC '@missing' is not in the closed PC list"},
      {"suspend",
       [&](mlir::ModuleOp file) {
         firstOp<SuspendOp>(file).getWake().setType(ValueType::get(
             &context, mlir::FlatSymbolRefAttr::get(&context, "cpp_bool")));
       },
       "suspend requires one exact typed wake"},
      {"terminate",
       [&](mlir::ModuleOp file) {
         firstOp<TerminateOp>(file).setStatus("cancelled");
       },
       "terminal status must be exactly"},
      {"export",
       [&](mlir::ModuleOp file) {
         firstOp<ExportOp>(file).setRole("event_kind");
       },
       "export role reference '@event_kind' has incompatible"},
      {"dispatch",
       [&](mlir::ModuleOp file) {
         firstOp<DispatchOp>(file).setPath("Top.alias");
       },
       "target, path, indices, and IDs must jointly match"},
      {"activate",
       [&](mlir::ModuleOp file) {
         ActivateOp op;
         file.walk([&](ActivateOp candidate) { op = candidate; });
         op.erase();
       },
       "activation edges must exactly equal computed"},
      {"return",
       [&](mlir::ModuleOp file) { firstOp<ReturnOp>(file)->setOperands({}); },
       "return values must exactly match ordered module exports"},
  }};

  for (const OperationCase &testCase : cases) {
    SCOPED_TRACE(testCase.operation.str());
    auto file = parseValidModel(context);
    ASSERT_TRUE(file);
    testCase.mutate(*file);
    std::string diagnostic = expectVerificationFailure(*file);
    EXPECT_TRUE(llvm::StringRef(diagnostic).contains(testCase.expected))
        << diagnostic;
  }
}

TEST(ACSimOpsTest, EveryTypeHasATableDrivenSemanticNegative) {
  mlir::MLIRContext context;
  loadTestDialects(context);
  struct TypeCase {
    llvm::StringLiteral type;
    std::function<void(mlir::ModuleOp)> mutate;
    llvm::StringLiteral expected;
    bool directExport = false;
  };
  const std::array<TypeCase, 11> cases = {{
      {"value",
       [&](mlir::ModuleOp file) {
         firstOp<LiveLoadOp>(file).getResult().setType(ValueType::get(
             &context, mlir::FlatSymbolRefAttr::get(&context, "event_kind")));
       },
       "live load must resolve to an exact typed slot"},
      {"expr",
       [&](mlir::ModuleOp file) {
         firstOp<InlineOp>(file).getResult().setType(ExprType::get(
             &context, mlir::FlatSymbolRefAttr::get(&context, "event_kind")));
       },
       "C++ type reference '@event_kind' has incompatible"},
      {"owner",
       [&](mlir::ModuleOp file) {
         firstOp<InstanceOp>(file).getResult().setType(OwnerType::get(
             &context, mlir::FlatSymbolRefAttr::get(&context, "pure")));
       },
       "owner/ref type requires a stateful binding"},
      {"ref",
       [&](mlir::ModuleOp file) {
         firstOp<ElementOp>(file).getResult().setType(RefType::get(
             &context, mlir::FlatSymbolRefAttr::get(&context, "pure")));
       },
       "owner/ref type requires a stateful binding"},
      {"port",
       [&](mlir::ModuleOp file) {
         PortOp op = firstOp<PortOp>(file);
         PortType type = mlir::cast<PortType>(op.getResult().getType());
         op.getResult().setType(
             PortType::get(&context, type.getInterface(),
                           mlir::FlatSymbolRefAttr::get(&context, "initiator"),
                           type.getPayload(), type.getProtocol()));
       },
       "port projection must exactly match"},
      {"resource",
       [&](mlir::ModuleOp file) {
         ResourceOp op = firstOp<ResourceOp>(file);
         ResourceType type = mlir::cast<ResourceType>(op.getResult().getType());
         op.getResult().setType(ResourceType::get(
             &context, type.getResource(),
             mlir::FlatSymbolRefAttr::get(&context, "consumer")));
       },
       "resource projection must exactly match"},
      {"array",
       [&](mlir::ModuleOp file) {
         ArrayOp op = firstOp<ArrayOp>(file);
         op.getResult().setType(ArrayType::get(
             &context, mlir::DenseI64ArrayAttr::get(&context, {3}),
             OwnerType::get(&context, mlir::FlatSymbolRefAttr::get(
                                          &context, "stateful"))));
       },
       "array result shape and owning element binding must be exact"},
      {"object_id",
       [&](mlir::ModuleOp file) {
         ActivateOp op = firstOp<ActivateOp>(file);
         llvm::SmallVector<DispatchOp> dispatches;
         file.walk(
             [&](DispatchOp candidate) { dispatches.push_back(candidate); });
         op->setOperand(1, dispatches[1].getObject());
       },
       "activation edges must exactly equal computed"},
      {"activation_id",
       [&](mlir::ModuleOp file) {
         ActivateOp op = firstOp<ActivateOp>(file);
         llvm::SmallVector<DispatchOp> dispatches;
         file.walk(
             [&](DispatchOp candidate) { dispatches.push_back(candidate); });
         op->setOperand(0, dispatches[1].getActivation());
       },
       "activation edges must exactly equal computed"},
      {"pc",
       [&](mlir::ModuleOp file) {
         ExportOp op = firstOp<ExportOp>(file);
         auto type = PcType::get(
             &context, mlir::FlatSymbolRefAttr::get(&context, "tick"));
         op.getValue().setType(type);
         op.getResult().setType(type);
       },
       "owners, IDs, PCs, and wakes cannot escape", true},
      {"wake",
       [&](mlir::ModuleOp file) {
         firstOp<SuspendOp>(file).getWake().setType(WakeType::get(
             &context, mlir::FlatSymbolRefAttr::get(&context, "cpp_bool")));
       },
       "wake kind reference '@cpp_bool' has incompatible"},
  }};

  for (const TypeCase &testCase : cases) {
    SCOPED_TRACE(testCase.type.str());
    auto file = parseValidModel(context);
    ASSERT_TRUE(file);
    testCase.mutate(*file);
    std::string diagnostic;
    if (testCase.directExport) {
      ExportOp exportOp = firstOp<ExportOp>(*file);
      diagnostic = expectDirectVerificationFailure(
          context, [&] { return exportOp.verify(); });
    } else {
      diagnostic = expectVerificationFailure(*file);
    }
    EXPECT_TRUE(llvm::StringRef(diagnostic).contains(testCase.expected))
        << diagnostic;
  }
}

TEST(ACSimOpsTest, BindingLockNestedDomainsAreClosedAndDeclarative) {
  mlir::MLIRContext context;
  loadTestDialects(context);

  struct MutationCase {
    llvm::StringLiteral name;
    std::function<void(BindingOp)> mutate;
    llvm::StringLiteral expected;
  };
  const std::array<MutationCase, 11> cases = {{
      {"cpp-header",
       [&](BindingOp binding) {
         replaceNestedDictionaryField(
             binding, "cpp", "header",
             mlir::StringAttr::get(&context, "../escape.hpp"));
       },
       "cpp.header must be a repository-relative header path"},
      {"cpp-symbol",
       [&](BindingOp binding) {
         replaceNestedDictionaryField(
             binding, "cpp", "symbol",
             mlir::StringAttr::get(&context, "gfsim::make()"));
       },
       "must be declarative qualified names"},
      {"construction-kind",
       [&](BindingOp binding) {
         replaceNestedDictionaryField(binding, "construction", "kind",
                                      mlir::StringAttr::get(&context, ""));
       },
       "construction kind must be non-empty"},
      {"construction-raw-argument",
       [&](BindingOp binding) {
         replaceNestedDictionaryField(
             binding, "construction", "arguments",
             mlir::ArrayAttr::get(
                 &context, {mlir::StringAttr::get(&context, "make();")}));
       },
       "construction arguments must be canonical static data"},
      {"effect-ownership",
       [&](BindingOp binding) {
         replaceNestedDictionaryField(
             binding, "ownership", "kind",
             mlir::StringAttr::get(&context, "unique"));
       },
       "binding ownership is inconsistent with effect"},
      {"cardinality",
       [&](BindingOp binding) {
         replaceRecordArrayField(binding, "ports", 0, "cardinality",
                                 mlir::StringAttr::get(&context, ""));
       },
       "cardinality must be a non-negative integer or non-empty normalized "
       "static expression"},
      {"delegation",
       [&](BindingOp binding) {
         replaceRecordArrayField(binding, "ports", 0, "delegation",
                                 mlir::StringAttr::get(&context, "optional"));
       },
       "delegation must be forbidden, allowed, or required"},
      {"endpoint-ownership",
       [&](BindingOp binding) {
         replaceRecordArrayField(binding, "ports", 0, "ownership",
                                 mlir::StringAttr::get(&context, "temporary"));
       },
       "endpoint ownership must be owned, borrowed, or shared"},
      {"time-domain",
       [&](BindingOp binding) {
         replaceRecordArrayField(binding, "ports", 0, "time_domain",
                                 mlir::StringAttr::get(&context, "clock();"));
       },
       "time domain must be a canonical identifier"},
      {"result-name",
       [&](BindingOp binding) {
         replaceRecordArrayField(binding, "results", 0, "name",
                                 mlir::StringAttr::get(&context, "bad.name"));
       },
       "result name must be a canonical identifier"},
      {"activation-name",
       [&](BindingOp binding) {
         replaceRecordArrayField(binding, "activation_sources", 0, "name",
                                 mlir::StringAttr::get(&context, "wake()"));
       },
       "activation-source name must be a canonical identifier"},
  }};

  for (const MutationCase &testCase : cases) {
    SCOPED_TRACE(testCase.name.str());
    auto file = parseValidModel(context);
    ASSERT_TRUE(file);
    BindingOp binding;
    file->walk([&](BindingOp candidate) {
      if ((testCase.name == "effect-ownership" ||
           testCase.name == "result-name") &&
          candidate.getSymName() == "pure")
        binding = candidate;
      else if (testCase.name == "activation-name" &&
               candidate.getSymName() == "top")
        binding = candidate;
      else if (!binding && candidate.getSymName() == "stateful")
        binding = candidate;
    });
    ASSERT_TRUE(binding);
    testCase.mutate(binding);
    std::string diagnostic = expectDirectVerificationFailure(
        context, [&] { return binding.verify(); });
    EXPECT_TRUE(llvm::StringRef(diagnostic).contains(testCase.expected))
        << diagnostic;
  }
}

TEST(ACSimOpsTest, BindLocalVerifierUsesExactAccessorRoleRecords) {
  mlir::MLIRContext context;
  loadTestDialects(context);

  auto wrongPortRole = parseValidModel(context);
  ASSERT_TRUE(wrongPortRole);
  PortOp sourcePort;
  BindOp portBind;
  wrongPortRole->walk([&](PortOp candidate) {
    if (!sourcePort)
      sourcePort = candidate;
  });
  wrongPortRole->walk([&](BindOp candidate) {
    if (candidate.getKind() == "port")
      portBind = candidate;
  });
  auto sourcePortType = mlir::cast<PortType>(sourcePort.getResult().getType());
  sourcePort.getResult().setType(
      PortType::get(&context, sourcePortType.getInterface(),
                    mlir::FlatSymbolRefAttr::get(&context, "initiator"),
                    sourcePortType.getPayload(), sourcePortType.getProtocol()));
  std::string portDiagnostic = expectDirectVerificationFailure(
      context, [&] { return portBind.verify(); });
  EXPECT_TRUE(llvm::StringRef(portDiagnostic)
                  .contains("port bind endpoints must match exact output/input "
                            "binding-lock records"))
      << portDiagnostic;

  auto wrongResourceAccessor = parseValidModel(context);
  ASSERT_TRUE(wrongResourceAccessor);
  ResourceOp sourceResource;
  BindOp resourceBind;
  wrongResourceAccessor->walk([&](ResourceOp candidate) {
    if (!sourceResource)
      sourceResource = candidate;
  });
  wrongResourceAccessor->walk([&](BindOp candidate) {
    if (candidate.getKind() == "resource")
      resourceBind = candidate;
  });
  sourceResource.setAccessorAttr(
      mlir::FlatSymbolRefAttr::get(&context, "resource_in_accessor"));
  std::string resourceDiagnostic = expectDirectVerificationFailure(
      context, [&] { return resourceBind.verify(); });
  EXPECT_TRUE(llvm::StringRef(resourceDiagnostic)
                  .contains("resource bind endpoints must match exact "
                            "initiator/target binding-lock records"))
      << resourceDiagnostic;

  auto wrongPortAccessor = parseValidModel(context);
  ASSERT_TRUE(wrongPortAccessor);
  sourcePort = {};
  portBind = {};
  wrongPortAccessor->walk([&](PortOp candidate) {
    if (!sourcePort)
      sourcePort = candidate;
  });
  wrongPortAccessor->walk([&](BindOp candidate) {
    if (candidate.getKind() == "port")
      portBind = candidate;
  });
  sourcePort.setAccessorAttr(
      mlir::FlatSymbolRefAttr::get(&context, "port_in_accessor"));
  portDiagnostic = expectDirectVerificationFailure(
      context, [&] { return portBind.verify(); });
  EXPECT_TRUE(llvm::StringRef(portDiagnostic)
                  .contains("port bind endpoints must match exact output/input "
                            "binding-lock records"))
      << portDiagnostic;

  auto wrongResourceRole = parseValidModel(context);
  ASSERT_TRUE(wrongResourceRole);
  sourceResource = {};
  resourceBind = {};
  wrongResourceRole->walk([&](ResourceOp candidate) {
    if (!sourceResource)
      sourceResource = candidate;
  });
  wrongResourceRole->walk([&](BindOp candidate) {
    if (candidate.getKind() == "resource")
      resourceBind = candidate;
  });
  ResourceType sourceResourceType =
      mlir::cast<ResourceType>(sourceResource.getResult().getType());
  sourceResource.getResult().setType(
      ResourceType::get(&context, sourceResourceType.getResource(),
                        mlir::FlatSymbolRefAttr::get(&context, "consumer")));
  resourceDiagnostic = expectDirectVerificationFailure(
      context, [&] { return resourceBind.verify(); });
  EXPECT_TRUE(llvm::StringRef(resourceDiagnostic)
                  .contains("resource bind endpoints must match exact "
                            "initiator/target binding-lock records"))
      << resourceDiagnostic;
}

TEST(ACSimOpsTest, ExportBindIsACanonicalTypedConstructionRelation) {
  mlir::MLIRContext context;
  loadTestDialects(context);
  auto file = parseValidModel(context);
  ASSERT_TRUE(file);
  ExportOp exportOp;
  file->walk([&](ExportOp candidate) { exportOp = candidate; });
  ASSERT_TRUE(exportOp);
  mlir::OpBuilder builder(exportOp);
  builder.setInsertionPointAfter(exportOp);
  BindOp::create(builder, exportOp.getLoc(), exportOp.getValue(),
                 exportOp.getResult(), builder.getStringAttr("export"));
  EXPECT_TRUE(mlir::succeeded(mlir::verify(*file)));
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

  detail::ModelVerificationLimits dependencyLimits;
  dependencyLimits.maxDependencyNodes = 4;
  checkLimit(dependencyLimits,
             "dependency graph exceeds ACSim v0.1 capability 4");
}

TEST(ACSimOpsTest, CyclicSsaDependencyFailsExplicitlyWithoutRecursion) {
  mlir::MLIRContext context;
  loadTestDialects(context);
  auto file = parseValidModel(context);
  ASSERT_TRUE(file);
  ModelOp model = *file->getOps<ModelOp>().begin();
  llvm::SmallVector<InlineOp> inlineOps;
  file->walk([&](InlineOp operation) { inlineOps.push_back(operation); });
  ASSERT_EQ(inlineOps.size(), 2u);
  inlineOps.front()->insertOperands(0, inlineOps.back().getResult());

  std::string diagnostic =
      expectDirectVerificationFailure(context, [&] { return model.verify(); });
  EXPECT_TRUE(llvm::StringRef(diagnostic)
                  .contains("typed SSA dependency graph contains a cycle"))
      << diagnostic;
}

TEST(ACSimOpsTest, CyclicProcessPcDependencyFailsExplicitly) {
  mlir::MLIRContext context;
  loadTestDialects(context);
  auto file = parseValidModel(context);
  ASSERT_TRUE(file);
  SuspendOp suspend = firstOp<SuspendOp>(*file);
  ASSERT_TRUE(suspend);
  mlir::OpBuilder builder(suspend);
  ContinueOp::create(builder, suspend.getLoc(), "entry");
  suspend.erase();
  std::string diagnostic = expectVerificationFailure(*file);
  EXPECT_TRUE(
      llvm::StringRef(diagnostic)
          .contains(
              "process continue graph must prove bounded acyclic progress"))
      << diagnostic;
}

TEST(ACSimOpsTest, ReusableExpansionCycleAndTotalCapFailExplicitly) {
  mlir::MLIRContext context;
  loadTestDialects(context);

  auto cycleFile = parseReusableModel(context);
  ASSERT_TRUE(cycleFile);
  InstanceOp child;
  cycleFile->walk([&](InstanceOp candidate) {
    if (candidate.getSymName() == "child")
      child = candidate;
  });
  ASSERT_TRUE(child);
  child.setBindingAttr(mlir::FlatSymbolRefAttr::get(&context, "leaf"));
  child.setTargetAttr(mlir::SymbolRefAttr::get(&context, "Leaf"));
  child.setStaticArgsAttr(mlir::ArrayAttr::get(
      &context,
      {mlir::IntegerAttr::get(mlir::IntegerType::get(&context, 64), 2)}));
  child.setSpecializationFingerprint(
      "sha256:"
      "8000000000000000000000000000000000000000000000000000000000000000");
  child.getResult().setType(
      OwnerType::get(&context, mlir::FlatSymbolRefAttr::get(&context, "leaf")));
  std::string cycleDiagnostic = expectVerificationFailure(*cycleFile);
  EXPECT_TRUE(llvm::StringRef(cycleDiagnostic)
                  .contains("active module specialization cycle"))
      << cycleDiagnostic;

  auto capFile = parseReusableModel(context);
  ASSERT_TRUE(capFile);
  detail::ModelVerificationLimits limits;
  limits.maxExpandedObjects = 3;
  detail::ScopedModelVerificationLimits scopedLimits(limits);
  std::string capDiagnostic = expectVerificationFailure(*capFile);
  EXPECT_TRUE(
      llvm::StringRef(capDiagnostic)
          .contains("expanded hierarchy exceeds ACSim v0.1 capability 3"))
      << capDiagnostic;
}

TEST(ACSimOpsTest, DeepSsaDependenciesUseABoundedIterativeWorklist) {
  mlir::MLIRContext context;
  loadTestDialects(context);
  auto file = parseValidModel(context);
  ASSERT_TRUE(file);
  InlineOp firstInline;
  file->walk([&](InlineOp candidate) {
    if (!firstInline)
      firstInline = candidate;
  });
  ASSERT_TRUE(firstInline);
  mlir::OpBuilder builder(firstInline);
  mlir::Value previous;
  constexpr unsigned depth = 20000;
  for (unsigned index = 0; index != depth; ++index) {
    auto operation = InlineOp::create(
        builder, firstInline.getLoc(), firstInline.getResult().getType(),
        previous ? mlir::ValueRange{previous} : mlir::ValueRange{}, "pure");
    previous = operation.getResult();
  }
  firstInline->insertOperands(0, previous);
  EXPECT_TRUE(mlir::succeeded(mlir::verify(*file)));
}

TEST(ACSimOpsTest, DeepProcessPcChainUsesATopologicalWorklist) {
  mlir::MLIRContext context;
  loadTestDialects(context);
  auto file = parseValidModel(context);
  ASSERT_TRUE(file);
  ProcessOp oldProcess;
  file->walk([&](ProcessOp candidate) { oldProcess = candidate; });
  ASSERT_TRUE(oldProcess);
  constexpr unsigned depth = 4096;
  llvm::SmallVector<mlir::Attribute> pcs;
  pcs.reserve(depth);
  for (unsigned index = 0; index != depth; ++index)
    pcs.push_back(
        mlir::FlatSymbolRefAttr::get(&context, ("pc" + std::to_string(index))));
  mlir::OpBuilder builder(oldProcess);
  ProcessOp process = ProcessOp::create(
      builder, oldProcess.getLoc(), oldProcess.getCaptures(), "tick", "top",
      oldProcess.getCaptureNamesAttr(), "pc0",
      mlir::ArrayAttr::get(&context, pcs), oldProcess.getLiveSlotsAttr(), depth,
      oldProcess.getSpecializationFingerprint(), depth);
  for (unsigned index = 0; index != depth; ++index) {
    mlir::Region &state = process.getStates()[index];
    mlir::Block *block = &state.emplaceBlock();
    for (mlir::Value capture : process.getCaptures())
      block->addArgument(capture.getType(), process.getLoc());
    builder.setInsertionPointToEnd(block);
    if (index + 1 == depth)
      TerminateOp::create(builder, process.getLoc(), "success");
    else
      ContinueOp::create(builder, process.getLoc(),
                         "pc" + std::to_string(index + 1));
  }
  oldProcess.erase();
  EXPECT_TRUE(mlir::succeeded(mlir::verify(*file)));
}

TEST(ACSimOpsTest, ForwardOnlyMultiBlockProcessCfgIsCanonical) {
  mlir::MLIRContext context;
  loadTestDialects(context);
  auto file = parseValidModel(context);
  ASSERT_TRUE(file);
  ProcessOp process = firstOp<ProcessOp>(*file);
  mlir::Region &entryState = process.getStates().front();
  mlir::Block &entry = entryState.front();
  auto continuation = mlir::cast<ContinueOp>(entry.getTerminator());
  mlir::FlatSymbolRefAttr target = continuation.getTargetPcAttr();
  mlir::Block *next = &entryState.emplaceBlock();
  mlir::OpBuilder builder(continuation);
  mlir::cf::BranchOp::create(builder, continuation.getLoc(), next);
  continuation.erase();
  builder.setInsertionPointToEnd(next);
  ContinueOp::create(builder, process.getLoc(), target);
  EXPECT_TRUE(mlir::succeeded(mlir::verify(*file)));
}

} // namespace
} // namespace acir::acsim
