// Atomic ACIR-to-ACSim whole-model lowering (ac-lower-to-acsim).
//
// Converts one frozen, verified ACIR file into one canonical acsim.model in a
// single transaction:
//   ac.module (concrete, () -> ())  -> acsim.module with ownership placements
//   ac.instance / ac.instances      -> acsim.instance (one per named member)
//   ac.array (homogeneous)          -> acsim.array
//   ac.module.extern                -> acsim.binding from the in-memory exact
//                                      binding resolution (no lock round-trip)
//   ac.process (yield-only plan)    -> acsim.process enum-PC state machine
//   selected ac.system              -> acsim.model with exact fingerprints,
//                                      canonical construction/destruction
//                                      order, dispatch rows, and self
//                                      activation edges
//
// Every validation failure is diagnosed with an ACLOWER-* code before any IR
// mutation, so a rejected input never publishes a partial acsim.model.
#include "acir/Conversion/ACIRToACSim/ACIRToACSim.h"

#include "Analysis/ProcessStatePlanInternal.h"
#include "acir/Analysis/ProcessStatePlan.h"
#include "acir/Bindings/Binding.h"
#include "acir/Dialect/ACIR/ACIROps.h"
#include "acir/Dialect/ACSim/ACSimDialect.h"
#include "acir/Dialect/ACSim/ACSimOps.h"
#include "acir/Dialect/ACSim/ACSimTypes.h"
#include "acir/Transforms/ResolveBindings.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace mlir;

namespace acir {
namespace {

constexpr llvm::StringLiteral kEpoch = "0.1";
constexpr uint64_t kMaxExpandedRows = 1U << 20;

InFlightDiagnostic lowerError(Operation *op, llvm::StringRef code,
                              const llvm::Twine &message) {
  return op->emitError() << code << ": " << message;
}

// ---------------------------------------------------------------------------
// Canonical static values: MLIR attributes <-> RFC 8785 JSON
// ---------------------------------------------------------------------------

/// Convert a frozen ACIR static attribute to its canonical JSON value. The
/// accepted domain mirrors the ac-resolve-gfsim-bindings normalizer.
llvm::Expected<llvm::json::Value> staticValueToJson(Attribute attribute) {
  auto unsupported = [&]() {
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "ACLOWER-PARAM-PHASE: unsupported static attribute kind");
  };
  if (auto boolean = dyn_cast<BoolAttr>(attribute))
    return llvm::json::Value(boolean.getValue());
  if (auto integer = dyn_cast<IntegerAttr>(attribute)) {
    const llvm::APInt &value = integer.getValue();
    if (!value.isSignedIntN(64))
      return unsupported();
    return llvm::json::Value(value.getSExtValue());
  }
  if (auto floating = dyn_cast<FloatAttr>(attribute)) {
    double value = floating.getValueAsDouble();
    if (!std::isfinite(value) || (std::signbit(value) && value == 0.0))
      return unsupported();
    return llvm::json::Value(value);
  }
  if (auto string = dyn_cast<StringAttr>(attribute))
    return llvm::json::Value(string.getValue());
  if (auto array = dyn_cast<ArrayAttr>(attribute)) {
    llvm::json::Array values;
    for (Attribute element : array) {
      auto converted = staticValueToJson(element);
      if (!converted)
        return converted.takeError();
      values.push_back(std::move(*converted));
    }
    return llvm::json::Value(std::move(values));
  }
  if (auto dictionary = dyn_cast<DictionaryAttr>(attribute)) {
    llvm::json::Object values;
    for (NamedAttribute named : dictionary) {
      auto converted = staticValueToJson(named.getValue());
      if (!converted)
        return converted.takeError();
      values[named.getName().getValue()] = std::move(*converted);
    }
    return llvm::json::Value(std::move(values));
  }
  if (isa<TypeAttr, SymbolRefAttr>(attribute)) {
    std::string printed;
    llvm::raw_string_ostream output(printed);
    output << attribute;
    return llvm::json::Value(output.str());
  }
  return unsupported();
}

/// Convert a binding-lock JSON static value back to a canonical MLIR
/// attribute. Returns a null attribute for values outside the closed domain.
Attribute jsonToStaticAttribute(OpBuilder &builder,
                                const llvm::json::Value &value) {
  switch (value.kind()) {
  case llvm::json::Value::Boolean:
    return builder.getBoolAttr(*value.getAsBoolean());
  case llvm::json::Value::Number:
    if (auto integer = value.getAsInteger())
      return builder.getI64IntegerAttr(*integer);
    if (auto number = value.getAsNumber())
      return builder.getF64FloatAttr(*number);
    return Attribute();
  case llvm::json::Value::String:
    return builder.getStringAttr(*value.getAsString());
  case llvm::json::Value::Array: {
    llvm::SmallVector<Attribute> elements;
    for (const llvm::json::Value &element : *value.getAsArray()) {
      Attribute converted = jsonToStaticAttribute(builder, element);
      if (!converted)
        return Attribute();
      elements.push_back(converted);
    }
    return builder.getArrayAttr(elements);
  }
  case llvm::json::Value::Object: {
    llvm::SmallVector<NamedAttribute> members;
    for (const auto &member : *value.getAsObject()) {
      Attribute converted = jsonToStaticAttribute(builder, member.second);
      if (!converted)
        return Attribute();
      members.push_back(
          builder.getNamedAttr(member.first, converted));
    }
    return builder.getDictionaryAttr(members);
  }
  case llvm::json::Value::Null:
    return Attribute();
  }
  return Attribute();
}

/// Fingerprint a canonical JSON descriptor with the shared RFC 8785 + SHA-256
/// recipe used across the binding infrastructure.
std::string fingerprintJson(const llvm::json::Value &value) {
  auto canonical = bindings::canonicalizeJson(value);
  if (!canonical) {
    llvm::consumeError(canonical.takeError());
    return {};
  }
  return bindings::sha256Fingerprint(*canonical);
}

// ---------------------------------------------------------------------------
// acsim.type symbol table
// ---------------------------------------------------------------------------

struct TypeDeclaration {
  std::string identity;
  std::string symbol;
  std::string cpp;
  std::string kind;
  std::string fingerprint;
};

/// Assigns deterministic canonical symbols and fingerprints to every C++
/// realization identity referenced by binding records or generated process
/// helpers. Identities are interned in sorted order so symbol assignment is
/// independent of discovery order.
class TypeSymbolTable {
public:
  /// Intern one identity. `fingerprint` may be empty, in which case the
  /// fingerprint is the SHA-256 of the identity itself.
  mlir::LogicalResult intern(Operation *reporter, llvm::StringRef identity,
                             llvm::StringRef kind, llvm::StringRef cpp,
                             llvm::StringRef fingerprint = llvm::StringRef()) {
    auto found = entries.find(identity.str());
    if (found != entries.end()) {
      TypeDeclaration &existing = found->second;
      if (existing.kind != kind)
        return lowerError(reporter, "ACLOWER-TYPE-MISMATCH",
                          "realization identity '" + identity +
                              "' is used with conflicting acsim.type kinds '" +
                              existing.kind + "' and '" + kind + "'");
      if (!fingerprint.empty() && existing.fingerprint != fingerprint)
        return lowerError(
            reporter, "ACLOWER-FINGERPRINT",
            "realization identity '" + identity +
                "' carries conflicting fingerprints across binding records");
      return mlir::success();
    }
    TypeDeclaration declaration;
    declaration.identity = identity.str();
    declaration.kind = kind.str();
    declaration.cpp = cpp.str();
    declaration.fingerprint = fingerprint.empty()
                                  ? bindings::sha256Fingerprint(identity)
                                  : fingerprint.str();
    entries.emplace(declaration.identity, std::move(declaration));
    return mlir::success();
  }

