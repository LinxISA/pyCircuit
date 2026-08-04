#ifndef ACIR_ANALYSIS_MODELANALYSIS_H
#define ACIR_ANALYSIS_MODELANALYSIS_H

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/LogicalResult.h"

#include <cstdint>
#include <string>

namespace mlir {
class ArrayAttr;
}

namespace acir {

namespace ac {
class ProcessOp;
}

/// Fixed capability limits keep hostile or malformed models diagnosable and
/// make the whole-model analyses independent of allocator failure behavior.
inline constexpr uint64_t kMaxModelAnalysisNodes = 1U << 20;
inline constexpr uint64_t kMaxModelAnalysisEdges = 1U << 22;
inline constexpr uint64_t kMaxPureCallFunctions = 1U << 16;
inline constexpr uint64_t kMaxPureCallEdges = 1U << 18;
inline constexpr uint64_t kMaxPureCallDepth = 1024;

/// Deterministic operation counts for the indexed state/topology owner join.
/// These counters deliberately count logical index operations rather than
/// allocator or wall-clock behavior, so performance regressions are exact and
/// platform independent.
struct OwnerManifestWork {
  uint64_t stateIndexInsertions = 0;
  uint64_t topologyIndexLookups = 0;

  uint64_t total() const { return stateIndexInsertions + topologyIndexLookups; }
};

class ModelAnalysis {
public:
  explicit ModelAnalysis(mlir::ModuleOp model) : model(model) {}

  /// Runs the complete IR-stage semantic closure. Existing operation
  /// verifiers remain authoritative for local contracts; this analysis adds
  /// deterministic whole-model selection, purity, dependency, and frozen
  /// integrity checks.
  mlir::LogicalResult verify();

  /// Verifies module-level require/ensure conditions at topology freeze.
  mlir::LogicalResult verifyFreezeContracts();

  /// Returns the SHA-256 of canonical topology-only state. Raw process bodies
  /// are excluded, while their persisted continuation-safe semantic skeletons
  /// participate in the digest.
  std::string computeTopologyDigest();

  /// Constructs the canonical hierarchy path/stable-ID manifest.
  mlir::FailureOr<mlir::ArrayAttr> buildFrozenOwnerManifest();

  OwnerManifestWork getLastOwnerManifestWork() const {
    return lastOwnerManifestWork;
  }

private:
  mlir::LogicalResult verifyPureProcessCalls();
  mlir::LogicalResult verifyZeroDelayDependencies();
  mlir::LogicalResult verifyFrozenIntegrity();

  mlir::ModuleOp model;
  OwnerManifestWork lastOwnerManifestWork;
};

/// Builds the continuation-safe semantic skeleton persisted for a frozen
/// process declaration.
mlir::ArrayAttr buildFrozenProcessSkeleton(ac::ProcessOp process);

mlir::LogicalResult verifyModel(mlir::ModuleOp model);
bool isTopologyFrozen(mlir::ModuleOp model);

} // namespace acir

#endif // ACIR_ANALYSIS_MODELANALYSIS_H
