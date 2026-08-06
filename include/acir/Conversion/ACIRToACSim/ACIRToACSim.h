#ifndef ACIR_CONVERSION_ACIRTOACSIM_ACIRTOACSIM_H
#define ACIR_CONVERSION_ACIRTOACSIM_ACIRTOACSIM_H

#include <memory>

namespace mlir {
class Pass;
class TypeConverter;
} // namespace mlir

namespace acir {

/// Create a pass that converts frozen ACIR structure into canonical ACSim.
/// The input must be a verified, frozen ACIR file with resolved bindings.
std::unique_ptr<mlir::Pass> createConvertACIRToACSimPass();

/// Register ACIR-to-ACSim type conversions on the given converter.
void populateACIRToACSimTypeConversions(mlir::TypeConverter &converter);

} // namespace acir

#endif // ACIR_CONVERSION_ACIRTOACSIM_ACIRTOACSIM_H