  /// Resolve symbols after all identities are interned.
  mlir::LogicalResult finalize(Operation *reporter) {
    llvm::StringMap<std::string> ownerBySymbol;
    for (auto &[identity, declaration] : entries) {
      std::string base = sanitize(declaration.identity);
      std::string symbol = base;
      for (unsigned suffix = 2; ownerBySymbol.count(symbol); ++suffix)
        symbol = base + "_" + std::to_string(suffix);
      ownerBySymbol.try_emplace(symbol, declaration.identity);
      declaration.symbol = symbol;
    }
    ordered.clear();
    for (auto &[identity, declaration] : entries)
      ordered.push_back(&declaration);
    llvm::sort(ordered, [](const TypeDeclaration *left,
                           const TypeDeclaration *right) {
      return left->symbol < right->symbol;
    });
    for (const TypeDeclaration *declaration : ordered)
      if (declaration->symbol.empty())
        return lowerError(reporter, "ACLOWER-FINGERPRINT",
                          "realization identity '" + declaration->identity +
                              "' has no canonical symbol");
    return mlir::success();
  }

  llvm::StringRef symbolFor(llvm::StringRef identity) const {
    auto found = entries.find(identity.str());
    return found == entries.end() ? llvm::StringRef()
                                  : llvm::StringRef(found->second.symbol);
  }

  llvm::ArrayRef<const TypeDeclaration *> declarations() const {
    return ordered;
  }

private:
  static std::string sanitize(llvm::StringRef identity) {
    std::string symbol;
    symbol.reserve(identity.size());
    for (char character : identity)
      symbol.push_back(llvm::isAlnum(character) || character == '_'
                           ? character
                           : '_');
    if (symbol.empty() || llvm::isDigit(symbol.front()))
      symbol.insert(symbol.begin(), '_');
    return symbol;
  }

  std::map<std::string, TypeDeclaration> entries;
  llvm::SmallVector<const TypeDeclaration *> ordered;
};

// ---------------------------------------------------------------------------
// Binding record conversion
// ---------------------------------------------------------------------------

/// Build the exact 20-field acsim.binding record dictionary from a typed
/// binding-lock record, mapping realization identities to canonical symbols.
mlir::Attribute convertBindingRecord(OpBuilder &builder,
                                     const bindings::BindingRecord &record,
                                     const TypeSymbolTable &types) {
  MLIRContext *context = builder.getContext();
  auto string = [&](llvm::StringRef value) { return builder.getStringAttr(value); };
  auto reference = [&](llvm::StringRef identity) {
    return FlatSymbolRefAttr::get(context, types.symbolFor(identity));
  };
  auto dictionary =
      [&](llvm::ArrayRef<NamedAttribute> members) -> DictionaryAttr {
    return builder.getDictionaryAttr(members);
  };
  auto named = [&](llvm::StringRef key,
                   Attribute value) { return builder.getNamedAttr(key, value); };

  llvm::SmallVector<Attribute> activationSources;
  for (const bindings::ActivationSourceBinding &source :
       record.activationSources())
    activationSources.push_back(dictionary(
        {named("kind", reference(source.kind)), named("name", string(source.name))}));

  llvm::SmallVector<Attribute> constructionArguments;
  for (const llvm::json::Value &argument : record.construction().arguments)
    constructionArguments.push_back(jsonToStaticAttribute(builder, argument));

  const bindings::CppBinding &cpp = record.cpp();
  DictionaryAttr entryPoints = dictionary(
      {named("pure", string(cpp.entryPoints.pure)),
       named("reset", string(cpp.entryPoints.reset)),
       named("validate", string(cpp.entryPoints.validate)),
       named("work", string(cpp.entryPoints.work)),
       named("xfer", string(cpp.entryPoints.xfer))});
  DictionaryAttr cppRecord = dictionary(
      {named("concept", string(cpp.conceptName)),
       named("entry_points", entryPoints), named("header", string(cpp.header)),
       named("symbol", string(cpp.symbol)), named("target", string(cpp.target))});

  DictionaryAttr construction = dictionary(
      {named("arguments", builder.getArrayAttr(constructionArguments)),
       named("kind", string(record.construction().kind))});
  DictionaryAttr ownership =
      dictionary({named("kind", string(record.ownership().kind)),
                  named("placement", string(record.ownership().placement))});

  llvm::SmallVector<Attribute> parameters;
  for (const bindings::ParameterBinding &parameter : record.parameters())
    parameters.push_back(dictionary(
        {named("acir_type", string(parameter.acirType)),
         named("cpp_type", string(parameter.cppType)),
         named("mapping", string(parameter.mapping)),
         named("name", string(parameter.name)),
         named("ordinal", builder.getI64IntegerAttr(parameter.ordinal)),
         named("value", jsonToStaticAttribute(builder, parameter.value))}));

  llvm::SmallVector<Attribute> ports;
  for (const bindings::PortBinding &port : record.ports())
    ports.push_back(dictionary(
        {named("accessor", reference(port.accessor)),
         named("cardinality", string(port.cardinality)),
         named("delegation", string(port.delegation)),
         named("direction", string(port.direction)),
         named("interface", reference(port.interface)),
         named("ownership", string(port.ownership)),
         named("payload", reference(port.payload)),
         named("protocol", reference(port.protocol)),
         named("role", reference(port.role)),
         named("time_domain", reference(port.timeDomain))}));

  llvm::SmallVector<Attribute> resources;
  for (const bindings::ResourceBinding &resource : record.resources())
    resources.push_back(dictionary(
        {named("accessor", reference(resource.accessor)),
         named("delegation", string(resource.delegation)),
         named("mode", string(resource.mode)),
         named("ownership", string(resource.ownership)),
         named("resource", reference(resource.resource)),
         named("role", reference(resource.role)),
         named("time_domain", reference(resource.timeDomain))}));

  llvm::SmallVector<Attribute> results;
  for (const bindings::ResultBinding &result : record.results())
    results.push_back(dictionary({named("cpp_type", reference(result.cppType)),
                                  named("name", string(result.name))}));

  return dictionary(
      {named("activation_sources", builder.getArrayAttr(activationSources)),
       named("availability", string(record.availability())),
       named("binding", string(record.binding())),
       named("binding_schema", string(record.bindingSchema())),
       named("component_schema", reference(record.componentSchema())),
       named("component_schema_fingerprint",
             string(record.componentSchemaFingerprint())),
       named("construction", construction),
       named("contract_epoch", string(record.contractEpoch())),
       named("cpp", cppRecord), named("cpp_type", reference(record.cppType())),
       named("effect", string(record.effect())),
       named("fingerprint", string(record.fingerprint())),
       named("implementation", reference(record.implementation())),
       named("ownership", ownership),
       named("parameters", builder.getArrayAttr(parameters)),
       named("ports", builder.getArrayAttr(ports)),
       named("provider", reference(record.provider())),
       named("provider_implementation_fingerprint",
             string(record.providerImplementationFingerprint())),
       named("resources", builder.getArrayAttr(resources)),
       named("results", builder.getArrayAttr(results))});
}

// ---------------------------------------------------------------------------
// Module and placement plans
// ---------------------------------------------------------------------------

struct PlacementPlan {
  enum class Kind { Instance, Array, Process };
  Kind kind = Kind::Instance;
  std::string name;
  // Instance/array realization.
  std::string targetSymbol;
  bool targetIsBinding = false;
  ArrayAttr staticArgs;
  std::string specialization;
  llvm::SmallVector<int64_t, 2> shape;
  // Binding-target dispatch thunks.
  std::string work;
  std::string xfer;
  std::string reset;
  std::string validate;
  // Process realization.
  ac::ProcessOp process;
  uint64_t fairnessCap = 1;
};

struct ModulePlan {
  ac::ModuleOp source;
  std::string name;
  ArrayAttr staticParams;
  std::string specialization;
  llvm::SmallVector<PlacementPlan> placements;
};

// ---------------------------------------------------------------------------
// The pass
// ---------------------------------------------------------------------------

class ACIRToACSimPass final
    : public PassWrapper<ACIRToACSimPass, OperationPass<mlir::ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ACIRToACSimPass)

  explicit ACIRToACSimPass(ACIRToACSimPassOptions options)
      : options(std::move(options)) {}

  llvm::StringRef getArgument() const override { return "ac-lower-to-acsim"; }
  llvm::StringRef getDescription() const override {
    return "Atomically lower one frozen ACIR model to canonical ACSim";
  }

  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<acsim::ACSimDialect>();
  }

  void runOnOperation() override {
    if (failed(lower(getOperation())))
      signalPassFailure();
  }

