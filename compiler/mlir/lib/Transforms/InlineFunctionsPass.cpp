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

static bool isInlineFunction(func::FuncOp f) {
  if (!f)
    return false;
  auto kindAttr = f->getAttrOfType<StringAttr>("pyc.kind");
  if (!kindAttr)
    return false;
  return kindAttr.getValue() == "function";
}

static LogicalResult inlineFunctionInstance(pyc::InstanceOp inst, func::FuncOp callee) {
  if (!callee)
    return inst.emitError("missing callee for inline function call");
  if (!llvm::hasSingleElement(callee.getBody()))
    return inst.emitError("inline function callee must have a single block body");

  Block &calleeBlock = callee.getBody().front();
  auto ret = dyn_cast<func::ReturnOp>(calleeBlock.getTerminator());
  if (!ret)
    return inst.emitError("inline function callee must terminate with func.return");

  if (callee.getNumArguments() != inst.getNumOperands())
    return inst.emitError("inline function operand count does not match callee signature");
  if (ret.getNumOperands() != inst.getNumResults())
    return inst.emitError("inline function result count does not match callee return");

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

struct InlineFunctionsPass : public PassWrapper<InlineFunctionsPass, OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(InlineFunctionsPass)

  StringRef getArgument() const override { return "pyc-inline-functions"; }
  StringRef getDescription() const override {
    return "Inline pyc.instance callsites that target funcs tagged with pyc.kind=\"function\"";
  }

  void runOnOperation() override {
    ModuleOp mod = getOperation();

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
          if (!isInlineFunction(callee))
            continue;
          if (callee == caller) {
            inst.emitError("recursive inline function call is not supported");
            signalPassFailure();
            return;
          }

          if (failed(inlineFunctionInstance(inst, callee))) {
            signalPassFailure();
            return;
          }
          changed = true;
        }
      }
    }
  }
};

} // namespace

std::unique_ptr<mlir::Pass> createInlineFunctionsPass() { return std::make_unique<InlineFunctionsPass>(); }

static PassRegistration<InlineFunctionsPass> pass;

} // namespace pyc
