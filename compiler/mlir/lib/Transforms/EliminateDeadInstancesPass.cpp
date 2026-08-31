#include "pyc/Transforms/Passes.h"

#include "pyc/Dialect/PYC/PYCOps.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/SmallVector.h"

using namespace mlir;

namespace pyc {
namespace {

template <typename OpT>
static bool allResultsUnused(OpT op) {
  for (Value v : op->getResults()) {
    if (!v.use_empty())
      return false;
  }
  return true;
}

static bool shouldKeep(Operation *op) {
  if (auto keep = op->getAttrOfType<BoolAttr>("pyc.debug_keep"))
    return keep.getValue();
  return false;
}

struct EliminateDeadInstancesPass : public PassWrapper<EliminateDeadInstancesPass, OperationPass<func::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(EliminateDeadInstancesPass)

  StringRef getArgument() const override { return "pyc-eliminate-dead-instances"; }
  StringRef getDescription() const override {
    return "Eliminate dead pyc.instance ops unless explicitly kept for probe/debug registration";
  }

  void runOnOperation() override {
    func::FuncOp f = getOperation();
    bool changed = true;
    while (changed) {
      changed = false;
      llvm::SmallVector<Operation *> toErase;

      f.walk([&](pyc::InstanceOp inst) {
        if (shouldKeep(inst))
          return;
        if (allResultsUnused(inst))
          toErase.push_back(inst.getOperation());
      });

      if (toErase.empty())
        break;
      for (Operation *op : toErase)
        op->erase();
      changed = true;
    }
  }
};

} // namespace

std::unique_ptr<::mlir::Pass> createEliminateDeadInstancesPass() {
  return std::make_unique<EliminateDeadInstancesPass>();
}

static PassRegistration<EliminateDeadInstancesPass> pass;

} // namespace pyc
