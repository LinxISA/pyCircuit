#include "acir/CodeGen/Build.h"

#include "llvm/ADT/Twine.h"

#include <system_error>

namespace acir::codegen {

llvm::Expected<BuildResult> buildGeneratedModel(const BuildRequest &) {
  return llvm::createStringError(
      std::make_error_code(std::errc::operation_not_supported),
      "ACLOWER-FINGERPRINT: build publication is not initialized");
}

} // namespace acir::codegen
