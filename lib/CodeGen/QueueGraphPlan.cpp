#include "acir/CodeGen/QueueGraphPlan.h"

#include "acir/Bindings/Binding.h"
#include "acir/Dialect/ACIR/ACIROps.h"
#include "acir/Dialect/ACIR/ACIRTypes.h"

#include "mlir/IR/Operation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <system_error>

namespace acir::codegen {
namespace {

llvm::Error planError(const llvm::Twine &message) {
  return llvm::createStringError(
      std::make_error_code(std::errc::invalid_argument),
      "ACLOWER-QUEUE-PLAN: " + message);
}

std::string printType(mlir::Type type) {
  std::string result;
  llvm::raw_string_ostream stream(result);
  stream << type;
  return result;
}

std::string printRegion(mlir::Region &region) {
  std::string result;
  llvm::raw_string_ostream stream(result);
  region.getParentOp()->print(stream);
  return result;
}

std::string scopePath(llvm::ArrayRef<std::string> scope) {
  std::string result;
  for (llvm::StringRef part : scope) {
    result.push_back('/');
    result.append(part);
  }
  return result.empty() ? "/" : result;
}

llvm::Expected<std::string>
queueName(mlir::Value value,
          const llvm::DenseMap<mlir::Value, std::string> &names) {
  auto found = names.find(value);
  if (found == names.end())
    return planError("Queue operand has no frozen logical identity");
  return found->second;
}

llvm::Expected<std::vector<std::string>>
queueNames(mlir::ValueRange values,
           const llvm::DenseMap<mlir::Value, std::string> &names) {
  std::vector<std::string> result;
  for (mlir::Value value : values) {
    auto name = queueName(value, names);
    if (!name)
      return name.takeError();
    result.push_back(std::move(*name));
  }
  return result;
}

llvm::Expected<std::vector<std::string>> outputNames(mlir::Operation *op,
                                                     size_t count) {
  std::vector<std::string> result;
  if (count == 1)
    if (auto name = op->getAttrOfType<mlir::StringAttr>("ac.name"))
      result.push_back(name.getValue().str());
  if (result.empty())
    if (auto names = op->getAttrOfType<mlir::ArrayAttr>("ac.output_names"))
      for (mlir::Attribute value : names) {
        auto name = mlir::dyn_cast<mlir::StringAttr>(value);
        if (!name)
          return planError("ac.output_names must contain only strings");
        result.push_back(name.getValue().str());
      }
  if (result.size() != count)
    return planError("Queue-producing op requires exact frozen output names");
  return result;
}

class Extractor {
public:
  explicit Extractor(mlir::ModuleOp module) : module(module) {}

  llvm::Expected<QueueGraphPlan> run() {
    auto system = module->getAttrOfType<mlir::StringAttr>("ac.system");
    if (!system || system.getValue().empty())
      return planError("module requires non-empty ac.system");
    plan.system = system.getValue().str();
    if (auto error = extractBlock(*module.getBody(), {}))
      return std::move(error);
    return std::move(plan);
  }

private:
  llvm::Error addQueue(mlir::Value value, llvm::StringRef name, uint64_t depth,
                       uint64_t latency, llvm::ArrayRef<std::string> scope) {
    if (name.empty() || !queueIdentities.insert(name).second)
      return planError("Queue logical identities must be non-empty and unique");
    auto queue = mlir::dyn_cast<ac::QueueType>(value.getType());
    if (!queue || depth == 0 || latency == 0)
      return planError("Queue plan requires typed positive depth and latency");
    names[value] = name.str();
    plan.queues.push_back({name.str(), printType(queue.getElementType()),
                           scopePath(scope), depth, latency});
    return llvm::Error::success();
  }

  llvm::Error addOutputs(mlir::Operation *op, mlir::ValueRange outputs,
                         llvm::ArrayRef<int64_t> depths,
                         llvm::ArrayRef<int64_t> latencies,
                         llvm::ArrayRef<std::string> scope,
                         std::vector<std::string> &result) {
    auto frozen = outputNames(op, outputs.size());
    if (!frozen)
      return frozen.takeError();
    if (depths.size() != outputs.size() || latencies.size() != outputs.size())
      return planError("Queue output metadata count mismatch");
    for (size_t index = 0; index < outputs.size(); ++index) {
      if (depths[index] <= 0 || latencies[index] <= 0)
        return planError("Queue depth and latency must be positive");
      auto error = addQueue(outputs[index], (*frozen)[index], depths[index],
                            latencies[index], scope);
      if (error)
        return error;
    }
    result = std::move(*frozen);
    return llvm::Error::success();
  }

