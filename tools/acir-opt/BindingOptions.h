#ifndef ACIR_OPT_BINDINGOPTIONS_H
#define ACIR_OPT_BINDINGOPTIONS_H

#include "acir/Transforms/ResolveBindings.h"

#include "llvm/Support/Error.h"

#include <optional>

namespace acir::opt {

llvm::Expected<std::optional<ResolveBindingsPassOptions>>
loadBindingCommandLineOptions();

} // namespace acir::opt

#endif // ACIR_OPT_BINDINGOPTIONS_H
