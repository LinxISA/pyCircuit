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

struct EliminateDeadStatePass : public PassWrapper<EliminateDeadStatePass, OperationPass<func::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(EliminateDeadStatePass)

  StringRef getArgument() const override { return "pyc-eliminate-dead-state"; }
  StringRef getDescription() const override {
    return "Eliminate dead sequential primitives with unobservable state";
  }

  void runOnOperation() override {
    func::FuncOp f = getOperation();
    bool changed = true;
    while (changed) {
      changed = false;
      llvm::SmallVector<Operation *> toErase;

      f.walk([&](Operation *op) {
        if (shouldKeep(op))
          return;
        if (auto r = dyn_cast<pyc::RegOp>(op)) {
          if (r.getQ().use_empty())
            toErase.push_back(op);
          return;
        }
        if (auto fifo = dyn_cast<pyc::FifoOp>(op)) {
          if (allResultsUnused(fifo))
            toErase.push_back(op);
          return;
        }
        if (auto mem = dyn_cast<pyc::ByteMemOp>(op)) {
          if (mem.getRdata().use_empty())
            toErase.push_back(op);
          return;
        }
        if (auto mem = dyn_cast<pyc::SyncMemOp>(op)) {
          if (mem.getRdata().use_empty())
            toErase.push_back(op);
          return;
        }
        if (auto mem = dyn_cast<pyc::SyncMemDPOp>(op)) {
          if (mem.getRdata0().use_empty() && mem.getRdata1().use_empty())
            toErase.push_back(op);
          return;
        }
        if (auto fifo = dyn_cast<pyc::AsyncFifoOp>(op)) {
          if (allResultsUnused(fifo))
            toErase.push_back(op);
          return;
        }
        if (auto s = dyn_cast<pyc::CdcSyncOp>(op)) {
          if (s.getOut().use_empty())
            toErase.push_back(op);
          return;
        }
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

std::unique_ptr<::mlir::Pass> createEliminateDeadStatePass() { return std::make_unique<EliminateDeadStatePass>(); }

static PassRegistration<EliminateDeadStatePass> pass;

} // namespace pyc
