// ACIR-to-ACSim structural conversion pass.
// TO BE IMPLEMENTED — currently a no-op placeholder.
#include "acir/Conversion/ACIRToACSim/ACIRToACSim.h"
#include "acir/Dialect/ACIR/ACIRDialect.h"
#include "acir/Dialect/ACSim/ACSimDialect.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

namespace acir {
namespace {

struct ConvertACIRToACSim
    : public mlir::PassWrapper<ConvertACIRToACSim,
                               mlir::OperationPass<mlir::ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ConvertACIRToACSim)

  llvm::StringRef getArgument() const override {
    return "convert-acir-to-acsim";
  }
  llvm::StringRef getDescription() const override {
    return "Lower frozen ACIR structure into canonical ACSim (not yet "
           "implemented)";
  }

  void runOnOperation() override {
    // TODO: implement ACIR-to-ACSim conversion
    mlir::ModuleOp module = getOperation();
    if (!module->hasAttr("ac.contract_epoch")) {
      module.emitError("input must have ac.contract_epoch = \"0.1\"");
      return signalPassFailure();
    }
    module.emitError("ACIR-to-ACSim conversion not yet implemented");
    return signalPassFailure();
  }
};

} // namespace

std::unique_ptr<mlir::Pass> createConvertACIRToACSimPass() {
  return std::make_unique<ConvertACIRToACSim>();
}

void populateACIRToACSimTypeConversions(mlir::TypeConverter &) {
  // TODO: register type conversions
}

} // namespace acir
