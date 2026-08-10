#include "ProcessStatePlanInternal.h"

#include "mlir/IR/Diagnostics.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/FormatVariadic.h"

#include <map>

using namespace mlir;

namespace acir::detail {

LogicalResult
PlanSetBuilder::planProcessLiveness(ControlPlan &control,
                                    const ProcessStateLimits &limits) {
  std::map<std::string, ProcessLiveSlotId> slotsByPath;
  for (auto &transition : control.transitions) {
    if (transition->sourcePc == transition->targetPc)
      continue;
    auto sourceBlock = llvm::find_if(control.blocks, [&](const auto &block) {
      return block->pc == transition->sourcePc;
    });
    auto targetBlock = llvm::find_if(control.blocks, [&](const auto &block) {
      return block->pc == transition->targetPc;
    });
    if (sourceBlock == control.blocks.end() ||
        targetBlock == control.blocks.end())
      return failure();

    std::map<std::string, ProcessPlannedValue> definitions;
    for (const ProcessActionPlan &action : (*sourceBlock)->actions)
      for (const ProcessPlannedValue &result : action.results())
        if (result.kind() == ProcessPlannedValueKind::Original)
          definitions.emplace(result.original().path().str(), result);

    for (const ProcessActionPlan &action : (*targetBlock)->actions) {
      for (const ProcessPlannedValue &operand : action.operands()) {
        if (operand.kind() != ProcessPlannedValueKind::Original)
          continue;
        auto definition = definitions.find(operand.original().path().str());
        if (definition == definitions.end())
          continue;
        auto [slotIt, inserted] = slotsByPath.emplace(
            definition->first,
            ProcessLiveSlotId(static_cast<uint32_t>(control.liveSlots.size())));
        ProcessLiveSlotId slotId = slotIt->second;
        if (inserted) {
          if (control.liveSlots.size() >= limits.maxLiveSlots)
            return failure();
          auto slot = std::make_shared<ProcessLiveSlotPlan::Impl>();
          slot->id = slotId;
          slot->name = llvm::formatv("live{0:D8}", slotId.value()).str();
          slot->type = definition->second.type();
          slot->memberValues.push_back(definition->second);
          control.liveSlots.push_back(std::move(slot));
        }
        if (llvm::none_of(transition->stores, [&](const auto &store) {
              return store.impl_->slot == slotId;
            })) {
          auto store = std::make_shared<ProcessTransitionStorePlan::Impl>();
          store->slot = slotId;
          store->source = definition->second;
          if (definition->second.kind() == ProcessPlannedValueKind::Original)
            store->sourceValue = definition->second.original().value();
          transition->stores.push_back(ProcessTransitionStorePlan(store));
        }
        auto existingLoad = llvm::find_if(
            transition->loads, [&](const ProcessTransitionLoadPlan &load) {
              return load.impl_->slot == slotId;
            });
        if (existingLoad == transition->loads.end()) {
          auto load = std::make_shared<ProcessTransitionLoadPlan::Impl>();
          load->slot = slotId;
          load->replacements.push_back(operand);
          transition->loads.push_back(ProcessTransitionLoadPlan(load));
          (*targetBlock)->loads.push_back(ProcessTransitionLoadPlan(load));
        } else {
          auto load = std::const_pointer_cast<ProcessTransitionLoadPlan::Impl>(
              existingLoad->impl_);
          load->replacements.push_back(operand);
        }
      }
    }
  }
  return success();
}

} // namespace acir::detail
