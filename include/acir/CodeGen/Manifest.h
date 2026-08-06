#ifndef ACIR_CODEGEN_MANIFEST_H
#define ACIR_CODEGEN_MANIFEST_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace acir::codegen {

// ── Fingerprint ───────────────────────────────────────────────────────

/// A SHA-256 content fingerprint as 64 lowercase hex characters.
using Fingerprint = std::string;

/// Compute the SHA-256 fingerprint of raw bytes.
Fingerprint computeFingerprint(const std::string &content);

/// Compute the deterministic fingerprint from a list of inputs.
Fingerprint compositeFingerprint(const std::vector<Fingerprint> &inputs);

// ── Source file ───────────────────────────────────────────────────────

struct SourceFile {
  std::string relativePath; // e.g. "src/modules/Top_workload.cpp"
  std::string content;
  Fingerprint fingerprint;
};

// ── Build manifest ─────────────────────────────────────────────────────

/// Immutable record of a generated build: what went in, what came out,
/// and how to verify it hasn't been tampered with.
struct BuildManifest {
  /// Contract epoch for the generation tool.
  std::string contractEpoch = "0.1";

  /// Schema identifier for the manifest itself.
  std::string schema = "acir-build-manifest-0.1";

  /// Fingerprint of the input ACSim model before generation.
  Fingerprint inputFingerprint;

  /// Fingerprint of the toolchain configuration.
  Fingerprint toolchainFingerprint;

  /// Fingerprint of the selected build profile.
  Fingerprint profileFingerprint;

  /// Fingerprint of the binding resolution result.
  Fingerprint bindingFingerprint;

  /// All generated source files with their fingerprints.
  std::vector<SourceFile> sources;

  /// Composite fingerprint of all outputs.
  Fingerprint outputFingerprint;

  /// Mapping of component catalog entries to their fingerprints.
  std::map<std::string, Fingerprint> componentFingerprints;

  /// Compute the composite fingerprint from all inputs.
  void finalize();
};

// ── Cache key ─────────────────────────────────────────────────────────

/// A cache key identifies a build configuration for fingerprint-based
/// caching. Two builds with identical cache keys produce identical output.
struct CacheKey {
  Fingerprint inputFingerprint;
  Fingerprint toolchainFingerprint;
  Fingerprint profileFingerprint;
  Fingerprint bindingFingerprint;

  /// Deterministic string representation for file/directory naming.
  std::string toString() const;
};

/// Compute a cache key from a build manifest.
CacheKey cacheKeyFromManifest(const BuildManifest &manifest);

// ── Staged output ─────────────────────────────────────────────────────

/// Directory layout for staged code generation.
/// Files are written atomically and verified before being committed.
struct StagedOutput {
  /// Root output directory.
  std::string outputDir;

  /// Subdirectory for generated source files.
  std::string sourceSubdir = "src";

  /// Subdirectory for generated header files.
  std::string includeSubdir = "include";

  /// Subdirectory for build artifacts.
  std::string buildSubdir = "build";

  /// Manifest file name.
  std::string manifestFile = "build-manifest.json";

  /// Write a source file to the staged output directory.
  /// Returns the full path written.
  std::string writeSource(const SourceFile &file);

  /// Write the build manifest.
  std::string writeManifest(const BuildManifest &manifest);
};

} // namespace acir::codegen

#endif // ACIR_CODEGEN_MANIFEST_H
