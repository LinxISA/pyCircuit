// ACIR-to-ACSim type conversion helpers.
#include "acir/Conversion/ACIRToACSim/ACIRToACSim.h"
#include "acir/Dialect/ACSim/ACSimDialect.h"
#include "acir/Dialect/ACSim/ACSimTypes.h"
#include "mlir/Transforms/DialectConversion.h"

namespace acir {

void populateACIRToACSimTypeConversions(mlir::TypeConverter &converter) {
  converter.addConversion([](mlir::Type type) -> std::optional<mlir::Type> {
    // Builtin types pass through unchanged.
    if (mlir::isa<mlir::IntegerType, mlir::FloatType, mlir::IndexType,
                  mlir::NoneType>(type))
      return type;

    // ACSim types are already in the target dialect.
    if (type.getDialect().getNamespace() ==
        acsim::ACSimDialect::getDialectNamespace())
      return type;

    // Unsupported types are handled at op-conversion time.
    return std::nullopt;
  });
}

} // namespace acir
