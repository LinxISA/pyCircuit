#include "acir/CodeGen/Generator.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <sstream>
#include <system_error>

namespace acir::codegen {
namespace {

llvm::Error generatorError(const llvm::Twine &code,
                           const llvm::Twine &message) {
  return llvm::createStringError(
      std::make_error_code(std::errc::invalid_argument), code + ": " + message);
}

bool isIdentifier(llvm::StringRef value) {
  if (value.empty() ||
      !(std::isalpha(static_cast<unsigned char>(value.front())) ||
        value.front() == '_'))
    return false;
  return std::all_of(value.drop_front().begin(), value.end(), [](char value) {
    return std::isalnum(static_cast<unsigned char>(value)) || value == '_';
  });
}

bool isQualifiedName(llvm::StringRef value) {
  if (value.empty() || value.starts_with(':') || value.ends_with(':'))
    return false;
  while (!value.empty()) {
    auto split = value.split("::");
    if (!isIdentifier(split.first))
      return false;
    if (split.second.empty())
      return true;
    value = split.second;
  }
  return true;
}

bool isIncludePath(llvm::StringRef value) {
  return !value.empty() && !value.starts_with('/') && !value.contains('\\') &&
         !value.contains('"') && !value.contains("..") &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return character >= 0x20 && character != 0x7f;
         });
}

bool isNormalizedRelativePath(llvm::StringRef value) {
  if (value.empty() || value.starts_with('/') || value.ends_with('/') ||
      value.contains('\\'))
    return false;
  while (!value.empty()) {
    auto [component, rest] = value.split('/');
    if (component.empty() || component == "." || component == "..")
      return false;
    value = rest;
  }
  return true;
}

const BindingPlan *findBinding(const ModelPlan &plan, llvm::StringRef symbol) {
  auto found = std::find_if(
      plan.bindings.begin(), plan.bindings.end(),
      [&](const BindingPlan &binding) { return binding.symbol == symbol; });
  return found == plan.bindings.end() ? nullptr : &*found;
}

const ModulePlan *findModule(const ModelPlan &plan, llvm::StringRef symbol) {
  auto found = std::find_if(
      plan.modules.begin(), plan.modules.end(),
      [&](const ModulePlan &module) { return module.symbol == symbol; });
  return found == plan.modules.end() ? nullptr : &*found;
}

llvm::Expected<std::string> placementType(const ModelPlan &plan,
                                          const PlacementPlan &placement) {
  llvm::StringRef target = placement.target;
  target = target.take_until([](char value) { return value == ':'; });
  std::string base;
  if (const BindingPlan *binding = findBinding(plan, target))
    base = binding->cppSymbol;
  else if (const ModulePlan *module = findModule(plan, target))
    base = module->className;
  else
    return generatorError("ACLOWER-TYPE-MISMATCH",
                          "placement target has no typed realization");

  for (auto extent = placement.shape.rbegin(); extent != placement.shape.rend();
       ++extent) {
    std::string wrapped = "std::array<";
    wrapped.append(base)
        .append(", ")
        .append(std::to_string(*extent))
        .append(">");
    base = std::move(wrapped);
  }
  return base;
}

GeneratedFile makeFile(std::string path, std::string content) {
  GeneratedFile file{std::move(path), std::move(content), {}};
  file.fingerprint = computeFingerprint(file.content);
  return file;
}

llvm::Expected<GeneratedFile> moduleHeader(const ModelPlan &plan,
                                           const ModulePlan &module) {
  std::set<std::string> headers;
  for (const PlacementPlan &placement : module.placements) {
    llvm::StringRef target = placement.target;
    target = target.take_until([](char value) { return value == ':'; });
    if (const BindingPlan *binding = findBinding(plan, target))
      headers.insert(binding->header);
  }

  std::ostringstream output;
  output << "#pragma once\n\n#include \"gfsim/object.h\"\n";
  if (std::any_of(module.placements.begin(), module.placements.end(),
                  [](const PlacementPlan &placement) {
                    return !placement.shape.empty();
                  }))
    output << "#include <array>\n";
  for (const std::string &header : headers)
    output << "#include \"" << header << "\"\n";
  for (const ProcessPlan &process : module.processes)
    output << "#include \"generated/processes/" << process.className
           << ".h\"\n";
  output << "\nnamespace acsim_generated {\n\nclass " << module.className
         << " final : public gfsim::Module {\npublic:\n  " << module.className
         << "();\n\nprivate:\n";
  for (const PlacementPlan &placement : module.placements) {
    auto type = placementType(plan, placement);
    if (!type)
      return type.takeError();
    output << "  " << *type << ' ' << placement.memberName << ";\n";
  }
  for (const ProcessPlan &process : module.processes)
    output << "  " << process.className << ' ' << process.symbol << "_;\n";
  output << "};\n\n} // namespace acsim_generated\n";
  return makeFile("include/generated/modules/" + module.className + ".h",
                  output.str());
}

