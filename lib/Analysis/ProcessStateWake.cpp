#include "ProcessStatePlanInternal.h"

#include "acir/Dialect/ACIR/ACIROps.h"

#include "llvm/ADT/SmallVector.h"

using namespace mlir;

namespace acir::detail {

// Wake planning is primarily done inline during continuation planning
// (see ProcessStateContinuation.cpp). This file exists as a separate
// compilation unit for the wake-specific validation and declaration
// resolution that can be called after the initial control plan is built.
//
// planProcessWakes is the separable wake planning pass.

} // namespace acir::detail