private:
  mlir::LogicalResult lower(mlir::ModuleOp input);

  /// Validation and planning. No IR mutation happens in this phase.
  mlir::LogicalResult plan(mlir::ModuleOp input);

  mlir::LogicalResult planModule(ac::ModuleOp module, ModulePlan &planned);
  mlir::LogicalResult planInstanceTarget(
      Operation *placement, llvm::StringRef definition,
      DictionaryAttr staticArgs, llvm::StringRef enclosingModule,
      PlacementPlan &planned);
  mlir::LogicalResult planProcesses(mlir::ModuleOp input);
  mlir::LogicalResult expand(mlir::ModuleOp input);

  void expandModule(unsigned moduleIndex, llvm::StringRef pathPrefix,
                    llvm::SmallSet<unsigned, 8> &active);

  /// Emission. Runs only after every check succeeded.
  void emit(mlir::ModuleOp input);
  void emitModuleBody(OpBuilder &builder, const ModulePlan &planned);
  void emitProcessBody(OpBuilder &builder, const PlacementPlan &placement);

  std::string moduleFingerprint(ac::ModuleOp module);
  std::string processFingerprint(const ModulePlan &module,
                                 const PlacementPlan &process);
  std::string bindingInstanceFingerprint(llvm::StringRef binding,
                                         ArrayAttr values);

  ACIRToACSimPassOptions options;

  // Planning state.
  ac::SystemOp selectedSystem;
  llvm::StringMap<unsigned> moduleIndexByName; // concrete modules
  llvm::StringMap<ac::ModuleExternOp> externByName;
  llvm::SmallVector<ModulePlan, 0> modules; // sorted by name
  std::optional<bindings::BindingResolutionResult> resolution;
  std::optional<ProcessStatePlanSet> processPlans;
  TypeSymbolTable typeSymbols;
  std::string wakeTypeSymbol;
  std::string wakeImplSymbol;

  struct RuntimeRow {
    unsigned moduleIndex;
    unsigned placementIndex;
    std::string path;
    llvm::SmallVector<int64_t, 2> indices;
  };
  llvm::SmallVector<std::string> constructionOrder;
  llvm::SmallVector<RuntimeRow> runtimeRows;

  // Fingerprints.
  std::string frozenAcirFingerprint;
  std::string bindingLockFingerprint;
  std::string providerFingerprint;
  std::string schemaSetFingerprint;
  std::string profileFingerprint;
  std::string toolchainFingerprint;

  // Set when owner expansion detects an instantiation cycle.
  bool expansionCycle = false;
};

std::string ACIRToACSimPass::moduleFingerprint(ac::ModuleOp module) {
  llvm::json::Object descriptor;
  descriptor["module"] = module.getSymName();
  llvm::json::Object parameters;
  for (NamedAttribute named : module.getStaticParams()) {
    auto value = staticValueToJson(named.getValue());
    if (!value) {
      llvm::consumeError(value.takeError());
      continue;
    }
    parameters[named.getName().getValue()] = std::move(*value);
  }
  descriptor["static"] = std::move(parameters);
  return fingerprintJson(llvm::json::Value(std::move(descriptor)));
}

std::string
ACIRToACSimPass::processFingerprint(const ModulePlan &module,
                                    const PlacementPlan &process) {
  llvm::json::Object descriptor;
  descriptor["module"] = module.name;
  descriptor["module_specialization"] = module.specialization;
  descriptor["process"] = process.name;
  return fingerprintJson(llvm::json::Value(std::move(descriptor)));
}

