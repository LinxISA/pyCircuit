// ACIR-to-ACSim type conversion helpers.
// TO BE IMPLEMENTED — currently a no-op placeholder.
#include "acir/Conversion/ACIRToACSim/ACIRToACSim.h"
#include "acir/Dialect/ACIR/ACIRDialect.h"
#include "acir/Dialect/ACSim/ACSimDialect.h"
#include "mlir/Transforms/DialectConversion.h"

namespace acir {

void populateACIRToACSimTypeConversions(mlir::TypeConverter &converter) {
  // TODO: register type conversion callbacks
  (void)converter;
}

} // namespace acir
