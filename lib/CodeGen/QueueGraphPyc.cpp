#include "acir/CodeGen/QueueGraphPyc.h"
#include "acir/CodeGen/QueueBlockContract.h"

#include "llvm/ADT/ArrayRef.h"
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
              llvm::ArrayRef<std::string> inputData,
              llvm::ArrayRef<std::string> inputTypes, size_t yieldIndex,
              unsigned &nextValue, std::ostringstream &body) {
  if (inputData.size() != inputTypes.size() || inputData.empty())
    return pycError("transform input data/type arity mismatch");
  llvm::StringMap<std::string> values;
  llvm::StringMap<std::string> types;
  for (size_t index = 0; index < inputData.size(); ++index) {
    std::string name = index == 0 ? "item" : "item" + std::to_string(index);
    values[name] = inputData[index];
    types[name] = inputTypes[index];
  }
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
      } else if (expression.kind == "cmp") {
        if (expression.operands.size() != 2)
          return pycError("comparison expression arity mismatch");
        auto second = value(expression.operands[1]);
        auto firstType = valueType(expression.operands[0]);
        auto secondType = valueType(expression.operands[1]);
        if (!second)
          return second.takeError();
        if (!firstType)
          return firstType.takeError();
        if (!secondType)
          return secondType.takeError();
        if (*firstType != *secondType)
          return pycError("comparison operand types must match");
        auto type = pycType(plan, *firstType);
        if (!type)
          return type.takeError();

        llvm::StringRef opcode;
        std::string lhs = *first;
        std::string rhs = *second;
        bool negate = false;
        if (expression.predicate == "eq" || expression.predicate == "ne") {
          opcode = "eq";
          negate = expression.predicate == "ne";
        } else if (expression.predicate == "slt" ||
                   expression.predicate == "sge") {
          opcode = "slt";
          negate = expression.predicate == "sge";
        } else if (expression.predicate == "sgt" ||
                   expression.predicate == "sle") {
          opcode = "slt";
          std::swap(lhs, rhs);
          negate = expression.predicate == "sle";
        } else {
          return pycError("unsupported comparison predicate");
        }
        std::string compared = newValue();
        body << "    " << compared << " = pyc." << opcode.str() << ' ' << lhs
             << ", " << rhs << " : " << *type << "\n";
        if (negate) {
          result = newValue();
          body << "    " << result << " = pyc.not " << compared << " : i1\n";
        } else {
          result = std::move(compared);
        }
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
  if (yieldIndex >= block.yields.size())
    return pycError("transform yield index is outside result arity");
  return value(block.yields[yieldIndex]);
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
  if (!plan.scopes.empty()) {
    const QueueBlockContract *scope = findQueueBlockContract("scope");
    if (!scope || !scope->pycAvailable)
      return pycError("official opcode has no PYC lowering: 'scope'");
  }
  struct TransformProducer {
    const QueueBlockPlan *block = nullptr;
    size_t index = 0;
  };
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
  struct ForkState {
    std::vector<std::string> nextWires;
    std::vector<std::string> enableWires;
    std::vector<std::string> delivered;
  };
  struct FeedbackState {
    std::string validNext;
    std::string validEnable;
    std::string valid;
    std::string dataNext;
    std::string dataEnable;
    std::string data;
    std::string iterationNext;
    std::string iterationEnable;
    std::string iteration;
    std::string selectedValid;
    std::string selectedIteration;
    std::string condition;
    std::string updated;
    std::string underLimit;
    std::string dataType;
    std::string iterationType;
  };
  std::vector<const QueueBlockPlan *> sources;
  std::vector<const QueueBlockPlan *> sinks;
  std::vector<const QueueBlockPlan *> observations;
  llvm::StringMap<TransformProducer> transformByOutput;
  llvm::StringMap<const QueueBlockPlan *> broadcastByOutput;
  llvm::StringMap<const QueueBlockPlan *> forkByOutput;
  llvm::StringMap<RouteProducer> routeByOutput;
  llvm::StringMap<const QueueBlockPlan *> mergeByOutput;
  llvm::StringMap<const QueueBlockPlan *> feedbackByOutput;
  for (const QueueBlockPlan &block : plan.blocks) {
    const QueueBlockContract *contract = findQueueBlockContract(block.kind);
    if (!contract || !contract->pycAvailable)
      return pycError("official opcode has no PYC lowering: '" + block.kind +
                      "'");
    if (block.kind == "source")
      sources.push_back(&block);
    else if (block.kind == "sink")
      sinks.push_back(&block);
    else if (block.kind == "observe")
      observations.push_back(&block);
    else if (block.kind == "transform") {
      if (block.inputs.empty() || block.outputs.empty() ||
          block.yields.size() != block.outputs.size())
        return pycError("transform output arity is unsupported");
      for (auto [index, output] : llvm::enumerate(block.outputs))
        transformByOutput[output] = TransformProducer{&block, index};
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
    } else if (block.kind == "fork") {
      if (block.inputs.size() != 1 || block.outputs.size() < 2)
        return pycError("fork arity is unsupported");
      for (const std::string &output : block.outputs)
        forkByOutput[output] = &block;
    } else if (block.kind == "merge") {
      if (block.outputs.size() != 1 || block.inputs.size() < 2)
        return pycError("merge arity is unsupported");
      if (block.policy != "priority" && block.policy != "round_robin")
        return pycError("PYC merge policy must be priority or round_robin");
      mergeByOutput[block.outputs.front()] = &block;
    } else if (block.kind == "feedback") {
      if (block.inputs.size() != 1 || block.outputs.size() != 1 ||
          block.yields.size() != 2 || block.maxIterations == 0)
        return pycError("feedback contract is unsupported");
      feedbackByOutput[block.outputs.front()] = &block;
    } else {
      return pycError("PYC QueueGraph supports source/transform/broadcast/fork/"
                      "route/merge/feedback/observe/sink");
    }
  }
  if (sources.empty() || sinks.empty())
    return pycError("PYC lowering requires at least one source and one sink");
  for (const QueuePlan &queue : plan.queues) {
    if (auto width = typeWidth(plan, queue.payloadType); !width)
      return width.takeError();
    if (queue.latency == 0)
      return pycError("PYC Queue latency must be positive");
  }
  llvm::StringMap<size_t> sourceBoundary;
  std::vector<std::string> inputPortTypes;
  for (auto [index, source] : llvm::enumerate(sources)) {
    const QueuePlan *queue = findQueue(plan, source->outputs.front());
    if (!queue)
      return pycError("source Queue is missing");
    auto type = pycType(plan, queue->payloadType);
    if (!type)
      return type.takeError();
    sourceBoundary[source->outputs.front()] = index;
    inputPortTypes.push_back(std::move(*type));
  }
  std::vector<std::string> outputPortTypes;
  for (const QueueBlockPlan *sink : sinks) {
    const QueuePlan *queue = findQueue(plan, sink->inputs.front());
    if (!queue)
      return pycError("sink Queue is missing");
    auto type = pycType(plan, queue->payloadType);
    if (!type)
      return type.takeError();
    outputPortTypes.push_back(std::move(*type));
  }
  auto inputName = [&](size_t index, llvm::StringRef suffix) {
    return sources.size() == 1
               ? ("%in_" + suffix).str()
               : ("%in" + std::to_string(index) + "_" + suffix.str());
  };
  auto outputName = [&](size_t index, llvm::StringRef suffix) {
    return sinks.size() == 1
               ? ("%out_" + suffix).str()
               : ("%out" + std::to_string(index) + "_" + suffix.str());
  };

  llvm::StringMap<std::string> readyWires;
  llvm::StringMap<std::string> inputReady;
  llvm::StringMap<std::string> outputValid;
  llvm::StringMap<std::string> outputData;
  llvm::StringMap<std::string> routeSelector;
  llvm::StringMap<std::string> routeCondition;
  llvm::StringMap<std::string> atomicTransformValid;
  llvm::StringMap<std::vector<std::string>> mergeGrants;
  llvm::StringMap<MergeState> mergeStates;
  llvm::StringMap<ForkState> forkStates;
  llvm::StringMap<FeedbackState> feedbackStates;
  llvm::StringMap<std::string> forkOfferValid;
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
    auto source = sourceBoundary.find(queue.name);
    if (source != sourceBoundary.end()) {
      producerValid = inputName(source->getValue(), "valid");
      producerData = inputName(source->getValue(), "data");
    } else {
      auto transformProducer = transformByOutput.find(queue.name);
      auto broadcastProducer = broadcastByOutput.find(queue.name);
      auto forkProducer = forkByOutput.find(queue.name);
      auto routeProducer = routeByOutput.find(queue.name);
      auto mergeProducer = mergeByOutput.find(queue.name);
      auto feedbackProducer = feedbackByOutput.find(queue.name);
      if (transformProducer != transformByOutput.end()) {
        const TransformProducer &producer = transformProducer->getValue();
        const QueueBlockPlan &transform = *producer.block;
        std::vector<std::string> inputDataValues;
        std::vector<std::string> inputTypes;
        std::string allValid;
        for (const std::string &inputName : transform.inputs) {
          auto valid = outputValid.find(inputName);
          auto data = outputData.find(inputName);
          const QueuePlan *inputQueue = findQueue(plan, inputName);
          if (valid == outputValid.end() || data == outputData.end() ||
              !inputQueue)
            return pycError("Queue transforms are not in topological order");
          allValid = allValid.empty()
                         ? valid->getValue()
                         : emitBinary("and", allValid, valid->getValue(), "i1");
          inputDataValues.push_back(data->getValue());
          inputTypes.push_back(inputQueue->payloadType);
        }
        if (transform.inputs.size() == 1 && transform.outputs.size() == 1) {
          producerValid = allValid;
        } else {
          producerValid = newValue();
          body << "    " << producerValid << " = pyc.wire : i1\n";
          atomicTransformValid[queue.name] = producerValid;
        }
        auto transformed =
            emitTransform(plan, transform, inputDataValues, inputTypes,
                          producer.index, nextValue, body);
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
      } else if (forkProducer != forkByOutput.end()) {
        const QueueBlockPlan &fork = *forkProducer->getValue();
        auto valid = outputValid.find(fork.inputs.front());
        auto data = outputData.find(fork.inputs.front());
        if (valid == outputValid.end() || data == outputData.end())
          return pycError("fork input is not available in topological order");
        auto state = forkStates.find(fork.name);
        if (state == forkStates.end()) {
          ForkState created;
          std::string zero = emitConstant(0, "i1");
          for (size_t index = 0; index < fork.outputs.size(); ++index) {
            std::string nextWire = newValue();
            std::string enableWire = newValue();
            std::string delivered = newValue();
            body << "    " << nextWire << " = pyc.wire : i1\n";
            body << "    " << enableWire << " = pyc.wire : i1\n";
            body << "    " << delivered << " = pyc.reg %clk, %rst, "
                 << enableWire << ", " << nextWire << ", " << zero << " : i1\n";
            created.nextWires.push_back(std::move(nextWire));
            created.enableWires.push_back(std::move(enableWire));
            created.delivered.push_back(std::move(delivered));
          }
          forkStates[fork.name] = std::move(created);
          state = forkStates.find(fork.name);
        }
        auto output =
            std::find(fork.outputs.begin(), fork.outputs.end(), queue.name);
        if (output == fork.outputs.end())
          return pycError("fork output identity is missing");
        const size_t index = std::distance(fork.outputs.begin(), output);
        std::string notDelivered = emitNot(state->getValue().delivered[index]);
        producerValid =
            emitBinary("and", valid->getValue(), notDelivered, "i1");
        forkOfferValid[queue.name] = producerValid;
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
              emitTransform(plan, route, {data->getValue()},
                            {inputQueue->payloadType}, 0, nextValue, body);
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
      } else if (feedbackProducer != feedbackByOutput.end()) {
        const QueueBlockPlan &feedback = *feedbackProducer->getValue();
        auto inputValidValue = outputValid.find(feedback.inputs.front());
        auto inputDataValue = outputData.find(feedback.inputs.front());
        const QueuePlan *inputQueue = findQueue(plan, feedback.inputs.front());
        if (inputValidValue == outputValid.end() ||
            inputDataValue == outputData.end() || !inputQueue)
          return pycError(
              "feedback input is not available in topological order");
        auto dataType = pycType(plan, inputQueue->payloadType);
        if (!dataType)
          return dataType.takeError();
        unsigned iterationWidth = 1;
        while (iterationWidth < 64 &&
               (uint64_t{1} << iterationWidth) <= feedback.maxIterations)
          ++iterationWidth;
        std::string iterationType = "i" + std::to_string(iterationWidth);
        std::string zeroValid = emitConstant(0, "i1");
        std::string zeroData = emitConstant(0, *dataType);
        std::string zeroIteration = emitConstant(0, iterationType);
        FeedbackState state;
        state.validNext = newValue();
        state.validEnable = newValue();
        state.valid = newValue();
        body << "    " << state.validNext << " = pyc.wire : i1\n";
        body << "    " << state.validEnable << " = pyc.wire : i1\n";
        body << "    " << state.valid << " = pyc.reg %clk, %rst, "
             << state.validEnable << ", " << state.validNext << ", "
             << zeroValid << " : i1\n";
        state.dataNext = newValue();
        state.dataEnable = newValue();
        state.data = newValue();
        body << "    " << state.dataNext << " = pyc.wire : " << *dataType
             << "\n";
        body << "    " << state.dataEnable << " = pyc.wire : i1\n";
        body << "    " << state.data << " = pyc.reg %clk, %rst, "
             << state.dataEnable << ", " << state.dataNext << ", " << zeroData
             << " : " << *dataType << "\n";
        state.iterationNext = newValue();
        state.iterationEnable = newValue();
        state.iteration = newValue();
        body << "    " << state.iterationNext
             << " = pyc.wire : " << iterationType << "\n";
        body << "    " << state.iterationEnable << " = pyc.wire : i1\n";
        body << "    " << state.iteration << " = pyc.reg %clk, %rst, "
             << state.iterationEnable << ", " << state.iterationNext << ", "
             << zeroIteration << " : " << iterationType << "\n";
        std::string selectedData = emitMux(
            state.valid, state.data, inputDataValue->getValue(), *dataType);
        state.selectedValid =
            emitBinary("or", state.valid, inputValidValue->getValue(), "i1");
        state.selectedIteration =
            emitMux(state.valid, state.iteration, zeroIteration, iterationType);
        auto updated =
            emitTransform(plan, feedback, {selectedData},
                          {inputQueue->payloadType}, 0, nextValue, body);
        if (!updated)
          return updated.takeError();
        state.updated = std::move(*updated);
        auto condition =
            emitTransform(plan, feedback, {selectedData},
                          {inputQueue->payloadType}, 1, nextValue, body);
        if (!condition)
          return condition.takeError();
        auto conditionType =
            yieldedType(feedback, feedback.yields[1], inputQueue->payloadType);
        if (!conditionType)
          return conditionType.takeError();
        auto conditionPycType = pycType(plan, *conditionType);
        if (!conditionPycType)
          return conditionPycType.takeError();
        if (*conditionPycType != "i1")
          return pycError("feedback condition must lower to i1");
        state.condition = std::move(*condition);
        std::string limit = emitConstant(feedback.maxIterations, iterationType);
        state.underLimit =
            emitBinary("ult", state.selectedIteration, limit, iterationType);
        std::string done = emitNot(state.condition);
        producerValid = emitBinary("and", state.selectedValid, done, "i1");
        producerData = std::move(selectedData);
        state.dataType = *dataType;
        state.iterationType = std::move(iterationType);
        feedbackStates[feedback.name] = std::move(state);
      } else {
        return pycError("Queue has no supported producer: '" + queue.name +
                        "'");
      }
    }
    auto dataType = pycType(plan, queue.payloadType);
    if (!dataType)
      return dataType.takeError();
    std::vector<std::string> stageReady;
    for (uint64_t stage = 0; stage + 1 < queue.latency; ++stage) {
      std::string ready = newValue();
      body << "    " << ready << " = pyc.wire : i1\n";
      stageReady.push_back(std::move(ready));
    }
    stageReady.push_back(readyWires[queue.name]);
    std::string currentValid = producerValid;
    std::string currentData = producerData;
    std::string firstReady;
    for (uint64_t stage = 0; stage < queue.latency; ++stage) {
      std::string inReady = newValue();
      std::string outValid = newValue();
      std::string outData = newValue();
      const uint64_t depth = stage == 0 ? queue.depth : 1;
      body << "    " << inReady << ", " << outValid << ", " << outData
           << " = pyc.fifo %clk, %rst, " << currentValid << ", " << currentData
           << ", " << stageReady[stage] << " {depth = " << depth
           << "} : " << *dataType << "\n";
      if (stage == 0)
        firstReady = inReady;
      else
        body << "    pyc.assign " << stageReady[stage - 1] << ", " << inReady
             << " : i1\n";
      currentValid = std::move(outValid);
      currentData = std::move(outData);
    }
    inputReady[queue.name] = std::move(firstReady);
    outputValid[queue.name] = std::move(currentValid);
    outputData[queue.name] = std::move(currentData);
  }
  for (const QueueBlockPlan &block : plan.blocks) {
    if (block.kind == "transform") {
      if (block.inputs.size() == 1 && block.outputs.size() == 1) {
        body << "    pyc.assign " << readyWires[block.inputs.front()] << ", "
             << inputReady[block.outputs.front()] << " : i1\n";
      } else {
        std::string allReady = inputReady[block.outputs.front()];
        for (size_t index = 1; index < block.outputs.size(); ++index)
          allReady = emitBinary("and", allReady,
                                inputReady[block.outputs[index]], "i1");
        std::string allValid = outputValid[block.inputs.front()];
        for (size_t index = 1; index < block.inputs.size(); ++index)
          allValid = emitBinary("and", allValid,
                                outputValid[block.inputs[index]], "i1");
        for (auto [index, input] : llvm::enumerate(block.inputs)) {
          std::string inputCanFire = allReady;
          for (auto [otherIndex, other] : llvm::enumerate(block.inputs)) {
            if (otherIndex == index)
              continue;
            inputCanFire =
                emitBinary("and", inputCanFire, outputValid[other], "i1");
          }
          body << "    pyc.assign " << readyWires[input] << ", " << inputCanFire
               << " : i1\n";
        }
        for (auto [index, output] : llvm::enumerate(block.outputs)) {
          auto validWire = atomicTransformValid.find(output);
          if (validWire == atomicTransformValid.end())
            return pycError("atomic transform valid wire is missing");
          std::string outputCanFire = allValid;
          for (auto [otherIndex, other] : llvm::enumerate(block.outputs)) {
            if (otherIndex == index)
              continue;
            outputCanFire =
                emitBinary("and", outputCanFire, inputReady[other], "i1");
          }
          body << "    pyc.assign " << validWire->getValue() << ", "
               << outputCanFire << " : i1\n";
        }
      }
    } else if (block.kind == "broadcast") {
      std::string allReady = inputReady[block.outputs.front()];
      for (size_t index = 1; index < block.outputs.size(); ++index)
        allReady =
            emitBinary("and", allReady, inputReady[block.outputs[index]], "i1");
      body << "    pyc.assign " << readyWires[block.inputs.front()] << ", "
           << allReady << " : i1\n";
    } else if (block.kind == "fork") {
      auto state = forkStates.find(block.name);
      auto inputValidValue = outputValid.find(block.inputs.front());
      if (state == forkStates.end() || inputValidValue == outputValid.end())
        return pycError("fork state or input valid is missing");
      std::vector<std::string> deliveredNow;
      for (auto [index, output] : llvm::enumerate(block.outputs)) {
        std::string accepted =
            emitBinary("and", forkOfferValid[output], inputReady[output], "i1");
        deliveredNow.push_back(emitBinary(
            "or", state->getValue().delivered[index], accepted, "i1"));
      }
      std::string deliveredAll = deliveredNow.front();
      for (size_t index = 1; index < deliveredNow.size(); ++index)
        deliveredAll =
            emitBinary("and", deliveredAll, deliveredNow[index], "i1");
      std::string complete =
          emitBinary("and", inputValidValue->getValue(), deliveredAll, "i1");
      body << "    pyc.assign " << readyWires[block.inputs.front()] << ", "
           << complete << " : i1\n";
      std::string zero = emitConstant(0, "i1");
      for (size_t index = 0; index < block.outputs.size(); ++index) {
        std::string next = emitMux(complete, zero, deliveredNow[index], "i1");
        body << "    pyc.assign " << state->getValue().nextWires[index] << ", "
             << next << " : i1\n";
        body << "    pyc.assign " << state->getValue().enableWires[index]
             << ", " << inputValidValue->getValue() << " : i1\n";
      }
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
    } else if (block.kind == "feedback") {
      auto state = feedbackStates.find(block.name);
      if (state == feedbackStates.end())
        return pycError("feedback state is missing");
      std::string notInternal = emitNot(state->getValue().valid);
      std::string canProceed =
          emitMux(state->getValue().condition, state->getValue().underLimit,
                  inputReady[block.outputs.front()], "i1");
      std::string accepted =
          emitBinary("and", state->getValue().selectedValid, canProceed, "i1");
      std::string externalReady =
          emitBinary("and", notInternal, canProceed, "i1");
      body << "    pyc.assign " << readyWires[block.inputs.front()] << ", "
           << externalReady << " : i1\n";
      std::string continueAccepted =
          emitBinary("and", accepted, state->getValue().condition, "i1");
      body << "    pyc.assign " << state->getValue().validNext << ", "
           << state->getValue().condition << " : i1\n";
      body << "    pyc.assign " << state->getValue().validEnable << ", "
           << accepted << " : i1\n";
      body << "    pyc.assign " << state->getValue().dataNext << ", "
           << state->getValue().updated << " : " << state->getValue().dataType
           << "\n";
      body << "    pyc.assign " << state->getValue().dataEnable << ", "
           << continueAccepted << " : i1\n";
      std::string one = emitConstant(1, state->getValue().iterationType);
      std::string nextIteration =
          emitBinary("add", state->getValue().selectedIteration, one,
                     state->getValue().iterationType);
      body << "    pyc.assign " << state->getValue().iterationNext << ", "
           << nextIteration << " : " << state->getValue().iterationType << "\n";
      body << "    pyc.assign " << state->getValue().iterationEnable << ", "
           << continueAccepted << " : i1\n";
      std::string atLimit = emitNot(state->getValue().underLimit);
      std::string limitCondition =
          emitBinary("and", state->getValue().condition, atLimit, "i1");
      std::string limitViolation = emitBinary(
          "and", state->getValue().selectedValid, limitCondition, "i1");
      std::string limitOk = emitNot(limitViolation);
      body << "    pyc.assert " << limitOk
           << " {msg = \"feedback_iteration_limit\"}\n";
    }
  }
  for (auto [index, sink] : llvm::enumerate(sinks))
    body << "    pyc.assign " << readyWires[sink->inputs.front()] << ", "
         << outputName(index, "ready") << " : i1\n";
  for (const QueueBlockPlan *observation : observations) {
    const QueuePlan *queue = findQueue(plan, observation->inputs.front());
    auto type = queue ? pycType(plan, queue->payloadType)
                      : llvm::Expected<std::string>(
                            pycError("observation Queue is missing"));
    if (!type)
      return type.takeError();
    std::string alias = newValue();
    body << "    " << alias << " = pyc.alias "
         << outputData[observation->inputs.front()] << " {pyc.name = \""
         << observation->name << "\"} : " << *type << "\n";
  }

  llvm::StringRef top = plan.system;
  std::vector<std::string> arguments = {"%clk: !pyc.clock", "%rst: !pyc.reset"};
  std::vector<std::string> argumentNames = {"clk", "rst"};
  for (size_t index = 0; index < sources.size(); ++index) {
    arguments.push_back(inputName(index, "valid") + ": i1");
    arguments.push_back(inputName(index, "data") + ": " +
                        inputPortTypes[index]);
    argumentNames.push_back(inputName(index, "valid").substr(1));
    argumentNames.push_back(inputName(index, "data").substr(1));
  }
  for (size_t index = 0; index < sinks.size(); ++index) {
    arguments.push_back(outputName(index, "ready") + ": i1");
    argumentNames.push_back(outputName(index, "ready").substr(1));
  }
  std::vector<std::string> resultTypes;
  std::vector<std::string> resultNames;
  std::vector<std::string> returnValues;
  for (auto [index, sink] : llvm::enumerate(sinks)) {
    resultTypes.push_back("i1");
    resultTypes.push_back(outputPortTypes[index]);
    resultNames.push_back(outputName(index, "valid").substr(1));
    resultNames.push_back(outputName(index, "data").substr(1));
    returnValues.push_back(outputValid[sink->inputs.front()]);
    returnValues.push_back(outputData[sink->inputs.front()]);
  }
  for (auto [index, source] : llvm::enumerate(sources)) {
    resultTypes.push_back("i1");
    resultNames.push_back(inputName(index, "ready").substr(1));
    returnValues.push_back(inputReady[source->outputs.front()]);
  }
  auto writeList =
      [](std::ostringstream &stream, const std::vector<std::string> &values,
         llvm::StringRef prefix = {}, llvm::StringRef suffix = {}) {
        for (auto [index, value] : llvm::enumerate(values)) {
          if (index)
            stream << ", ";
          stream << prefix.str() << value << suffix.str();
        }
      };
  std::ostringstream output;
  output << "module attributes {pyc.top = @" << top.str()
         << ", pyc.frontend.contract = \"pycircuit\"} {\n  func.func @"
         << top.str() << '(';
  writeList(output, arguments);
  output << ") -> (";
  writeList(output, resultTypes);
  output << ") attributes {arg_names = [";
  writeList(output, argumentNames, "\"", "\"");
  output << "], result_names = [";
  writeList(output, resultNames, "\"", "\"");
  output << "], pyc.value_params = [], pyc.value_param_types = [], "
            "pyc.kind = \"module\", pyc.inline = \"false\", "
            "pyc.params = \"{}\", pyc.base = \""
         << top.str() << "\", pyc.struct.metrics = \"" << kStructMetrics.str()
         << "\", pyc.struct.collections = \"[]\"} {\n"
         << body.str() << "    func.return ";
  writeList(output, returnValues);
  output << " : ";
  writeList(output, resultTypes);
  output << "\n  }\n}\n";
  return output.str();
}

} // namespace acir::codegen
