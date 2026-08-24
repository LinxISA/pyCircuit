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

const QueuePayloadPlan *findPayload(const QueueGraphPlan &plan,
                                    llvm::StringRef type) {
  constexpr llvm::StringLiteral prefix = "!ac.struct<@types::@";
  if (!type.starts_with(prefix) || !type.ends_with('>'))
    return nullptr;
  llvm::StringRef name = type.drop_front(prefix.size()).drop_back();
  auto found =
      std::find_if(plan.payloads.begin(), plan.payloads.end(),
                   [&](const auto &payload) { return payload.name == name; });
  return found == plan.payloads.end() ? nullptr : &*found;
}

llvm::Expected<unsigned> typeWidth(const QueueGraphPlan &plan,
                                   llvm::StringRef type) {
  if (type.starts_with('i')) {
    unsigned width = 0;
    if (!type.drop_front().getAsInteger(10, width) && width > 0)
      return width;
  }
  const QueuePayloadPlan *payload = findPayload(plan, type);
  if (!payload)
    return pycError("unsupported PYC payload type '" + type + "'");
  unsigned total = 0;
  for (const QueuePayloadFieldPlan &field : payload->fields) {
    auto width = typeWidth(plan, field.type);
    if (!width)
      return width.takeError();
    total += *width;
  }
  if (total == 0)
    return pycError("packed payload width must be positive");
  return total;
}

llvm::Expected<std::string> pycType(const QueueGraphPlan &plan,
                                    llvm::StringRef type) {
  auto width = typeWidth(plan, type);
  if (!width)
    return width.takeError();
  return "i" + std::to_string(*width);
}

struct FieldLayout {
  unsigned lsb = 0;
  unsigned width = 0;
  std::string type;
};

llvm::Expected<FieldLayout> fieldLayout(const QueueGraphPlan &plan,
                                        llvm::StringRef recordType,
                                        llvm::StringRef fieldName) {
  const QueuePayloadPlan *payload = findPayload(plan, recordType);
  if (!payload)
    return pycError("field access requires a packed struct payload");
  auto total = typeWidth(plan, recordType);
  if (!total)
    return total.takeError();
  unsigned cursor = *total;
  for (const QueuePayloadFieldPlan &field : payload->fields) {
    auto width = typeWidth(plan, field.type);
    if (!width)
      return width.takeError();
    cursor -= *width;
    if (field.name == fieldName)
      return FieldLayout{cursor, *width, field.type};
  }
  return pycError("unknown packed struct field '" + fieldName + "'");
}

const QueuePlan *findQueue(const QueueGraphPlan &plan, llvm::StringRef name) {
  auto found =
      std::find_if(plan.queues.begin(), plan.queues.end(),
                   [&](const QueuePlan &queue) { return queue.name == name; });
  return found == plan.queues.end() ? nullptr : &*found;
}

