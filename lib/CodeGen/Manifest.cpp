#include "acir/CodeGen/Manifest.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/SHA256.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace acir::codegen {

Fingerprint computeFingerprint(const std::string &content) {
  auto hash = llvm::SHA256::hash(llvm::ArrayRef<uint8_t>(
      reinterpret_cast<const uint8_t *>(content.data()), content.size()));
  std::ostringstream os;
  for (uint8_t byte : hash)
    os << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(byte);
  return os.str();
}

Fingerprint compositeFingerprint(const std::vector<Fingerprint> &inputs) {
  std::string combined;
  for (const auto &fp : inputs) combined += fp + "\n";
  return computeFingerprint(combined);
}

void BuildManifest::finalize() {
  std::vector<Fingerprint> inputs;
  if (!inputFingerprint.empty()) inputs.push_back(inputFingerprint);
  if (!toolchainFingerprint.empty()) inputs.push_back(toolchainFingerprint);
  if (!profileFingerprint.empty()) inputs.push_back(profileFingerprint);
  if (!bindingFingerprint.empty()) inputs.push_back(bindingFingerprint);
  for (const auto &src : sources) inputs.push_back(src.fingerprint);
  outputFingerprint = compositeFingerprint(inputs);
}

std::string CacheKey::toString() const {
  return inputFingerprint.substr(0, 16) + "_" +
         toolchainFingerprint.substr(0, 8) + "_" +
         profileFingerprint.substr(0, 8) + "_" +
         bindingFingerprint.substr(0, 8);
}

CacheKey cacheKeyFromManifest(const BuildManifest &manifest) {
  return {manifest.inputFingerprint, manifest.toolchainFingerprint,
          manifest.profileFingerprint, manifest.bindingFingerprint};
}

std::string StagedOutput::writeSource(const SourceFile &file) {
  namespace fs = std::filesystem;
  fs::path dir = fs::path(outputDir) / sourceSubdir;
  fs::create_directories(dir);
  fs::path filePath = dir / file.relativePath;
  fs::create_directories(filePath.parent_path());
  std::ofstream out(filePath);
  out << file.content;
  return filePath.string();
}

std::string StagedOutput::writeManifest(const BuildManifest &manifest) {
  namespace fs = std::filesystem;
  fs::create_directories(outputDir);
  fs::path filePath = fs::path(outputDir) / manifestFile;
  std::ofstream out(filePath);
  out << "{\n";
  out << "  \"contract_epoch\": \"" << manifest.contractEpoch << "\",\n";
  out << "  \"schema\": \"" << manifest.schema << "\",\n";
  out << "  \"input_fingerprint\": \"" << manifest.inputFingerprint << "\",\n";
  out << "  \"output_fingerprint\": \"" << manifest.outputFingerprint << "\",\n";
  out << "  \"sources\": [\n";
  for (size_t i = 0; i < manifest.sources.size(); ++i) {
    out << "    { \"path\": \"" << manifest.sources[i].relativePath
        << "\", \"fingerprint\": \"" << manifest.sources[i].fingerprint << "\" }";
    if (i + 1 < manifest.sources.size()) out << ",";
    out << "\n";
  }
  out << "  ]\n}\n";
  return filePath.string();
}

} // namespace acir::codegen
