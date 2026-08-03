#ifndef ACIR_INITALLPASSES_H
#define ACIR_INITALLPASSES_H

#include "acir/Dialect/ACIR/ACIRTypes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"

#include <memory>

namespace acir {

class VerifyContractEpochPass
    : public mlir::PassWrapper<VerifyContractEpochPass,
                               mlir::OperationPass<mlir::ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(VerifyContractEpochPass)

  llvm::StringRef getArgument() const override {
    return "verify-ac-contract-epoch";
  }

  llvm::StringRef getDescription() const override {
    return "Verify the Agentic Circuit public file contract epoch";
  }

  void runOnOperation() override {
    mlir::ModuleOp module = getOperation();
    auto epoch = module->getAttrOfType<mlir::StringAttr>("ac.contract_epoch");
    if (!epoch || epoch.getValue() != "0.1") {
      module.emitError(
          "expected top-level 'ac.contract_epoch' string attribute "
          "equal to \"0.1\"");
      signalPassFailure();
      return;
    }

    mlir::WalkResult result = module.walk([&](mlir::Operation *operation) {
      for (mlir::Type type : operation->getOperandTypes()) {
        if (!acir::ac::containsChannelType(type))
          continue;
        operation->emitError("channel type is only permitted in an "
                             "ac.interface channel declaration");
        return mlir::WalkResult::interrupt();
      }
      for (mlir::Type type : operation->getResultTypes()) {
        if (!acir::ac::containsChannelType(type))
          continue;
        operation->emitError("channel type is only permitted in an "
                             "ac.interface channel declaration");
        return mlir::WalkResult::interrupt();
      }
      for (mlir::Region &region : operation->getRegions())
        for (mlir::Block &block : region)
          for (mlir::BlockArgument argument : block.getArguments()) {
            if (!acir::ac::containsChannelType(argument.getType()))
              continue;
            operation->emitError("channel type is only permitted in an "
                                 "ac.interface channel declaration");
            return mlir::WalkResult::interrupt();
          }
      return mlir::WalkResult::advance();
    });
    if (result.wasInterrupted())
      signalPassFailure();
  }
};

inline void registerAllPasses() {
  mlir::registerPass([]() -> std::unique_ptr<mlir::Pass> {
    return std::make_unique<VerifyContractEpochPass>();
  });
}

} // namespace acir

#endif // ACIR_INITALLPASSES_H
