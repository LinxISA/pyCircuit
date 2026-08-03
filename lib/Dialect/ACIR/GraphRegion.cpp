#include "acir/Dialect/ACIR/GraphRegion.h"

#include "acir/Dialect/ACIR/ACIRDialect.h"
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

void StructuralProviderRegistry::registerExternal(StringRef name) {
  externalProviders.insert(name);
}

void StructuralProviderRegistry::registerGenerator(StringRef name) {
  generatorProviders.insert(name);
}

bool StructuralProviderRegistry::hasExternal(StringRef name) const {
  return externalProviders.contains(name);
}

bool StructuralProviderRegistry::hasGenerator(StringRef name) const {
  return generatorProviders.contains(name);
}

StructuralProviderRegistry &
getStructuralProviderRegistry(MLIRContext *context) {
  auto *dialect = context->getOrLoadDialect<ACIRDialect>();
  auto *interface =
      dialect->getRegisteredInterface<StructuralProviderDialectInterface>();
  assert(interface && "ACIR structural provider interface must be registered");
  return interface->getRegistry();
}

bool isConcreteStaticValue(Attribute value) {
  if (!value)
    return false;
  if (isa<IntegerAttr, BoolAttr, StringAttr, TypeAttr, SymbolRefAttr>(value))
    return true;
  if (auto dictionary = dyn_cast<DictionaryAttr>(value)) {
    if (dictionary.size() != 2)
      return false;
    auto amount = dictionary.getAs<IntegerAttr>("value");
    auto unit = dictionary.getAs<StringAttr>("unit");
    return amount && amount.getType().isSignlessInteger(64) && unit &&
           !unit.getValue().empty();
  }
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
    if (!isa_and_nonnull<ModuleOp>(root)) {
      if (system.getSelected())
        return system.emitOpError(
            "selected root must resolve to a materialized ac.module");
      return system.emitOpError() << "root '" << system.getRootAttr()
                                  << "' does not resolve to ac.module";
    }
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

  constexpr uint64_t maxHierarchyDepth = 1024;
  constexpr uint64_t maxHierarchyOwners = 1048576;
  struct ExpansionStats {
    uint64_t owners = 0;
    uint64_t depth = 0;
  };
  auto saturatedAdd = [](uint64_t left, uint64_t right, uint64_t cap) {
    return left >= cap || right >= cap || left > cap - right ? cap
                                                             : left + right;
  };
  auto saturatedMultiply = [](uint64_t left, uint64_t right, uint64_t cap) {
    return left && right > cap / left ? cap : std::min(left * right, cap);
  };
  llvm::DenseMap<Operation *, ExpansionStats> expansionMemo;
  std::function<ExpansionStats(Operation *)> expansionStats =
      [&](Operation *definition) -> ExpansionStats {
    auto module = dyn_cast_or_null<ModuleOp>(definition);
    if (!module)
      return {};
    if (auto found = expansionMemo.find(module); found != expansionMemo.end())
      return found->second;
    ExpansionStats stats;
    for (Operation &child : module.getBody().front()) {
      ExpansionStats childStats;
      uint64_t localOwners = 0;
      uint64_t localDepth = 0;
      if (auto instance = dyn_cast<InstanceOp>(child)) {
        childStats = expansionStats(
            SymbolTable::lookupSymbolIn(file, instance.getDefinitionAttr()));
        localOwners =
            saturatedAdd(1, childStats.owners, maxHierarchyOwners + 1);
        localDepth = saturatedAdd(1, childStats.depth, maxHierarchyDepth + 1);
      } else if (auto array = dyn_cast<ArrayOp>(child)) {
        uint64_t count = 1;
        for (int64_t extent : array.getShape())
          count = saturatedMultiply(count, static_cast<uint64_t>(extent),
                                    maxHierarchyOwners + 1);
        childStats = expansionStats(
            SymbolTable::lookupSymbolIn(file, array.getDefinitionAttr()));
        uint64_t perElement =
            saturatedAdd(1, childStats.owners, maxHierarchyOwners + 1);
        localOwners = saturatedAdd(
            1, saturatedMultiply(count, perElement, maxHierarchyOwners + 1),
            maxHierarchyOwners + 1);
        localDepth = count == 0 ? 1
                                : saturatedAdd(2, childStats.depth,
                                               maxHierarchyDepth + 1);
      } else if (auto instances = dyn_cast<InstancesOp>(child)) {
        localOwners = 1;
        localDepth = 1;
        for (Attribute reference : instances.getDefinitions()) {
          childStats = expansionStats(SymbolTable::lookupSymbolIn(
              file, cast<FlatSymbolRefAttr>(reference)));
          localOwners = saturatedAdd(
              localOwners,
              saturatedAdd(1, childStats.owners, maxHierarchyOwners + 1),
              maxHierarchyOwners + 1);
          localDepth =
              std::max(localDepth, saturatedAdd(2, childStats.depth,
                                                maxHierarchyDepth + 1));
        }
      }
      stats.owners =
          saturatedAdd(stats.owners, localOwners, maxHierarchyOwners + 1);
      stats.depth = std::max(stats.depth, localDepth);
    }
    expansionMemo[module] = stats;
    return stats;
  };
  ExpansionStats selectedStats = expansionStats(
      SymbolTable::lookupSymbolIn(file, selectedSystem.getRootAttr()));
  if (selectedStats.depth > maxHierarchyDepth)
    return selectedSystem.emitOpError()
           << "elaborated hierarchy depth exceeds bound " << maxHierarchyDepth;
  if (saturatedAdd(1, selectedStats.owners, maxHierarchyOwners + 1) >
      maxHierarchyOwners)
    return selectedSystem.emitOpError()
           << "elaborated hierarchy owner count exceeds bound "
           << maxHierarchyOwners;

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
        std::string collectionPath =
            (parentPath + "." + instances.getPath()).str();
        std::string collectionId =
            (parentId + "/" + instances.getStableId()).str();
        if (failed(registerOwner(&child, collectionPath, collectionId)))
          return failure();
        for (size_t index = 0; index < instances.getDefinitions().size();
             ++index) {
          StringRef segment =
              cast<StringAttr>(instances.getPaths()[index]).getValue();
          StringRef localId =
              cast<StringAttr>(instances.getStableIds()[index]).getValue();
          std::string path = (collectionPath + "." + segment).str();
          std::string id = (collectionId + "/" + localId).str();
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
