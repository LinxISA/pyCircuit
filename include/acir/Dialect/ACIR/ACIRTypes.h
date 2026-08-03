#ifndef ACIR_DIALECT_ACIR_ACIRTYPES_H
#define ACIR_DIALECT_ACIR_ACIRTYPES_H

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Types.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"

#include "acir/Dialect/ACIR/ACIREnums.h.inc"

#define GET_TYPEDEF_CLASSES
#include "acir/Dialect/ACIR/ACIRTypes.h.inc"

namespace acir::ac {

/// Returns true when `type` is, or recursively contains, an interface-only
/// channel type.
bool containsChannelType(mlir::Type type);

} // namespace acir::ac

#endif // ACIR_DIALECT_ACIR_ACIRTYPES_H
