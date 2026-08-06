// ACIR-to-ACSim structural conversion pass.
//
// Converts frozen, verified ACIR into canonical ACSim:
//   ac.module          → acsim.module
//   ac.instance        → acsim.instance
//   ac.array           → acsim.array
//   ac.return          → acsim.return
//   ac.system          → acsim.model
//   ac.module.extern   → acsim.module (declaration)
//   ac.module.generated → acsim.module (declaration)
#include "acir/Conversion/ACIRToACSim/ACIRToACSim.h"
#include "acir/Dialect/ACIR/ACIROps.h"
#include "acir/Dialect/ACSim/ACSimOps.h"
#include "acir/Dialect/ACSim/ACSimTypes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/FormatVariadic.h"

#include <set>
#include <string>

namespace acir {
namespace {

constexpr llvm::StringLiteral kZeroFingerprint =
    "sha256:0000000000000000000000000000000000000000000000000000000000000000";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

mlir::StringAttr zeroFingerprint(mlir::OpBuilder &builder) {
  return builder.getStringAttr(kZeroFingerprint);
}

/// Convert ACIR DictionaryAttr static-params to ACSim ArrayAttr of
/// {key=..., value=...} records.
mlir::ArrayAttr convertStaticParams(mlir::OpBuilder &builder,
                                    mlir::DictionaryAttr params) {
  if (!params || params.empty())
    return builder.getArrayAttr({});
  llvm::SmallVector<mlir::Attribute> elements;
  for (auto kv : params) {
    elements.push_back(mlir::DictionaryAttr::get(
        builder.getContext(),
        {builder.getNamedAttr("key", kv.getName()),
         builder.getNamedAttr("value", kv.getValue())}));
  }
  return builder.getArrayAttr(elements);
}

/// Derive needed type symbols from a function type's results.
void collectNeededTypes(mlir::FunctionType fty,
                        llvm::StringMap<std::string> &neededTypes) {
  for (mlir::Type res : fty.getResults()) {
    std::string cpp;
    if (auto intTy = mlir::dyn_cast<mlir::IntegerType>(res)) {
      cpp = intTy.isUnsigned() ? "uint" : "int";
      cpp += std::to_string(intTy.getWidth()) + "_t";
    } else if (mlir::isa<mlir::FloatType>(res)) {
      cpp = "double";
    } else if (mlir::isa<mlir::IndexType>(res)) {
      cpp = "size_t";
    } else {
      cpp = "acsim_value";
    }
    neededTypes[llvm::formatv("type_{0}", neededTypes.size()).str()] = cpp;
  }
}

/// Build the interface dict for an acsim.module from an ACIR function type.
mlir::DictionaryAttr buildInterface(mlir::OpBuilder &builder,
                                    mlir::FunctionType fty,
                                    llvm::StringMap<std::string> &typeMap) {
  llvm::SmallVector<mlir::Attribute> resultRecords;
  int idx = 0;
  for (mlir::Type res : fty.getResults()) {
    std::string symName = llvm::formatv("r{0}", idx++).str();
    // Map result to a type symbol.
    std::string cpp;
    if (auto intTy = mlir::dyn_cast<mlir::IntegerType>(res))
      cpp = (intTy.isUnsigned() ? "uint" : "int") +
            std::to_string(intTy.getWidth()) + "_t";
    else if (mlir::isa<mlir::FloatType>(res))
      cpp = "double";
    else if (mlir::isa<mlir::IndexType>(res))
      cpp = "size_t";
    else
      cpp = "acsim_value";

    typeMap[cpp] = cpp;
    resultRecords.push_back(mlir::DictionaryAttr::get(
        builder.getContext(),
        {builder.getNamedAttr("name", builder.getStringAttr(symName)),
         builder.getNamedAttr("cpp_type",
                              mlir::SymbolRefAttr::get(builder.getContext(),
                                                       cpp))}));
  }
  return mlir::DictionaryAttr::get(
      builder.getContext(),
      {builder.getNamedAttr("ports", builder.getArrayAttr({})),
       builder.getNamedAttr("resources", builder.getArrayAttr({})),
       builder.getNamedAttr("results", builder.getArrayAttr(resultRecords))});
}

/// Build an exports list from the number of return values.
mlir::ArrayAttr buildExports(mlir::OpBuilder &builder, unsigned numResults) {
  llvm::SmallVector<mlir::Attribute> exports;
  for (unsigned i = 0; i < numResults; ++i)
    exports.push_back(
        builder.getStringAttr(llvm::formatv("out_{0}", i).str()));
  return builder.getArrayAttr(exports);
}

// ---------------------------------------------------------------------------
// Construction-order computation
// ---------------------------------------------------------------------------

using ModuleIndex = llvm::StringMap<acir::ac::ModuleOp>;

void collectPaths(llvm::StringRef parent, llvm::StringRef modName,
                  ModuleIndex &index,
                  llvm::SmallVectorImpl<std::string> &paths) {
  auto it = index.find(modName);
  if (it == index.end())
    return;
  auto mod = it->second;
  if (mod.getBody().empty())
    return;

  struct Child {
    std::string name;
    std::string target;
    bool isArray;
    llvm::SmallVector<int64_t, 1> shape;
  };
  llvm::SmallVector<Child> children;

  for (auto &op : mod.getBody().front()) {
    if (auto inst = mlir::dyn_cast<acir::ac::InstanceOp>(op)) {
      children.push_back({inst.getSymName().str(),
                          inst.getDefinition().str(), false, {}});
    } else if (auto arr = mlir::dyn_cast<acir::ac::ArrayOp>(op)) {
      children.push_back({arr.getSymName().str(),
                          arr.getDefinition().str(), true,
                          llvm::SmallVector<int64_t, 1>(arr.getShape())});
    }
  }
  llvm::sort(children,
             [](const Child &a, const Child &b) { return a.name < b.name; });

  for (auto &child : children) {
    if (child.isArray) {
      int64_t count = child.shape.empty() ? 1 : child.shape[0];
      for (int64_t i = 0; i < count; ++i) {
        std::string elemPath =
            (parent + "." + child.name + "[" + std::to_string(i) + "]").str();
        paths.push_back(elemPath);
        collectPaths(elemPath, child.target, index, paths);
      }
    } else {
      std::string childPath = (parent + "." + child.name).str();
      paths.push_back(childPath);
      collectPaths(childPath, child.target, index, paths);
    }
  }
}

// ---------------------------------------------------------------------------
// Instance / array / return conversion
// ---------------------------------------------------------------------------

mlir::Value convertInstance(mlir::OpBuilder &builder,
                            acir::ac::InstanceOp inst) {
  auto target = mlir::SymbolRefAttr::get(builder.getContext(),
                                         inst.getDefinition());
  auto ownerTy = acsim::OwnerType::get(builder.getContext(), target);
  auto args = convertStaticParams(builder, inst.getStaticArgsAttr());
  auto newInst = builder.create<acsim::InstanceOp>(
      inst.getLoc(), ownerTy, inst.getSymName(), target, args,
      kZeroFingerprint);
  return newInst.getResult();
}

mlir::Value convertArray(mlir::OpBuilder &builder, acir::ac::ArrayOp arr) {
  auto target = mlir::SymbolRefAttr::get(builder.getContext(),
                                         arr.getDefinition());
  auto ownerTy = acsim::OwnerType::get(builder.getContext(), target);
  auto arrTy = acsim::ArrayType::get(
      builder.getContext(), builder.getDenseI64ArrayAttr(arr.getShape()),
      ownerTy);
  auto newArr = builder.create<acsim::ArrayOp>(
      arr.getLoc(), arrTy, arr.getSymName(), target, arr.getStaticArgsAttr(),
      kZeroFingerprint, arr.getShape());
  return newArr.getResult();
}

// ---------------------------------------------------------------------------
// Module conversion
// ---------------------------------------------------------------------------

void convertModuleBody(mlir::OpBuilder &builder, acir::ac::ModuleOp acMod) {
  llvm::DenseMap<mlir::Value, mlir::Value> mapping;
  for (auto &op : acMod.getBody().front()) {
    if (auto inst = mlir::dyn_cast<acir::ac::InstanceOp>(op)) {
      auto val = convertInstance(builder, inst);
      for (unsigned i = 0; i < inst.getNumResults(); ++i)
        mapping[inst.getResult(i)] = val;
    } else if (auto arr = mlir::dyn_cast<acir::ac::ArrayOp>(op)) {
      auto val = convertArray(builder, arr);
      for (unsigned i = 0; i < arr.getNumResults(); ++i)
        mapping[arr.getResult(i)] = val;
    } else if (auto ret = mlir::dyn_cast<acir::ac::ReturnOp>(op)) {
      llvm::SmallVector<mlir::Value> ops;
      for (auto v : ret.getOperands()) {
        auto it = mapping.find(v);
        ops.push_back(it != mapping.end() ? it->second : v);
      }
      builder.create<acsim::ReturnOp>(ret.getLoc(), ops);
    }
    // Skip ac.process, ac.queue etc. — Task 13.
  }
}

void convertConcreteModule(mlir::OpBuilder &builder, acir::ac::ModuleOp acMod,
                           llvm::StringMap<std::string> &typeMap) {
  auto fty = acMod.getFunctionType();
  auto iface = buildInterface(builder, fty, typeMap);
  auto statics = convertStaticParams(builder, acMod.getStaticParams());
  auto exports = buildExports(builder, fty.getNumResults());

  auto mod = builder.create<acsim::ModuleOp>(
      acMod.getLoc(), acMod.getSymNameAttr(), iface, statics,
      zeroFingerprint(builder), exports);

  auto *body = new mlir::Block();
  mod.getBody().push_back(body);
  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(body);
  convertModuleBody(builder, acMod);
}

void convertDeclModule(mlir::OpBuilder &builder, mlir::FunctionType fty,
                       mlir::StringAttr name,
                       mlir::DictionaryAttr staticParams,
                       llvm::StringMap<std::string> &typeMap) {
  auto iface = buildInterface(builder, fty, typeMap);
  auto statics = convertStaticParams(builder, staticParams);
  auto exports = buildExports(builder, fty.getNumResults());
  auto mod = builder.create<acsim::ModuleOp>(
      builder.getUnknownLoc(), name, iface, statics,
      zeroFingerprint(builder), exports);
  auto *body = new mlir::Block();
  mod.getBody().push_back(body);
  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(body);
  builder.create<acsim::ReturnOp>(builder.getUnknownLoc(), mlir::ValueRange{});
}

// ---------------------------------------------------------------------------
// Model emission
// ---------------------------------------------------------------------------

void emitTypeDecls(mlir::OpBuilder &builder,
                   llvm::StringMap<std::string> &typeMap) {
  std::set<std::string> sorted;
  for (auto &kv : typeMap)
    sorted.insert(kv.first().str());
  for (auto &name : sorted) {
    builder.create<acsim::TypeOp>(builder.getUnknownLoc(),
                                  builder.getStringAttr(name),
                                  builder.getStringAttr(typeMap[name]),
                                  builder.getStringAttr("value"),
                                  zeroFingerprint(builder));
  }
}

// ---------------------------------------------------------------------------
// Pass
// ---------------------------------------------------------------------------

struct ConvertACIRToACSim final
    : public mlir::PassWrapper<ConvertACIRToACSim,
                               mlir::OperationPass<mlir::ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ConvertACIRToACSim)

