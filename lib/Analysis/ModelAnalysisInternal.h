#ifndef ACIR_ANALYSIS_MODELANALYSISINTERNAL_H
#define ACIR_ANALYSIS_MODELANALYSISINTERNAL_H

#include "acir/Dialect/ACIR/ACIROps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/LogicalResult.h"

#include <string>

namespace acir::detail {

/// Returns whether any top-level or nested reserved freeze attribute exists.
bool hasTopologyFreezeEvidence(mlir::ModuleOp model);

/// Internal seal construction primitives. These declarations are deliberately
/// unavailable from the installed public include tree; only the trusted
/// first-freeze writer and frozen-integrity verifier consume them.
mlir::FailureOr<mlir::ArrayAttr> buildFrozenOwnerManifest(mlir::ModuleOp model);
mlir::FailureOr<mlir::ArrayAttr>
buildFrozenProcessSkeleton(ac::ProcessOp process);
std::string computeTopologyDigest(mlir::ModuleOp model);

} // namespace acir::detail

#endif // ACIR_ANALYSIS_MODELANALYSISINTERNAL_H