GeneratedFile moduleSource(const ModulePlan &module) {
  std::ostringstream output;
  output << "#include \"generated/modules/" << module.className
         << ".h\"\n\n#include <stdexcept>\n\nnamespace acsim_generated {\n\n"
         << module.className << "::" << module.className
         << "() : gfsim::Module(\"" << module.symbol
         << "\", gfsim::kInvalidObjectId, nullptr) {\n";
  for (const PlacementPlan &placement : module.placements) {
    if (placement.shape.empty()) {
      output << "  if (!attachChild(" << placement.memberName << "))\n"
             << "    throw std::logic_error(\"ACLOWER-OWNERSHIP\");\n";
    } else if (placement.shape.size() == 1) {
      output << "  for (auto &element : " << placement.memberName << ")\n"
             << "    if (!attachChild(element))\n"
             << "      throw std::logic_error(\"ACLOWER-OWNERSHIP\");\n";
    }
  }
  for (const ProcessPlan &process : module.processes)
    output << "  if (!attachChild(" << process.symbol << "_))\n"
           << "    throw std::logic_error(\"ACLOWER-OWNERSHIP\");\n";
  output << "}\n\n} // namespace acsim_generated\n";
  return makeFile("src/generated/modules/" + module.className + ".cpp",
                  output.str());
}

GeneratedFile processHeader(const ProcessPlan &process) {
  std::ostringstream output;
  output << "#pragma once\n\n#include \"gfsim/process.h\"\n\n"
         << "namespace acsim_generated {\n\nclass " << process.className
         << " final : public gfsim::ProcessRuntime<" << process.className
         << "> {\npublic:\n  static constexpr uint64_t kFairnessWork = "
         << process.fairnessWork << ";\n};\n\n} // namespace acsim_generated\n";
  return makeFile("include/generated/processes/" + process.className + ".h",
                  output.str());
}

GeneratedFile processSource(const ProcessPlan &process) {
  return makeFile("src/generated/processes/" + process.className + ".cpp",
                  "#include \"generated/processes/" + process.className +
                      ".h\"\n");
}

std::vector<std::string> expectedPaths(const ModelPlan &plan) {
  std::vector<std::string> paths = {
      "include/generated/dispatch.h", "include/generated/model.h",
      "src/generated/main.cpp", "src/generated/model.cpp"};
  for (const ModulePlan &module : plan.modules) {
    paths.push_back("include/generated/modules/" + module.className + ".h");
    paths.push_back("src/generated/modules/" + module.className + ".cpp");
    for (const ProcessPlan &process : module.processes) {
      paths.push_back("include/generated/processes/" + process.className +
                      ".h");
      paths.push_back("src/generated/processes/" + process.className + ".cpp");
    }
  }
  std::sort(paths.begin(), paths.end());
  paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
  return paths;
}

} // namespace

llvm::Expected<SourceBundle> generateModelSources(const ModelPlan &plan) {
  if (auto error = validateModelPlan(plan))
    return std::move(error);
  for (const BindingPlan &binding : plan.bindings) {
    const std::array entryPoints = {
        llvm::StringRef(binding.entryPoints.pure),
        llvm::StringRef(binding.entryPoints.reset),
        llvm::StringRef(binding.entryPoints.validate),
        llvm::StringRef(binding.entryPoints.work),
        llvm::StringRef(binding.entryPoints.xfer)};
    if (!isIncludePath(binding.header) || !isQualifiedName(binding.cppSymbol) ||
        !isQualifiedName(binding.conceptName) ||
        std::any_of(entryPoints.begin(), entryPoints.end(),
                    [](llvm::StringRef entryPoint) {
                      return !entryPoint.empty() &&
                             !isQualifiedName(entryPoint);
                    }))
      return generatorError("ACLOWER-PARAM-PHASE",
                            "binding contains an unsafe C++ token");
  }

  SourceBundle bundle;
  bundle.files.push_back(makeFile("include/generated/dispatch.h",
                                  "#pragma once\n\n// Static dispatch is "
                                  "emitted with the model harness.\n"));
  bundle.files.push_back(makeFile(
      "include/generated/model.h",
      "#pragma once\n\n// Deterministic generated model declarations.\n"));
  bundle.files.push_back(
      makeFile("src/generated/main.cpp",
               "#include \"generated/model.h\"\n\nint main() { return 0; }\n"));
  bundle.files.push_back(
      makeFile("src/generated/model.cpp", "#include \"generated/model.h\"\n"));
  for (const ModulePlan &module : plan.modules) {
    auto header = moduleHeader(plan, module);
    if (!header)
      return header.takeError();
    bundle.files.push_back(std::move(*header));
    bundle.files.push_back(moduleSource(module));
    for (const ProcessPlan &process : module.processes) {
      bundle.files.push_back(processHeader(process));
      bundle.files.push_back(processSource(process));
    }
  }
  std::sort(bundle.files.begin(), bundle.files.end(),
            [](const GeneratedFile &left, const GeneratedFile &right) {
              return left.relativePath < right.relativePath;
            });
  if (auto error = validateSourceBundle(plan, bundle))
    return std::move(error);
  return bundle;
}

llvm::Error validateSourceBundle(const ModelPlan &plan,
                                 const SourceBundle &bundle) {
  const std::vector<std::string> required = expectedPaths(plan);
  if (bundle.files.size() != required.size())
    return generatorError("ACLOWER-FINGERPRINT",
                          "source bundle has an incomplete file set");
  for (size_t index = 0; index < bundle.files.size(); ++index) {
    const GeneratedFile &file = bundle.files[index];
    if (file.relativePath != required[index] ||
        !isNormalizedRelativePath(file.relativePath))
      return generatorError("ACLOWER-FINGERPRINT",
                            "source paths are not canonical and complete");
    if (file.fingerprint != computeFingerprint(file.content) ||
        file.content.find('\r') != std::string::npos)
      return generatorError("ACLOWER-FINGERPRINT",
                            "source content fingerprint is invalid");
  }
  return llvm::Error::success();
}

} // namespace acir::codegen
