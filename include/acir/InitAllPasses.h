#ifndef ACIR_INITALLPASSES_H
#define ACIR_INITALLPASSES_H

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
    if (epoch && epoch.getValue() == "0.1")
      return;

    module.emitError("expected top-level 'ac.contract_epoch' string attribute "
                     "equal to \"0.1\"");
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