llvm::Expected<std::string>
emitTransform(const QueueGraphPlan &plan, const QueueBlockPlan &block,
              llvm::StringRef inputData, llvm::StringRef inputType,
              unsigned &nextValue, std::ostringstream &body) {
  llvm::StringMap<std::string> values;
  llvm::StringMap<std::string> types;
  values["item"] = inputData.str();
  types["item"] = inputType.str();
  auto newValue = [&]() { return "%v" + std::to_string(nextValue++); };
  auto value = [&](llvm::StringRef name) -> llvm::Expected<std::string> {
    auto found = values.find(name);
    if (found == values.end())
      return pycError("transform expression references unknown value '" + name +
                      "'");
    return found->getValue();
  };
  auto valueType = [&](llvm::StringRef name) -> llvm::Expected<std::string> {
    auto found = types.find(name);
    if (found == types.end())
      return pycError("transform value has no type: '" + name + "'");
    return found->getValue();
  };
  for (const QueueExpressionPlan &expression : block.expressions) {
    std::string result;
    if (expression.kind == "constant") {
      result = newValue();
      llvm::StringRef literal = expression.literal;
      auto type = pycType(plan, expression.type);
      if (!type)
        return type.takeError();
      body << "    " << result << " = pyc.constant "
           << literal.split(" : ").first.str() << " : " << *type << "\n";
    } else {
      if (expression.operands.empty())
        return pycError("transform expression operand is missing");
      auto first = value(expression.operands[0]);
      if (!first)
        return first.takeError();
      if (expression.kind == "add" || expression.kind == "sub" ||
          expression.kind == "mul") {
        result = newValue();
        if (expression.operands.size() != 2)
          return pycError("binary transform expression arity mismatch");
        auto second = value(expression.operands[1]);
        if (!second)
          return second.takeError();
        auto type = pycType(plan, expression.type);
        if (!type)
          return type.takeError();
        body << "    " << result << " = pyc." << expression.kind << ' '
             << *first << ", " << *second << " : " << *type << "\n";
      } else if (expression.kind == "get") {
        auto recordType = valueType(expression.operands[0]);
        if (!recordType)
          return recordType.takeError();
        auto layout = fieldLayout(plan, *recordType, expression.field);
        auto sourceType = pycType(plan, *recordType);
        auto resultType = pycType(plan, expression.type);
        if (!layout)
          return layout.takeError();
        if (!sourceType)
          return sourceType.takeError();
        if (!resultType)
          return resultType.takeError();
        result = newValue();
        body << "    " << result << " = pyc.extract " << *first
             << " {lsb = " << layout->lsb << "} : " << *sourceType << " -> "
             << *resultType << "\n";
      } else if (expression.kind == "with") {
        if (expression.operands.size() != 2)
          return pycError("packed field update arity mismatch");
        auto second = value(expression.operands[1]);
        auto recordType = valueType(expression.operands[0]);
        if (!second)
          return second.takeError();
        if (!recordType)
          return recordType.takeError();
        auto layout = fieldLayout(plan, *recordType, expression.field);
        auto totalWidth = typeWidth(plan, *recordType);
        if (!layout)
          return layout.takeError();
        if (!totalWidth)
          return totalWidth.takeError();
        std::vector<std::pair<std::string, unsigned>> parts;
        const unsigned highWidth = *totalWidth - layout->lsb - layout->width;
        if (highWidth > 0) {
          std::string high = newValue();
          body << "    " << high << " = pyc.extract " << *first
               << " {lsb = " << layout->lsb + layout->width << "} : i"
               << *totalWidth << " -> i" << highWidth << "\n";
          parts.emplace_back(std::move(high), highWidth);
        }
        parts.emplace_back(*second, layout->width);
        if (layout->lsb > 0) {
          std::string low = newValue();
          body << "    " << low << " = pyc.extract " << *first
               << " {lsb = 0} : i" << *totalWidth << " -> i" << layout->lsb
               << "\n";
          parts.emplace_back(std::move(low), layout->lsb);
        }
        if (parts.size() == 1) {
          result = parts.front().first;
        } else {
          result = newValue();
          body << "    " << result << " = pyc.concat(";
          for (auto [index, part] : llvm::enumerate(parts)) {
            if (index)
              body << ", ";
            body << part.first;
          }
          body << ") : (";
          for (auto [index, part] : llvm::enumerate(parts)) {
            if (index)
              body << ", ";
            body << 'i' << part.second;
          }
          body << ") -> i" << *totalWidth << "\n";
        }
      } else {
        return pycError("unsupported PYC transform expression");
      }
    }
    values[expression.result] = result;
    types[expression.result] = expression.type;
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
    if (auto width = typeWidth(plan, queue.payloadType); !width)
      return width.takeError();
    if (queue.latency != 1)
      return pycError("initial PYC slice requires Queue latency=1");
  }
  const QueuePlan *sourceQueue =
      findQueue(plan, sources.front()->outputs.front());
  const QueuePlan *sinkQueuePlan =
      findQueue(plan, sinks.front()->inputs.front());
  const bool missingBoundary = !sourceQueue || !sinkQueuePlan;
  if (missingBoundary)
    return pycError("source or sink Queue is missing");
  auto inputPortType = pycType(plan, sourceQueue->payloadType);
  auto outputPortType = pycType(plan, sinkQueuePlan->payloadType);
  if (!inputPortType)
    return inputPortType.takeError();
  if (!outputPortType)
    return outputPortType.takeError();

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
      const QueuePlan *inputQueue = findQueue(plan, transform.inputs.front());
      const bool missingInput = !inputQueue;
      if (missingInput)
        return pycError("transform input Queue is missing");
      auto transformed =
          emitTransform(plan, transform, data->getValue(),
                        inputQueue->payloadType, nextValue, body);
      if (!transformed)
        return transformed.takeError();
      producerData = std::move(*transformed);
    }
    std::string inReady = "%v" + std::to_string(nextValue++);
    std::string outValid = "%v" + std::to_string(nextValue++);
    std::string outData = "%v" + std::to_string(nextValue++);
    auto dataType = pycType(plan, queue.payloadType);
    if (!dataType)
      return dataType.takeError();
    body << "    " << inReady << ", " << outValid << ", " << outData
         << " = pyc.fifo %clk, %rst, " << producerValid << ", " << producerData
         << ", " << readyWires[queue.name] << " {depth = " << queue.depth
         << "} : " << *dataType << "\n";
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
            "%in_data: "
         << *inputPortType << ", %out_ready: i1) -> (i1, " << *outputPortType
         << ", i1) "
            "attributes {arg_names = [\"clk\", \"rst\", \"in_valid\", "
            "\"in_data\", \"out_ready\"], result_names = [\"out_valid\", "
            "\"out_data\", \"in_ready\"], pyc.value_params = [], "
            "pyc.value_param_types = [], pyc.kind = \"module\", "
            "pyc.inline = \"false\", pyc.params = \"{}\", pyc.base = \""
         << top.str() << "\", pyc.struct.metrics = \"" << kStructMetrics.str()
         << "\", pyc.struct.collections = \"[]\"} {\n"
         << body.str() << "    func.return " << outputValid[sinkQueue] << ", "
         << outputData[sinkQueue] << ", "
         << inputReady[sources.front()->outputs.front()] << " : i1, "
         << *outputPortType << ", i1\n  }\n}\n";
  return output.str();
}

} // namespace acir::codegen
