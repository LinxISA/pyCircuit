// Installed-tree consumer for the public ProcessStatePlan API.
//
// Builds only against the installed AgenticCircuit package: it includes the
// public header and resolves the two public entry points from the installed
// ACIRAnalysis library through its exported CMake target. Plan sets are
// produced by the ac-lower-process-state pass inside acir-opt, so a consumer
// can only receive them; this smoke binary verifies the installed surface is
// complete and linkable without any source-tree include path.
#include "acir/Analysis/ProcessStatePlan.h"

#include "mlir/Support/LogicalResult.h"

int main() {
  acir::ProcessStateLimits limits;
  if (limits.maxProcesses == 0 || limits.maxTransitions == 0)
    return 1;

  auto *verify = &acir::verifyProcessStatePlan;
  auto *serialize = &acir::serializeProcessStatePlan;
  if (verify == nullptr || serialize == nullptr)
    return 1;
  return 0;
}
