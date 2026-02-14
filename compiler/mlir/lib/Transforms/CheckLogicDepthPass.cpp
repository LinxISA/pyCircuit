#include "pyc/Transforms/Passes.h"

#include "pyc/Dialect/PYC/PYCOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

#include <algorithm>
#include <limits>

using namespace mlir;

namespace pyc {
namespace {

static bool isSequentialOp(Operation *op) {
  return isa<pyc::RegOp, pyc::FifoOp, pyc::ByteMemOp, pyc::SyncMemOp, pyc::SyncMemDPOp, pyc::AsyncFifoOp, pyc::CdcSyncOp,
             pyc::InstanceOp>(op);
}

static int64_t opCost(Operation *op) {
  if (isSequentialOp(op))
    return 0;
  if (isa<pyc::WireOp, pyc::AssignOp, pyc::AliasOp, pyc::ConstantOp, arith::ConstantOp>(op))
    return 0;
  return 1;
}

class CheckLogicDepthPass : public PassWrapper<CheckLogicDepthPass, OperationPass<func::FuncOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(CheckLogicDepthPass)

  explicit CheckLogicDepthPass(unsigned depth = 32) : maxDepthLimit(depth) {}

  StringRef getArgument() const override { return "pyc-check-logic-depth"; }
  StringRef getDescription() const override {
    return "Check strict combinational depth and compute WNS/TNS against LOGIC_DEPTH";
  }

  void runOnOperation() override {
    func::FuncOp f = getOperation();
    const int64_t limit = static_cast<int64_t>(maxDepthLimit);

    llvm::DenseMap<Value, int64_t> memo;
    llvm::DenseSet<Value> visiting;

    auto depthOf = [&](auto &&self, Value v) -> int64_t {
      if (!v)
        return 0;
      auto it = memo.find(v);
      if (it != memo.end())
        return it->second;
      if (!visiting.insert(v).second)
        return limit + 1;

      int64_t d = 0;
      Operation *def = v.getDefiningOp();
      if (!def || isSequentialOp(def)) {
        d = 0;
      } else {
        int64_t inMax = 0;
        for (Value in : def->getOperands())
          inMax = std::max(inMax, self(self, in));
        d = inMax + opCost(def);
      }

      visiting.erase(v);
      memo.try_emplace(v, d);
      return d;
    };

    int64_t maxDepth = 0;
    int64_t wns = std::numeric_limits<int64_t>::max();
    int64_t tns = 0;
    bool failed = false;

    auto observeEndpoint = [&](Operation *op, Value v) {
      int64_t d = depthOf(depthOf, v);
      maxDepth = std::max(maxDepth, d);
      int64_t slack = limit - d;
      wns = std::min(wns, slack);
      if (slack < 0)
        tns += slack;
      if (d > limit) {
        op->emitError("logic depth exceeds limit: depth=") << d << " limit=" << limit;
        failed = true;
      }
    };

    f.walk([&](Operation *op) {
      if (auto ret = dyn_cast<func::ReturnOp>(op)) {
        for (Value v : ret.getOperands())
          observeEndpoint(op, v);
        return;
      }
      if (auto a = dyn_cast<pyc::AssertOp>(op)) {
        observeEndpoint(op, a.getCond());
        return;
      }
      if (isSequentialOp(op)) {
        for (Value v : op->getOperands())
          observeEndpoint(op, v);
      }
    });

    if (wns == std::numeric_limits<int64_t>::max())
      wns = limit;

    auto i64Ty = IntegerType::get(f.getContext(), 64);
    f->setAttr("pyc.logic_depth.max", IntegerAttr::get(i64Ty, maxDepth));
    f->setAttr("pyc.logic_depth.wns", IntegerAttr::get(i64Ty, wns));
    f->setAttr("pyc.logic_depth.tns", IntegerAttr::get(i64Ty, tns));

    if (failed)
      signalPassFailure();
  }

private:
  unsigned maxDepthLimit = 32;
};

} // namespace

std::unique_ptr<::mlir::Pass> createCheckLogicDepthPass(unsigned logicDepth) {
  return std::make_unique<CheckLogicDepthPass>(logicDepth);
}

static PassRegistration<CheckLogicDepthPass> pass;

} // namespace pyc