  llvm::Error extractBlock(mlir::Block &block, std::vector<std::string> scope) {
    for (mlir::Operation &operation : block) {
      if (auto source = mlir::dyn_cast<ac::SourceOp>(operation)) {
        std::vector<std::string> outputs;
        if (auto error = addOutputs(
                source, source->getResults(), {int64_t(source.getDepth())},
                {int64_t(source.getLatency())}, scope, outputs))
          return error;
        plan.blocks.push_back({"source",
                               outputs.front(),
                               scopePath(scope),
                               {},
                               outputs,
                               {uint64_t(source.getDepth())},
                               {uint64_t(source.getLatency())}});
        continue;
      }
      if (auto transform = mlir::dyn_cast<ac::TransformOp>(operation)) {
        auto inputs = queueNames(transform.getInputs(), names);
        if (!inputs)
          return inputs.takeError();
        std::vector<std::string> outputs;
        if (auto error =
                addOutputs(transform, transform.getOutputs(),
                           transform.getOutputDepthsAttr().asArrayRef(),
                           transform.getOutputLatenciesAttr().asArrayRef(),
                           scope, outputs))
          return error;
        QueueBlockPlan blockPlan{"transform", outputs.front(), scopePath(scope),
                                 std::move(*inputs), outputs};
        for (int64_t value : transform.getOutputDepths())
          blockPlan.depths.push_back(value);
        for (int64_t value : transform.getOutputLatencies())
          blockPlan.latencies.push_back(value);
        blockPlan.region = printRegion(transform.getBody());
        plan.blocks.push_back(std::move(blockPlan));
        continue;
      }
      if (auto broadcast = mlir::dyn_cast<ac::BroadcastOp>(operation)) {
        auto input = queueName(broadcast.getInput(), names);
        if (!input)
          return input.takeError();
        std::vector<std::string> outputs;
        if (auto error =
                addOutputs(broadcast, broadcast.getOutputs(),
                           broadcast.getOutputDepthsAttr().asArrayRef(),
                           broadcast.getOutputLatenciesAttr().asArrayRef(),
                           scope, outputs))
          return error;
        QueueBlockPlan blockPlan{"broadcast",
                                 "broadcast_" + *input,
                                 scopePath(scope),
                                 {*input},
                                 outputs};
        for (int64_t value : broadcast.getOutputDepths())
          blockPlan.depths.push_back(value);
        for (int64_t value : broadcast.getOutputLatencies())
          blockPlan.latencies.push_back(value);
        plan.blocks.push_back(std::move(blockPlan));
        continue;
      }
      if (auto route = mlir::dyn_cast<ac::RouteOp>(operation)) {
        auto input = queueName(route.getInput(), names);
        if (!input)
          return input.takeError();
        std::vector<std::string> outputs;
        if (auto error = addOutputs(route, route.getOutputs(),
                                    route.getOutputDepthsAttr().asArrayRef(),
                                    route.getOutputLatenciesAttr().asArrayRef(),
                                    scope, outputs))
          return error;
        QueueBlockPlan blockPlan{"route",
                                 "route_" + outputs.front(),
                                 scopePath(scope),
                                 {*input},
                                 outputs};
        for (int64_t value : route.getOutputDepths())
          blockPlan.depths.push_back(value);
        for (int64_t value : route.getOutputLatencies())
          blockPlan.latencies.push_back(value);
        blockPlan.region = printRegion(route.getSelector());
        plan.blocks.push_back(std::move(blockPlan));
        continue;
      }
      if (auto merge = mlir::dyn_cast<ac::MergeOp>(operation)) {
        auto inputs = queueNames(merge.getInputs(), names);
        if (!inputs)
          return inputs.takeError();
        std::vector<std::string> outputs;
        if (auto error = addOutputs(
                merge, merge->getResults(), {int64_t(merge.getDepth())},
                {int64_t(merge.getLatency())}, scope, outputs))
          return error;
        plan.blocks.push_back({"merge",
                               outputs.front(),
                               scopePath(scope),
                               std::move(*inputs),
                               outputs,
                               {uint64_t(merge.getDepth())},
                               {uint64_t(merge.getLatency())},
                               merge.getPolicy().str()});
        continue;
      }
      if (auto feedback = mlir::dyn_cast<ac::FeedbackOp>(operation)) {
        auto input = queueName(feedback.getInput(), names);
        if (!input)
          return input.takeError();
        std::vector<std::string> outputs;
        if (auto error =
                addOutputs(feedback, feedback->getResults(),
                           {int64_t(feedback.getDepth())},
                           {int64_t(feedback.getLatency())}, scope, outputs))
          return error;
        QueueBlockPlan blockPlan{"feedback",
                                 outputs.front(),
                                 scopePath(scope),
                                 {*input},
                                 outputs,
                                 {uint64_t(feedback.getDepth())},
                                 {uint64_t(feedback.getLatency())},
                                 "",
                                 uint64_t(feedback.getMaxIterations())};
        blockPlan.region = printRegion(feedback.getBody());
        plan.blocks.push_back(std::move(blockPlan));
        continue;
      }
      if (auto nested = mlir::dyn_cast<ac::ScopeOp>(operation)) {
        std::vector<std::string> nestedScope = scope;
        nestedScope.push_back(nested.getSymName().str());
        plan.scopes.push_back(scopePath(nestedScope));
        mlir::Block &body = nested.getBody().front();
        if (body.getNumArguments() != nested.getInputs().size())
          return planError("scope input arity mismatch");
        for (size_t index = 0; index < nested.getInputs().size(); ++index) {
          auto name = queueName(nested.getInputs()[index], names);
          if (!name)
            return name.takeError();
          names[body.getArgument(index)] = std::move(*name);
        }
        if (auto error = extractBlock(body, nestedScope))
          return error;
        auto yield = mlir::dyn_cast<ac::ScopeYieldOp>(body.getTerminator());
        bool invalidYield = !yield;
        if (yield)
          invalidYield = yield.getQueues().size() != nested.getOutputs().size();
        if (invalidYield)
          return planError("scope output arity mismatch");
        for (size_t index = 0; index < nested.getOutputs().size(); ++index) {
          auto name = queueName(yield.getQueues()[index], names);
          if (!name)
            return name.takeError();
          names[nested.getOutputs()[index]] = std::move(*name);
        }
        continue;
      }
      auto sink = mlir::dyn_cast<ac::SinkOp>(operation);
      if (sink) {
        auto input = queueName(sink.getInput(), names);
        if (!input)
          return input.takeError();
        auto name = sink->getAttrOfType<mlir::StringAttr>("ac.name");
        if (!name || name.getValue().empty())
          return planError("sink requires frozen ac.name");
        plan.blocks.push_back(
            {"sink", name.getValue().str(), scopePath(scope), {*input}, {}});
        continue;
      }
      if (mlir::isa<ac::ScopeYieldOp>(operation) ||
          operation.hasTrait<mlir::OpTrait::IsTerminator>() ||
          mlir::isa<ac::TypeScopeOp>(operation))
        continue;
      if (operation.getName().getDialectNamespace() == "ac")
        return planError("unsupported ACIR op in QueueGraph plan: " +
                         operation.getName().getStringRef());
    }
    return llvm::Error::success();
  }

