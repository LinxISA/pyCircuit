#include "acir/Dialect/ACIR/GraphRegion.h"

#include "acir/Dialect/ACIR/ACIROps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"

#include <functional>

using namespace mlir;

namespace acir::ac {
namespace {

bool validSegment(StringRef segment) {
  return !segment.empty() && llvm::all_of(segment, [](char c) {
    return llvm::isAlnum(c) || c == '_' || c == '-';
  });
}

bool isModuleDeclaration(Operation *op) {
  return isa_and_nonnull<ModuleOp, ModuleExternOp, ModuleGeneratedOp>(op);
}

SmallVector<Operation *> instantiatedDefinitions(ModuleOp module,
                                                 mlir::ModuleOp file) {
  SmallVector<Operation *> definitions;
  for (Operation &child : module.getBody().front()) {
    if (auto instance = dyn_cast<InstanceOp>(child)) {
      definitions.push_back(
          SymbolTable::lookupSymbolIn(file, instance.getDefinitionAttr()));
    } else if (auto array = dyn_cast<ArrayOp>(child)) {
      definitions.push_back(
          SymbolTable::lookupSymbolIn(file, array.getDefinitionAttr()));
    } else if (auto instances = dyn_cast<InstancesOp>(child)) {
      for (Attribute definition : instances.getDefinitions())
        definitions.push_back(SymbolTable::lookupSymbolIn(
            file, cast<FlatSymbolRefAttr>(definition)));
    }
  }
  return definitions;
}

LogicalResult verifyFiniteInstantiationGraph(mlir::ModuleOp file) {
  enum class State : uint8_t { Unvisited, Active, Complete };
  llvm::DenseMap<Operation *, State> states;
  SmallVector<ModuleOp> stack;
  std::function<LogicalResult(ModuleOp)> visit =
      [&](ModuleOp module) -> LogicalResult {
    states[module] = State::Active;
    stack.push_back(module);
    for (Operation *definition : instantiatedDefinitions(module, file)) {
      auto child = dyn_cast_or_null<ModuleOp>(definition);
      if (!child)
        continue;
      if (states.lookup(child) == State::Active) {
        auto start = llvm::find(stack, child);
        InFlightDiagnostic diagnostic =
            module.emitOpError("recursive module instantiation cycle: ");
        for (auto current = start; current != stack.end(); ++current)
          diagnostic << '@' << current->getSymName() << " -> ";
        diagnostic << '@' << child.getSymName();
        return failure();
      }
      if (states.lookup(child) == State::Unvisited && failed(visit(child)))
        return failure();
    }
    stack.pop_back();
    states[module] = State::Complete;
    return success();
  };

  for (ModuleOp module : file.getOps<ModuleOp>())
    if (states.lookup(module) == State::Unvisited && failed(visit(module)))
      return failure();
  return success();
}

} // namespace

bool isConcreteStaticValue(Attribute value) {
  if (!value)
    return false;
  if (isa<IntegerAttr, BoolAttr, StringAttr, TypeAttr, SymbolRefAttr, UnitAttr>(
          value))
    return true;
  if (auto array = dyn_cast<ArrayAttr>(value))
    return llvm::all_of(array, isConcreteStaticValue);
  if (auto dictionary = dyn_cast<DictionaryAttr>(value))
    return llvm::all_of(dictionary, [](NamedAttribute item) {
      return isConcreteStaticValue(item.getValue());
    });
  return false;
}

std::string buildArrayElementPath(StringRef base, ArrayRef<int64_t> indices) {
  std::string path = base.str();
  for (int64_t index : indices) {
    path.push_back('[');
    path.append(std::to_string(index));
    path.push_back(']');
  }
  return path;
}

LogicalResult verifyGraphStructure(Operation *topLevel) {
  auto file = dyn_cast<mlir::ModuleOp>(topLevel);
  if (!file)
    return success();

  unsigned selected = 0;
  SystemOp selectedSystem;
  for (SystemOp system : file.getOps<SystemOp>()) {
    if (system.getSelected()) {
      ++selected;
      selectedSystem = system;
    }
    Operation *root = SymbolTable::lookupSymbolIn(file, system.getRootAttr());
    if (!isModuleDeclaration(root))
      return system.emitOpError() << "root '" << system.getRootAttr()
                                  << "' does not resolve to a module";
    if (FlatSymbolRefAttr workload = system.getPrimaryWorkloadAttr()) {
      Operation *target = SymbolTable::lookupSymbolIn(file, workload);
      if (!target)
        return system.emitOpError()
               << "primary workload '" << workload << "' is unresolved";
      if (target->getName().getStringRef() != "ac.process")
        return system.emitOpError() << "primary workload '" << workload
                                    << "' does not resolve to ac.process";
    }
  }
  if (!file.getOps<SystemOp>().empty() && selected != 1)
    return file.emitError() << "ACIR file requires exactly one selected "
                               "ac.system, found "
                            << selected;
  if (failed(verifyFiniteInstantiationGraph(file)))
    return failure();
  if (!selectedSystem)
    return success();

  llvm::StringSet<> paths;
  llvm::StringSet<> stableIds;
  auto registerOwner = [&](Operation *op, StringRef path,
                           StringRef stableId) -> LogicalResult {
    if (!paths.insert(path).second)
      return op->emitOpError()
             << "duplicate elaborated hierarchy path '" << path << "'";
    if (!stableIds.insert(stableId).second)
      return op->emitOpError()
             << "duplicate elaborated stable ownership id '" << stableId << "'";
    return success();
  };

  std::function<LogicalResult(Operation *, StringRef, StringRef)> elaborate =
      [&](Operation *definition, StringRef parentPath,
          StringRef parentId) -> LogicalResult {
    auto module = dyn_cast<ModuleOp>(definition);
    if (!module)
      return success();
    for (Operation &child : module.getBody().front()) {
      if (auto instance = dyn_cast<InstanceOp>(child)) {
        std::string path = (parentPath + "." + instance.getPath()).str();
        std::string id = (parentId + "/" + instance.getStableId()).str();
        if (failed(registerOwner(&child, path, id)) ||
            failed(elaborate(
                SymbolTable::lookupSymbolIn(file, instance.getDefinitionAttr()),
                path, id)))
          return failure();
      } else if (auto array = dyn_cast<ArrayOp>(child)) {
        std::string basePath = (parentPath + "." + array.getPath()).str();
        std::string baseId = (parentId + "/" + array.getStableId()).str();
        if (failed(registerOwner(&child, basePath, baseId)))
          return failure();
        uint64_t count = 1;
        for (int64_t extent : array.getShape())
          count *= static_cast<uint64_t>(extent);
        SmallVector<int64_t> indices(array.getShape().size());
        for (uint64_t ordinal = 0; ordinal < count; ++ordinal) {
          uint64_t remainder = ordinal;
          for (int64_t dimension = array.getShape().size() - 1; dimension >= 0;
               --dimension) {
            int64_t extent = array.getShape()[dimension];
            indices[dimension] = remainder % static_cast<uint64_t>(extent);
            remainder /= static_cast<uint64_t>(extent);
          }
          std::string path = buildArrayElementPath(basePath, indices);
          std::string id = buildArrayElementPath(baseId, indices);
          if (failed(registerOwner(&child, path, id)) ||
              failed(elaborate(
                  SymbolTable::lookupSymbolIn(file, array.getDefinitionAttr()),
                  path, id)))
            return failure();
        }
      } else if (auto instances = dyn_cast<InstancesOp>(child)) {
        for (size_t index = 0; index < instances.getDefinitions().size();
             ++index) {
          StringRef segment =
              cast<StringAttr>(instances.getPaths()[index]).getValue();
          StringRef localId =
              cast<StringAttr>(instances.getStableIds()[index]).getValue();
          std::string path = (parentPath + "." + segment).str();
          std::string id = (parentId + "/" + localId).str();
          auto target =
              cast<FlatSymbolRefAttr>(instances.getDefinitions()[index]);
          if (failed(registerOwner(&child, path, id)) ||
              failed(elaborate(SymbolTable::lookupSymbolIn(file, target), path,
                               id)))
            return failure();
        }
      }
    }
    return success();
  };

  if (!validSegment(selectedSystem.getRootName()))
    return selectedSystem.emitOpError(
        "root instance name must be one stable hierarchy segment");
  std::string root = selectedSystem.getRootName().str();
  paths.insert(root);
  stableIds.insert(root);
  return elaborate(
      SymbolTable::lookupSymbolIn(file, selectedSystem.getRootAttr()), root,
      root);
}

} // namespace acir::ac
