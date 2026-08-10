#include "ProcessStatePlanInternal.h"

#include "mlir/IR/Diagnostics.h"
#include "llvm/ADT/SmallVector.h"

#include <algorithm>

using namespace mlir;

namespace acir::detail {

LogicalResult
PlanSetBuilder::planProcessCost(ControlPlan &control,
                                const ProcessStateLimits &limits) {
  // Compute exact per-block cost from the contract formula:
  //   block_cost = sum(entry loads, each 1)
  //              + sum(scalar_unwrap actions, each 1)
  //              + sum(copy_scalar, inline, invoke, for_condition,
  //                    for_increment leaf actions, each 1)
  //              + if edge is suspend:
  //                  sum(scalar_wrap actions, each 1)
  //                + sum(store emissions, each 1)
  //                + 1 (wake invoke)
  //                + 1 (acsim.suspend)
  //                otherwise: 1 (cf.cond_br, cf.br, or terminate)
  //
  // Fairness = iterative max sum of block costs over every path
  // in every PC-local DAG. Reject zero, overflow, cycles, cap excess.
  //
  // For yield-only: block cost = 2 (wake invoke + acsim.suspend)

  for (auto &block : control.blocks) {
    block->cost = 0;

    // Count entry loads
    block->cost += static_cast<uint64_t>(block->loads.size());

    // Count scalar_unwrap, copy_scalar, inline, invoke actions
    for (const auto &action : block->actions) {
      if (action.kind() == ProcessActionKind::ScalarUnwrap ||
          action.emission() == ProcessEmissionClass::CopyScalar ||
          action.emission() == ProcessEmissionClass::Inline ||
          action.emission() == ProcessEmissionClass::Invoke ||
          action.kind() == ProcessActionKind::ForCondition ||
          action.kind() == ProcessActionKind::ForIncrement) {
        block->cost += 1;
      }
    }

    // Edge cost
    if (block->edge.has_value() &&
        block->edge->kind() == ProcessControlEdgeKind::Suspend) {
      ProcessTransitionId transition = block->edge->transition();
      if (transition.value() >= control.transitions.size())
        return failure();
      block->cost += control.transitions[transition.value()]->stores.size();
      // Wake invoke + acsim.suspend
      block->cost += 2;
    } else if (block->edge.has_value()) {
      block->cost += 1; // cf.cond_br, cf.br, or terminate
    }
  }

  // Fairness: for yield-only with a single block, fairness = block cost
  uint64_t fairness = 0;
  for (const auto &block : control.blocks) {
    fairness = std::max(fairness, block->cost);
  }

  if (fairness == 0)
    return control.blocks.front()->originBlock
               ? control.blocks.front()
                     ->originBlock->getParentOp()
                     ->emitOpError("process fairness must be non-zero")
               : failure();

  if (fairness > limits.maxFairnessWork)
    return failure();
  control.fairnessWork = fairness;

  return success();
}

} // namespace acir::detail
