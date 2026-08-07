#ifndef ACIR_CONVERSION_ACIRTOACSIM_ACIRTOACSIM_H
#define ACIR_CONVERSION_ACIRTOACSIM_ACIRTOACSIM_H

#include "acir/Bindings/Registry.h"

#include <memory>
#include <string>
#include <vector>

namespace mlir {
class Pass;
} // namespace mlir

namespace acir {

/// Options for the atomic ACIR-to-ACSim lowering. The pass resolves exact
/// gfsim bindings in memory through the same closed candidate/request
/// registry contract used by ac-resolve-gfsim-bindings; no lock file
/// round-trip is involved.
struct ACIRToACSimPassOptions {
  std::vector<bindings::BindingCandidate> candidates;
  std::vector<bindings::BindingRequest> requests;
  std::string profile;
  std::string target;
  /// Test hook: when non-zero, replaces the built-in v0.1 expanded-row
  /// capability bound so the ACLOWER-DISPATCH overflow path is observable
  /// without a million-row input. Production drivers leave this at zero.
  uint64_t maxExpandedRows = 0;
};

/// Create the atomic `ac-lower-to-acsim` pass.
///
/// The pass converts one frozen, verified ACIR file (contract epoch "0.1",
/// `ac.topology_frozen = true`, exactly one selected ac.system) into one
/// canonical ACSim model in a single transaction. Every validation failure
/// is diagnosed with an ACLOWER-* code before any mutation, so a failed
/// lowering never publishes a partial acsim.model.
///
/// v0.1 stage boundary (rejected with an explicit diagnostic, never silently
/// dropped): module signatures other than `() -> ()`, queue/resource/address/
/// time-domain/view topology constructs, heterogeneous array
/// specializations, non-canonical module declaration order, and process
/// bodies beyond the yield-only form planned by ProcessStatePlan.
std::unique_ptr<mlir::Pass>
createACIRToACSimPass(ACIRToACSimPassOptions options);

} // namespace acir

#endif // ACIR_CONVERSION_ACIRTOACSIM_ACIRTOACSIM_H
