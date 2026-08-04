#include "acir/Dialect/ACSim/ACSimOps.h"
#include "ACSimOpsTestHooks.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinDialect.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/Verifier.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>

using namespace mlir;

namespace acir::acsim {
namespace {
thread_local detail::ModelVerificationWork *modelVerificationWorkCollector =
    nullptr;
thread_local const detail::ModelVerificationLimits *modelVerificationLimits =
    nullptr;

const detail::ModelVerificationLimits &currentModelVerificationLimits() {
  static constexpr detail::ModelVerificationLimits defaults;
  return modelVerificationLimits ? *modelVerificationLimits : defaults;
}
} // namespace

namespace detail {

ScopedModelVerificationWorkCollector::ScopedModelVerificationWorkCollector(
    ModelVerificationWork &work)
    : previous(modelVerificationWorkCollector) {
  modelVerificationWorkCollector = &work;
}

ScopedModelVerificationWorkCollector::~ScopedModelVerificationWorkCollector() {
  modelVerificationWorkCollector = previous;
}

ScopedModelVerificationLimits::ScopedModelVerificationLimits(
    const ModelVerificationLimits &limits)
    : previous(modelVerificationLimits) {
  modelVerificationLimits = &limits;
}

ScopedModelVerificationLimits::~ScopedModelVerificationLimits() {
  modelVerificationLimits = previous;
}

} // namespace detail

namespace {

constexpr StringLiteral kSourceMapAttrName = "acsim.source_map";

bool isSha256(StringRef value) {
  if (!value.consume_front("sha256:") || value.size() != 64)
    return false;
  return llvm::all_of(value, [](char c) {
    return std::isdigit(static_cast<unsigned char>(c)) ||
           (c >= 'a' && c <= 'f');
  });
}

LogicalResult verifyFingerprint(Operation *operation, StringAttr fingerprint,
                                StringRef label = "fingerprint") {
  if (fingerprint && isSha256(fingerprint.getValue()))
    return success();
  return operation->emitOpError() << label
                                  << " must be sha256: followed by 64 "
                                     "lowercase hexadecimal digits";
}

bool hasRawCppFragment(StringRef value) {
  return value.contains(';') || value.contains('{') || value.contains('}') ||
         value.contains('\n') || value.contains('#');
}

bool isCppQualifiedSymbol(StringRef value) {
  if (value.empty() || value.starts_with("::") || value.ends_with("::"))
    return false;
  while (!value.empty()) {
    auto [segment, remainder] = value.split("::");
    if (segment.empty() ||
        !(std::isalpha(static_cast<unsigned char>(segment.front())) ||
          segment.front() == '_') ||
        !llvm::all_of(segment.drop_front(), [](char character) {
          return std::isalnum(static_cast<unsigned char>(character)) ||
                 character == '_';
        }))
      return false;
    value = remainder;
  }
  return true;
}

bool hasExactKeys(DictionaryAttr dictionary, ArrayRef<StringLiteral> keys) {
  if (!dictionary || dictionary.size() != keys.size())
    return false;
  return llvm::all_of(keys,
                      [&](StringLiteral key) { return dictionary.get(key); });
}

LogicalResult verifyBindingLockShape(BindingOp binding) {
  constexpr std::array<StringLiteral, 20> topKeys = {
      "activation_sources",
      "availability",
      "binding",
      "binding_schema",
      "component_schema",
      "component_schema_fingerprint",
      "construction",
      "contract_epoch",
      "cpp",
      "cpp_type",
      "effect",
      "fingerprint",
      "implementation",
      "ownership",
      "parameters",
      "ports",
      "provider",
      "provider_implementation_fingerprint",
      "resources",
      "results",
  };
  DictionaryAttr record = binding.getRecord();
  if (!hasExactKeys(record, topKeys))
    return binding.emitOpError(
        "binding lock must contain exactly the acsim-binding-0.1 fields");
  auto identity = record.getAs<StringAttr>("binding");
  auto epoch = record.getAs<StringAttr>("contract_epoch");
  auto availability = record.getAs<StringAttr>("availability");
  if (!identity || identity.getValue() != binding.getSymName() ||
      binding.getBindingSchema() != "acsim-binding-0.1" || !epoch ||
      epoch.getValue() != "0.1" || !availability ||
      availability.getValue() != "available" ||
      (binding.getEffect() != "pure" && binding.getEffect() != "stateful") ||
      !binding.getCppTypeAttr() || !binding.getSchemaAttr() ||
      !binding.getProviderAttr() || !binding.getImplementationAttr())
    return binding.emitOpError(
        "binding lock identity, epoch, availability, and effect are invalid");
  for (StringLiteral field :
       {StringLiteral("component_schema_fingerprint"),
        StringLiteral("provider_implementation_fingerprint"),
        StringLiteral("fingerprint")}) {
    auto fingerprint = record.getAs<StringAttr>(field);
    if (!fingerprint || !isSha256(fingerprint.getValue()))
      return binding.emitOpError() << "binding lock field '" << field
                                   << "' requires an exact fingerprint";
  }

  constexpr std::array<StringLiteral, 5> cppKeys = {
      "concept", "entry_points", "header", "symbol", "target"};
  constexpr std::array<StringLiteral, 5> entryKeys = {
      "pure", "reset", "validate", "work", "xfer"};
  DictionaryAttr cpp = binding.getCppRecord();
  auto entries =
      cpp ? cpp.getAs<DictionaryAttr>("entry_points") : DictionaryAttr();
  if (!hasExactKeys(cpp, cppKeys) || !hasExactKeys(entries, entryKeys))
    return binding.emitOpError(
        "binding lock C++ record and entry points must be exact");
  for (StringLiteral field :
       {StringLiteral("concept"), StringLiteral("header"),
        StringLiteral("symbol"), StringLiteral("target")}) {
    auto value = cpp.getAs<StringAttr>(field);
    if (!value || value.getValue().empty() ||
        hasRawCppFragment(value.getValue()))
      return binding.emitOpError(
          "binding metadata cannot contain raw C++ or emitter behavior");
  }
  for (StringLiteral field : entryKeys) {
    auto value = entries.getAs<StringAttr>(field);
    if (!value ||
        (!value.getValue().empty() && !isCppQualifiedSymbol(value.getValue())))
      return binding.emitOpError(
          "binding entry points must be empty or qualified C++ symbols");
  }
  if ((binding.getEffect() == "pure" &&
       entries.getAs<StringAttr>("pure").getValue().empty()) ||
      (binding.getEffect() == "stateful" &&
       (entries.getAs<StringAttr>("work").getValue().empty() ||
        entries.getAs<StringAttr>("xfer").getValue().empty())))
    return binding.emitOpError(
        "binding effect requires its exact executable entry points");

  constexpr std::array<StringLiteral, 2> constructionKeys = {"arguments",
                                                             "kind"};
  constexpr std::array<StringLiteral, 2> ownershipKeys = {"kind", "placement"};
  auto construction = record.getAs<DictionaryAttr>("construction");
  auto ownership = record.getAs<DictionaryAttr>("ownership");
  if (!hasExactKeys(construction, constructionKeys) ||
      !construction.getAs<ArrayAttr>("arguments") ||
      !construction.getAs<StringAttr>("kind") ||
      !hasExactKeys(ownership, ownershipKeys) ||
      !ownership.getAs<StringAttr>("kind") ||
      !ownership.getAs<StringAttr>("placement"))
    return binding.emitOpError(
        "binding construction and ownership records must be exact");

  constexpr std::array<StringLiteral, 6> parameterKeys = {
      "acir_type", "cpp_type", "mapping", "name", "ordinal", "value"};
  auto parameters = record.getAs<ArrayAttr>("parameters");
  if (!parameters)
    return binding.emitOpError("binding parameters must be a static array");
  int64_t expectedOrdinal = 0;
  llvm::StringSet<> parameterNames;
  for (Attribute attribute : parameters) {
    auto parameter = dyn_cast<DictionaryAttr>(attribute);
    auto name = parameter ? parameter.getAs<StringAttr>("name") : StringAttr();
    auto ordinal =
        parameter ? parameter.getAs<IntegerAttr>("ordinal") : IntegerAttr();
    auto mapping =
        parameter ? parameter.getAs<StringAttr>("mapping") : StringAttr();
    if (!hasExactKeys(parameter, parameterKeys) || !name || !ordinal ||
        ordinal.getInt() != expectedOrdinal++ || !mapping ||
        !parameter.getAs<StringAttr>("acir_type") ||
        !parameter.getAs<StringAttr>("cpp_type") || !parameter.get("value") ||
        !parameterNames.insert(name.getValue()).second)
      return binding.emitOpError(
          "binding lock parameter must contain exact static fields");
    if (!llvm::is_contained({StringRef("template_argument"),
                             StringRef("constexpr_argument"),
                             StringRef("constructor_constant")},
                            mapping.getValue()))
      return binding.emitOpError("parameter mapping must be template_argument, "
                                 "constexpr_argument, or constructor_constant");
  }
  auto verifyRecordArray = [&](StringRef name,
                               ArrayRef<StringLiteral> keys) -> LogicalResult {
    auto records = record.getAs<ArrayAttr>(name);
    if (!records)
      return binding.emitOpError()
             << "binding " << name << " must be a static record array";
    for (Attribute attribute : records)
      if (!hasExactKeys(dyn_cast<DictionaryAttr>(attribute), keys))
        return binding.emitOpError()
               << "binding " << name
               << " records must have exact closed fields";
    return success();
  };
  constexpr std::array<StringLiteral, 10> portKeys = {
      "accessor",  "cardinality", "delegation", "direction", "interface",
      "ownership", "payload",     "protocol",   "role",      "time_domain"};
  constexpr std::array<StringLiteral, 7> resourceKeys = {
      "accessor", "delegation", "mode",       "ownership",
      "resource", "role",       "time_domain"};
  constexpr std::array<StringLiteral, 2> resultKeys = {"cpp_type", "name"};
  constexpr std::array<StringLiteral, 2> activationKeys = {"kind", "name"};
  if (failed(verifyRecordArray("ports", portKeys)) ||
      failed(verifyRecordArray("resources", resourceKeys)) ||
      failed(verifyRecordArray("results", resultKeys)) ||
      failed(verifyRecordArray("activation_sources", activationKeys)))
    return failure();
  llvm::StringSet<> portAccessors;
  for (Attribute attribute : record.getAs<ArrayAttr>("ports")) {
    auto port = cast<DictionaryAttr>(attribute);
    auto direction = port.getAs<StringAttr>("direction");
    auto accessor = port.getAs<FlatSymbolRefAttr>("accessor");
    if (!accessor || !portAccessors.insert(accessor.getValue()).second ||
        !port.getAs<FlatSymbolRefAttr>("interface") ||
        !port.getAs<FlatSymbolRefAttr>("role") ||
        !port.getAs<FlatSymbolRefAttr>("payload") ||
        !port.getAs<FlatSymbolRefAttr>("protocol") || !direction ||
        !llvm::is_contained({StringRef("input"), StringRef("output")},
                            direction.getValue()) ||
        !port.getAs<StringAttr>("cardinality") ||
        !port.getAs<BoolAttr>("delegation") ||
        !port.getAs<StringAttr>("ownership") ||
        !port.getAs<StringAttr>("time_domain"))
      return binding.emitOpError(
          "binding port records require exact typed endpoint metadata");
  }
  llvm::StringSet<> resourceAccessors;
  for (Attribute attribute : record.getAs<ArrayAttr>("resources")) {
    auto resource = cast<DictionaryAttr>(attribute);
    auto mode = resource.getAs<StringAttr>("mode");
    auto accessor = resource.getAs<FlatSymbolRefAttr>("accessor");
    if (!accessor || !resourceAccessors.insert(accessor.getValue()).second ||
        !resource.getAs<FlatSymbolRefAttr>("resource") ||
        !resource.getAs<FlatSymbolRefAttr>("role") || !mode ||
        !llvm::is_contained({StringRef("initiator"), StringRef("target")},
                            mode.getValue()) ||
        !resource.getAs<BoolAttr>("delegation") ||
        !resource.getAs<StringAttr>("ownership") ||
        !resource.getAs<StringAttr>("time_domain"))
      return binding.emitOpError(
          "binding resource records require exact typed endpoint metadata");
  }
  llvm::StringSet<> resultNames;
  for (Attribute attribute : record.getAs<ArrayAttr>("results")) {
    auto result = cast<DictionaryAttr>(attribute);
    auto name = result.getAs<StringAttr>("name");
    if (!result.getAs<FlatSymbolRefAttr>("cpp_type") || !name ||
        !resultNames.insert(name.getValue()).second)
      return binding.emitOpError(
          "binding result records require exact typed metadata");
  }
  llvm::StringSet<> activationNames;
  for (Attribute attribute : record.getAs<ArrayAttr>("activation_sources")) {
    auto source = cast<DictionaryAttr>(attribute);
    auto name = source.getAs<StringAttr>("name");
    if (!source.getAs<FlatSymbolRefAttr>("kind") || !name ||
        !activationNames.insert(name.getValue()).second)
      return binding.emitOpError(
          "binding activation-source records require exact typed metadata");
  }
  return success();
}

std::string symbolKey(SymbolRefAttr reference) {
  std::string result = reference.getRootReference().getValue().str();
  for (FlatSymbolRefAttr nested : reference.getNestedReferences()) {
    result.append("::");
    result.append(nested.getValue());
  }
  return result;
}

StringAttr symbolName(Operation *operation) {
  return operation->getAttrOfType<StringAttr>(SymbolTable::getSymbolAttrName());
}

ModuleOp enclosingConstructionModule(Operation *operation) {
  return operation->getParentOfType<ModuleOp>();
}

std::string definitionKey(Operation *operation) {
  StringAttr name = symbolName(operation);
  if (!name)
    return {};
  if (isa<TypeOp, BindingOp, ModuleOp>(operation))
    return name.getValue().str();
  if (ModuleOp module = enclosingConstructionModule(operation)) {
    std::string key = module.getSymName().str();
    key.append("::");
    key.append(name.getValue());
    return key;
  }
  return name.getValue().str();
}

uint64_t arrayVolume(ArrayRef<int64_t> shape) {
  uint64_t volume = 1;
  for (int64_t extent : shape) {
    if (extent == 0)
      return 0;
    volume *= static_cast<uint64_t>(extent);
  }
  return volume;
}

uint64_t arrayVolume(DenseI64ArrayAttr shape) {
  return arrayVolume(shape.asArrayRef());
}

SmallVector<int64_t> lexicographicIndices(ArrayRef<int64_t> shape,
                                          uint64_t ordinal) {
  SmallVector<int64_t> indices(shape.size(), 0);
  for (size_t index = shape.size(); index > 0; --index) {
    uint64_t extent = static_cast<uint64_t>(shape[index - 1]);
    if (!extent)
      return indices;
    indices[index - 1] = static_cast<int64_t>(ordinal % extent);
    ordinal /= extent;
  }
  return indices;
}

bool lexicographicallyLess(ArrayRef<int64_t> left, ArrayRef<int64_t> right) {
  return std::lexicographical_compare(left.begin(), left.end(), right.begin(),
                                      right.end());
}

struct ModelIndex {
  SmallVector<Operation *> ordered;
  SmallVector<Operation *> ownedPreorder;
  llvm::StringMap<Operation *> definitions;
  llvm::DenseMap<Operation *, uint64_t> positions;
};

LogicalResult verifySourceMap(Operation *operation, Attribute attribute) {
  auto records = dyn_cast<ArrayAttr>(attribute);
  constexpr std::array<StringLiteral, 5> keys = {"column", "end_column",
                                                 "end_line", "file", "line"};
  if (!records)
    return operation->emitOpError(
        "acsim.source_map must be an array of exact source records");
  for (Attribute item : records) {
    auto record = dyn_cast<DictionaryAttr>(item);
    auto file = record ? record.getAs<StringAttr>("file") : StringAttr();
    auto line = record ? record.getAs<IntegerAttr>("line") : IntegerAttr();
    auto column = record ? record.getAs<IntegerAttr>("column") : IntegerAttr();
    auto endLine =
        record ? record.getAs<IntegerAttr>("end_line") : IntegerAttr();
    auto endColumn =
        record ? record.getAs<IntegerAttr>("end_column") : IntegerAttr();
    if (!hasExactKeys(record, keys) || !file || file.getValue().empty() ||
        !line || !column || !endLine || !endColumn || line.getInt() <= 0 ||
        column.getInt() <= 0 || endLine.getInt() < line.getInt() ||
        endColumn.getInt() <= 0 ||
        (endLine.getInt() == line.getInt() &&
         endColumn.getInt() < column.getInt()))
      return operation->emitOpError(
          "acsim.source_map records require exact ordered positive ranges");
  }
  return success();
}

LogicalResult preflightModel(ModelOp model) {
  struct Frame {
    Operation *operation;
    uint64_t depth;
  };
  SmallVector<Frame> stack{{model.getOperation(), 0}};
  uint64_t nodes = 0;
  uint64_t edges = 0;
  uint64_t totalArrayVolume = 0;
  uint64_t attributeElements = 0;
  uint64_t attributeStringBytes = 0;
  const detail::ModelVerificationLimits &limits =
      currentModelVerificationLimits();

  while (!stack.empty()) {
    Frame frame = stack.pop_back_val();
    if (modelVerificationWorkCollector)
      ++modelVerificationWorkCollector->preflightOperationVisits;
    if (++nodes > limits.maxNodes)
      return model.emitOpError() << "model node count exceeds ACSim v0.1 "
                                    "capability "
                                 << limits.maxNodes;
    if (frame.depth > limits.maxRegionDepth)
      return frame.operation->emitOpError()
             << "region nesting exceeds ACSim v0.1 capability "
             << limits.maxRegionDepth;
    SmallVector<Attribute> attributeStack;
    if (frame.operation->getAttrs().size() >
        limits.maxAttributeElements - attributeElements)
      return frame.operation->emitOpError(
          "attribute element count exceeds ACSim v0.1 capability");
    for (NamedAttribute named : frame.operation->getAttrs()) {
      if (named.getName().size() >
          limits.maxAttributeStringBytes - attributeStringBytes)
        return frame.operation->emitOpError(
            "attribute string bytes exceed ACSim v0.1 capability");
      attributeStringBytes += named.getName().size();
      attributeStack.push_back(named.getValue());
    }
    while (!attributeStack.empty()) {
      Attribute attribute = attributeStack.pop_back_val();
      if (++attributeElements > limits.maxAttributeElements)
        return frame.operation->emitOpError(
            "attribute element count exceeds ACSim v0.1 capability");
      auto addString = [&](StringRef value) -> LogicalResult {
        if (value.size() >
            limits.maxAttributeStringBytes - attributeStringBytes)
          return frame.operation->emitOpError(
              "attribute string bytes exceed ACSim v0.1 capability");
        attributeStringBytes += value.size();
        return success();
      };
      if (auto string = dyn_cast<StringAttr>(attribute)) {
        if (failed(addString(string.getValue())))
          return failure();
      } else if (auto symbol = dyn_cast<SymbolRefAttr>(attribute)) {
        if (failed(addString(symbolKey(symbol))))
          return failure();
      } else if (auto array = dyn_cast<ArrayAttr>(attribute)) {
        if (attributeStack.size() >
                limits.maxAttributeElements - attributeElements ||
            array.size() > limits.maxAttributeElements - attributeElements -
                               attributeStack.size())
          return frame.operation->emitOpError(
              "attribute element count exceeds ACSim v0.1 capability");
        attributeStack.append(array.begin(), array.end());
      } else if (auto dictionary = dyn_cast<DictionaryAttr>(attribute)) {
        for (NamedAttribute named : dictionary) {
          if (failed(addString(named.getName().getValue())))
            return failure();
          if (attributeStack.size() >=
              limits.maxAttributeElements - attributeElements)
            return frame.operation->emitOpError(
                "attribute element count exceeds ACSim v0.1 capability");
          attributeStack.push_back(named.getValue());
        }
      } else if (auto dense = dyn_cast<DenseArrayAttr>(attribute)) {
        uint64_t count = dense.size();
        if (count > limits.maxAttributeElements - attributeElements)
          return frame.operation->emitOpError(
              "attribute element count exceeds ACSim v0.1 capability");
        attributeElements += count;
      } else if (auto dense = dyn_cast<DenseElementsAttr>(attribute)) {
        uint64_t count = dense.getNumElements();
        if (count > limits.maxAttributeElements - attributeElements)
          return frame.operation->emitOpError(
              "attribute element count exceeds ACSim v0.1 capability");
        attributeElements += count;
      }
    }
    uint64_t operandEdges = frame.operation->getNumOperands();
    if (modelVerificationWorkCollector)
      modelVerificationWorkCollector->edgeVisits += operandEdges;
    if (operandEdges > limits.maxEdges ||
        edges > limits.maxEdges - operandEdges)
      return frame.operation->emitOpError()
             << "model edge count exceeds ACSim v0.1 capability "
             << limits.maxEdges;
    edges += operandEdges;
    if (auto array = dyn_cast<ArrayOp>(frame.operation)) {
      auto type = dyn_cast<ArrayType>(array.getResult().getType());
      if (type) {
        uint64_t volume = 1;
        for (int64_t extent : type.getShape().asArrayRef()) {
          if (extent < 0 ||
              (extent != 0 && volume > limits.maxExpandedObjects /
                                           static_cast<uint64_t>(extent)))
            return array.emitOpError()
                   << "expanded array volume exceeds ACSim v0.1 capability "
                   << limits.maxExpandedObjects;
          volume *= static_cast<uint64_t>(extent);
        }
        if (volume > limits.maxExpandedObjects ||
            totalArrayVolume > limits.maxExpandedObjects - volume)
          return array.emitOpError()
                 << "expanded array volume exceeds ACSim v0.1 capability "
                 << limits.maxExpandedObjects;
        totalArrayVolume += volume;
      }
    }
    for (Region &region : frame.operation->getRegions()) {
      for (Block &block : region) {
        if (Operation *terminator = block.getTerminator()) {
          uint64_t successors = terminator->getNumSuccessors();
          if (modelVerificationWorkCollector)
            modelVerificationWorkCollector->edgeVisits += successors;
          if (successors > limits.maxEdges ||
              edges > limits.maxEdges - successors)
            return terminator->emitOpError()
                   << "model edge count exceeds ACSim v0.1 capability "
                   << limits.maxEdges;
          edges += successors;
        }
        for (Operation &child : llvm::reverse(block)) {
          if (stack.size() >= limits.maxNodes - nodes)
            return frame.operation->emitOpError()
                   << "model node count exceeds ACSim v0.1 capability "
                   << limits.maxNodes;
          stack.push_back({&child, frame.depth + 1});
        }
      }
    }
  }
  return success();
}

SmallVector<Operation *> collectPreorder(ModelOp model) {
  SmallVector<Operation *> result;
  SmallVector<Operation *> stack{model.getOperation()};
  while (!stack.empty()) {
    Operation *operation = stack.pop_back_val();
    if (modelVerificationWorkCollector)
      ++modelVerificationWorkCollector->preorderOperationVisits;
    result.push_back(operation);
    for (Region &region : llvm::reverse(operation->getRegions()))
      for (Block &block : llvm::reverse(region))
        for (Operation &child : llvm::reverse(block))
          stack.push_back(&child);
  }
  return result;
}

bool isProcessOperation(Operation *operation) {
  return isa<LiveLoadOp, LiveStoreOp, InvokeOp, ContinueOp, SuspendOp,
             TerminateOp>(operation);
}

bool isModuleOperation(Operation *operation) {
  return isa<InstanceOp, ArrayOp, ElementOp, PortOp, ResourceOp, BindOp,
             InlineOp, ProcessOp, ExportOp, ReturnOp>(operation);
}

bool isModelOperation(Operation *operation) {
  return isa<TypeOp, BindingOp, ModuleOp, DispatchOp, ActivateOp>(operation);
}

LogicalResult verifyClosedLegality(ModelOp model,
                                   ArrayRef<Operation *> ordered) {
  for (Operation *operation : ordered) {
    if (modelVerificationWorkCollector)
      ++modelVerificationWorkCollector->closureOperationVisits;
    for (NamedAttribute attribute : operation->getDiscardableAttrs()) {
      StringRef name = attribute.getName().getValue();
      if (name != kSourceMapAttrName)
        return operation->emitOpError() << "unknown public attribute '" << name
                                        << "' is not legal in canonical ACSim";
      if (failed(verifySourceMap(operation, attribute.getValue())))
        return failure();
    }

    if (operation == model.getOperation())
      continue;

    if (ProcessOp process = operation->getParentOfType<ProcessOp>()) {
      (void)process;
      if (isProcessOperation(operation))
        continue;
      if (isa<UnrealizedConversionCastOp>(operation))
        return operation->emitOpError(
            "conversion placeholders are not legal in canonical ACSim");
      StringRef dialect = operation->getName().getDialectNamespace();
      if ((dialect == "builtin" || dialect == "arith" || dialect == "index" ||
           dialect == "cf") &&
          isMemoryEffectFree(operation) && operation->getNumRegions() == 0)
        continue;
      return operation->emitOpError()
             << "operation '" << operation->getName()
             << "' is not legal in an acsim.process body";
    }

    Operation *parent = operation->getParentOp();
    if (parent == model.getOperation() && isModelOperation(operation))
      continue;
    if (isa<ModuleOp>(parent) && isModuleOperation(operation))
      continue;
    return operation->emitOpError() << "operation '" << operation->getName()
                                    << "' is not legal in canonical ACSim";
  }
  return success();
}

LogicalResult buildIndex(ModelOp model, ModelIndex &index) {
  index.ordered = collectPreorder(model);
  uint64_t position = 0;
  for (Operation *operation : index.ordered) {
    if (modelVerificationWorkCollector)
      ++modelVerificationWorkCollector->indexOperationVisits;
    index.positions[operation] = position++;
  }

  auto addDefinition = [&](Operation *operation) -> LogicalResult {
    std::string key = definitionKey(operation);
    if (key.empty())
      return success();
    auto [iterator, inserted] = index.definitions.try_emplace(key, operation);
    if (!inserted)
      return operation->emitOpError()
             << "duplicate canonical symbol or placement '" << key << "'";
    return success();
  };

  for (Operation *operation : index.ordered) {
    if (modelVerificationWorkCollector)
      ++modelVerificationWorkCollector->indexOperationVisits;
    if (isa<TypeOp, BindingOp, ModuleOp, InstanceOp, ArrayOp, ProcessOp,
            ExportOp>(operation) &&
        failed(addDefinition(operation)))
      return failure();
  }
  return success();
}

Operation *resolveReference(const ModelIndex &index, Operation *from,
                            SymbolRefAttr reference) {
  if (modelVerificationWorkCollector)
    ++modelVerificationWorkCollector->referenceLookups;
  std::string key = symbolKey(reference);
  if (!reference.getNestedReferences().empty())
    return index.definitions.lookup(key);
  if (ModuleOp module = enclosingConstructionModule(from)) {
    std::string local = module.getSymName().str();
    local.append("::");
    local.append(key);
    if (Operation *definition = index.definitions.lookup(local))
      return definition;
  }
  return index.definitions.lookup(key);
}

template <typename... Expected>
FailureOr<Operation *>
requireReference(const ModelIndex &index, Operation *from,
                 SymbolRefAttr reference, StringRef label,
                 bool requireEarlier = true) {
  Operation *definition = resolveReference(index, from, reference);
  if (!definition)
    return from->emitOpError()
           << label << " reference '" << reference << "' is unresolved";
  if (!isa<Expected...>(definition))
    return from->emitOpError() << label << " reference '" << reference
                               << "' resolves to incompatible operation '"
                               << definition->getName() << "'";
  if (requireEarlier && definition != from &&
      index.positions.lookup(definition) >= index.positions.lookup(from))
    return from->emitOpError() << label << " reference '" << reference
                               << "' appears before its construction";
  return definition;
}

LogicalResult requireTypeKind(const ModelIndex &index, Operation *from,
                              SymbolRefAttr reference,
                              ArrayRef<StringRef> allowedKinds,
                              StringRef label) {
  FailureOr<Operation *> definition =
      requireReference<TypeOp>(index, from, reference, label);
  if (failed(definition))
    return failure();
  StringRef kind = cast<TypeOp>(*definition).getKind();
  if (llvm::is_contained(allowedKinds, kind))
    return success();
  return from->emitOpError()
         << label << " reference '" << reference
         << "' has incompatible acsim.type kind '" << kind << "'";
}

LogicalResult verifyCanonicalType(Type type, Operation *from,
                                  const ModelIndex &index) {
  return llvm::TypeSwitch<Type, LogicalResult>(type)
      .Case<ValueType, ExprType>([&](auto valueType) {
        const std::array<StringRef, 2> kinds = {"value", "packet"};
        return requireTypeKind(index, from, valueType.getSymbol(), kinds,
                               "C++ type");
      })
      .Case<OwnerType, RefType>([&](auto ownerType) -> LogicalResult {
        FailureOr<Operation *> definition = requireReference<BindingOp>(
            index, from, ownerType.getSymbol(), "binding");
        if (failed(definition))
          return failure();
        if (cast<BindingOp>(*definition).getEffect() != "stateful")
          return from->emitOpError()
                 << "owner/ref type requires a stateful binding";
        return success();
      })
      .Case<PortType>([&](PortType port) {
        const std::array<StringRef, 1> interfaceKinds = {"interface"};
        const std::array<StringRef, 1> roleKinds = {"role"};
        const std::array<StringRef, 2> payloadKinds = {"value", "packet"};
        const std::array<StringRef, 1> protocolKinds = {"protocol"};
        if (failed(requireTypeKind(index, from, port.getInterface(),
                                   interfaceKinds, "interface")) ||
            failed(requireTypeKind(index, from, port.getRole(), roleKinds,
                                   "role")) ||
            failed(requireTypeKind(index, from, port.getPayload(), payloadKinds,
                                   "payload")) ||
            failed(requireTypeKind(index, from, port.getProtocol(),
                                   protocolKinds, "protocol")))
          return failure();
        return success();
      })
      .Case<ResourceType>([&](ResourceType resource) {
        const std::array<StringRef, 1> resourceKinds = {"resource"};
        const std::array<StringRef, 1> roleKinds = {"role"};
        return success(
            succeeded(requireTypeKind(index, from, resource.getResource(),
                                      resourceKinds, "resource")) &&
            succeeded(requireTypeKind(index, from, resource.getRole(),
                                      roleKinds, "role")));
      })
      .Case<ArrayType>([&](ArrayType array) {
        return verifyCanonicalType(array.getElementType(), from, index);
      })
      .Case<PcType>([&](PcType pc) {
        return success(succeeded(requireReference<ProcessOp>(
            index, from, pc.getSymbol(), "process")));
      })
      .Case<WakeType>([&](WakeType wake) {
        const std::array<StringRef, 1> kinds = {"wake"};
        return requireTypeKind(index, from, wake.getSymbol(), kinds,
                               "wake kind");
      })
      .Case<ObjectIdType, ActivationIdType>([](auto) { return success(); })
      .Default([&](Type other) -> LogicalResult {
        if (from->getParentOfType<ProcessOp>() && !isProcessOperation(from) &&
            isa<IntegerType, FloatType, IndexType>(other))
          return success();
        return from->emitOpError()
               << "type '" << other << "' is not legal in canonical ACSim";
      });
}

LogicalResult verifyModelFingerprints(ModelOp model) {
  constexpr std::array<StringLiteral, 6> expected = {
      "binding_lock", "frozen_acir", "profile",
      "provider",     "schema_set",  "toolchain"};
  DictionaryAttr fingerprints = model.getFingerprints();
  if (!fingerprints || fingerprints.size() != expected.size())
    return model.emitOpError(
        "fingerprints must contain exactly frozen_acir, binding_lock, "
        "provider, profile, toolchain, and schema_set");
  for (StringLiteral name : expected) {
    auto value = fingerprints.getAs<StringAttr>(name);
    if (!value || !isSha256(value.getValue()))
      return model.emitOpError()
             << "fingerprint field '" << name
             << "' must be sha256: followed by 64 lowercase hexadecimal "
                "digits";
  }
  return success();
}

unsigned modelRank(Operation *operation) {
  return llvm::TypeSwitch<Operation *, unsigned>(operation)
      .Case<TypeOp>([](auto) { return 0; })
      .Case<BindingOp>([](auto) { return 1; })
      .Case<ModuleOp>([](auto) { return 2; })
      .Case<DispatchOp>([](auto) { return 3; })
      .Case<ActivateOp>([](auto) { return 4; })
      .Default([](auto) { return 5; });
}

unsigned moduleRank(Operation *operation) {
  if (auto bind = dyn_cast<BindOp>(operation))
    return bind.getKind() == "pure_view" ? 5 : 3;
  return llvm::TypeSwitch<Operation *, unsigned>(operation)
      .Case<InstanceOp, ArrayOp>([](auto) { return 0; })
      .Case<ElementOp>([](auto) { return 1; })
      .Case<PortOp, ResourceOp>([](auto) { return 2; })
      .Case<InlineOp>([](auto) { return 4; })
      .Case<ExportOp>([](auto) { return 6; })
      .Case<ProcessOp>([](auto) { return 7; })
      .Case<ReturnOp>([](auto) { return 100; })
      .Default([](auto) { return 99; });
}

LogicalResult verifyDeterministicOrder(ModelOp model) {
  unsigned previousRank = 0;
  std::string previousName;
  bool first = true;
  for (Operation &operation : model.getBody().front()) {
    if (modelVerificationWorkCollector)
      ++modelVerificationWorkCollector->orderingOperationVisits;
    unsigned rank = modelRank(&operation);
    if (!first && rank < previousRank)
      return operation.emitOpError(
          "model declarations are not in deterministic canonical order");
    StringAttr name = symbolName(&operation);
    if (!first && rank == previousRank && name &&
        name.getValue() <= previousName)
      return operation.emitOpError(
          "same-kind model declarations must be strictly symbol-sorted");
    previousRank = rank;
    previousName = name ? name.getValue().str() : std::string();
    first = false;
  }

  for (Operation &operation : model.getBody().front()) {
    auto module = dyn_cast<ModuleOp>(operation);
    if (!module)
      continue;
    unsigned prior = 0;
    bool moduleFirst = true;
    std::string priorPlacement;
    for (Operation &child : module.getBody().front()) {
      if (modelVerificationWorkCollector)
        ++modelVerificationWorkCollector->orderingOperationVisits;
      unsigned rank = moduleRank(&child);
      if (!moduleFirst && rank < prior)
        return child.emitOpError(
            "module construction is not in deterministic canonical order");
      if (rank == 0) {
        StringAttr name = symbolName(&child);
        if (!moduleFirst && prior == rank && name &&
            name.getValue() <= priorPlacement)
          return child.emitOpError(
              "owned placements must be strictly symbol-sorted");
        priorPlacement = name ? name.getValue().str() : std::string();
      }
      prior = rank;
      moduleFirst = false;
    }
  }
  return success();
}

LogicalResult verifyConstructionOrder(ModelOp model, ModelIndex &index) {
  FailureOr<Operation *> root = requireReference<ModuleOp>(
      index, model, model.getRootAttr(), "root", false);
  if (failed(root))
    return failure();

  llvm::StringSet<> paths;
  llvm::DenseMap<Operation *, SmallVector<Operation *>> children;
  for (Operation *operation : index.ordered) {
    if (modelVerificationWorkCollector)
      ++modelVerificationWorkCollector->constructionOperationVisits;
    if (!isa<InstanceOp, ArrayOp, ProcessOp>(operation))
      continue;
    StringAttr path = operation->getAttrOfType<StringAttr>("path");
    if (!path || path.getValue().empty())
      return operation->emitOpError(
          "owned placement requires a non-empty path");
    if (!paths.insert(path.getValue()).second)
      return operation->emitOpError()
             << "duplicate hierarchy path '" << path << "'";

    auto owner = operation->getAttrOfType<SymbolRefAttr>("owner");
    FailureOr<Operation *> ownerDefinition =
        requireReference<ModuleOp, InstanceOp, ArrayOp>(index, operation, owner,
                                                        "owner");
    if (failed(ownerDefinition))
      return failure();
    children[*ownerDefinition].push_back(operation);
    StringAttr ownerPath =
        (*ownerDefinition)->getAttrOfType<StringAttr>("path");
    if (!ownerPath && isa<ModuleOp>(*ownerDefinition))
      ownerPath = cast<ModuleOp>(*ownerDefinition).getPathAttr();
    if (!ownerPath || !path.getValue().starts_with(ownerPath.getValue()) ||
        path.getValue().size() <= ownerPath.getValue().size() ||
        path.getValue()[ownerPath.getValue().size()] != '.')
      return operation->emitOpError()
             << "hierarchy path must be a named child of owner path '"
             << ownerPath << "'";
  }

  for (auto &entry : children)
    llvm::sort(entry.second, [](Operation *left, Operation *right) {
      return definitionKey(left) < definitionKey(right);
    });
  SmallVector<std::string> actual;
  SmallVector<Operation *> stack;
  for (Operation *child : llvm::reverse(children.lookup(*root)))
    stack.push_back(child);
  while (!stack.empty()) {
    Operation *operation = stack.pop_back_val();
    index.ownedPreorder.push_back(operation);
    actual.push_back(definitionKey(operation));
    for (Operation *child : llvm::reverse(children.lookup(operation)))
      stack.push_back(child);
  }
  size_t placementCount = llvm::count_if(index.ordered, [](Operation *op) {
    return isa<InstanceOp, ArrayOp, ProcessOp>(op);
  });
  if (index.ownedPreorder.size() != placementCount)
    return model.emitOpError(
        "every ownership chain must descend from the model root");

  auto readOrder = [&](ArrayAttr order, StringRef label,
                       SmallVectorImpl<std::string> &keys) -> LogicalResult {
    llvm::StringSet<> unique;
    for (Attribute attribute : order) {
      auto reference = dyn_cast<SymbolRefAttr>(attribute);
      if (!reference)
        return model.emitOpError()
               << label << " entries must be symbol references";
      std::string key = symbolKey(reference);
      if (!unique.insert(key).second)
        return model.emitOpError()
               << label << " contains duplicate '" << key << "'";
      Operation *definition = index.definitions.lookup(key);
      if (!definition || !isa<InstanceOp, ArrayOp, ProcessOp>(definition))
        return model.emitOpError()
               << label << " reference '" << reference << "' is unresolved";
      keys.push_back(std::move(key));
    }
    return success();
  };

  SmallVector<std::string> construction;
  SmallVector<std::string> destruction;
  if (failed(readOrder(model.getConstructionOrder(), "construction order",
                       construction)) ||
      failed(readOrder(model.getDestructionOrder(), "destruction order",
                       destruction)))
    return failure();
  if (construction != actual)
    return model.emitOpError(
        "construction order must equal canonical ownership preorder");
  SmallVector<std::string> reversed(actual.rbegin(), actual.rend());
  if (destruction != reversed)
    return model.emitOpError(
        "destruction order must be the exact reverse of construction order");
  return success();
}

LogicalResult verifyProcess(ProcessOp process, const ModelIndex &index) {
  if (process.getFairnessCap() <= 0 ||
      static_cast<uint64_t>(process.getFairnessCap()) > kMaxModelNodes)
    return process.emitOpError(
        "fairness cap must be a positive bounded static integer");
  SmallVector<std::string> pcs;
  llvm::StringSet<> pcSet;
  for (Attribute attribute : process.getPcs()) {
    auto reference = dyn_cast<FlatSymbolRefAttr>(attribute);
    if (!reference || !pcSet.insert(reference.getValue()).second)
      return process.emitOpError(
          "pcs must be a non-empty ordered list of unique flat symbols");
    pcs.push_back(reference.getValue().str());
  }
  if (pcs.empty() || !pcSet.contains(process.getEntryPc()))
    return process.emitOpError(
        "entry PC must occur exactly once in the closed PC list");
  if (process.getStates().size() != pcs.size())
    return process.emitOpError(
        "process requires exactly one ordered state region per PC");
  if (pcs.front() != process.getEntryPc())
    return process.emitOpError(
        "the first ordered state region must be the entry PC");

  if (process.getCaptureNames().size() != process.getCaptures().size())
    return process.emitOpError(
        "process captures must have one exact ordered name per operand");
  llvm::StringSet<> captureNames;
  for (Attribute attribute : process.getCaptureNames()) {
    auto name = dyn_cast<StringAttr>(attribute);
    if (!name || name.getValue().empty() ||
        !captureNames.insert(name.getValue()).second)
      return process.emitOpError(
          "process capture names must be unique non-empty strings");
  }

  llvm::StringMap<Type> slots;
  for (Attribute attribute : process.getLiveSlots()) {
    auto dictionary = dyn_cast<DictionaryAttr>(attribute);
    auto name =
        dictionary ? dictionary.getAs<StringAttr>("name") : StringAttr();
    auto type = dictionary ? dictionary.getAs<TypeAttr>("type") : TypeAttr();
    if (!dictionary || dictionary.size() != 2 || !name || !type ||
        !isa<ValueType>(type.getValue()) ||
        !slots.try_emplace(name.getValue(), type.getValue()).second)
      return process.emitOpError(
          "live slots require unique exact {name, type} value records");
    if (failed(verifyCanonicalType(type.getValue(), process, index)))
      return failure();
  }

  for (Region &state : process.getStates()) {
    if (state.empty())
      return process.emitOpError("every PC requires a non-empty state region");
    Block &entry = state.front();
    if (entry.getNumArguments() != process.getCaptures().size())
      return process.emitOpError(
          "process state arguments must exactly match declared typed captures");
    for (auto [argument, capture] :
         llvm::zip_equal(entry.getArguments(), process.getCaptures()))
      if (argument.getType() != capture.getType())
        return process.emitOpError("process state arguments must exactly match "
                                   "declared typed captures");
    llvm::DenseMap<Block *, unsigned> blockOrdinals;
    unsigned ordinal = 0;
    for (Block &block : state) {
      blockOrdinals[&block] = ordinal++;
      if (block.empty())
        return process.emitOpError("every process block requires operations");
      Operation *terminator = block.getTerminator();
      if (!terminator ||
          (!isa<ContinueOp, SuspendOp, TerminateOp>(terminator) &&
           terminator->getName().getDialectNamespace() != "cf"))
        return block.front().emitOpError(
            "every process path must continue, suspend, terminate, or use cf");
    }
    for (Block &block : state) {
      Operation *terminator = block.getTerminator();
      for (Block *successor : terminator->getSuccessors()) {
        if (successor->getParent() != &state)
          return terminator->emitOpError("ordinary cf edges cannot cross "
                                         "process PC suspension boundaries");
        if (blockOrdinals.lookup(successor) <= blockOrdinals.lookup(&block))
          return terminator->emitOpError(
              "intra-PC control flow must prove bounded acyclic progress");
      }
    }
  }

  auto verifyTarget = [&](Operation *operation,
                          FlatSymbolRefAttr target) -> LogicalResult {
    if (pcSet.contains(target.getValue()))
      return success();
    return operation->emitOpError()
           << "target PC '" << target << "' is not in the closed PC list";
  };
  for (Region &state : process.getStates()) {
    for (Block &block : state) {
      for (Operation &operation : block) {
        if (auto load = dyn_cast<LiveLoadOp>(operation)) {
          if (resolveReference(index, load, load.getProcessAttr()) != process ||
              !slots.contains(load.getSlot()) ||
              slots.lookup(load.getSlot()) != load.getResult().getType())
            return load.emitOpError("live load must resolve to an exact typed "
                                    "slot of this process");
        } else if (auto store = dyn_cast<LiveStoreOp>(operation)) {
          if (resolveReference(index, store, store.getProcessAttr()) !=
                  process ||
              !slots.contains(store.getSlot()) ||
              slots.lookup(store.getSlot()) != store.getValue().getType())
            return store.emitOpError("live store must resolve to an exact "
                                     "typed slot of this process");
        } else if (auto next = dyn_cast<ContinueOp>(operation)) {
          if (failed(verifyTarget(next, next.getTargetPcAttr())))
            return failure();
        } else if (auto suspend = dyn_cast<SuspendOp>(operation)) {
          if (!isa<WakeType>(suspend.getWake().getType()) ||
              failed(verifyTarget(suspend, suspend.getTargetPcAttr())))
            return suspend.emitOpError(
                "suspend requires one exact typed wake and a closed next PC");
        } else if (auto terminate = dyn_cast<TerminateOp>(operation)) {
          if (terminate.getStatus() != "success" &&
              terminate.getStatus() != "failure")
            return terminate.emitOpError(
                "terminal status must be exactly 'success' or 'failure'");
        }
      }
    }
  }

  llvm::StringMap<unsigned> pcOrdinals;
  for (auto [ordinal, pc] : llvm::enumerate(pcs))
    pcOrdinals[pc] = ordinal;
  SmallVector<std::optional<uint64_t>> memo(pcs.size());
  SmallVector<bool> active(pcs.size(), false);
  std::function<FailureOr<uint64_t>(unsigned)> longestState =
      [&](unsigned stateOrdinal) -> FailureOr<uint64_t> {
    if (memo[stateOrdinal])
      return *memo[stateOrdinal];
    if (active[stateOrdinal])
      return process.emitOpError(
          "process continue graph must prove bounded acyclic progress");
    active[stateOrdinal] = true;
    Region &state = process.getStates()[stateOrdinal];
    llvm::DenseMap<Block *, uint64_t> blockCost;
    uint64_t stateMaximum = 0;
    for (Block &block : llvm::reverse(state)) {
      uint64_t suffix = 0;
      Operation *terminator = block.getTerminator();
      if (auto next = dyn_cast<ContinueOp>(terminator)) {
        auto found = pcOrdinals.find(next.getTargetPc());
        if (found == pcOrdinals.end())
          return next.emitOpError("target PC is not in the closed PC list");
        FailureOr<uint64_t> downstream = longestState(found->second);
        if (failed(downstream))
          return failure();
        suffix = *downstream;
      } else {
        for (Block *successor : terminator->getSuccessors())
          suffix = std::max(suffix, blockCost.lookup(successor));
      }
      uint64_t own = block.getOperations().size();
      if (suffix > kMaxModelNodes - own)
        return process.emitOpError("process fairness work count overflows");
      blockCost[&block] = own + suffix;
      stateMaximum = std::max(stateMaximum, own + suffix);
    }
    active[stateOrdinal] = false;
    memo[stateOrdinal] = stateMaximum;
    return stateMaximum;
  };
  uint64_t maximumPath = 0;
  for (unsigned ordinal = 0; ordinal != pcs.size(); ++ordinal) {
    FailureOr<uint64_t> cost = longestState(ordinal);
    if (failed(cost))
      return failure();
    maximumPath = std::max(maximumPath, *cost);
  }
  if (maximumPath > static_cast<uint64_t>(process.getFairnessCap()))
    return process.emitOpError()
           << "fairness cap " << process.getFairnessCap()
           << " is below maximum local execution path " << maximumPath;
  return success();
}

BindingOp bindingForBase(Value value, const ModelIndex &index) {
  SymbolRefAttr symbol;
  if (auto owner = dyn_cast<OwnerType>(value.getType()))
    symbol = owner.getSymbol();
  else if (auto reference = dyn_cast<RefType>(value.getType()))
    symbol = reference.getSymbol();
  if (!symbol)
    return {};
  return dyn_cast_or_null<BindingOp>(
      index.definitions.lookup(symbolKey(symbol)));
}

DictionaryAttr findEndpoint(BindingOp binding, StringRef field,
                            FlatSymbolRefAttr accessor) {
  if (!binding)
    return {};
  auto records = binding.getRecord().getAs<ArrayAttr>(field);
  if (!records)
    return {};
  for (Attribute attribute : records) {
    auto record = dyn_cast<DictionaryAttr>(attribute);
    if (record && record.getAs<FlatSymbolRefAttr>("accessor") == accessor)
      return record;
  }
  return {};
}

LogicalResult verifyBindingReferenceFingerprint(BindingOp binding,
                                                const ModelIndex &index,
                                                FlatSymbolRefAttr reference,
                                                StringRef fingerprintField,
                                                StringRef label) {
  Operation *definition = resolveReference(index, binding, reference);
  auto type = dyn_cast_or_null<TypeOp>(definition);
  auto fingerprint = binding.getRecord().getAs<StringAttr>(fingerprintField);
  if (!type || !fingerprint || fingerprint != type.getFingerprintAttr())
    return binding.emitOpError()
           << label << " fingerprint must exactly match referenced acsim.type";
  return success();
}

ArrayAttr bindingStaticValues(BindingOp binding) {
  SmallVector<Attribute> values;
  auto parameters = binding.getRecord().getAs<ArrayAttr>("parameters");
  if (!parameters)
    return {};
  for (Attribute attribute : parameters) {
    auto record = dyn_cast<DictionaryAttr>(attribute);
    if (!record || !record.get("value"))
      return {};
    values.push_back(record.get("value"));
  }
  return ArrayAttr::get(binding.getContext(), values);
}

LogicalResult
verifyPlacementTarget(Operation *operation, FlatSymbolRefAttr bindingReference,
                      SymbolRefAttr targetReference, ArrayAttr staticArguments,
                      StringAttr specialization, const ModelIndex &index) {
  auto bindingDefinition = requireReference<BindingOp>(
      index, operation, bindingReference, "binding");
  if (failed(bindingDefinition))
    return failure();
  BindingOp binding = cast<BindingOp>(*bindingDefinition);
  FailureOr<Operation *> targetDefinition =
      requireReference<BindingOp, ModuleOp>(index, operation, targetReference,
                                            "placement target");
  if (failed(targetDefinition))
    return failure();
  Operation *target = *targetDefinition;
  if (auto targetBinding = dyn_cast<BindingOp>(target)) {
    if (targetBinding != binding)
      return operation->emitOpError(
          "placement target binding must equal the declared binding");
  } else {
    auto targetModule = cast<ModuleOp>(target);
    if (targetModule.getBindingAttr() != bindingReference ||
        targetModule.getStaticParams() != staticArguments ||
        targetModule.getSpecializationFingerprintAttr() != specialization)
      return operation->emitOpError(
          "reusable module target requires exact binding, static arguments, "
          "and specialization fingerprint");
  }
  if (bindingStaticValues(binding) != staticArguments)
    return operation->emitOpError(
        "static arguments must exactly match ordered binding-lock parameters");
  return success();
}

Operation *capturedPlacement(Value value) {
  if (Operation *definition = value.getDefiningOp()) {
    if (isa<InstanceOp, ArrayOp>(definition))
      return definition;
    if (auto element = dyn_cast<ElementOp>(definition))
      return element.getArray().getDefiningOp();
    if (auto port = dyn_cast<PortOp>(definition))
      return capturedPlacement(port.getBase());
    if (auto resource = dyn_cast<ResourceOp>(definition))
      return capturedPlacement(resource.getBase());
  }
  return nullptr;
}

LogicalResult verifyCaptureBoundary(ProcessOp process, Value capture,
                                    const ModelIndex &index) {
  if (!isa<OwnerType, RefType>(capture.getType()))
    return success();
  Operation *placement = capturedPlacement(capture);
  Operation *boundary =
      resolveReference(index, process, process.getOwnerAttr());
  llvm::SmallPtrSet<Operation *, 16> visited;
  while (placement && visited.insert(placement).second) {
    if (placement == boundary)
      return success();
    auto owner = placement->getAttrOfType<SymbolRefAttr>("owner");
    placement = owner ? resolveReference(index, placement, owner) : nullptr;
  }
  return process.emitOpError(
      "captured owner/ref must remain within the process ownership boundary");
}

LogicalResult verifyModulesAndTypedGraph(ModelOp model,
                                         const ModelIndex &index) {
  llvm::DenseMap<Value, SmallVector<int64_t>> lastProjection;
  llvm::SmallSet<std::pair<void *, void *>, 16> bindingPairs;
  llvm::StringMap<StringAttr> specializationByKey;
  llvm::StringMap<std::string> keyBySpecialization;
  auto recordSpecialization = [&](Operation *placement, SymbolRefAttr target,
                                  ArrayAttr arguments,
                                  StringAttr fingerprint) -> LogicalResult {
    std::string key = symbolKey(target);
    std::string printedArguments;
    llvm::raw_string_ostream(printedArguments) << arguments;
    key.push_back(':');
    key.append(printedArguments);
    auto [sameKey, inserted] =
        specializationByKey.try_emplace(key, fingerprint);
    if (!inserted && sameKey->second != fingerprint)
      return placement->emitOpError("identical target and static arguments "
                                    "require one specialization fingerprint");
    auto [sameFingerprint, unique] =
        keyBySpecialization.try_emplace(fingerprint.getValue(), key);
    if (!unique && sameFingerprint->second != key)
      return placement->emitOpError(
          "different specialization inputs require distinct fingerprints");
    return success();
  };

  for (Operation *operation : index.ordered) {
    if (modelVerificationWorkCollector)
      ++modelVerificationWorkCollector->semanticOperationVisits;
    for (Type type : operation->getOperandTypes())
      if (failed(verifyCanonicalType(type, operation, index)))
        return failure();
    for (Type type : operation->getResultTypes())
      if (failed(verifyCanonicalType(type, operation, index)))
        return failure();
    for (Region &region : operation->getRegions())
      for (Block &block : region)
        for (BlockArgument argument : block.getArguments())
          if (failed(verifyCanonicalType(argument.getType(), operation, index)))
            return failure();

    if (auto binding = dyn_cast<BindingOp>(operation)) {
      const std::array<StringRef, 2> cppKinds = {"value", "packet"};
      const std::array<StringRef, 1> schemaKinds = {"schema"};
      const std::array<StringRef, 1> providerKinds = {"provider"};
      const std::array<StringRef, 1> implementationKinds = {"implementation"};
      if (failed(requireTypeKind(index, binding, binding.getCppTypeAttr(),
                                 cppKinds, "C++ type")) ||
          failed(requireTypeKind(index, binding, binding.getSchemaAttr(),
                                 schemaKinds, "schema")) ||
          failed(requireTypeKind(index, binding, binding.getProviderAttr(),
                                 providerKinds, "provider")) ||
          failed(requireTypeKind(index, binding,
                                 binding.getImplementationAttr(),
                                 implementationKinds, "implementation")))
        return failure();
      if (failed(verifyBindingReferenceFingerprint(
              binding, index, binding.getSchemaAttr(),
              "component_schema_fingerprint", "component schema")) ||
          failed(verifyBindingReferenceFingerprint(
              binding, index, binding.getProviderAttr(),
              "provider_implementation_fingerprint", "provider")))
        return failure();
      for (StringRef field : {StringRef("ports"), StringRef("resources")})
        for (Attribute item : binding.getRecord().getAs<ArrayAttr>(field)) {
          auto endpoint = cast<DictionaryAttr>(item);
          const std::array<StringRef, 1> accessorKinds = {"accessor"};
          const std::array<StringRef, 1> roleKinds = {"role"};
          if (failed(requireTypeKind(
                  index, binding, endpoint.getAs<FlatSymbolRefAttr>("accessor"),
                  accessorKinds, "endpoint accessor")) ||
              failed(requireTypeKind(index, binding,
                                     endpoint.getAs<FlatSymbolRefAttr>("role"),
                                     roleKinds, "endpoint role")))
            return failure();
        }
      for (Attribute item : binding.getRecord().getAs<ArrayAttr>("ports")) {
        auto endpoint = cast<DictionaryAttr>(item);
        const std::array<StringRef, 1> interfaceKinds = {"interface"};
        const std::array<StringRef, 2> payloadKinds = {"value", "packet"};
        const std::array<StringRef, 1> protocolKinds = {"protocol"};
        if (failed(requireTypeKind(
                index, binding, endpoint.getAs<FlatSymbolRefAttr>("interface"),
                interfaceKinds, "endpoint interface")) ||
            failed(requireTypeKind(index, binding,
                                   endpoint.getAs<FlatSymbolRefAttr>("payload"),
                                   payloadKinds, "endpoint payload")) ||
            failed(requireTypeKind(
                index, binding, endpoint.getAs<FlatSymbolRefAttr>("protocol"),
                protocolKinds, "endpoint protocol")))
          return failure();
      }
      for (Attribute item : binding.getRecord().getAs<ArrayAttr>("resources")) {
        auto endpoint = cast<DictionaryAttr>(item);
        const std::array<StringRef, 1> resourceKinds = {"resource"};
        if (failed(requireTypeKind(
                index, binding, endpoint.getAs<FlatSymbolRefAttr>("resource"),
                resourceKinds, "endpoint resource")))
          return failure();
      }
      for (Attribute item : binding.getRecord().getAs<ArrayAttr>("results")) {
        auto result = cast<DictionaryAttr>(item);
        const std::array<StringRef, 2> resultKinds = {"value", "packet"};
        if (failed(requireTypeKind(index, binding,
                                   result.getAs<FlatSymbolRefAttr>("cpp_type"),
                                   resultKinds, "result C++ type")))
          return failure();
      }
      for (Attribute item :
           binding.getRecord().getAs<ArrayAttr>("activation_sources")) {
        auto source = cast<DictionaryAttr>(item);
        const std::array<StringRef, 1> wakeKinds = {"wake"};
        if (failed(requireTypeKind(index, binding,
                                   source.getAs<FlatSymbolRefAttr>("kind"),
                                   wakeKinds, "activation source kind")))
          return failure();
      }
    } else if (auto module = dyn_cast<ModuleOp>(operation)) {
      auto definition = requireReference<BindingOp>(
          index, module, module.getBindingAttr(), "binding");
      if (failed(definition))
        return failure();
      if (bindingStaticValues(cast<BindingOp>(*definition)) !=
          module.getStaticParams())
        return module.emitOpError(
            "module static parameters must exactly match its binding lock");
    } else if (auto instance = dyn_cast<InstanceOp>(operation)) {
      auto ownerType = dyn_cast<OwnerType>(instance.getResult().getType());
      if (failed(verifyPlacementTarget(
              instance, instance.getBindingAttr(), instance.getTargetAttr(),
              instance.getStaticArgs(),
              instance.getSpecializationFingerprintAttr(), index)) ||
          failed(recordSpecialization(
              instance, instance.getTargetAttr(), instance.getStaticArgs(),
              instance.getSpecializationFingerprintAttr())))
        return failure();
      if (!ownerType || ownerType.getSymbol() != instance.getBindingAttr())
        return instance.emitOpError(
            "instance result must be owner of its exact stateful binding");
    } else if (auto array = dyn_cast<ArrayOp>(operation)) {
      if (failed(verifyPlacementTarget(
              array, array.getBindingAttr(), array.getTargetAttr(),
              array.getStaticArgs(), array.getSpecializationFingerprintAttr(),
              index)) ||
          failed(recordSpecialization(
              array, array.getTargetAttr(), array.getStaticArgs(),
              array.getSpecializationFingerprintAttr())))
        return failure();
      auto type = dyn_cast<ArrayType>(array.getResult().getType());
      auto element =
          type ? dyn_cast<OwnerType>(type.getElementType()) : OwnerType();
      if (!type || type.getShape().asArrayRef() != array.getShape() ||
          !element || element.getSymbol() != array.getBindingAttr())
        return array.emitOpError(
            "array result shape and owning element binding must be exact");
      uint64_t volume = arrayVolume(array.getShape());
      if (array.getObjectIds().size() != volume ||
          array.getActivationIds().size() != volume)
        return array.emitOpError(
            "array object and activation IDs must cover every element");
    } else if (auto element = dyn_cast<ElementOp>(operation)) {
      auto arrayType = dyn_cast<ArrayType>(element.getArray().getType());
      auto owner = arrayType ? dyn_cast<OwnerType>(arrayType.getElementType())
                             : OwnerType();
      auto reference = dyn_cast<RefType>(element.getResult().getType());
      if (!arrayType || !owner || !reference ||
          owner.getSymbol() != reference.getSymbol() ||
          element.getIndices().size() != arrayType.getShape().size())
        return element.emitOpError(
            "element must be a constant typed reference projection");
      for (auto [indexValue, extent] : llvm::zip_equal(
               element.getIndices(), arrayType.getShape().asArrayRef()))
        if (indexValue < 0 || indexValue >= extent)
          return element.emitOpError("element index is out of static bounds");
      SmallVector<int64_t> current(element.getIndices());
      auto found = lastProjection.find(element.getArray());
      if (found != lastProjection.end() &&
          !lexicographicallyLess(found->second, current))
        return element.emitOpError(
            "array element projections must be strictly lexicographic");
      lastProjection[element.getArray()] = std::move(current);
    } else if (auto port = dyn_cast<PortOp>(operation)) {
      if (!isa<OwnerType, RefType>(port.getBase().getType()) ||
          !isa<PortType>(port.getResult().getType()))
        return port.emitOpError(
            "port projection requires a typed owner/ref and typed port result");
      const std::array<StringRef, 1> kinds = {"accessor"};
      if (failed(requireTypeKind(index, port, port.getAccessorAttr(), kinds,
                                 "port accessor")))
        return failure();
      BindingOp binding = bindingForBase(port.getBase(), index);
      DictionaryAttr endpoint =
          binding ? findEndpoint(binding, "ports", port.getAccessorAttr())
                  : DictionaryAttr();
      PortType type = cast<PortType>(port.getResult().getType());
      if (!endpoint ||
          endpoint.getAs<FlatSymbolRefAttr>("interface") !=
              type.getInterface() ||
          endpoint.getAs<FlatSymbolRefAttr>("role") != type.getRole() ||
          endpoint.getAs<FlatSymbolRefAttr>("payload") != type.getPayload() ||
          endpoint.getAs<FlatSymbolRefAttr>("protocol") != type.getProtocol())
        return port.emitOpError("port projection must exactly match its "
                                "binding-lock endpoint record");
    } else if (auto resource = dyn_cast<ResourceOp>(operation)) {
      if (!isa<OwnerType, RefType>(resource.getBase().getType()) ||
          !isa<ResourceType>(resource.getResult().getType()))
        return resource.emitOpError("resource projection requires a typed "
                                    "owner/ref and resource result");
      const std::array<StringRef, 1> kinds = {"accessor"};
      if (failed(requireTypeKind(index, resource, resource.getAccessorAttr(),
                                 kinds, "resource accessor")))
        return failure();
      BindingOp binding = bindingForBase(resource.getBase(), index);
      DictionaryAttr endpoint =
          binding
              ? findEndpoint(binding, "resources", resource.getAccessorAttr())
              : DictionaryAttr();
      ResourceType type = cast<ResourceType>(resource.getResult().getType());
      if (!endpoint ||
          endpoint.getAs<FlatSymbolRefAttr>("resource") != type.getResource() ||
          endpoint.getAs<FlatSymbolRefAttr>("role") != type.getRole())
        return resource.emitOpError("resource projection must exactly match "
                                    "its binding-lock endpoint record");
    } else if (auto bind = dyn_cast<BindOp>(operation)) {
      if (!llvm::is_contained({StringRef("port"), StringRef("resource"),
                               StringRef("export"), StringRef("pure_view")},
                              bind.getKind()))
        return bind.emitOpError("unknown closed typed binding kind");
      if (bind.getKind() == "port") {
        auto source = bind.getSource().getDefiningOp<PortOp>();
        auto target = bind.getTarget().getDefiningOp<PortOp>();
        DictionaryAttr sourceRecord =
            source ? findEndpoint(bindingForBase(source.getBase(), index),
                                  "ports", source.getAccessorAttr())
                   : DictionaryAttr();
        DictionaryAttr targetRecord =
            target ? findEndpoint(bindingForBase(target.getBase(), index),
                                  "ports", target.getAccessorAttr())
                   : DictionaryAttr();
        if (!sourceRecord || !targetRecord ||
            sourceRecord.getAs<StringAttr>("direction").getValue() !=
                "output" ||
            targetRecord.getAs<StringAttr>("direction").getValue() != "input" ||
            sourceRecord.get("cardinality") !=
                targetRecord.get("cardinality") ||
            sourceRecord.get("delegation") != targetRecord.get("delegation") ||
            sourceRecord.get("ownership") != targetRecord.get("ownership") ||
            sourceRecord.get("time_domain") != targetRecord.get("time_domain"))
          return bind.emitOpError("port binding must connect exact output and "
                                  "input endpoint records");
      } else if (bind.getKind() == "resource") {
        auto source = bind.getSource().getDefiningOp<ResourceOp>();
        auto target = bind.getTarget().getDefiningOp<ResourceOp>();
        DictionaryAttr sourceRecord =
            source ? findEndpoint(bindingForBase(source.getBase(), index),
                                  "resources", source.getAccessorAttr())
                   : DictionaryAttr();
        DictionaryAttr targetRecord =
            target ? findEndpoint(bindingForBase(target.getBase(), index),
                                  "resources", target.getAccessorAttr())
                   : DictionaryAttr();
        if (!sourceRecord || !targetRecord ||
            sourceRecord.getAs<StringAttr>("mode").getValue() != "initiator" ||
            targetRecord.getAs<StringAttr>("mode").getValue() != "target" ||
            sourceRecord.get("delegation") != targetRecord.get("delegation") ||
            sourceRecord.get("ownership") != targetRecord.get("ownership") ||
            sourceRecord.get("time_domain") != targetRecord.get("time_domain"))
          return bind.emitOpError("resource binding must connect exact "
                                  "initiator and target endpoint records");
      } else if (bind.getKind() == "pure_view") {
        auto target = bind.getTarget().getDefiningOp<InlineOp>();
        if (!target || !llvm::is_contained(target.getArgs(), bind.getSource()))
          return bind.emitOpError(
              "pure_view target must directly consume the source expression");
      } else if (bind.getSource().getType() != bind.getTarget().getType()) {
        return bind.emitOpError(
            "export binding endpoints must have exactly equal types");
      }
      std::pair<void *, void *> key{bind.getSource().getAsOpaquePointer(),
                                    bind.getTarget().getAsOpaquePointer()};
      if (!bindingPairs.insert(key).second)
        return bind.emitOpError(
            "each typed construction relation must lower exactly once");
    } else if (auto inlineOp = dyn_cast<InlineOp>(operation)) {
      FailureOr<Operation *> definition = requireReference<BindingOp>(
          index, inlineOp, inlineOp.getBindingAttr(), "inline binding");
      if (failed(definition) ||
          cast<BindingOp>(*definition).getEffect() != "pure" ||
          !isa<ExprType>(inlineOp.getResult().getType()) ||
          !isMemoryEffectFree(inlineOp))
        return inlineOp.emitOpError(
            "inline requires a pure binding and effect-free expr result");
    } else if (auto invoke = dyn_cast<InvokeOp>(operation)) {
      FailureOr<Operation *> definition = requireReference<BindingOp>(
          index, invoke, invoke.getBindingAttr(), "invoke binding");
      if (failed(definition))
        return failure();
      if (cast<BindingOp>(*definition).getEffect() != "stateful")
        return invoke.emitOpError("invoke requires a stateful binding");
      for (Type type : invoke.getResultTypes())
        if (!isa<ValueType, WakeType>(type))
          return invoke.emitOpError(
              "invoke results must be exact value or wake types");
    } else if (auto exportOp = dyn_cast<ExportOp>(operation)) {
      if (exportOp.getValue().getType() != exportOp.getResult().getType())
        return exportOp.emitOpError(
            "export result type must exactly preserve its internal value");
      const std::array<StringRef, 1> kinds = {"role"};
      if (failed(requireTypeKind(index, exportOp, exportOp.getRoleAttr(), kinds,
                                 "export role")))
        return failure();
    } else if (auto process = dyn_cast<ProcessOp>(operation)) {
      FailureOr<Operation *> definition = requireReference<BindingOp>(
          index, process, process.getBindingAttr(), "process binding");
      if (failed(definition) ||
          cast<BindingOp>(*definition).getEffect() != "stateful")
        return process.emitOpError(
            "process requires an exact stateful binding-lock record");
      for (Value capture : process.getCaptures())
        if (failed(verifyCaptureBoundary(process, capture, index)))
          return failure();
      if (failed(verifyProcess(process, index)))
        return failure();
    }
  }

  for (Operation &operation : model.getBody().front()) {
    auto module = dyn_cast<ModuleOp>(operation);
    if (!module)
      continue;
    if (module.getBody().empty() || module.getBody().front().empty() ||
        !isa<ReturnOp>(module.getBody().front().back()))
      return module.emitOpError("module must end in one acsim.return");
    SmallVector<ExportOp> exports;
    for (Operation &child : module.getBody().front())
      if (auto exportOp = dyn_cast<ExportOp>(child))
        exports.push_back(exportOp);
    if (module.getExports().size() != exports.size())
      return module.emitOpError(
          "module exports metadata must exactly cover ordered exports");
    for (auto [attribute, exportOp] :
         llvm::zip_equal(module.getExports(), exports)) {
      auto reference = dyn_cast<FlatSymbolRefAttr>(attribute);
      if (!reference || reference.getValue() != exportOp.getSymName())
        return module.emitOpError(
            "module export order must match acsim.export declaration order");
    }
    auto returnOp = cast<ReturnOp>(module.getBody().front().back());
    if (returnOp.getValues().size() != exports.size())
      return returnOp.emitOpError(
          "return values must exactly match ordered module exports");
    for (auto [value, exportOp] :
         llvm::zip_equal(returnOp.getValues(), exports))
      if (value != exportOp.getResult())
        return returnOp.emitOpError(
            "return values must be the exact ordered export results");
  }
  return success();
}

struct RuntimeRow {
  std::string target;
  SmallVector<int64_t> indices;
  int64_t activationId = -1;
  Operation *placement = nullptr;
};

LogicalResult verifyDispatchAndActivation(ModelOp model,
                                          const ModelIndex &index) {
  llvm::DenseMap<int64_t, RuntimeRow> objects;
  llvm::DenseMap<int64_t, int64_t> activations;
  int64_t nextObjectId = 0;
  int64_t nextActivationId = 0;

  auto addRuntime = [&](Operation *operation, int64_t objectId,
                        int64_t activationId,
                        ArrayRef<int64_t> indices) -> LogicalResult {
    if (objectId < 0 || activationId < 0)
      return operation->emitOpError(
          "object and activation IDs must be non-negative");
    if (objectId != nextObjectId)
      return operation->emitOpError(
          "runtime object IDs must equal canonical ownership preorder");
    if (activationId != nextActivationId)
      return operation->emitOpError(
          "activation-source IDs must equal canonical ownership preorder");
    RuntimeRow row{definitionKey(operation), SmallVector<int64_t>(indices),
                   activationId, operation};
    if (modelVerificationWorkCollector)
      ++modelVerificationWorkCollector->expandedRuntimeRows;
    if (!objects.try_emplace(objectId, std::move(row)).second)
      return operation->emitOpError("duplicate runtime object ID");
    if (!activations.try_emplace(activationId, objectId).second)
      return operation->emitOpError("duplicate activation-source ID");
    ++nextObjectId;
    ++nextActivationId;
    return success();
  };

  if (modelVerificationWorkCollector)
    modelVerificationWorkCollector->runtimeOperationVisits +=
        index.ordered.size();
  for (Operation *operation : index.ownedPreorder) {
    if (auto instance = dyn_cast<InstanceOp>(operation)) {
      if (failed(addRuntime(instance, instance.getObjectId(),
                            instance.getActivationId(), {})))
        return failure();
    } else if (auto process = dyn_cast<ProcessOp>(operation)) {
      if (failed(addRuntime(process, process.getObjectId(),
                            process.getActivationId(), {})))
        return failure();
    } else if (auto array = dyn_cast<ArrayOp>(operation)) {
      uint64_t volume = arrayVolume(array.getShape());
      for (uint64_t ordinal = 0; ordinal < volume; ++ordinal) {
        SmallVector<int64_t> indices =
            lexicographicIndices(array.getShape(), ordinal);
        if (failed(addRuntime(array, array.getObjectIds()[ordinal],
                              array.getActivationIds()[ordinal], indices)))
          return failure();
      }
    }
  }
  if (objects.size() > kMaxExpandedObjects)
    return model.emitOpError() << "expanded object count exceeds ACSim v0.1 "
                                  "capability "
                               << kMaxExpandedObjects;
  for (int64_t id = 0, end = static_cast<int64_t>(objects.size()); id < end;
       ++id)
    if (!objects.contains(id))
      return model.emitOpError(
          "runtime object IDs must be dense canonical ownership preorder");
  for (int64_t id = 0, end = static_cast<int64_t>(activations.size()); id < end;
       ++id)
    if (!activations.contains(id))
      return model.emitOpError("activation-source IDs must be dense");

  llvm::DenseMap<int64_t, DispatchOp> dispatchByObject;
  for (Operation &operation : model.getBody().front()) {
    if (modelVerificationWorkCollector)
      ++modelVerificationWorkCollector->runtimeOperationVisits;
    auto dispatch = dyn_cast<DispatchOp>(operation);
    if (!dispatch)
      continue;
    int64_t id = dispatch.getObjectId();
    auto found = objects.find(id);
    if (found == objects.end())
      return dispatch.emitOpError(
          "dispatch object ID has no owned runtime object");
    std::string target = symbolKey(dispatch.getTargetAttr());
    if (target != found->second.target ||
        dispatch.getIndices() != ArrayRef<int64_t>(found->second.indices) ||
        dispatch.getActivationId() != found->second.activationId)
      return dispatch.emitOpError(
          "dispatch target, indices, and IDs must match canonical ownership");
    if (!dispatchByObject.try_emplace(id, dispatch).second)
      return dispatch.emitOpError(
          "runtime object has more than one dispatch row");
    FlatSymbolRefAttr bindingReference;
    if (auto instance = dyn_cast<InstanceOp>(found->second.placement))
      bindingReference = instance.getBindingAttr();
    else if (auto array = dyn_cast<ArrayOp>(found->second.placement))
      bindingReference = array.getBindingAttr();
    else if (auto process = dyn_cast<ProcessOp>(found->second.placement))
      bindingReference = process.getBindingAttr();
    auto binding = dyn_cast_or_null<BindingOp>(
        index.definitions.lookup(symbolKey(bindingReference)));
    auto entryPoints =
        binding ? binding.getCppRecord().getAs<DictionaryAttr>("entry_points")
                : DictionaryAttr();
    if (!entryPoints ||
        dispatch.getWorkAttr() != entryPoints.getAs<StringAttr>("work") ||
        dispatch.getXferAttr() != entryPoints.getAs<StringAttr>("xfer") ||
        dispatch.getResetAttr() != entryPoints.getAs<StringAttr>("reset") ||
        dispatch.getValidateAttr() != entryPoints.getAs<StringAttr>("validate"))
      return dispatch.emitOpError(
          "dispatch thunks must exactly match the placement binding lock");
  }
  if (dispatchByObject.size() != objects.size())
    return model.emitOpError(
        "every runtime object requires exactly one typed dispatch row");

  llvm::DenseMap<Operation *, SmallVector<int64_t>> placementObjects;
  for (auto &[id, row] : objects)
    placementObjects[row.placement].push_back(id);
  for (auto &entry : placementObjects)
    llvm::sort(entry.second);

  std::function<std::set<int64_t>(Value, llvm::SmallPtrSetImpl<void *> &)>
      idsForValue;
  idsForValue =
      [&](Value value,
          llvm::SmallPtrSetImpl<void *> &active) -> std::set<int64_t> {
    void *key = value.getAsOpaquePointer();
    if (!active.insert(key).second)
      return {};
    std::set<int64_t> result;
    if (auto argument = dyn_cast<BlockArgument>(value)) {
      if (auto process =
              dyn_cast<ProcessOp>(argument.getOwner()->getParentOp())) {
        if (argument.getArgNumber() < process.getCaptures().size()) {
          auto nested = idsForValue(
              process.getCaptures()[argument.getArgNumber()], active);
          result.insert(nested.begin(), nested.end());
        }
      }
    } else if (Operation *definition = value.getDefiningOp()) {
      if (auto instance = dyn_cast<InstanceOp>(definition)) {
        result.insert(instance.getObjectId());
      } else if (auto element = dyn_cast<ElementOp>(definition)) {
        auto array = element.getArray().getDefiningOp<ArrayOp>();
        if (array) {
          uint64_t ordinal = 0;
          for (auto [indexValue, extent] :
               llvm::zip_equal(element.getIndices(), array.getShape()))
            ordinal = ordinal * static_cast<uint64_t>(extent) +
                      static_cast<uint64_t>(indexValue);
          if (ordinal < array.getObjectIds().size())
            result.insert(array.getObjectIds()[ordinal]);
        }
      } else if (auto process = dyn_cast<ProcessOp>(definition)) {
        result.insert(process.getObjectId());
      } else {
        for (Value operand : definition->getOperands()) {
          auto nested = idsForValue(operand, active);
          result.insert(nested.begin(), nested.end());
        }
      }
    }
    active.erase(key);
    return result;
  };
  auto collectIds = [&](Value value) {
    llvm::SmallPtrSet<void *, 16> active;
    return idsForValue(value, active);
  };

  std::set<std::pair<int64_t, int64_t>> expected;
  for (auto &[id, row] : objects)
    expected.insert({row.activationId, id});
  for (Operation *operation : index.ordered) {
    if (auto bind = dyn_cast<BindOp>(operation)) {
      for (int64_t source : collectIds(bind.getSource()))
        for (int64_t target : collectIds(bind.getTarget()))
          expected.insert({objects.lookup(source).activationId, target});
    } else if (auto process = dyn_cast<ProcessOp>(operation)) {
      for (Value capture : process.getCaptures())
        for (int64_t source : collectIds(capture))
          expected.insert(
              {objects.lookup(source).activationId, process.getObjectId()});
    } else if (auto invoke = dyn_cast<InvokeOp>(operation)) {
      if (auto process = invoke->getParentOfType<ProcessOp>())
        for (Value argument : invoke.getArgs())
          for (int64_t source : collectIds(argument))
            expected.insert(
                {objects.lookup(source).activationId, process.getObjectId()});
    }
  }

  std::pair<int64_t, int64_t> previous{-1, -1};
  std::set<std::pair<int64_t, int64_t>> actual;
  for (Operation &operation : model.getBody().front()) {
    if (modelVerificationWorkCollector)
      ++modelVerificationWorkCollector->runtimeOperationVisits;
    auto activate = dyn_cast<ActivateOp>(operation);
    if (!activate)
      continue;
    auto sourceDispatch = activate.getSource().getDefiningOp<DispatchOp>();
    auto targetDispatch = activate.getTarget().getDefiningOp<DispatchOp>();
    if (!sourceDispatch || !targetDispatch ||
        activate.getSource() != sourceDispatch.getActivation() ||
        activate.getTarget() != targetDispatch.getObject())
      return activate.emitOpError(
          "activation edge must consume dispatch-produced typed IDs");
    std::pair<int64_t, int64_t> edge{sourceDispatch.getActivationId(),
                                     targetDispatch.getObjectId()};
    if (edge <= previous)
      return activate.emitOpError(
          "activation edges must be deduplicated and sorted by source,target");
    previous = edge;
    actual.insert(edge);
  }
  if (actual != expected)
    return model.emitOpError(
        "activation edges must exactly equal computed static dependencies");
  return success();
}

} // namespace

ParseResult ProcessOp::parse(OpAsmParser &parser, OperationState &result) {
  Builder &builder = parser.getBuilder();
  StringAttr name;
  FlatSymbolRefAttr binding;
  SmallVector<OpAsmParser::UnresolvedOperand> captures;
  SmallVector<Type> captureTypes;
  ArrayAttr captureNames;
  FlatSymbolRefAttr owner;
  StringAttr path;
  FlatSymbolRefAttr entryPc;
  ArrayAttr pcs;
  ArrayAttr liveSlots;
  int64_t objectId;
  int64_t activationId;
  int64_t fairnessCap;
  StringAttr specializationFingerprint;

  if (parser.parseSymbolName(name, SymbolTable::getSymbolAttrName(),
                             result.attributes) ||
      parser.parseKeyword("binding") || parser.parseAttribute(binding) ||
      parser.parseKeyword("captures") || parser.parseLParen())
    return failure();
  if (failed(parser.parseOptionalRParen())) {
    do {
      captures.emplace_back();
      Type type;
      if (parser.parseOperand(captures.back()) || parser.parseColonType(type))
        return failure();
      captureTypes.push_back(type);
    } while (succeeded(parser.parseOptionalComma()));
    if (parser.parseRParen())
      return failure();
  }
  if (parser.resolveOperands(captures, captureTypes,
                             parser.getCurrentLocation(), result.operands) ||
      parser.parseKeyword("names") || parser.parseAttribute(captureNames) ||
      parser.parseKeyword("owner") || parser.parseAttribute(owner) ||
      parser.parseKeyword("path") || parser.parseAttribute(path) ||
      parser.parseKeyword("object") || parser.parseInteger(objectId) ||
      parser.parseKeyword("activation") || parser.parseInteger(activationId) ||
      parser.parseKeyword("entry") || parser.parseAttribute(entryPc) ||
      parser.parseKeyword("pcs") || parser.parseAttribute(pcs) ||
      parser.parseKeyword("live") || parser.parseAttribute(liveSlots) ||
      parser.parseKeyword("fairness") || parser.parseInteger(fairnessCap) ||
      parser.parseKeyword("specialization") ||
      parser.parseAttribute(specializationFingerprint))
    return failure();

  result.addAttribute("binding", binding);
  result.addAttribute("capture_names", captureNames);
  result.addAttribute("owner", owner);
  result.addAttribute("path", path);
  result.addAttribute("object_id", builder.getI64IntegerAttr(objectId));
  result.addAttribute("activation_id", builder.getI64IntegerAttr(activationId));
  result.addAttribute("entry_pc", entryPc);
  result.addAttribute("pcs", pcs);
  result.addAttribute("live_slots", liveSlots);
  result.addAttribute("fairness_cap", builder.getI64IntegerAttr(fairnessCap));
  result.addAttribute("specialization_fingerprint", specializationFingerprint);
  if (parser.parseOptionalAttrDictWithKeyword(result.attributes) ||
      parser.parseLBrace())
    return failure();

  for (Attribute attribute : pcs) {
    auto expected = dyn_cast<FlatSymbolRefAttr>(attribute);
    FlatSymbolRefAttr label;
    if (!expected || parser.parseKeyword("state") ||
        parser.parseAttribute(label))
      return parser.emitError(parser.getCurrentLocation(),
                              "expected one flat state label per PC");
    if (label != expected)
      return parser.emitError(parser.getCurrentLocation())
             << "state label " << label << " does not match ordered PC "
             << expected;
    Region *state = result.addRegion();
    if (parser.parseRegion(*state))
      return failure();
  }
  return parser.parseRBrace();
}

void ProcessOp::print(OpAsmPrinter &printer) {
  printer << ' ';
  printer.printSymbolName(getSymName());
  printer << " binding " << getBindingAttr() << " captures(";
  llvm::interleaveComma(getCaptures(), printer, [&](Value capture) {
    printer << capture << " : " << capture.getType();
  });
  printer << ") names " << getCaptureNamesAttr() << " owner " << getOwnerAttr()
          << " path " << getPathAttr() << " object " << getObjectId()
          << " activation " << getActivationId() << " entry "
          << getEntryPcAttr() << " pcs " << getPcsAttr() << " live "
          << getLiveSlotsAttr() << " fairness " << getFairnessCap()
          << " specialization " << getSpecializationFingerprintAttr();
  printer.printOptionalAttrDictWithKeyword(
      (*this)->getAttrs(),
      {SymbolTable::getSymbolAttrName(), "binding", "capture_names", "owner",
       "path", "object_id", "activation_id", "entry_pc", "pcs", "live_slots",
       "fairness_cap", "specialization_fingerprint"});
  printer << " {";
  for (auto [pc, state] : llvm::zip(getPcs(), getStates())) {
    printer << "\nstate " << pc << ' ';
    printer.printRegion(state, /*printEntryBlockArgs=*/true,
                        /*printBlockTerminators=*/true,
                        /*printEmptyBlock=*/true);
  }
  printer << "\n}";
}

LogicalResult ModelOp::verify() {
  if (getContractEpoch() != "0.1")
    return emitOpError("contract epoch must be exactly \"0.1\"");
  auto parentModule = dyn_cast_or_null<mlir::ModuleOp>((*this)->getParentOp());
  if (!parentModule)
    return emitOpError("acsim.model must be directly inside builtin.module");
  unsigned models = 0;
  for (Operation &operation : *parentModule.getBody())
    models += isa<ModelOp>(operation);
  if (models != 1)
    return emitOpError("canonical ACSim requires exactly one acsim.model");
  if (parentModule.getBody()->getOperations().size() != 1)
    return emitOpError(
        "acsim.model must be the sole operation in the canonical file");
  if (failed(verifyModelFingerprints(*this)))
    return failure();
  if (getBody().empty())
    return emitOpError("model requires one closed body block");

  if (failed(preflightModel(*this)))
    return failure();
  ModelIndex index;
  if (failed(buildIndex(*this, index)) ||
      failed(verifyClosedLegality(*this, index.ordered)) ||
      failed(verifyDeterministicOrder(*this)) ||
      failed(verifyConstructionOrder(*this, index)) ||
      failed(verifyModulesAndTypedGraph(*this, index)) ||
      failed(verifyDispatchAndActivation(*this, index)))
    return failure();
  return success();
}

LogicalResult TypeOp::verify() {
  constexpr std::array<StringLiteral, 13> kinds = {
      "accessor", "implementation", "interface", "packet", "policy",
      "protocol", "provider",       "resource",  "role",   "schema",
      "value",    "wake",           "payload"};
  if (!llvm::is_contained(kinds, getKind()))
    return emitOpError("kind is not a closed ACSim C++ realization kind");
  if (getCppName().empty() || hasRawCppFragment(getCppName()))
    return emitOpError(
        "C++ spelling must be a non-empty declarative symbol/type spelling");
  return verifyFingerprint(*this, getFingerprintAttr());
}

LogicalResult BindingOp::verify() { return verifyBindingLockShape(*this); }

LogicalResult ModuleOp::verify() {
  if (getPath().empty())
    return emitOpError("module path must be non-empty");
  if (failed(verifyFingerprint(*this, getSpecializationFingerprintAttr(),
                               "specialization fingerprint")))
    return failure();
  if (getBody().empty() || getBody().front().empty() ||
      !isa<ReturnOp>(getBody().front().back()))
    return emitOpError("module must end in acsim.return");
  return success();
}

LogicalResult InstanceOp::verify() {
  if (getObjectId() < 0 || getActivationId() < 0 || getPath().empty())
    return emitOpError(
        "instance requires non-negative IDs and a non-empty path");
  if (failed(verifyFingerprint(*this, getSpecializationFingerprintAttr(),
                               "specialization fingerprint")))
    return failure();
  auto owner = dyn_cast<OwnerType>(getResult().getType());
  if (!owner || owner.getSymbol() != getBindingAttr())
    return emitOpError("result must be owner of the exact binding");
  return success();
}

LogicalResult ArrayOp::verify() {
  if (failed(verifyFingerprint(*this, getSpecializationFingerprintAttr(),
                               "specialization fingerprint")))
    return failure();
  auto type = dyn_cast<ArrayType>(getResult().getType());
  if (!type || type.getShape().asArrayRef() != getShape())
    return emitOpError("result array type must exactly match shape metadata");
  uint64_t volume = arrayVolume(getShape());
  if (getObjectIds().size() != volume || getActivationIds().size() != volume)
    return emitOpError("object and activation IDs must cover array volume");
  return success();
}

LogicalResult ElementOp::verify() {
  auto array = dyn_cast<ArrayType>(getArray().getType());
  if (!array || getIndices().size() != array.getShape().size())
    return emitOpError("indices must have the exact static array rank");
  return success();
}

LogicalResult PortOp::verify() {
  if (isa<OwnerType, RefType>(getBase().getType()) &&
      isa<PortType>(getResult().getType()))
    return success();
  return emitOpError("requires owner/ref input and typed port result");
}

LogicalResult ResourceOp::verify() {
  if (isa<OwnerType, RefType>(getBase().getType()) &&
      isa<ResourceType>(getResult().getType()))
    return success();
  return emitOpError("requires owner/ref input and resource result");
}

LogicalResult BindOp::verify() {
  if (getKind() == "port") {
    auto source = dyn_cast<PortType>(getSource().getType());
    auto target = dyn_cast<PortType>(getTarget().getType());
    if (source && target && source.getInterface() == target.getInterface() &&
        source.getPayload() == target.getPayload() &&
        source.getProtocol() == target.getProtocol() &&
        source.getRole() != target.getRole())
      return success();
  } else if (getKind() == "resource") {
    auto source = dyn_cast<ResourceType>(getSource().getType());
    auto target = dyn_cast<ResourceType>(getTarget().getType());
    if (source && target && source.getResource() == target.getResource() &&
        source.getRole() != target.getRole())
      return success();
  } else if (getKind() == "pure_view") {
    if (getSource() != getTarget() &&
        getSource().getType() == getTarget().getType() &&
        isa<ExprType>(getSource().getType()))
      return success();
  }
  return emitOpError("typed binding endpoints are not exactly compatible");
}

LogicalResult InlineOp::verify() {
  if (!isa<ExprType>(getResult().getType()) || !isMemoryEffectFree(*this))
    return emitOpError("inline must be effect-free and produce expr");
  return success();
}

LogicalResult ProcessOp::verify() {
  if (getObjectId() < 0 || getActivationId() < 0 || getPath().empty())
    return emitOpError(
        "process requires non-negative IDs and a non-empty path");
  return verifyFingerprint(*this, getSpecializationFingerprintAttr(),
                           "specialization fingerprint");
}

LogicalResult LiveLoadOp::verify() {
  return isa<ValueType>(getResult().getType())
             ? success()
             : emitOpError("live load must produce a typed value");
}

LogicalResult LiveStoreOp::verify() {
  return isa<ValueType>(getValue().getType())
             ? success()
             : emitOpError("live store requires a typed value");
}

LogicalResult InvokeOp::verify() {
  for (Type type : getResultTypes())
    if (!isa<ValueType, WakeType>(type))
      return emitOpError("results must be typed values or wakes");
  return success();
}

LogicalResult ContinueOp::verify() { return success(); }

LogicalResult SuspendOp::verify() {
  return isa<WakeType>(getWake().getType())
             ? success()
             : emitOpError("requires an exact typed wake");
}

LogicalResult TerminateOp::verify() {
  if (getStatus() != "success" && getStatus() != "failure")
    return emitOpError("status must be exactly 'success' or 'failure'");
  return success();
}

LogicalResult ExportOp::verify() {
  if (getValue().getType() != getResult().getType())
    return emitOpError("must preserve the exact exported type");
  if (isa<OwnerType, ObjectIdType, ActivationIdType, PcType, WakeType>(
          getResult().getType()))
    return emitOpError("owners, IDs, PCs, and wakes cannot escape a module");
  return success();
}

LogicalResult DispatchOp::verify() {
  if (getObjectId() < 0 || getActivationId() < 0)
    return emitOpError("object and activation IDs must be non-negative");
  for (StringRef thunk : {getWork(), getXfer(), getReset(), getValidate()})
    if (!isCppQualifiedSymbol(thunk))
      return emitOpError(
          "dispatch thunks must be non-empty declarative C++ symbols");
  return success();
}

LogicalResult ActivateOp::verify() { return success(); }

LogicalResult ReturnOp::verify() {
  if (!isa_and_nonnull<ModuleOp>((*this)->getParentOp()))
    return emitOpError("must terminate an acsim.module body");
  return success();
}

LogicalResult verifyCanonicalACSimFile(mlir::ModuleOp module) {
  unsigned models = 0;
  bool hasACSimOperation = false;
  ModelOp model;
  SmallVector<Operation *> stack;
  for (Operation &operation : llvm::reverse(*module.getBody()))
    stack.push_back(&operation);
  while (!stack.empty()) {
    Operation *operation = stack.pop_back_val();
    if (operation->getName().getDialectNamespace() == "acsim")
      hasACSimOperation = true;
    if (auto candidate = dyn_cast<ModelOp>(operation)) {
      ++models;
      model = candidate;
    }
    for (Region &region : llvm::reverse(operation->getRegions()))
      for (Block &block : llvm::reverse(region))
        for (Operation &child : llvm::reverse(block))
          stack.push_back(&child);
  }
  if (!hasACSimOperation)
    return success();
  if (models != 1)
    return module.emitError("canonical ACSim requires exactly one acsim.model");
  if (model->getParentOp() != module)
    return model.emitOpError(
        "canonical acsim.model must be directly inside the file module");
  auto epoch = module->getAttrOfType<StringAttr>("ac.contract_epoch");
  auto discardable = module->getDiscardableAttrs();
  if (!epoch || epoch.getValue() != "0.1" ||
      std::distance(discardable.begin(), discardable.end()) != 1)
    return module.emitError("canonical ACSim file attributes must be exactly "
                            "ac.contract_epoch = \"0.1\"");
  return success();
}

} // namespace acir::acsim

#define GET_OP_CLASSES
#include "acir/Dialect/ACSim/ACSimOps.cpp.inc"
