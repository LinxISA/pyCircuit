#include "acir/CodeGen/QueueGraphGenerator.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <system_error>

namespace acir::codegen {
namespace {

llvm::Error generatorError(const llvm::Twine &message) {
  return llvm::createStringError(
      std::make_error_code(std::errc::invalid_argument),
      "ACLOWER-QUEUE-CXX: " + message);
}

std::string identifier(llvm::StringRef value) {
  std::string result;
  for (char character : value)
    result.push_back(
        std::isalnum(static_cast<unsigned char>(character)) ? character : '_');
  if (result.empty() ||
      std::isdigit(static_cast<unsigned char>(result.front())))
    result.insert(result.begin(), '_');
  return result;
}

std::string className(llvm::StringRef value) {
  std::string result;
  bool capitalize = true;
  for (char character : value) {
    if (!std::isalnum(static_cast<unsigned char>(character))) {
      capitalize = true;
      continue;
    }
    result.push_back(capitalize ? static_cast<char>(std::toupper(
                                      static_cast<unsigned char>(character)))
                                : character);
    capitalize = false;
  }
  if (result.empty() ||
      std::isdigit(static_cast<unsigned char>(result.front())))
    result.insert(result.begin(), '_');
  return result;
}

llvm::Expected<std::string> cppType(llvm::StringRef type) {
  if (type == "i64")
    return std::string("std::int64_t");
  if (type == "i1")
    return std::string("bool");
  constexpr llvm::StringLiteral prefix = "!ac.struct<@types::@";
  if (type.starts_with(prefix) && type.ends_with('>'))
    return type.drop_front(prefix.size()).drop_back().str();
  return generatorError("no C++ realization for ACIR type '" + type + "'");
}

std::vector<std::string> pathParts(llvm::StringRef path) {
  std::vector<std::string> result;
  while (!path.empty()) {
    path = path.ltrim('/');
    if (path.empty())
      break;
    auto split = path.split('/');
    result.push_back(split.first.str());
    path = split.second;
  }
  return result;
}

std::string commonPath(llvm::StringRef left, llvm::StringRef right) {
  std::vector<std::string> lhs = pathParts(left);
  std::vector<std::string> rhs = pathParts(right);
  std::string result;
  for (size_t index = 0; index < std::min(lhs.size(), rhs.size()); ++index) {
    if (lhs[index] != rhs[index])
      break;
    result.push_back('/');
    result.append(lhs[index]);
  }
  return result.empty() ? "/" : result;
}

llvm::Expected<std::string> emitExpressionBody(const QueueBlockPlan &block,
                                               llvm::StringRef yield,
                                               unsigned indent) {
  std::ostringstream output;
  std::string padding(indent, ' ');
  for (const QueueExpressionPlan &expression : block.expressions) {
    auto operand = [&](size_t index) -> llvm::Expected<llvm::StringRef> {
      if (index >= expression.operands.size())
        return generatorError("expression operand arity mismatch");
      return llvm::StringRef(expression.operands[index]);
    };
    if (expression.kind == "constant") {
      llvm::StringRef literal = expression.literal;
      output << padding << "auto " << expression.result << " = "
             << literal.split(" : ").first.str() << ";\n";
      continue;
    }
    auto first = operand(0);
    if (!first)
      return first.takeError();
    if (expression.kind == "get") {
      output << padding << "auto " << expression.result << " = " << first->str()
             << '.' << expression.field << ";\n";
      continue;
    }
    auto second = operand(1);
    if (!second)
      return second.takeError();
    if (expression.kind == "with") {
      output << padding << "auto " << expression.result << " = " << first->str()
             << ";\n";
      output << padding << expression.result << '.' << expression.field << " = "
             << second->str() << ";\n";
      continue;
    }
    llvm::StringRef operation;
    if (expression.kind == "add")
      operation = "+";
    else if (expression.kind == "sub")
      operation = "-";
    else if (expression.kind == "mul")
      operation = "*";
    else if (expression.kind == "cmp") {
      operation = llvm::StringSwitch<llvm::StringRef>(expression.predicate)
                      .Case("eq", "==")
                      .Case("ne", "!=")
                      .Case("slt", "<")
                      .Case("sle", "<=")
                      .Case("sgt", ">")
                      .Case("sge", ">=")
                      .Default("");
    }
    if (operation.empty())
      return generatorError("unsupported Var expression kind '" +
                            expression.kind + "'");
    output << padding << "auto " << expression.result << " = " << first->str()
           << ' ' << operation.str() << ' ' << second->str() << ";\n";
  }
  output << padding << "return " << yield.str() << ";\n";
  return output.str();
}

const QueuePlan *findQueue(const QueueGraphPlan &plan, llvm::StringRef name) {
  auto found =
      std::find_if(plan.queues.begin(), plan.queues.end(),
                   [&](const QueuePlan &queue) { return queue.name == name; });
  return found == plan.queues.end() ? nullptr : &*found;
}

bool isRuntimeBlock(const QueueBlockPlan &block) {
  return block.kind != "source";
}

} // namespace

llvm::Expected<std::string> generateQueueGraphCpp(const QueueGraphPlan &plan) {
  if (plan.system.empty() || plan.queues.empty() || plan.blocks.empty())
    return generatorError("QueueGraph plan is incomplete");

  llvm::StringMap<std::string> queueMembers;
  llvm::StringMap<std::string> queueOwners;
  for (const QueuePlan &queue : plan.queues) {
    if (queueMembers.contains(queue.name))
      return generatorError("Queue names must be unique");
    queueMembers[queue.name] = identifier(queue.name) + "_";
    queueOwners[queue.name] = queue.scope;
  }
  for (const QueueBlockPlan &block : plan.blocks)
    for (const std::string &input : block.inputs) {
      auto owner = queueOwners.find(input);
      if (owner == queueOwners.end())
        return generatorError("block input references unknown Queue '" + input +
                              "'");
      owner->getValue() = commonPath(owner->getValue(), block.scope);
    }

  llvm::StringMap<std::string> scopeMembers;
  for (auto [index, scope] : llvm::enumerate(plan.scopes))
    scopeMembers[scope] = "scope_" + std::to_string(index) + "_";
  auto modulePointer =
      [&](llvm::StringRef path) -> llvm::Expected<std::string> {
    if (path == "/")
      return std::string("this");
    auto found = scopeMembers.find(path);
    if (found == scopeMembers.end())
      return generatorError("unknown scope path '" + path + "'");
    return "&" + found->getValue();
  };
  auto attach = [&](llvm::StringRef path,
                    llvm::StringRef member) -> llvm::Expected<std::string> {
    if (path == "/")
      return "    attachChild(" + member.str() + ");";
    auto found = scopeMembers.find(path);
    if (found == scopeMembers.end())
      return generatorError("unknown attachment scope '" + path + "'");
    return "    " + found->getValue() + ".attachChild(" + member.str() + ");";
  };

  std::vector<const QueueBlockPlan *> runtimeBlocks;
  for (const QueueBlockPlan &block : plan.blocks)
    if (isRuntimeBlock(block))
      runtimeBlocks.push_back(&block);
  llvm::StringMap<uint64_t> queueIds;
  for (auto [index, queue] : llvm::enumerate(plan.queues))
    queueIds[queue.name] = index;
  uint64_t nextId = plan.queues.size();
  llvm::StringMap<uint64_t> blockIds;
  for (auto [index, block] : llvm::enumerate(runtimeBlocks))
    blockIds[block->name + "#" + std::to_string(index)] = nextId++;

  std::ostringstream output;
  output << "// Generated from frozen ACIR QueueGraph plan; do not edit.\n"
            "#include \"gfsim/dispatch.h\"\n"
            "#include \"gfsim/object.h\"\n"
            "#include \"gfsim/queue.h\"\n"
            "#include \"gfsim/queue_blocks.h\"\n\n"
            "#include <array>\n#include <cstdint>\n#include <limits>\n\n"
            "namespace ac_generated {\n\n";
  for (const QueuePayloadPlan &payload : plan.payloads) {
    output << "struct " << payload.name << " {\n";
    for (const QueuePayloadFieldPlan &field : payload.fields) {
      auto type = cppType(field.type);
      if (!type)
        return type.takeError();
      output << "  " << *type << ' ' << field.name << "{};\n";
    }
    output << "};\n\n";
  }

  for (auto [index, block] : llvm::enumerate(runtimeBlocks)) {
    if (block->kind != "transform" && block->kind != "route")
      continue;
    if (block->yields.size() != 1 || block->inputs.size() != 1)
      return generatorError("transform/route policy arity is unsupported");
    const QueuePlan *input = findQueue(plan, block->inputs.front());
    if (!input)
      return generatorError("policy input Queue is missing");
    auto inputType = cppType(input->payloadType);
    if (!inputType)
      return inputType.takeError();
    std::string policy = "block_" + std::to_string(index) + "_policy";
    output << "struct " << policy << " {\n  ";
    if (block->kind == "route")
      output << "size_t";
    else {
      const QueuePlan *result = findQueue(plan, block->outputs.front());
      if (!result)
        return generatorError("transform output Queue is missing");
      auto resultType = cppType(result->payloadType);
      if (!resultType)
        return resultType.takeError();
      output << *resultType;
    }
    output << " operator()(const " << *inputType << " &item) const {\n";
    auto body = emitExpressionBody(*block, block->yields.front(), 4);
    if (!body)
      return body.takeError();
    if (block->kind == "route")
      output << "    return static_cast<size_t>([&]() {\n"
             << *body << "    }());\n";
    else
      output << *body;
    output << "  }\n};\n\n";
  }

  std::string modelClass = className(plan.system);
  output << "class " << modelClass
         << " final : public gfsim::Module {\npublic:\n  " << modelClass
         << "() : gfsim::Module(\"" << plan.system
         << "\", gfsim::kInvalidObjectId, nullptr),\n";
  std::vector<std::string> initializers;
  for (const std::string &scope : plan.scopes) {
    llvm::StringRef parent = llvm::StringRef(scope).rsplit('/').first;
    if (parent.empty())
      parent = "/";
    auto parentPointer = modulePointer(parent);
    if (!parentPointer)
      return parentPointer.takeError();
    initializers.push_back(
        scopeMembers[scope] + "(\"" + pathParts(scope).back() +
        "\", gfsim::kInvalidObjectId, " + *parentPointer + ")");
  }
  for (const QueuePlan &queue : plan.queues) {
    auto type = cppType(queue.payloadType);
    auto parent = modulePointer(queueOwners[queue.name]);
    if (!type)
      return type.takeError();
    if (!parent)
      return parent.takeError();
    initializers.push_back(queueMembers[queue.name] + "(\"" + queue.name +
                           "\", " + std::to_string(queueIds[queue.name]) +
                           ", " + *parent + ", " + std::to_string(queue.depth) +
                           ", std::numeric_limits<size_t>::max(), nullptr, " +
                           std::to_string(queue.latency) + ")");
  }
  size_t sinkIndex = 0;
  for (auto [index, block] : llvm::enumerate(runtimeBlocks)) {
    auto parent = modulePointer(block->scope);
    if (!parent)
      return parent.takeError();
    std::string member = "block_" + std::to_string(index) + "_";
    std::string key = block->name + "#" + std::to_string(index);
    if (block->kind == "transform") {
      initializers.push_back(member + "(\"" + block->name + "\", " +
                             std::to_string(blockIds[key]) + ", " + *parent +
                             ", " + queueMembers[block->inputs[0]] + ", " +
                             queueMembers[block->outputs[0]] + ")");
    } else if (block->kind == "route") {
      const QueuePlan *input = findQueue(plan, block->inputs[0]);
      auto type = input ? cppType(input->payloadType)
                        : llvm::Expected<std::string>(
                              generatorError("route input Queue is missing"));
      if (!type)
        return type.takeError();
      std::string outputs;
      for (auto [outputIndex, name] : llvm::enumerate(block->outputs)) {
        if (outputIndex)
          outputs.append(", ");
        outputs.append("&").append(queueMembers[name]);
      }
      initializers.push_back(member + "(\"" + block->name + "\", " +
                             std::to_string(blockIds[key]) + ", " + *parent +
                             ", " + queueMembers[block->inputs[0]] +
                             ", std::array<gfsim::SimQueue<" + *type + "> *, " +
                             std::to_string(block->outputs.size()) + ">{" +
                             outputs + "})");
    } else if (block->kind == "merge") {
      const QueuePlan *result = findQueue(plan, block->outputs[0]);
      auto type = result ? cppType(result->payloadType)
                         : llvm::Expected<std::string>(
                               generatorError("merge output Queue is missing"));
      if (!type)
        return type.takeError();
      std::string inputs;
      for (auto [inputIndex, name] : llvm::enumerate(block->inputs)) {
        if (inputIndex)
          inputs.append(", ");
        inputs.append("&").append(queueMembers[name]);
      }
      std::string policy = block->policy == "priority"
                               ? "gfsim::QueueMergePolicy::Priority"
                               : "gfsim::QueueMergePolicy::RoundRobin";
      initializers.push_back(member + "(\"" + block->name + "\", " +
                             std::to_string(blockIds[key]) + ", " + *parent +
                             ", std::array<gfsim::SimQueue<" + *type + "> *, " +
                             std::to_string(block->inputs.size()) + ">{" +
                             inputs + "}, " + queueMembers[block->outputs[0]] +
                             ", " + policy + ")");
    } else if (block->kind == "sink") {
      initializers.push_back(member + "(\"" + block->name + "\", " +
                             std::to_string(blockIds[key]) + ", " + *parent +
                             ", " + queueMembers[block->inputs[0]] + ")");
      ++sinkIndex;
    } else {
      return generatorError("unsupported native Queue block '" + block->kind +
                            "'");
    }
  }
  for (auto [index, initializer] : llvm::enumerate(initializers))
    output << "        " << initializer
           << (index + 1 == initializers.size() ? "\n" : ",\n");
  output << "  {\n    setPath(\"/" << plan.system << "\");\n";
  for (const std::string &scope : plan.scopes) {
    llvm::StringRef parent = llvm::StringRef(scope).rsplit('/').first;
    if (parent.empty())
      parent = "/";
    auto line = attach(parent, scopeMembers[scope]);
    if (!line)
      return line.takeError();
    output << *line << '\n';
  }
  for (const QueuePlan &queue : plan.queues) {
    auto line = attach(queueOwners[queue.name], queueMembers[queue.name]);
    if (!line)
      return line.takeError();
    output << *line << '\n';
  }
  for (auto [index, block] : llvm::enumerate(runtimeBlocks)) {
    auto line = attach(block->scope, "block_" + std::to_string(index) + "_");
    if (!line)
      return line.takeError();
    output << *line << '\n';
  }
  output << "  }\n\n";
  for (const QueueBlockPlan &block : plan.blocks)
    if (block.kind == "source") {
      const QueuePlan *queue = findQueue(plan, block.outputs.front());
      auto type = queue ? cppType(queue->payloadType)
                        : llvm::Expected<std::string>(
                              generatorError("source Queue is missing"));
      if (!type)
        return type.takeError();
      output << "  gfsim::SimQueue<" << *type << "> &" << block.outputs.front()
             << "() { return " << queueMembers[block.outputs.front()]
             << "; }\n";
    }
  sinkIndex = 0;
  for (auto [index, block] : llvm::enumerate(runtimeBlocks))
    if (block->kind == "sink") {
      const QueuePlan *queue = findQueue(plan, block->inputs.front());
      auto type = queue ? cppType(queue->payloadType)
                        : llvm::Expected<std::string>(
                              generatorError("sink Queue is missing"));
      if (!type)
        return type.takeError();
      output << "  const std::vector<" << *type << "> &sink_" << sinkIndex
             << "_values() const { return block_" << index
             << "_.received(); }\n";
      ++sinkIndex;
    }
  output << "\n  std::array<gfsim::DispatchRow, " << nextId
         << "> dispatch_rows() {\n    return {\n";
  size_t row = 0;
  for (const QueuePlan &queue : plan.queues)
    output << "        gfsim::makeDispatchRow(&" << queueMembers[queue.name]
           << "),\n";
  for (size_t index = 0; index < runtimeBlocks.size(); ++index) {
    ++row;
    output << "        gfsim::makeDispatchRow(&block_" << index << "_)"
           << (index + 1 == runtimeBlocks.size() ? "\n" : ",\n");
  }
  output << "    };\n  }\n\nprivate:\n";
  for (const std::string &scope : plan.scopes)
    output << "  gfsim::Module " << scopeMembers[scope] << ";\n";
  for (const QueuePlan &queue : plan.queues) {
    auto type = cppType(queue.payloadType);
    if (!type)
      return type.takeError();
    output << "  gfsim::SimQueue<" << *type << "> " << queueMembers[queue.name]
           << ";\n";
  }
  sinkIndex = 0;
  for (auto [index, block] : llvm::enumerate(runtimeBlocks)) {
    if (block->kind == "transform") {
      const QueuePlan *input = findQueue(plan, block->inputs[0]);
      const QueuePlan *result = findQueue(plan, block->outputs[0]);
      auto inputType = input ? cppType(input->payloadType)
                             : llvm::Expected<std::string>(
                                   generatorError("transform input missing"));
      auto resultType = result ? cppType(result->payloadType)
                               : llvm::Expected<std::string>(generatorError(
                                     "transform output missing"));
      if (!inputType)
        return inputType.takeError();
      if (!resultType)
        return resultType.takeError();
      output << "  gfsim::QueueTransform<" << *inputType << ", " << *resultType
             << ", block_" << index << "_policy> block_" << index << "_;\n";
    } else if (block->kind == "route") {
      const QueuePlan *input = findQueue(plan, block->inputs[0]);
      auto type = input ? cppType(input->payloadType)
                        : llvm::Expected<std::string>(
                              generatorError("route input missing"));
      if (!type)
        return type.takeError();
      output << "  gfsim::QueueRoute<" << *type << ", " << block->outputs.size()
             << ", block_" << index << "_policy> block_" << index << "_;\n";
    } else if (block->kind == "merge") {
      const QueuePlan *result = findQueue(plan, block->outputs[0]);
      auto type = result ? cppType(result->payloadType)
                         : llvm::Expected<std::string>(
                               generatorError("merge output missing"));
      if (!type)
        return type.takeError();
      output << "  gfsim::QueueMerge<" << *type << ", " << block->inputs.size()
             << "> block_" << index << "_;\n";
    } else if (block->kind == "sink") {
      const QueuePlan *input = findQueue(plan, block->inputs[0]);
      auto type = input ? cppType(input->payloadType)
                        : llvm::Expected<std::string>(
                              generatorError("sink input missing"));
      if (!type)
        return type.takeError();
      output << "  gfsim::QueueSink<" << *type << "> block_" << index << "_;\n";
      ++sinkIndex;
    }
  }
  output << "};\n\n} // namespace ac_generated\n";
  return output.str();
}

} // namespace acir::codegen