  llvm::StringRef getArgument() const override {
    return "convert-acir-to-acsim";
  }
  llvm::StringRef getDescription() const override {
    return "Lower frozen ACIR structure into canonical ACSim";
  }

  void runOnOperation() override {
    mlir::ModuleOp input = getOperation();
    auto &ctx = getContext();

    // Collect ACIR operations.
    llvm::SmallVector<acir::ac::SystemOp> systems;
    llvm::SmallVector<acir::ac::ModuleOp> modules;
    llvm::SmallVector<acir::ac::ModuleExternOp> externs;
    llvm::SmallVector<acir::ac::ModuleGeneratedOp> generated;

    for (auto &op : *input.getBody()) {
      if (auto sys = mlir::dyn_cast<acir::ac::SystemOp>(op))
        systems.push_back(sys);
      else if (auto mod = mlir::dyn_cast<acir::ac::ModuleOp>(op))
        modules.push_back(mod);
      else if (auto ext = mlir::dyn_cast<acir::ac::ModuleExternOp>(op))
        externs.push_back(ext);
      else if (auto gen = mlir::dyn_cast<acir::ac::ModuleGeneratedOp>(op))
        generated.push_back(gen);
    }

    if (modules.empty() && externs.empty() && generated.empty())
      return;

    // Determine root module.
    std::string rootName;
    if (!systems.empty()) {
      rootName = systems.front().getRootName().str();
    } else {
      // Root is the module never referenced as a target.
      llvm::StringSet<> targets;
      for (auto mod : modules) {
        if (mod.getBody().empty())
          continue;
        for (auto &op : mod.getBody().front()) {
          if (auto inst = mlir::dyn_cast<acir::ac::InstanceOp>(op))
            targets.insert(inst.getDefinition());
          else if (auto arr = mlir::dyn_cast<acir::ac::ArrayOp>(op))
            targets.insert(arr.getDefinition());
        }
      }
      for (auto mod : modules) {
        if (!targets.contains(mod.getSymName())) {
          rootName = mod.getSymName().str();
          break;
        }
      }
      if (rootName.empty())
        rootName = modules.back().getSymName().str();
    }
    std::string modelName =
        systems.empty() ? rootName : systems.front().getSymName().str();

    // Build module index for construction order.
    ModuleIndex modIndex;
    for (auto mod : modules)
      modIndex[mod.getSymName()] = mod;

    // Compute construction order.
    llvm::SmallVector<std::string> ctorPaths;
    ctorPaths.push_back(rootName);
    collectPaths(rootName, rootName, modIndex, ctorPaths);

    llvm::SmallVector<mlir::Attribute> ctorAttrs, dtorAttrs;
    mlir::OpBuilder builder(&ctx);
    for (auto &p : ctorPaths)
      ctorAttrs.push_back(builder.getStringAttr(p));
    for (auto it = ctorAttrs.rbegin(); it != ctorAttrs.rend(); ++it)
      dtorAttrs.push_back(*it);

    // Build model attributes.
    auto zfp = zeroFingerprint(builder);
    llvm::SmallVector<mlir::NamedAttribute> fpEntries;
    for (auto key : {"binding_lock", "frozen_acir", "profile", "provider",
                     "schema_set", "toolchain"})
      fpEntries.push_back(builder.getNamedAttr(key, zfp));

    // Collect needed type declarations.
    llvm::StringMap<std::string> typeMap;

    // Clear the input body and create the model.
    llvm::SmallVector<mlir::Operation *> toErase;
    for (auto &op : *input.getBody())
      toErase.push_back(&op);

    builder.setInsertionPointToEnd(input.getBody());
    auto model = builder.create<acsim::ModelOp>(
        input.getLoc(), builder.getStringAttr(modelName),
        builder.getStringAttr("0.1"),
        mlir::SymbolRefAttr::get(builder.getStringAttr(rootName)),
        builder.getArrayAttr(ctorAttrs), builder.getArrayAttr(dtorAttrs),
        builder.getDictionaryAttr(fpEntries));

    for (auto *op : toErase)
      op->erase();

    // Build model body.
    auto *modelBody = new mlir::Block();
    model.getBody().push_back(modelBody);
    builder.setInsertionPointToStart(modelBody);

    // Emit type declarations.
    // First pass: collect type names from all module interfaces.
    for (auto mod : modules)
      collectNeededTypes(mod.getFunctionType(), typeMap);
    for (auto ext : externs)
      collectNeededTypes(ext.getFunctionType(), typeMap);
    for (auto gen : generated)
      collectNeededTypes(gen.getFunctionType(), typeMap);
    emitTypeDecls(builder, typeMap);

    // Emit modules in sorted order.
    struct Entry {
      std::string name;
      mlir::Operation *op;
      enum { Concrete, Extern, Generated } kind;
    };
    llvm::SmallVector<Entry> sortedMods;
    for (auto mod : modules)
      sortedMods.push_back({mod.getSymName().str(), mod.getOperation(),
                            Entry::Concrete});
    for (auto ext : externs)
      sortedMods.push_back({ext.getSymName().str(), ext.getOperation(),
                            Entry::Extern});
    for (auto gen : generated)
      sortedMods.push_back({gen.getSymName().str(), gen.getOperation(),
                            Entry::Generated});
    llvm::sort(sortedMods,
               [](const Entry &a, const Entry &b) { return a.name < b.name; });

    // Rebuild typeMap for emission.
    typeMap.clear();
    for (auto &entry : sortedMods) {
      switch (entry.kind) {
      case Entry::Concrete: {
        auto mod = mlir::cast<acir::ac::ModuleOp>(entry.op);
        convertConcreteModule(builder, mod, typeMap);
        break;
      }
      case Entry::Extern: {
        auto ext = mlir::cast<acir::ac::ModuleExternOp>(entry.op);
        convertDeclModule(builder, ext.getFunctionType(), ext.getSymNameAttr(),
                          ext.getStaticParams(), typeMap);
        break;
      }
      case Entry::Generated: {
        auto gen = mlir::cast<acir::ac::ModuleGeneratedOp>(entry.op);
        convertDeclModule(builder, gen.getFunctionType(), gen.getSymNameAttr(),
                          gen.getStaticParams(), typeMap);
        break;
      }
      }
    }
  }
};

} // namespace

std::unique_ptr<mlir::Pass> createConvertACIRToACSimPass() {
  return std::make_unique<ConvertACIRToACSim>();
}

void populateACIRToACSimTypeConversions(mlir::TypeConverter &) {}

} // namespace acir