std::string
ACIRToACSimPass::bindingInstanceFingerprint(llvm::StringRef binding,
                                            ArrayAttr values) {
  llvm::json::Object descriptor;
  descriptor["binding"] = binding;
  llvm::json::Array staticValues;
  for (Attribute value : values) {
    auto converted = staticValueToJson(value);
    if (!converted) {
      llvm::consumeError(converted.takeError());
      continue;
    }
    staticValues.push_back(std::move(*converted));
  }
  descriptor["static"] = std::move(staticValues);
  return fingerprintJson(llvm::json::Value(std::move(descriptor)));
}

// ---------------------------------------------------------------------------
// Planning
// ---------------------------------------------------------------------------

mlir::LogicalResult
ACIRToACSimPass::planInstanceTarget(Operation *placement,
                                    llvm::StringRef definition,
                                    DictionaryAttr staticArgs,
                                    llvm::StringRef enclosingModule,
                                    PlacementPlan &planned) {
  auto externIt = externByName.find(definition);
  auto moduleIt = moduleIndexByName.find(definition);
  if (externIt == externByName.end() && moduleIt == moduleIndexByName.end())
    return lowerError(placement, "ACLOWER-BINDING-MISSING",
                      "placement definition '@" + definition +
                          "' is not a module or external declaration");

  DictionaryAttr declaredParams;
  if (externIt != externByName.end())
    declaredParams = externIt->second.getStaticParams();
  else
    declaredParams = modules[moduleIt->second].source.getStaticParams();
  // Zero-volume arrays carry no per-element dictionaries; the declared
  // parameters are the single specialization.
  if (staticArgs && staticArgs != declaredParams)
    return lowerError(placement, "ACLOWER-PARAM-PHASE",
                      "placement static arguments must exactly equal the "
                      "frozen static parameters of '@" +
                          definition +
                          "' (per-instance specialization is outside the v0.1 "
                          "lowering stage)");

  if (externIt != externByName.end()) {
    // External declaration: realization comes from the exact binding lock.
    std::string key = ("@" + definition).str();
    const bindings::ResolvedBinding *selection =
        resolution->selectionForResolutionKey(key);
    if (!selection)
      return lowerError(placement, "ACLOWER-BINDING-MISSING",
                        "no exact binding selection exists for external "
                        "declaration '@" +
                            definition + "'");
    const bindings::BindingRecord &record = selection->record();
    if (record.effect() != "stateful")
      return lowerError(placement, "ACLOWER-OWNERSHIP",
                        "ownership placement of external declaration '@" +
                            definition +
                            "' requires a stateful binding, but binding '" +
                            record.binding() + "' has effect '" +
                            record.effect() + "'");
    const bindings::CppEntryPoints &entryPoints = record.cpp().entryPoints;
    if (entryPoints.work.empty() || entryPoints.xfer.empty() ||
        entryPoints.reset.empty() || entryPoints.validate.empty())
      return lowerError(placement, "ACLOWER-DISPATCH",
                        "binding '" + record.binding() +
                            "' has empty dispatch entry points and cannot "
                            "realize a runtime object");
    planned.targetSymbol = record.binding().str();
    planned.targetIsBinding = true;
    OpBuilder builder(placement->getContext());
    llvm::SmallVector<Attribute> values;
    for (const bindings::ParameterBinding &parameter : record.parameters()) {
      Attribute value = jsonToStaticAttribute(builder, parameter.value);
      if (!value)
        return lowerError(placement, "ACLOWER-PARAM-PHASE",
                          "binding '" + record.binding() +
                              "' parameter '" + parameter.name +
                              "' has a value outside the canonical static "
                              "domain");
      values.push_back(value);
    }
    planned.staticArgs = builder.getArrayAttr(values);
    planned.specialization =
        bindingInstanceFingerprint(record.binding(), planned.staticArgs);
    planned.work = entryPoints.work;
    planned.xfer = entryPoints.xfer;
    planned.reset = entryPoints.reset;
    planned.validate = entryPoints.validate;
    return mlir::success();
  }

  // Concrete generated module target.
  ModulePlan &target = modules[moduleIt->second];
  if (definition >= enclosingModule)
    return lowerError(
        placement, "ACLOWER-OWNERSHIP",
        "canonical ACSim declares modules in strictly symbol-sorted order, so "
        "module '@" +
            enclosingModule + "' cannot instantiate '@" + definition +
            "'; rename so every instantiated module sorts before its parent");
  planned.targetSymbol = target.name;
  planned.targetIsBinding = false;
  planned.staticArgs = target.staticParams;
  planned.specialization = target.specialization;
  return mlir::success();
}

