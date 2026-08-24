#include "acir/CodeGen/QueueGraphPyc.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

#include <algorithm>
#include <sstream>
#include <system_error>

namespace acir::codegen {
namespace {

llvm::Error pycError(const llvm::Twine &message) {
  return llvm::createStringError(
      std::make_error_code(std::errc::invalid_argument),
      "ACLOWER-PYC: " + message);
}

bool isScalarPayload(llvm::StringRef type) { return type == "i64"; }

llvm::Expected<std::string> emitTransform(const QueueBlockPlan &block,
                                          llvm::StringRef inputData,
                                          unsigned &nextValue,
                                          std::ostringstream &body) {
  llvm::StringMap<std::string> values;
  values["item"] = inputData.str();
  auto value = [&](llvm::StringRef name) -> llvm::Expected<std::string> {
    auto found = values.find(name);
    if (found == values.end())
      return pycError("transform expression references unknown value '" + name +
                      "'");
    return found->getValue();
  };
  for (const QueueExpressionPlan &expression : block.expressions) {
    std::string result = "%v" + std::to_string(nextValue++);
    if (expression.kind == "constant") {
      llvm::StringRef literal = expression.literal;
      body << "    " << result << " = pyc.constant "
           << literal.split(" : ").first.str() << " : " << expression.type
           << "\n";
    } else {
      if (expression.operands.empty())
        return pycError("transform expression operand is missing");
      auto first = value(expression.operands[0]);
      if (!first)
        return first.takeError();
      if (expression.kind == "add" || expression.kind == "sub" ||
          expression.kind == "mul") {
        if (expression.operands.size() != 2)
          return pycError("binary transform expression arity mismatch");
        auto second = value(expression.operands[1]);
        if (!second)
          return second.takeError();
        body << "    " << result << " = pyc." << expression.kind << ' '
             << *first << ", " << *second << " : " << expression.type << "\n";
      } else {
        return pycError("initial PYC slice supports scalar arithmetic only");
      }
    }
    values[expression.result] = result;
  }
  if (block.yields.size() != 1)
    return pycError("transform requires exactly one yielded value");
  return value(block.yields.front());
}

constexpr llvm::StringLiteral kStructMetrics =
    "{\\\"ast_node_count\\\":0,\\\"collection_count\\\":0,"
    "\\\"collection_instance_count\\\":0,"
    "\\\"estimated_inline_cost\\\":0,\\\"hardware_call_count\\\":0,"
    "\\\"instance_count\\\":0,\\\"loop_count\\\":0,"
    "\\\"module_call_count\\\":0,"
    "\\\"module_family_collection_count\\\":0,"
    "\\\"repeat_pressure\\\":0,\\\"repeated_body_clusters\\\":[],"
    "\\\"source_loc\\\":0,\\\"state_alloc_count\\\":0,"
    "\\\"state_call_count\\\":0}";

} // namespace

llvm::Expected<std::string> generateQueueGraphPyc(const QueueGraphPlan &plan) {
  std::vector<const QueueBlockPlan *> sources;
  std::vector<const QueueBlockPlan *> sinks;
  llvm::StringMap<const QueueBlockPlan *> transformByOutput;
  for (const QueueBlockPlan &block : plan.blocks) {
    if (block.kind == "source")
      sources.push_back(&block);
    else if (block.kind == "sink")
      sinks.push_back(&block);
    else if (block.kind == "transform") {
      if (block.outputs.size() != 1)
        return pycError("transform output arity is unsupported");
      transformByOutput[block.outputs.front()] = &block;
    } else {
      return pycError("initial PYC slice supports source/transform/sink only");
    }
  }
  if (sources.size() != 1 || sinks.size() != 1)
    return pycError("initial PYC slice requires one source and one sink");
  for (const QueuePlan &queue : plan.queues) {
    if (!isScalarPayload(queue.payloadType))
      return pycError("initial PYC slice requires i64 Queue payloads");
    if (queue.latency != 1)
      return pycError("initial PYC slice requires Queue latency=1");
  }

  llvm::StringMap<std::string> readyWires;
  llvm::StringMap<std::string> inputReady;
  llvm::StringMap<std::string> outputValid;
  llvm::StringMap<std::string> outputData;
  std::ostringstream body;
  unsigned nextValue = 0;
  for (const QueuePlan &queue : plan.queues) {
    std::string ready = "%v" + std::to_string(nextValue++);
    readyWires[queue.name] = ready;
    body << "    " << ready << " = pyc.wire : i1\n";
  }
  for (const QueuePlan &queue : plan.queues) {
    std::string producerValid;
    std::string producerData;
    const QueueBlockPlan *source =
        sources.front()->outputs.front() == queue.name ? sources.front()
                                                       : nullptr;
    if (source) {
      producerValid = "%in_valid";
      producerData = "%in_data";
    } else {
      auto producer = transformByOutput.find(queue.name);
      if (producer == transformByOutput.end())
        return pycError("Queue has no supported producer: '" + queue.name +
                        "'");
      const QueueBlockPlan &transform = *producer->getValue();
      if (transform.inputs.size() != 1)
        return pycError("transform input arity is unsupported");
      auto valid = outputValid.find(transform.inputs.front());
      auto data = outputData.find(transform.inputs.front());
      if (valid == outputValid.end() || data == outputData.end())
        return pycError("Queue transforms are not in topological order");
      producerValid = valid->getValue();
      auto transformed =
          emitTransform(transform, data->getValue(), nextValue, body);
      if (!transformed)
        return transformed.takeError();
      producerData = std::move(*transformed);
    }
    std::string inReady = "%v" + std::to_string(nextValue++);
    std::string outValid = "%v" + std::to_string(nextValue++);
    std::string outData = "%v" + std::to_string(nextValue++);
    body << "    " << inReady << ", " << outValid << ", " << outData
         << " = pyc.fifo %clk, %rst, " << producerValid << ", " << producerData
         << ", " << readyWires[queue.name] << " {depth = " << queue.depth
         << "} : i64\n";
    inputReady[queue.name] = inReady;
    outputValid[queue.name] = outValid;
    outputData[queue.name] = outData;
  }
  for (const QueueBlockPlan &block : plan.blocks) {
    if (block.kind == "transform") {
      body << "    pyc.assign " << readyWires[block.inputs.front()] << ", "
           << inputReady[block.outputs.front()] << " : i1\n";
    }
  }
  const std::string &sinkQueue = sinks.front()->inputs.front();
  body << "    pyc.assign " << readyWires[sinkQueue] << ", %out_ready : i1\n";

  llvm::StringRef top = plan.system;
  std::ostringstream output;
  output << "module attributes {pyc.top = @" << top.str()
         << ", pyc.frontend.contract = \"pycircuit\"} {\n"
         << "  func.func @" << top.str()
         << "(%clk: !pyc.clock, %rst: !pyc.reset, %in_valid: i1, "
            "%in_data: i64, %out_ready: i1) -> (i1, i64, i1) "
            "attributes {arg_names = [\"clk\", \"rst\", \"in_valid\", "
            "\"in_data\", \"out_ready\"], result_names = [\"out_valid\", "
            "\"out_data\", \"in_ready\"], pyc.value_params = [], "
            "pyc.value_param_types = [], pyc.kind = \"module\", "
            "pyc.inline = \"false\", pyc.params = \"{}\", pyc.base = \""
         << top.str() << "\", pyc.struct.metrics = \"" << kStructMetrics.str()
         << "\", pyc.struct.collections = \"[]\"} {\n"
         << body.str() << "    func.return " << outputValid[sinkQueue] << ", "
         << outputData[sinkQueue] << ", "
         << inputReady[sources.front()->outputs.front()]
         << " : i1, i64, i1\n  }\n}\n";
  return output.str();
}

} // namespace acir::codegen
