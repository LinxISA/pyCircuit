#include "pyc/Transforms/Passes.h"

#include "pyc/Dialect/PYC/PYCOps.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/STLExtras.h"

using namespace mlir;

namespace pyc {
namespace {

static LogicalResult flattenInstance(pyc::InstanceOp inst, func::FuncOp callee) {
  if (!callee)
    return inst.emitError("missing callee for flatten");
  if (!llvm::hasSingleElement(callee.getBody()))
    return inst.emitError("flatten callee must have a single block body");

  Block &calleeBlock = callee.getBody().front();
  auto ret = dyn_cast<func::ReturnOp>(calleeBlock.getTerminator());
  if (!ret)
    return inst.emitError("flatten callee must terminate with func.return");

  if (callee.getNumArguments() != inst.getNumOperands())
    return inst.emitError("flatten operand count does not match callee signature");
  if (ret.getNumOperands() != inst.getNumResults())
    return inst.emitError("flatten result count does not match callee return");

  IRMapping mapping;
  for (auto [arg, in] : llvm::zip(callee.getArguments(), inst.getOperands()))
    mapping.map(arg, in);

  OpBuilder builder(inst);
  for (Operation &op : calleeBlock.without_terminator()) {
    Operation *cloned = builder.clone(op, mapping);
    for (auto [oldRes, newRes] : llvm::zip(op.getResults(), cloned->getResults()))
      mapping.map(oldRes, newRes);
  }

  for (auto [oldRes, retVal] : llvm::zip(inst.getResults(), ret.getOperands()))
    oldRes.replaceAllUsesWith(mapping.lookup(retVal));
  inst.erase();
  return success();
}

struct FlattenInstancesPass : public PassWrapper<FlattenInstancesPass, OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FlattenInstancesPass)

  StringRef getArgument() const override { return "pyc-flatten-instances"; }
  StringRef getDescription() const override {
    return "Inline all pyc.instance callsites regardless of pyc.kind (flatten module hierarchy)";
  }

  void runOnOperation() override {
    ModuleOp mod = getOperation();

    llvm::DenseSet<func::FuncOp> inlinedCallees;

    bool changed = true;
    while (changed) {
      changed = false;
      for (func::FuncOp caller : mod.getOps<func::FuncOp>()) {
        if (caller.getBody().empty())
          continue;
        for (Operation &op : llvm::make_early_inc_range(caller.getBody().front())) {
          auto inst = dyn_cast<pyc::InstanceOp>(op);
          if (!inst)
            continue;

          auto calleeAttr = inst->getAttrOfType<FlatSymbolRefAttr>("callee");
          if (!calleeAttr)
            continue;
          func::FuncOp callee = mod.lookupSymbol<func::FuncOp>(calleeAttr.getValue());
          if (!callee)
            continue;
          if (callee == caller) {
            inst.emitError("recursive instance call is not supported in flatten mode");
            signalPassFailure();
            return;
          }

          if (failed(flattenInstance(inst, callee))) {
            signalPassFailure();
            return;
          }
          inlinedCallees.insert(callee);
          changed = true;
        }
      }
    }

    // Mark inlined callees as private so SymbolDCE can remove them.
    for (func::FuncOp callee : inlinedCallees) {
      bool stillUsed = false;
      for (func::FuncOp f : mod.getOps<func::FuncOp>()) {
        if (f.getBody().empty())
          continue;
        f.walk([&](pyc::InstanceOp inst) {
          auto attr = inst->getAttrOfType<FlatSymbolRefAttr>("callee");
          if (attr && attr.getValue() == callee.getSymName())
            stillUsed = true;
        });
        if (stillUsed)
          break;
      }
      if (!stillUsed)
        callee.setVisibility(SymbolTable::Visibility::Private);
    }
  }
};

} // namespace

std::unique_ptr<mlir::Pass> createFlattenInstancesPass() { return std::make_unique<FlattenInstancesPass>(); }

static PassRegistration<FlattenInstancesPass> pass;

} // namespace pyc
