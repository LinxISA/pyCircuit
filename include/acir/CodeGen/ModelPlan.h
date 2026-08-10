#ifndef ACIR_CODEGEN_MODELPLAN_H
#define ACIR_CODEGEN_MODELPLAN_H

#include "acir/CodeGen/Manifest.h"

#include "mlir/IR/BuiltinOps.h"
#include "llvm/Support/Error.h"

#include <compare>
#include <cstdint>
#include <string>
#include <vector>

namespace acir::codegen {

enum class TypeKind {
  Accessor,
  Implementation,
  Interface,
  Packet,
  Policy,
  Protocol,
  Provider,
  Resource,
  Role,
  Schema,
  TimeDomain,
  Value,
  Wake,
  Payload,
};

struct TypePlan {
  std::string symbol;
  TypeKind kind;
  std::string cppType;
  Fingerprint fingerprint;
};

enum class RuntimeObjectKind { External, Process };

struct RuntimeObjectPlan {
  uint32_t objectId = 0;
  uint32_t activationId = 0;
  std::string targetSymbol;
  std::string hierarchyPath;
  std::vector<uint64_t> indices;
  RuntimeObjectKind objectKind = RuntimeObjectKind::External;
  std::string workThunk;
  std::string xferThunk;
  std::string resetThunk;
  std::string validateThunk;
};

struct ActivationEdgePlan {
  uint32_t sourceId = 0;
  uint32_t targetId = 0;

  auto operator<=>(const ActivationEdgePlan &) const = default;
};

struct SourceMapPlan {
  std::string stableIdentity;
  std::string source;
};

struct ModelPlan {
  std::string modelSymbol;
  std::string rootSymbol;
  std::string contractEpoch;
  Fingerprint frozenAcirFingerprint;
  Fingerprint bindingLockFingerprint;
  Fingerprint providerFingerprint;
  Fingerprint profileFingerprint;
  Fingerprint toolchainFingerprint;
  Fingerprint schemaSetFingerprint;
  std::vector<std::string> constructionOrder;
  std::vector<std::string> destructionOrder;
  std::vector<TypePlan> types;
  std::vector<RuntimeObjectPlan> runtimeObjects;
  std::vector<ActivationEdgePlan> activationEdges;
  std::vector<SourceMapPlan> sourceMap;
};

llvm::Expected<ModelPlan> buildModelPlan(mlir::ModuleOp canonicalACSim);
llvm::Error validateModelPlan(const ModelPlan &plan);

} // namespace acir::codegen

#endif // ACIR_CODEGEN_MODELPLAN_H