mlir::LogicalResult ACIRToACSimPass::planModule(ac::ModuleOp module,
                                                ModulePlan &planned) {
  FunctionType signature = module.getFunctionType();
  if (signature.getNumInputs() != 0 || signature.getNumResults() != 0) {
    std::string printed;
    llvm::raw_string_ostream stream(printed);
    stream << signature;
    return lowerError(module, "ACLOWER-TYPE-MISMATCH",
                      "ac-lower-to-acsim v0.1 supports exactly '() -> ()' "
                      "module signatures; module '@" +
                          module.getSymName() + "' has '" + stream.str() +
                          "'");
  }

  OpBuilder builder(module->getContext());
  llvm::SmallVector<Attribute> staticValues;
  for (NamedAttribute named : module.getStaticParams())
    staticValues.push_back(named.getValue());
  planned.staticParams = builder.getArrayAttr(staticValues);
  planned.specialization = moduleFingerprint(module);

  llvm::SmallVector<PlacementPlan> processes;
  for (Operation &operation : module.getBody().front()) {
    if (auto instance = dyn_cast<ac::InstanceOp>(operation)) {
      PlacementPlan placement;
      placement.kind = PlacementPlan::Kind::Instance;
      placement.name = instance.getSymName().str();
      if (failed(planInstanceTarget(instance, instance.getDefinition(),
                                    instance.getStaticArgs(), planned.name,
                                    placement)))
        return mlir::failure();
      planned.placements.push_back(std::move(placement));
      continue;
    }
    if (auto array = dyn_cast<ac::ArrayOp>(operation)) {
      PlacementPlan placement;
      placement.kind = PlacementPlan::Kind::Array;
      placement.name = array.getSymName().str();
      placement.shape.assign(array.getShape().begin(), array.getShape().end());
      // Homogeneous arrays require one exact specialization per element.
      DictionaryAttr first;
      for (Attribute element : array.getStaticArgs()) {
        auto arguments = dyn_cast<DictionaryAttr>(element);
        if (!arguments)
          return lowerError(array, "ACLOWER-ARRAY",
                            "array static arguments must be concrete "
                            "dictionaries");
        if (!first)
          first = arguments;
        else if (arguments != first)
          return lowerError(array, "ACLOWER-ARRAY",
                            "differently specialized array elements are "
                            "outside the v0.1 lowering stage; lower them as "
                            "ordered named members instead");
      }
      if (failed(planInstanceTarget(array, array.getDefinition(),
                                    first, planned.name, placement)))
        return mlir::failure();
      planned.placements.push_back(std::move(placement));
      continue;
    }
    if (auto collection = dyn_cast<ac::InstancesOp>(operation)) {
      for (auto [index, definitionAttribute] :
           llvm::enumerate(collection.getDefinitions())) {
        auto definition = cast<FlatSymbolRefAttr>(definitionAttribute);
        auto arguments =
            cast<DictionaryAttr>(collection.getStaticArgs()[index]);
        PlacementPlan placement;
        placement.kind = PlacementPlan::Kind::Instance;
        placement.name =
            cast<StringAttr>(collection.getNames()[index]).getValue().str();
        if (failed(planInstanceTarget(collection, definition.getValue(),
                                      arguments, planned.name, placement)))
          return mlir::failure();
        planned.placements.push_back(std::move(placement));
      }
      continue;
    }
    if (auto process = dyn_cast<ac::ProcessOp>(operation)) {
      if (!process.getCaptures().empty())
        return lowerError(process, "ACLOWER-PROCESS-STATE",
                          "process captures are outside the v0.1 lowering "
                          "stage");
      if (!process.getBody().hasOneBlock() ||
          !llvm::hasSingleElement(process.getBody().front()) ||
          !isa<ac::YieldSimOp>(process.getBody().front().front()))
        return lowerError(process, "ACLOWER-PROCESS-STATE",
                          "ac-lower-to-acsim v0.1 lowers exactly the "
                          "yield-only process form planned by "
                          "ProcessStatePlan; process '@" +
                              process.getSymName() +
                              "' has an unsupported body");
      PlacementPlan placement;
      placement.kind = PlacementPlan::Kind::Process;
      placement.name = process.getSymName().str();
      placement.process = process;
      processes.push_back(std::move(placement));
      continue;
    }
    if (auto returnOp = dyn_cast<ac::ReturnOp>(operation)) {
      if (!returnOp.getOperands().empty())
        return lowerError(returnOp, "ACLOWER-TYPE-MISMATCH",
                          "module results are outside the v0.1 lowering "
                          "stage");
      continue;
    }
    return lowerError(&operation, "ACLOWER-UNSUPPORTED-CONSTRUCT",
                      "operation '" + operation.getName().getStringRef() +
                          "' has no ACSim realization in the v0.1 lowering "
                          "stage (queues, resources, address maps, time "
                          "domains, views, and instrumentation are rejected, "
                          "never silently dropped)");
  }

  llvm::sort(planned.placements,
             [](const PlacementPlan &left, const PlacementPlan &right) {
               return left.name < right.name;
             });
  llvm::sort(processes,
             [](const PlacementPlan &left, const PlacementPlan &right) {
               return left.name < right.name;
             });
  for (auto &process : processes)
    planned.placements.push_back(std::move(process));
  return mlir::success();
}

mlir::LogicalResult ACIRToACSimPass::planProcesses(mlir::ModuleOp input) {
  bool hasProcess = false;
  input.walk([&](ac::ProcessOp) { hasProcess = true; });
  if (!hasProcess)
    return mlir::success();

  auto plans = detail::PlanSetBuilder::buildYieldOnly(input);
  if (failed(plans))
    return mlir::failure();
  if (failed(verifyProcessStatePlan(*plans)))
    return mlir::failure();
  processPlans = std::move(*plans);

  // The yield-only plan names one generated next-delta wake helper; adopt its
  // exact compiler-generated identities for the ACSim realization.
  for (const ProcessGeneratedCalleePlan &callee : processPlans->callees()) {
    if (callee.role() != ProcessHelperRole::WakeNextDelta)
      continue;
    llvm::StringRef symbol = callee.symbol();
    symbol.consume_front("@");
    wakeImplSymbol = symbol.str();
    if (failed(typeSymbols.intern(input, symbol, "implementation",
                                  callee.cpp(), callee.fingerprint())))
      return mlir::failure();
  }
  if (wakeImplSymbol.empty())
    return lowerError(input, "ACLOWER-PROCESS-STATE",
                      "process-state plan has no next-delta wake realization");
  for (const ProcessStatePlan &process : processPlans->processes()) {
    if (process.wakes().size() != 1)
      return lowerError(process.process(), "ACLOWER-PROCESS-STATE",
                        "yield-only process plan requires exactly one wake");
    llvm::StringRef typeKey = process.wakes().front().typeKey();
    typeKey.consume_front("@");
    wakeTypeSymbol = typeKey.str();
  }
  if (failed(typeSymbols.intern(input, wakeTypeSymbol, "wake",
                                "acir::generated::wake_next_delta")))
    return mlir::failure();

  // Attach plan-derived fairness caps to the module placements.
  for (ModulePlan &module : modules)
    for (PlacementPlan &placement : module.placements) {
      if (placement.kind != PlacementPlan::Kind::Process)
        continue;
      std::string key = "@" + module.name + "::@" + placement.name;
      const ProcessStatePlan *plan =
          processPlans->lookupByDefinitionKey(key);
      if (!plan)
        return lowerError(placement.process, "ACLOWER-PROCESS-STATE",
                          "process-state plan is missing process '@" +
                              placement.name + "'");
      placement.fairnessCap = std::max<uint64_t>(plan->fairnessWork(), 2);
      placement.specialization = processFingerprint(module, placement);
    }
  return mlir::success();
}

