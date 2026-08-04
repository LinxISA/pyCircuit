#include "acir/Transforms/Passes.h"

#include "acir/Analysis/ModelAnalysis.h"
#include "acir/Dialect/ACIR/ACIROps.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/SmallVector.h"

using namespace mlir;

namespace acir {
namespace {

Operation *lookupOwner(ModuleOp model, SymbolRefAttr reference) {
  if (!reference)
    return nullptr;
  Operation *root =
      SymbolTable::lookupSymbolIn(model, reference.getRootReference());
  if (reference.getNestedReferences().empty())
    return root;
  auto definition = dyn_cast_or_null<ac::ModuleOp>(root);
  if (!definition || definition.getBody().empty() ||
      reference.getNestedReferences().size() != 1)
    return nullptr;
  StringRef local = reference.getNestedReferences().front().getValue();
  for (Operation &operation : definition.getBody().front()) {
    auto name =
        operation.getAttrOfType<StringAttr>(SymbolTable::getSymbolAttrName());
    if (name && name.getValue() == local)
      return &operation;
  }
  return nullptr;
}

FailureOr<DictionaryAttr> findManifestOwner(ArrayAttr manifest,
                                            SymbolRefAttr owner,
                                            StringRef kind = {}) {
  for (Attribute attribute : manifest) {
    auto record = cast<DictionaryAttr>(attribute);
    if (record.getAs<SymbolRefAttr>("owner") != owner)
      continue;
    if (!kind.empty() && record.getAs<StringAttr>("kind").getValue() != kind)
      continue;
    return record;
  }
  return failure();
}

LogicalResult freezeTopology(ModuleOp model) {
  if (failed(canonicalizeModel(model)))
    return failure();
  if (isTopologyFrozen(model))
    return verifyModel(model);

  ModelAnalysis analysis(model);
  if (failed(analysis.verify()) || failed(analysis.verifyFreezeContracts()))
    return failure();

  ac::SystemOp selected;
  for (ac::SystemOp system : model.getOps<ac::SystemOp>())
    if (system.getSelected()) {
      selected = system;
      break;
    }
  if (!selected)
    return model.emitError(
        "topology freeze requires exactly one selected ac.system");
  if (!selected.getPrimaryWorkloadAttr())
    return selected.emitOpError(
        "selected system requires one canonical primary workload at topology "
        "freeze");

  FailureOr<ArrayAttr> ownerManifest = analysis.buildFrozenOwnerManifest();
  if (failed(ownerManifest))
    return failure();
  Builder builder(model.getContext());
  model->setAttr("ac.freeze_epoch", builder.getStringAttr("0.1"));
  model->setAttr(
      "ac.frozen_system",
      FlatSymbolRefAttr::get(model.getContext(), selected.getSymName()));
  model->setAttr("ac.frozen_owners", *ownerManifest);

  SymbolRefAttr workload = selected.getPrimaryWorkloadAttr();
  FailureOr<DictionaryAttr> workloadOwner =
      findManifestOwner(*ownerManifest, workload, "ac.process");
  if (failed(workloadOwner))
    return selected.emitOpError()
           << "primary workload '" << workload
           << "' has no unique elaborated absolute owner";
  model->setAttr(
      "ac.frozen_primary_workload",
      builder.getDictionaryAttr({
          builder.getNamedAttr("reference", workload),
          builder.getNamedAttr("path", (*workloadOwner).get("path")),
          builder.getNamedAttr("stable_id", (*workloadOwner).get("stable_id")),
      }));

  SmallVector<Attribute> frozenInstrumentation;
  for (Attribute attribute : selected.getInstrumentation()) {
    auto reference = cast<SymbolRefAttr>(attribute);
    ArrayRef<FlatSymbolRefAttr> nested = reference.getNestedReferences();
    SymbolRefAttr processReference = SymbolRefAttr::get(
        model.getContext(), reference.getRootReference(), {nested.front()});
    FailureOr<DictionaryAttr> processOwner =
        findManifestOwner(*ownerManifest, processReference, "ac.process");
    if (failed(processOwner))
      return selected.emitOpError() << "instrumentation '" << reference
                                    << "' has no elaborated process owner";
    StringRef local = nested.back().getValue();
    frozenInstrumentation.push_back(builder.getDictionaryAttr({
        builder.getNamedAttr("reference", reference),
        builder.getNamedAttr(
            "path", builder.getStringAttr(
                        ((*processOwner).getAs<StringAttr>("path").getValue() +
                         "." + local)
                            .str())),
        builder.getNamedAttr(
            "stable_id",
            builder.getStringAttr(
                ((*processOwner).getAs<StringAttr>("stable_id").getValue() +
                 "/" + local)
                    .str())),
    }));
  }
  model->setAttr("ac.frozen_instrumentation",
                 builder.getArrayAttr(frozenInstrumentation));

  // Persist the relevant absolute owner set on each declaration. Reused
  // definitions intentionally receive multiple records rather than a lossy
  // definition-local identity.
  llvm::DenseMap<Operation *, SmallVector<Attribute>> byDeclaration;
  for (Attribute attribute : *ownerManifest) {
    auto record = cast<DictionaryAttr>(attribute);
    if (record.getAs<StringAttr>("kind").getValue() == "ac.system_root")
      continue;
    if (Operation *declaration =
            lookupOwner(model, record.getAs<SymbolRefAttr>("owner")))
      byDeclaration[declaration].push_back(record);
  }
  for (auto &[declaration, records] : byDeclaration) {
    llvm::sort(records, [](Attribute left, Attribute right) {
      auto leftRecord = cast<DictionaryAttr>(left);
      auto rightRecord = cast<DictionaryAttr>(right);
      return leftRecord.getAs<StringAttr>("path").getValue() <
             rightRecord.getAs<StringAttr>("path").getValue();
    });
    declaration->setAttr("ac.frozen_owners", builder.getArrayAttr(records));
  }

  for (ac::ModuleOp module : model.getOps<ac::ModuleOp>()) {
    if (module.getBody().empty())
      continue;
    for (Operation &operation : module.getBody().front())
      if (isa<ac::RequireOp, ac::EnsureOp>(operation))
        operation.setAttr("ac.freeze_proven", builder.getBoolAttr(true));
  }

  for (Attribute attribute : *ownerManifest) {
    auto record = cast<DictionaryAttr>(attribute);
    if (record.getAs<StringAttr>("kind").getValue() != "ac.process")
      continue;
    ArrayAttr traces = record.getAs<ArrayAttr>("trace_sources");
    if (!traces)
      continue;
    auto process = dyn_cast_or_null<ac::ProcessOp>(
        lookupOwner(model, record.getAs<SymbolRefAttr>("owner")));
    if (!process)
      continue;
    process.getBody().walk([&](ac::TraceOpenOp trace) {
      if (!llvm::is_contained(traces, builder.getStringAttr(trace.getSource())))
        return;
      trace->setAttr(
          "ac.frozen_owner",
          builder.getDictionaryAttr({
              builder.getNamedAttr("path", record.get("path")),
              builder.getNamedAttr("stable_id", record.get("stable_id")),
              builder.getNamedAttr("source", trace.getSourceAttr()),
          }));
    });
  }

  model->setAttr("ac.topology_frozen", builder.getBoolAttr(true));
  model->setAttr("ac.topology_digest",
                 builder.getStringAttr(analysis.computeTopologyDigest()));
  return verifyModel(model);
}

#define GEN_PASS_DEF_FREEZETOPOLOGYPASS
#include "acir/Transforms/Passes.h.inc"

struct FreezeTopologyPass : impl::FreezeTopologyPassBase<FreezeTopologyPass> {
  void runOnOperation() override {
    if (failed(freezeTopology(getOperation())))
      signalPassFailure();
  }
};

} // namespace

std::unique_ptr<Pass> createFreezeTopologyPass() {
  return std::make_unique<FreezeTopologyPass>();
}

} // namespace acir
