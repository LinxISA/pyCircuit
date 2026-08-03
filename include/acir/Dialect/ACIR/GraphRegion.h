#ifndef ACIR_DIALECT_ACIR_GRAPHREGION_H
#define ACIR_DIALECT_ACIR_GRAPHREGION_H

#include "mlir/IR/Attributes.h"
#include "mlir/Support/LogicalResult.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include <string>

namespace mlir {
class Operation;
}

namespace acir::ac {

/// Verifies whole-file hierarchy selection, stable ownership identities and
/// the statically-resolved subset of topology freeze implemented by ACIR v0.1.
mlir::LogicalResult verifyGraphStructure(mlir::Operation *topLevel);

/// Returns true for concrete builtin static parameter values admitted by the
/// public v0.1 graph contract.
bool isConcreteStaticValue(mlir::Attribute value);

/// Builds the canonical lexicographic element path for a static N-D array.
std::string buildArrayElementPath(llvm::StringRef base,
                                  llvm::ArrayRef<int64_t> indices);

} // namespace acir::ac

#endif