mlir::LogicalResult ACIRToACSimPass::plan(mlir::ModuleOp input) {
  auto epoch = input->getAttrOfType<StringAttr>("ac.contract_epoch");
  if (!epoch || epoch.getValue() != kEpoch)
    return lowerError(input, "ACLOWER-EPOCH-MISMATCH",
                      "ac-lower-to-acsim requires ac.contract_epoch exactly "
                      "\"0.1\"");
  auto frozen = input->getAttrOfType<BoolAttr>("ac.topology_frozen");
  auto freezeEpoch = input->getAttrOfType<StringAttr>("ac.freeze_epoch");
  if (!frozen || !frozen.getValue() || !freezeEpoch ||
      freezeEpoch.getValue() != kEpoch)
    return lowerError(input, "ACLOWER-EPOCH-MISMATCH",
                      "ac-lower-to-acsim requires a topology-frozen v0.1 "
                      "model; run ac-freeze-topology first");
  if (options.profile.empty() || options.target.empty())
    return lowerError(input, "ACLOWER-PROFILE",
                      "ac-lower-to-acsim requires an exact static build "
                      "profile and toolchain target");

  unsigned selectedCount = 0;
  for (auto system : input.getOps<ac::SystemOp>()) {
    if (!system.getSelected())
      continue;
    ++selectedCount;
    selectedSystem = system;
  }
  if (selectedCount != 1)
    return lowerError(input, "ACLOWER-OWNERSHIP",
                      "ac-lower-to-acsim requires exactly one selected "
                      "ac.system");

  // Inventory concrete modules, externals, and top-level declarations.
  for (Operation &operation : *input.getBody()) {
    if (auto module = dyn_cast<ac::ModuleOp>(operation)) {
      moduleIndexByName[module.getSymName()] = modules.size();
      ModulePlan planned;
      planned.source = module;
      planned.name = module.getSymName().str();
      modules.push_back(std::move(planned));
      continue;
    }
    if (auto external = dyn_cast<ac::ModuleExternOp>(operation)) {
      externByName[external.getSymName()] = external;
      continue;
    }
    if (isa<ac::ModuleGeneratedOp>(operation))
      return lowerError(&operation, "ACLOWER-UNSUPPORTED-CONSTRUCT",
                        "generator-based module declarations are outside the "
                        "v0.1 lowering stage");
    if (isa<ac::SystemOp, ac::TypeScopeOp, ac::TypeAliasOp, ac::StructOp,
            ac::EnumOp, ac::UnionOp, ac::PacketOp, ac::TransactionOp,
            ac::InterfaceOp, ac::ProtocolOp>(operation))
      continue; // Pure declarations are fully resolved before lowering.
    return lowerError(&operation, "ACLOWER-UNSUPPORTED-CONSTRUCT",
                      "top-level operation '" +
                          operation.getName().getStringRef() +
                          "' has no ACSim realization in the v0.1 lowering "
                          "stage");
  }

  llvm::sort(modules,
             [](const ModulePlan &left, const ModulePlan &right) {
               return left.name < right.name;
             });
  moduleIndexByName.clear();
  for (auto [index, module] : llvm::enumerate(modules))
    moduleIndexByName[module.name] = index;

  // The selected root must be a concrete generated module.
  llvm::StringRef rootName = selectedSystem.getRoot();
  if (!moduleIndexByName.count(rootName))
    return lowerError(selectedSystem, "ACLOWER-OWNERSHIP",
                      "selected system root '@" + rootName +
                          "' must be a concrete ac.module");

  // Resolve exact bindings in memory (shared contract with
  // ac-resolve-gfsim-bindings; no lock file round-trip).
  ResolveBindingsPassOptions resolveOptions;
  resolveOptions.candidates = options.candidates;
  resolveOptions.requests = options.requests;
  resolveOptions.profile = options.profile;
  resolveOptions.target = options.target;
  auto resolved = resolveModuleBindings(input, resolveOptions);
  if (!resolved) {
    input.emitError() << llvm::toString(resolved.takeError());
    return mlir::failure();
  }
  resolution = std::move(*resolved);

  // Plan every concrete module body.
  for (auto [index, module] : llvm::enumerate(modules))
    if (failed(planModule(module.source, modules[index])))
      return mlir::failure();

  if (failed(planProcesses(input)))
    return mlir::failure();

  // Intern every binding-record realization identity.
  for (const bindings::ResolvedBinding &selection : resolution->selections()) {
    const bindings::BindingRecord &record = selection.record();
    if (failed(typeSymbols.intern(input, record.componentSchema(), "schema",
                                  record.componentSchema(),
                                  record.componentSchemaFingerprint())) ||
        failed(typeSymbols.intern(input, record.implementation(),
                                  "implementation", record.implementation(),
                                  record.providerImplementationFingerprint())) ||
        failed(typeSymbols.intern(input, record.provider(), "provider",
                                  record.provider())) ||
        failed(typeSymbols.intern(input, record.cppType(), "value",
                                  record.cppType())))
      return mlir::failure();
    for (const bindings::PortBinding &port : record.ports())
      if (failed(typeSymbols.intern(input, port.accessor, "accessor",
                                    port.accessor)) ||
          failed(typeSymbols.intern(input, port.interface, "interface",
                                    port.interface)) ||
          failed(typeSymbols.intern(input, port.payload, "packet",
                                    port.payload)) ||
          failed(typeSymbols.intern(input, port.protocol, "protocol",
                                    port.protocol)) ||
          failed(typeSymbols.intern(input, port.role, "role", port.role)) ||
          failed(typeSymbols.intern(input, port.timeDomain, "time_domain",
                                    port.timeDomain)))
        return mlir::failure();
    for (const bindings::ResourceBinding &resource : record.resources())
      if (failed(typeSymbols.intern(input, resource.accessor, "accessor",
                                    resource.accessor)) ||
          failed(typeSymbols.intern(input, resource.resource, "resource",
                                    resource.resource)) ||
          failed(typeSymbols.intern(input, resource.role, "role",
                                    resource.role)) ||
          failed(typeSymbols.intern(input, resource.timeDomain, "time_domain",
                                    resource.timeDomain)))
        return mlir::failure();
    for (const bindings::ResultBinding &result : record.results())
      if (failed(typeSymbols.intern(input, result.cppType, "value",
                                    result.cppType)))
        return mlir::failure();
    for (const bindings::ActivationSourceBinding &source :
         record.activationSources())
      if (failed(typeSymbols.intern(input, source.kind, "wake", source.kind)))
        return mlir::failure();
  }
  if (failed(typeSymbols.finalize(input)))
    return mlir::failure();

  // Binding symbols must not collide with type or module symbols.
  for (const bindings::ResolvedBinding &selection : resolution->selections()) {
    llvm::StringRef binding = selection.record().binding();
    if (typeSymbols.symbolFor(binding).data() != nullptr ||
        moduleIndexByName.count(binding))
      return lowerError(input, "ACLOWER-BINDING-AMBIGUOUS",
                        "binding identity '" + binding +
                            "' collides with a type or module symbol");
  }

  // Fingerprints over exact inputs, computed before any mutation.
  std::string frozenText;
  {
    llvm::raw_string_ostream output(frozenText);
    input.print(output);
  }
  frozenAcirFingerprint = bindings::sha256Fingerprint(frozenText);
  bindingLockFingerprint = resolution->lockFingerprint().str();

  llvm::json::Array providers;
  llvm::json::Array schemas;
  {
    std::map<std::string, bool> uniqueProviders;
    std::map<std::string, bool> uniqueSchemas;
    for (const bindings::ResolvedBinding &selection : resolution->selections()) {
      uniqueProviders[selection.record().provider().str()] = true;
      uniqueSchemas[selection.record().componentSchema().str()] = true;
    }
    for (auto &[identity, unused] : uniqueProviders)
      providers.push_back(identity);
    for (auto &[identity, unused] : uniqueSchemas)
      schemas.push_back(identity);
  }
  providerFingerprint =
      fingerprintJson(llvm::json::Value(std::move(providers)));
  schemaSetFingerprint = fingerprintJson(llvm::json::Value(std::move(schemas)));
  profileFingerprint = fingerprintJson(llvm::json::Value(options.profile));
  toolchainFingerprint = fingerprintJson(llvm::json::Value(options.target));
  if (providerFingerprint.empty() || schemaSetFingerprint.empty() ||
      profileFingerprint.empty() || toolchainFingerprint.empty())
    return lowerError(input, "ACLOWER-FINGERPRINT",
                      "failed to derive canonical model fingerprints");

  // Deterministic owner/runtime expansion over the planned structure.
  llvm::SmallSet<unsigned, 8> active;
  expandModule(moduleIndexByName.lookup(rootName), rootName, active);
  if (expansionCycle)
    return lowerError(input, "ACLOWER-OWNERSHIP",
                      "module instantiation cycle cannot produce canonical "
                      "ACSim ownership order");
  if (constructionOrder.size() > kMaxExpandedRows ||
      runtimeRows.size() > kMaxExpandedRows)
    return lowerError(input, "ACLOWER-DISPATCH",
                      "expanded hierarchy exceeds the v0.1 capability bound");
  return mlir::success();
}

