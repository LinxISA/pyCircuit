#include "BindingOptions.h"

#include "acir/Bindings/Registry.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MemoryBuffer.h"

#include <string>
#include <vector>

namespace acir::opt {
namespace {

llvm::cl::OptionCategory BindingCategory("Exact binding resolution options");

llvm::cl::opt<bool> ResolveBindings(
    "ac-resolve-gfsim-bindings",
    llvm::cl::desc("Run exact frozen-ACIR gfsim binding resolution"),
    llvm::cl::init(false), llvm::cl::cat(BindingCategory));

llvm::cl::list<std::string> BindingRegistries(
    "ac-binding-registry",
    llvm::cl::desc(
        "Closed binding candidate/request registry JSON file (repeatable)"),
    llvm::cl::ZeroOrMore, llvm::cl::value_desc("file"),
    llvm::cl::cat(BindingCategory));

llvm::cl::opt<std::string> BindingLockOutput(
    "ac-binding-lock-output",
    llvm::cl::desc("Required atomic output path for acsim-bindings.lock.json"),
    llvm::cl::value_desc("file"), llvm::cl::init(""),
    llvm::cl::cat(BindingCategory));

llvm::cl::opt<std::string>
    BindingProfile("ac-binding-profile",
                   llvm::cl::desc("Exact static build profile identity"),
                   llvm::cl::value_desc("profile"), llvm::cl::init(""),
                   llvm::cl::cat(BindingCategory));

llvm::cl::opt<std::string>
    BindingTarget("ac-binding-target",
                  llvm::cl::desc("Exact toolchain target identity"),
                  llvm::cl::value_desc("target"), llvm::cl::init(""),
                  llvm::cl::cat(BindingCategory));

llvm::Error optionError(const llvm::Twine &message) {
  return llvm::createStringError(llvm::errc::invalid_argument,
                                 "ACLOWER-BINDING-OPTIONS: %s",
                                 message.str().c_str());
}

} // namespace

llvm::Expected<std::optional<ResolveBindingsPassOptions>>
loadBindingCommandLineOptions() {
  bool hasRelatedOption = !BindingRegistries.empty() ||
                          !BindingLockOutput.empty() ||
                          !BindingProfile.empty() || !BindingTarget.empty();
  if (!ResolveBindings) {
    if (hasRelatedOption)
      return optionError("binding options require --ac-resolve-gfsim-bindings");
    return std::optional<ResolveBindingsPassOptions>();
  }
  if (BindingRegistries.empty())
    return optionError("--ac-binding-registry is required");
  if (BindingLockOutput.empty())
    return optionError("--ac-binding-lock-output is required");
  if (BindingProfile.empty())
    return optionError("--ac-binding-profile is required");
  if (BindingTarget.empty())
    return optionError("--ac-binding-target is required");

  std::vector<std::string> registryPaths(BindingRegistries.begin(),
                                         BindingRegistries.end());
  llvm::sort(registryPaths);
  ResolveBindingsPassOptions options;
  options.profile = BindingProfile;
  options.target = BindingTarget;
  options.lockOutputPath = BindingLockOutput;
  for (const std::string &path : registryPaths) {
    auto buffer = llvm::MemoryBuffer::getFile(path, false, false);
    if (!buffer)
      return optionError(llvm::Twine("cannot read registry '") + path +
                         "': " + buffer.getError().message());
    auto document = bindings::parseBindingRegistry((*buffer)->getBuffer());
    if (!document)
      return document.takeError();
    options.candidates.insert(
        options.candidates.end(),
        std::make_move_iterator(document->candidates.begin()),
        std::make_move_iterator(document->candidates.end()));
    options.requests.insert(options.requests.end(),
                            std::make_move_iterator(document->requests.begin()),
                            std::make_move_iterator(document->requests.end()));
  }
  return std::optional<ResolveBindingsPassOptions>(std::move(options));
}

} // namespace acir::opt
