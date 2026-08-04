#ifndef ACIR_INITALLPASSES_H
#define ACIR_INITALLPASSES_H

#include "acir/Dialect/ACIR/ACIROps.h"
#include "acir/Dialect/ACIR/ACIRResources.h"
#include "acir/Transforms/Passes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Transforms/Passes.h"

#include <memory>

namespace acir {

class NormalizeACIRFilePass
    : public mlir::PassWrapper<NormalizeACIRFilePass,
                               mlir::OperationPass<mlir::ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(NormalizeACIRFilePass)

  llvm::StringRef getArgument() const override { return "normalize-ac-file"; }

  llvm::StringRef getDescription() const override {
    return "Normalize deterministic Agentic Circuit declaration order";
  }

  void runOnOperation() override {
    acir::ac::normalizeAddressMaps(getOperation());
  }
};

class VerifyACIRFilePass
    : public mlir::PassWrapper<VerifyACIRFilePass,
                               mlir::OperationPass<mlir::ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(VerifyACIRFilePass)

  llvm::StringRef getArgument() const override { return "verify-ac-file"; }

  llvm::StringRef getDescription() const override {
    return "Verify the Agentic Circuit epoch and whole-file legality";
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
      if (mlir::failed(acir::ac::verifyTopologyTypeUses(operation)))
        return mlir::WalkResult::interrupt();
      auto rejectChannel = [&](mlir::Attribute attribute) {
        if (!attribute)
          return false;
        return attribute
            .walk([](acir::ac::ChannelType) {
              return mlir::WalkResult::interrupt();
            })
            .wasInterrupted();
      };
      for (mlir::NamedAttribute attribute : operation->getAttrs()) {
        if (mlir::isa<acir::ac::PortOp>(operation) &&
            attribute.getName() == "type")
          continue;
        if (!rejectChannel(attribute.getValue()))
          continue;
        operation->emitError("channel type is only permitted in an "
                             "ac.interface channel declaration");
        return mlir::WalkResult::interrupt();
      }
      if ((!mlir::isa<acir::ac::PortOp>(operation) &&
           rejectChannel(operation->getPropertiesAsAttribute())) ||
          rejectChannel(mlir::LocationAttr(operation->getLoc()))) {
        operation->emitError("channel type is only permitted in an "
                             "ac.interface channel declaration");
        return mlir::WalkResult::interrupt();
      }
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
  mlir::registerTransformsPasses();
  registerACIRTransformsPasses();
  mlir::registerPass([]() -> std::unique_ptr<mlir::Pass> {
    return std::make_unique<NormalizeACIRFilePass>();
  });
  mlir::registerPass([]() -> std::unique_ptr<mlir::Pass> {
    return std::make_unique<VerifyACIRFilePass>();
  });
}

} // namespace acir

#endif // ACIR_INITALLPASSES_H