void ACIRToACSimPass::expandModule(
    unsigned moduleIndex, llvm::StringRef pathPrefix,
    llvm::SmallSet<unsigned, 8> &active) {
  ModulePlan &module = modules[moduleIndex];
  active.insert(moduleIndex);
  for (auto [placementIndex, placement] : llvm::enumerate(module.placements)) {
    auto elementPath = [&](llvm::ArrayRef<int64_t> indices) {
      std::string path = pathPrefix.str();
      path.push_back('.');
      path.append(placement.name);
      if (!indices.empty()) {
        path.push_back('[');
        llvm::raw_string_ostream stream(path);
        llvm::interleaveComma(indices, stream);
        stream << ']';
      }
      return path;
    };
    auto expandOne = [&](llvm::ArrayRef<int64_t> indices) {
      constructionOrder.push_back(elementPath(indices));
      if (placement.kind == PlacementPlan::Kind::Process ||
          placement.targetIsBinding) {
        RuntimeRow row;
        row.moduleIndex = moduleIndex;
        row.placementIndex = placementIndex;
        row.path = constructionOrder.back();
        row.indices.assign(indices.begin(), indices.end());
        runtimeRows.push_back(std::move(row));
        return;
      }
      unsigned targetIndex = moduleIndexByName.lookup(placement.targetSymbol);
      if (active.contains(targetIndex)) {
        // An instantiation cycle can never produce canonical ACSim.
        expansionCycle = true;
        constructionOrder.pop_back();
        return;
      }
      expandModule(targetIndex, constructionOrder.back(), active);
    };

    if (placement.kind == PlacementPlan::Kind::Array) {
      uint64_t volume = 1;
      for (int64_t extent : placement.shape) {
        if (extent == 0) {
          volume = 0;
          break;
        }
        volume *= static_cast<uint64_t>(extent);
      }
      for (uint64_t ordinal = 0; ordinal < volume; ++ordinal) {
        llvm::SmallVector<int64_t, 2> indices(placement.shape.size(), 0);
        uint64_t remainder = ordinal;
        for (size_t dimension = placement.shape.size(); dimension > 0;
             --dimension) {
          uint64_t extent = static_cast<uint64_t>(placement.shape[dimension - 1]);
          indices[dimension - 1] = static_cast<int64_t>(remainder % extent);
          remainder /= extent;
        }
        expandOne(indices);
      }
      continue;
    }
    expandOne({});
  }
  active.erase(moduleIndex);
}

// ---------------------------------------------------------------------------
// Emission
// ---------------------------------------------------------------------------

void ACIRToACSimPass::emitProcessBody(OpBuilder &builder,
                                      const PlacementPlan &placement) {
  MLIRContext *context = builder.getContext();
  auto entry = FlatSymbolRefAttr::get(context, "entry");
  auto process = acsim::ProcessOp::create(builder, 
      placement.process->getLoc(), ValueRange{}, placement.name,
      builder.getArrayAttr({}), "entry", builder.getArrayAttr({entry}),
      builder.getArrayAttr({}), placement.fairnessCap,
      placement.specialization, /*statesCount=*/1);

  Block *entryBlock = new Block();
  process.getStates().front().push_back(entryBlock);
  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(entryBlock);
  auto wakeType = acsim::WakeType::get(
      context, FlatSymbolRefAttr::get(context, wakeTypeSymbol));
  auto wake = acsim::InvokeOp::create(builder, 
      placement.process->getLoc(), TypeRange{wakeType}, ValueRange{},
      FlatSymbolRefAttr::get(context, wakeImplSymbol));
  acsim::SuspendOp::create(builder, placement.process->getLoc(),
                                   wake.getResults().front(), entry);
  (void)process;
}

void ACIRToACSimPass::emitModuleBody(OpBuilder &builder,
                                     const ModulePlan &planned) {
  MLIRContext *context = builder.getContext();
  DictionaryAttr interface = builder.getDictionaryAttr(
      {builder.getNamedAttr("ports", builder.getArrayAttr({})),
       builder.getNamedAttr("resources", builder.getArrayAttr({})),
       builder.getNamedAttr("results", builder.getArrayAttr({}))});
  auto module = acsim::ModuleOp::create(builder, 
      planned.source->getLoc(), builder.getStringAttr(planned.name), interface,
      planned.staticParams, builder.getStringAttr(planned.specialization),
      builder.getArrayAttr({}));
  Block *body = new Block();
  module.getBody().push_back(body);
  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(body);
  for (const PlacementPlan &placement : planned.placements) {
    switch (placement.kind) {
    case PlacementPlan::Kind::Instance: {
      auto target = SymbolRefAttr::get(context, placement.targetSymbol);
      auto ownerType = acsim::OwnerType::get(context, target);
      acsim::InstanceOp::create(builder, 
          planned.source->getLoc(), ownerType,
          builder.getStringAttr(placement.name), target, placement.staticArgs,
          builder.getStringAttr(placement.specialization));
      break;
    }
    case PlacementPlan::Kind::Array: {
      auto target = SymbolRefAttr::get(context, placement.targetSymbol);
      auto ownerType = acsim::OwnerType::get(context, target);
      auto shape = builder.getDenseI64ArrayAttr(placement.shape);
      auto arrayType = acsim::ArrayType::get(context, shape, ownerType);
      acsim::ArrayOp::create(builder, 
          planned.source->getLoc(), arrayType,
          builder.getStringAttr(placement.name), target, placement.staticArgs,
          builder.getStringAttr(placement.specialization), shape);
      break;
    }
    case PlacementPlan::Kind::Process:
      emitProcessBody(builder, placement);
      break;
    }
  }
  acsim::ReturnOp::create(builder, planned.source->getLoc(), ValueRange{});
}

