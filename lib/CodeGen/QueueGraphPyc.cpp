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

llvm::Expected<std::string> yieldedType(const QueueBlockPlan &block,
                                        llvm::StringRef yield,
                                        llvm::StringRef inputType) {
  if (yield == "item")
    return inputType.str();
  auto found = std::find_if(block.expressions.begin(), block.expressions.end(),
                            [&](const QueueExpressionPlan &expression) {
                              return expression.result == yield;
                            });
  if (found == block.expressions.end())
    return pycError("yield references unknown expression value");
  return found->type;
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
  struct RouteProducer {
    const QueueBlockPlan *block = nullptr;
    size_t index = 0;
  };
  struct MergeState {
    std::string nextWire;
    std::string enableWire;
    std::string cursor;
    std::string valid;
    std::string type;
  };
  std::vector<const QueueBlockPlan *> sources;
  std::vector<const QueueBlockPlan *> sinks;
  llvm::StringMap<const QueueBlockPlan *> transformByOutput;
  llvm::StringMap<const QueueBlockPlan *> broadcastByOutput;
  llvm::StringMap<RouteProducer> routeByOutput;
  llvm::StringMap<const QueueBlockPlan *> mergeByOutput;
  for (const QueueBlockPlan &block : plan.blocks) {
    if (block.kind == "source")
      sources.push_back(&block);
    else if (block.kind == "sink")
      sinks.push_back(&block);
    else if (block.kind == "transform") {
      if (block.outputs.size() != 1)
        return pycError("transform output arity is unsupported");
      transformByOutput[block.outputs.front()] = &block;
    } else if (block.kind == "route") {
      if (block.inputs.size() != 1 || block.outputs.size() < 2)
        return pycError("route arity is unsupported");
      for (auto [index, output] : llvm::enumerate(block.outputs))
        routeByOutput[output] = RouteProducer{&block, index};
    } else if (block.kind == "broadcast") {
      if (block.inputs.size() != 1 || block.outputs.size() < 2)
        return pycError("broadcast arity is unsupported");
      for (const std::string &output : block.outputs)
        broadcastByOutput[output] = &block;
    } else if (block.kind == "merge") {
      if (block.outputs.size() != 1 || block.inputs.size() < 2)
        return pycError("merge arity is unsupported");
      if (block.policy != "priority" && block.policy != "round_robin")
        return pycError("PYC merge policy must be priority or round_robin");
      mergeByOutput[block.outputs.front()] = &block;
    } else {
      return pycError("initial PYC slice supports "
                      "source/transform/broadcast/route/priority-merge/sink");
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
  llvm::StringMap<std::string> routeSelector;
  llvm::StringMap<std::string> routeCondition;
  llvm::StringMap<std::vector<std::string>> mergeGrants;
  llvm::StringMap<MergeState> mergeStates;
  std::ostringstream body;
  unsigned nextValue = 0;
  auto newValue = [&]() { return "%v" + std::to_string(nextValue++); };
  auto emitConstant = [&](uint64_t value, llvm::StringRef type) {
    std::string result = newValue();
    body << "    " << result << " = pyc.constant " << value << " : "
         << type.str() << "\n";
    return result;
  };
  auto emitBinary = [&](llvm::StringRef operation, llvm::StringRef lhs,
                        llvm::StringRef rhs, llvm::StringRef type) {
    std::string result = newValue();
    body << "    " << result << " = pyc." << operation.str() << ' ' << lhs.str()
         << ", " << rhs.str() << " : " << type.str() << "\n";
    return result;
  };
  auto emitNot = [&](llvm::StringRef value) {
    std::string result = newValue();
    body << "    " << result << " = pyc.not " << value.str() << " : i1\n";
    return result;
  };
  auto emitMux = [&](llvm::StringRef select, llvm::StringRef trueValue,
                     llvm::StringRef falseValue, llvm::StringRef type) {
    std::string result = newValue();
    body << "    " << result << " = pyc.mux " << select.str() << ", "
         << trueValue.str() << ", " << falseValue.str() << " : " << type.str()
         << "\n";
    return result;
  };
  for (const QueuePlan &queue : plan.queues) {
    std::string ready = newValue();
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
      auto transformProducer = transformByOutput.find(queue.name);
      auto broadcastProducer = broadcastByOutput.find(queue.name);
      auto routeProducer = routeByOutput.find(queue.name);
      auto mergeProducer = mergeByOutput.find(queue.name);
      if (transformProducer != transformByOutput.end()) {
        const QueueBlockPlan &transform = *transformProducer->getValue();
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
      } else if (broadcastProducer != broadcastByOutput.end()) {
        const QueueBlockPlan &broadcast = *broadcastProducer->getValue();
        auto valid = outputValid.find(broadcast.inputs.front());
        auto data = outputData.find(broadcast.inputs.front());
        if (valid == outputValid.end() || data == outputData.end())
          return pycError(
              "broadcast input is not available in topological order");
        producerValid = valid->getValue();
        producerData = data->getValue();
      } else if (routeProducer != routeByOutput.end()) {
        const RouteProducer &producer = routeProducer->getValue();
        const QueueBlockPlan &route = *producer.block;
        auto valid = outputValid.find(route.inputs.front());
        auto data = outputData.find(route.inputs.front());
        const QueuePlan *inputQueue = findQueue(plan, route.inputs.front());
        if (valid == outputValid.end() || data == outputData.end() ||
            !inputQueue)
          return pycError("route input is not available in topological order");
        auto selector = routeSelector.find(route.name);
        if (selector == routeSelector.end()) {
          auto selected =
              emitTransform(plan, route, data->getValue(),
                            inputQueue->payloadType, nextValue, body);
          if (!selected)
            return selected.takeError();
          routeSelector[route.name] = *selected;
          selector = routeSelector.find(route.name);
        }
        auto selectorType =
            yieldedType(route, route.yields.front(), inputQueue->payloadType);
        if (!selectorType)
          return selectorType.takeError();
        auto selectorPycType = pycType(plan, *selectorType);
        if (!selectorPycType)
          return selectorPycType.takeError();
        std::string index = emitConstant(producer.index, *selectorPycType);
        std::string condition =
            emitBinary("eq", selector->getValue(), index, *selectorPycType);
        routeCondition[queue.name] = condition;
        producerValid = emitBinary("and", valid->getValue(), condition, "i1");
        producerData = data->getValue();
      } else if (mergeProducer != mergeByOutput.end()) {
        const QueueBlockPlan &merge = *mergeProducer->getValue();
        std::vector<std::string> valids;
        std::vector<std::string> dataValues;
        for (const std::string &input : merge.inputs) {
          auto valid = outputValid.find(input);
          auto data = outputData.find(input);
          if (valid == outputValid.end() || data == outputData.end())
            return pycError(
                "merge input is not available in topological order");
          valids.push_back(valid->getValue());
          dataValues.push_back(data->getValue());
        }
        std::string any = valids.front();
        for (size_t index = 1; index < valids.size(); ++index) {
          any = emitBinary("or", any, valids[index], "i1");
        }
        std::vector<std::string> grants;
        if (merge.policy == "priority") {
          grants.push_back(valids.front());
          std::string prior = valids.front();
          for (size_t index = 1; index < valids.size(); ++index) {
            std::string notPrior = emitNot(prior);
            grants.push_back(emitBinary("and", valids[index], notPrior, "i1"));
            prior = emitBinary("or", prior, valids[index], "i1");
          }
        } else {
          unsigned pointerWidth = 1;
          while ((uint64_t{1} << pointerWidth) < valids.size())
            ++pointerWidth;
          std::string pointerType = "i" + std::to_string(pointerWidth);
          std::string nextWire = newValue();
          std::string enableWire = newValue();
          body << "    " << nextWire << " = pyc.wire : " << pointerType << "\n";
          body << "    " << enableWire << " = pyc.wire : i1\n";
          std::string zeroPointer = emitConstant(0, pointerType);
          std::string cursor = newValue();
          body << "    " << cursor << " = pyc.reg %clk, %rst, " << enableWire
               << ", " << nextWire << ", " << zeroPointer << " : "
               << pointerType << "\n";
          std::string falseValue = emitConstant(0, "i1");
          grants.assign(valids.size(), falseValue);
          for (size_t start = 0; start < valids.size(); ++start) {
            std::vector<std::string> caseGrants(valids.size(), falseValue);
            std::string prior;
            for (size_t offset = 0; offset < valids.size(); ++offset) {
              const size_t input = (start + offset) % valids.size();
              if (offset == 0) {
                caseGrants[input] = valids[input];
                prior = valids[input];
              } else {
                std::string notPrior = emitNot(prior);
                caseGrants[input] =
                    emitBinary("and", valids[input], notPrior, "i1");
                prior = emitBinary("or", prior, valids[input], "i1");
              }
            }
            std::string startValue = emitConstant(start, pointerType);
            std::string selectedCase =
                emitBinary("eq", cursor, startValue, pointerType);
            for (size_t input = 0; input < valids.size(); ++input)
              grants[input] =
                  emitMux(selectedCase, caseGrants[input], grants[input], "i1");
          }
          mergeStates[merge.name] =
              MergeState{nextWire, enableWire, cursor, any, pointerType};
        }
        auto outputType = pycType(plan, queue.payloadType);
        if (!outputType)
          return outputType.takeError();
        std::string selectedData = dataValues.back();
        for (size_t index = dataValues.size() - 1; index-- > 0;)
          selectedData = emitMux(grants[index], dataValues[index], selectedData,
                                 *outputType);
        mergeGrants[merge.name] = grants;
        producerValid = any;
        producerData = selectedData;
      } else {
        return pycError("Queue has no supported producer: '" + queue.name +
                        "'");
      }
    }
    std::string inReady = newValue();
    std::string outValid = newValue();
    std::string outData = newValue();
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
    } else if (block.kind == "broadcast") {
      std::string allReady = inputReady[block.outputs.front()];
      for (size_t index = 1; index < block.outputs.size(); ++index)
        allReady =
            emitBinary("and", allReady, inputReady[block.outputs[index]], "i1");
      body << "    pyc.assign " << readyWires[block.inputs.front()] << ", "
           << allReady << " : i1\n";
    } else if (block.kind == "route") {
      std::string selectedReady = emitConstant(0, "i1");
      for (size_t index = block.outputs.size(); index-- > 0;) {
        auto condition = routeCondition.find(block.outputs[index]);
        if (condition == routeCondition.end())
          return pycError("route output condition is missing");
        selectedReady =
            emitMux(condition->getValue(), inputReady[block.outputs[index]],
                    selectedReady, "i1");
      }
      body << "    pyc.assign " << readyWires[block.inputs.front()] << ", "
           << selectedReady << " : i1\n";
    } else if (block.kind == "merge") {
      auto grants = mergeGrants.find(block.name);
      if (grants == mergeGrants.end() ||
          grants->getValue().size() != block.inputs.size())
        return pycError("merge grant plan is missing");
      const std::string &ready = inputReady[block.outputs.front()];
      for (auto [index, input] : llvm::enumerate(block.inputs)) {
        std::string accepted =
            emitBinary("and", ready, grants->getValue()[index], "i1");
        body << "    pyc.assign " << readyWires[input] << ", " << accepted
             << " : i1\n";
      }
      auto state = mergeStates.find(block.name);
      if (state != mergeStates.end()) {
        std::string accepted =
            emitBinary("and", ready, state->getValue().valid, "i1");
        body << "    pyc.assign " << state->getValue().enableWire << ", "
             << accepted << " : i1\n";
        std::string next = state->getValue().cursor;
        for (size_t index = block.inputs.size(); index-- > 0;) {
          std::string nextValue = emitConstant(
              (index + 1) % block.inputs.size(), state->getValue().type);
          next = emitMux(grants->getValue()[index], nextValue, next,
                         state->getValue().type);
        }
        body << "    pyc.assign " << state->getValue().nextWire << ", " << next
             << " : " << state->getValue().type << "\n";
      }
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
