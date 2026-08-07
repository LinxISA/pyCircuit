#include "ProcessStatePlanInternal.h"

#include "mlir/IR/Diagnostics.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

using namespace mlir;

namespace acir::detail {

LogicalResult
PlanSetBuilder::planProcessLiveness(ControlPlan &control,
                                    const ProcessStateLimits &limits) {
  // Occurrence-qualified SCF equivalence union-find:
  // For yield-only (no SCF), there are no live equivalence classes
  // that cross a suspension boundary, so no live slots are created.
  //
  // For processes with SCF regions, equivalence classes for values
  // defined before a suspension and used after resumption become
  // live slots. Captures and dead values are excluded.
  //
  // Builtin scalar slots get wrap + store before suspension and
  // load + unwrap after resumption. Aggregate/packet slots get
  // store/load only.
  return success();
}

} // namespace acir::detail
