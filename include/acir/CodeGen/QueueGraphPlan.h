#ifndef ACIR_CODEGEN_QUEUEGRAPHPLAN_H
#define ACIR_CODEGEN_QUEUEGRAPHPLAN_H

#include "mlir/IR/BuiltinOps.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <string>
#include <vector>

namespace acir::codegen {

struct QueuePayloadFieldPlan {
  std::string name;
  std::string type;
};

struct QueuePayloadPlan {
  std::string name;
  std::vector<QueuePayloadFieldPlan> fields;
};

struct QueueExpressionPlan {
  std::string result;
  std::string kind;
  std::string type;
  std::vector<std::string> operands;
  std::string field;
  std::string predicate;
  std::string literal;
};

struct QueuePlan {
  std::string name;
  std::string payloadType;
  std::string scope;
  uint64_t depth = 1;
  uint64_t latency = 1;
};

struct QueueBlockPlan {
  std::string kind;
  std::string name;
  std::string scope;
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  std::vector<uint64_t> depths;
  std::vector<uint64_t> latencies;
  std::string policy;
  uint64_t maxIterations = 0;
  std::string region;
  std::vector<QueueExpressionPlan> expressions;
  std::vector<std::string> yields;
  uint64_t capacity = 0;
  uint64_t start = 0;
};

struct QueueGraphPlan {
  std::string system;
  std::vector<QueuePayloadPlan> payloads;
  std::vector<std::string> scopes;
  std::vector<QueuePlan> queues;
  std::vector<QueueBlockPlan> blocks;

  llvm::Expected<std::string> canonicalJson() const;
};

llvm::Expected<QueueGraphPlan> buildQueueGraphPlan(mlir::ModuleOp module);

} // namespace acir::codegen

#endif // ACIR_CODEGEN_QUEUEGRAPHPLAN_H