  mlir::ModuleOp module;
  QueueGraphPlan plan;
  llvm::DenseMap<mlir::Value, std::string> names;
  llvm::StringSet<> queueIdentities;
};

} // namespace

llvm::Expected<QueueGraphPlan> buildQueueGraphPlan(mlir::ModuleOp module) {
  return Extractor(module).run();
}

llvm::Expected<std::string> QueueGraphPlan::canonicalJson() const {
  llvm::json::Array scopeValues;
  for (const std::string &scope : scopes)
    scopeValues.push_back(scope);
  llvm::json::Array queueValues;
  for (const QueuePlan &queue : queues)
    queueValues.push_back(
        llvm::json::Object{{"depth", queue.depth},
                           {"latency", queue.latency},
                           {"name", queue.name},
                           {"payload_type", queue.payloadType},
                           {"scope", queue.scope}});
  llvm::json::Array blockValues;
  for (const QueueBlockPlan &block : blocks) {
    llvm::json::Array inputs;
    for (const std::string &input : block.inputs)
      inputs.push_back(input);
    llvm::json::Array outputs;
    for (const std::string &output : block.outputs)
      outputs.push_back(output);
    llvm::json::Array depths;
    for (uint64_t depth : block.depths)
      depths.push_back(depth);
    llvm::json::Array latencies;
    for (uint64_t latency : block.latencies)
      latencies.push_back(latency);
    blockValues.push_back(
        llvm::json::Object{{"depths", std::move(depths)},
                           {"inputs", std::move(inputs)},
                           {"kind", block.kind},
                           {"latencies", std::move(latencies)},
                           {"max_iterations", block.maxIterations},
                           {"name", block.name},
                           {"outputs", std::move(outputs)},
                           {"policy", block.policy},
                           {"region", block.region},
                           {"scope", block.scope}});
  }
  llvm::json::Object root{{"blocks", std::move(blockValues)},
                          {"queues", std::move(queueValues)},
                          {"schema", "agentic-circuit-queue-graph-plan"},
                          {"scopes", std::move(scopeValues)},
                          {"system", system},
                          {"version", "0.2"}};
  return bindings::canonicalizeJson(llvm::json::Value(std::move(root)));
}

} // namespace acir::codegen