void ACIRToACSimPass::emit(mlir::ModuleOp input) {
  MLIRContext *context = input.getContext();
  OpBuilder builder(context);
  builder.setInsertionPointToEnd(input.getBody());

  llvm::SmallVector<Attribute> construction;
  llvm::SmallVector<Attribute> destructionAttrs;
  for (const std::string &path : constructionOrder)
    construction.push_back(builder.getStringAttr(path));
  for (auto it = constructionOrder.rbegin(); it != constructionOrder.rend();
       ++it)
    destructionAttrs.push_back(builder.getStringAttr(*it));

  DictionaryAttr fingerprints = builder.getDictionaryAttr(
      {builder.getNamedAttr("frozen_acir",
                            builder.getStringAttr(frozenAcirFingerprint)),
       builder.getNamedAttr("binding_lock",
                            builder.getStringAttr(bindingLockFingerprint)),
       builder.getNamedAttr("provider",
                            builder.getStringAttr(providerFingerprint)),
       builder.getNamedAttr("profile",
                            builder.getStringAttr(profileFingerprint)),
       builder.getNamedAttr("toolchain",
                            builder.getStringAttr(toolchainFingerprint)),
       builder.getNamedAttr("schema_set",
                            builder.getStringAttr(schemaSetFingerprint))});

  auto model = acsim::ModelOp::create(builder, 
      input.getLoc(), builder.getStringAttr(selectedSystem.getSymName()),
      builder.getStringAttr(kEpoch),
      FlatSymbolRefAttr::get(context, selectedSystem.getRoot()),
      builder.getArrayAttr(construction),
      builder.getArrayAttr(destructionAttrs), fingerprints);

  Block *modelBody = new Block();
  model.getBody().push_back(modelBody);
  builder.setInsertionPointToStart(modelBody);

  // Rank 0: acsim.type declarations, strictly symbol-sorted.
  for (const TypeDeclaration *declaration : typeSymbols.declarations())
    acsim::TypeOp::create(builder, 
        input.getLoc(), builder.getStringAttr(declaration->symbol),
        builder.getStringAttr(declaration->cpp),
        builder.getStringAttr(declaration->kind),
        builder.getStringAttr(declaration->fingerprint));

  // Rank 1: acsim.binding records, strictly symbol-sorted.
  {
    llvm::SmallVector<const bindings::BindingRecord *> records;
    for (const bindings::ResolvedBinding &selection : resolution->selections())
      records.push_back(&selection.record());
    llvm::sort(records, [](const bindings::BindingRecord *left,
                           const bindings::BindingRecord *right) {
      return left->binding() < right->binding();
    });
    for (const bindings::BindingRecord *record : records)
      acsim::BindingOp::create(builder, 
          input.getLoc(), builder.getStringAttr(record->binding()),
          cast<DictionaryAttr>(
              convertBindingRecord(builder, *record, typeSymbols)));
  }

  // Rank 2: acsim.module declarations, strictly symbol-sorted.
  for (const ModulePlan &planned : modules)
    emitModuleBody(builder, planned);

  // Rank 3: one typed dispatch row per runtime object, dense IDs.
  llvm::SmallVector<acsim::DispatchOp> dispatches;
  for (auto [id, row] : llvm::enumerate(runtimeRows)) {
    const ModulePlan &module = modules[row.moduleIndex];
    const PlacementPlan &placement = module.placements[row.placementIndex];
    auto target = SymbolRefAttr::get(
        context, module.name,
        {FlatSymbolRefAttr::get(context, placement.name)});
    std::string work = placement.work;
    std::string xfer = placement.xfer;
    std::string reset = placement.reset;
    std::string validate = placement.validate;
    if (placement.kind == PlacementPlan::Kind::Process) {
      std::string base = ("acsim_generated::" + module.name + "::s" +
                          module.specialization.substr(7) + "::" +
                          placement.name + "::p" +
                          placement.specialization.substr(7) + "::");
      work = base + "work";
      xfer = base + "xfer";
      reset = base + "reset";
      validate = base + "validate";
    }
    dispatches.push_back(acsim::DispatchOp::create(builder, 
        input.getLoc(), acsim::ObjectIdType::get(context),
        acsim::ActivationIdType::get(context), target,
        builder.getStringAttr(row.path),
        builder.getDenseI64ArrayAttr(row.indices),
        builder.getI64IntegerAttr(static_cast<int64_t>(id)),
        builder.getI64IntegerAttr(static_cast<int64_t>(id)),
        builder.getStringAttr(work), builder.getStringAttr(xfer),
        builder.getStringAttr(reset), builder.getStringAttr(validate)));
  }

  // Rank 4: static activation adjacency. With no typed binds or captures in
  // this stage, the exact edge set is the self edge of every runtime row,
  // sorted by (source, target) -- which dense IDs already satisfy.
  for (auto [id, dispatch] : llvm::enumerate(dispatches)) {
    (void)id;
    acsim::ActivateOp::create(builder, input.getLoc(), dispatch.getActivation(),
                                      dispatch.getObject());
  }

  // Replace the frozen ACIR with the canonical ACSim file.
  llvm::SmallVector<Operation *> obsolete;
  for (Operation &operation : *input.getBody())
    if (&operation != model.getOperation())
      obsolete.push_back(&operation);
  for (Operation *operation : obsolete)
    operation->erase();

  llvm::SmallVector<NamedAttribute> retained;
  for (NamedAttribute attribute : input->getAttrs())
    if (attribute.getName() == "ac.contract_epoch")
      retained.push_back(attribute);
  input->setAttrs(retained);
}

mlir::LogicalResult ACIRToACSimPass::lower(mlir::ModuleOp input) {
  if (failed(plan(input)))
    return mlir::failure();
  emit(input);
  return mlir::success();
}

} // namespace

std::unique_ptr<mlir::Pass>
createACIRToACSimPass(ACIRToACSimPassOptions options) {
  return std::make_unique<ACIRToACSimPass>(std::move(options));
}

} // namespace acir
