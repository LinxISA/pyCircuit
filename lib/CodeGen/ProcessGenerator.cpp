#include "ProcessGenerator.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <system_error>
#include <type_traits>
#include <utility>

namespace acir::codegen::detail {
namespace {

template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

llvm::Error processError(const llvm::Twine &message) {
  return llvm::createStringError(
      std::make_error_code(std::errc::invalid_argument),
      llvm::Twine("ACLOWER-PROCESS-STATE: ") + message);
}

bool isIdentifier(llvm::StringRef value) {
  if (value.empty() ||
      !(std::isalpha(static_cast<unsigned char>(value.front())) ||
        value.front() == '_'))
    return false;
  return std::all_of(value.drop_front().begin(), value.end(), [](char value) {
    return std::isalnum(static_cast<unsigned char>(value)) || value == '_';
  });
}

const BindingPlan *findBinding(const ModelPlan &plan, llvm::StringRef symbol) {
  auto found = std::find_if(plan.bindings.begin(), plan.bindings.end(),
                            [&](const BindingPlan &binding) {
                              return binding.symbol == symbol ||
                                     binding.implementation == symbol;
                            });
  return found == plan.bindings.end() ? nullptr : &*found;
}

const TypePlan *findType(const ModelPlan &plan, llvm::StringRef symbol) {
  auto found =
      std::find_if(plan.types.begin(), plan.types.end(),
                   [&](const TypePlan &type) { return type.symbol == symbol; });
  return found == plan.types.end() ? nullptr : &*found;
}

llvm::Expected<std::string> cppType(const ModelPlan &plan,
                                    llvm::StringRef type) {
  if (type == "i1")
    return std::string("bool");
  if (type == "i8")
    return std::string("std::int8_t");
  if (type == "i16")
    return std::string("std::int16_t");
  if (type == "i32")
    return std::string("std::int32_t");
  if (type == "i64")
    return std::string("std::int64_t");
  if (type == "index")
    return std::string("std::size_t");
  if (type == "f32")
    return std::string("float");
  if (type == "f64")
    return std::string("double");

  const size_t symbolStart = type.find('@');
  const size_t symbolEnd = type.find('>', symbolStart);
  if (symbolStart == llvm::StringRef::npos ||
      symbolEnd == llvm::StringRef::npos)
    return processError("process value has no C++ type realization");
  const llvm::StringRef symbol = type.slice(symbolStart + 1, symbolEnd);
  if (type.starts_with("!acsim.ref<")) {
    if (const BindingPlan *binding = findBinding(plan, symbol))
      return binding->cppSymbol;
  } else if (type.starts_with("!acsim.wake<")) {
    return std::string("gfsim::ProcessWake");
  } else if (const TypePlan *realization = findType(plan, symbol)) {
    return realization->cppType;
  }
  return processError("process value references an unknown type");
}

const LiveSlotPlan *findSlot(const ProcessPlan &process, llvm::StringRef name) {
  auto found =
      std::find_if(process.liveSlots.begin(), process.liveSlots.end(),
                   [&](const LiveSlotPlan &slot) { return slot.name == name; });
  return found == process.liveSlots.end() ? nullptr : &*found;
}

const PcStatePlan *findState(const ProcessPlan &process, llvm::StringRef name) {
  auto found = std::find_if(
      process.states.begin(), process.states.end(),
      [&](const PcStatePlan &state) { return state.name == name; });
  return found == process.states.end() ? nullptr : &*found;
}

llvm::Expected<std::string> pcType(const ProcessPlan &process) {
  const uint32_t maximum =
      process.states.empty() ? 0 : process.states.back().ordinal;
  if (maximum <= std::numeric_limits<uint8_t>::max())
    return std::string("uint8_t");
  if (maximum <= std::numeric_limits<uint16_t>::max())
    return std::string("uint16_t");
  return std::string("uint32_t");
}

GeneratedFile makeFile(std::string path, std::string content) {
  GeneratedFile file{std::move(path), std::move(content), {}};
  file.fingerprint = computeFingerprint(file.content);
  return file;
}

llvm::Error validateProcess(const ModelPlan &plan, const ProcessPlan &process) {
  if (!isIdentifier(process.className) || !isIdentifier(process.symbol) ||
      process.states.empty() || process.fairnessWork == 0)
    return processError("process identity or fairness is invalid");

  std::set<std::string> pcNames;
  for (auto [ordinal, state] : llvm::enumerate(process.states)) {
    if (state.ordinal != ordinal || !isIdentifier(state.name) ||
        !pcNames.insert(state.name).second)
      return processError("process PCs are not dense identifiers");
  }
  if (!pcNames.contains(process.entryPc))
    return processError("entry PC is outside the closed PC set");

  std::set<std::string> slots;
  for (const LiveSlotPlan &slot : process.liveSlots) {
    if (!isIdentifier(slot.name) || !slots.insert(slot.name).second)
      return processError("live slots are not unique identifiers");
    if (!cppType(plan, slot.type))
      return processError("live slot has no exact C++ type");
  }

  for (const PcStatePlan &state : process.states) {
    std::map<std::string, std::string> values;
    for (auto [index, capture] : llvm::enumerate(process.captures))
      values.emplace("arg" + std::to_string(index), capture.type);

    auto requireValue = [&](llvm::StringRef value) -> llvm::Error {
      if (!values.contains(value.str()))
        return processError("operation uses a value outside its PC");
      return llvm::Error::success();
    };
    auto addResults =
        [&](const std::vector<std::string> &results,
            const std::vector<std::string> &types) -> llvm::Error {
      if (results.size() != types.size())
        return processError("operation result arity is inconsistent");
      for (auto [result, type] : llvm::zip_equal(results, types)) {
        if (!isIdentifier(result) || values.contains(result))
          return processError("operation result is not a fresh identifier");
        if (!cppType(plan, type))
          return processError("operation result has no exact C++ type");
        values.emplace(result, type);
      }
      return llvm::Error::success();
    };

    for (const ProcessOperationPlan &operation : state.operations) {
      llvm::Error error = std::visit(
          Overloaded{
              [&](const LiveLoadPlan &load) -> llvm::Error {
                const LiveSlotPlan *slot = findSlot(process, load.slot);
                if (!slot || slot->type != load.type)
                  return processError("live load slot or type is invalid");
                return addResults({load.resultValue}, {load.type});
              },
              [&](const LiveStorePlan &store) -> llvm::Error {
                if (auto error = requireValue(store.sourceValue))
                  return error;
                const LiveSlotPlan *slot = findSlot(process, store.slot);
                if (!slot || values.at(store.sourceValue) != slot->type)
                  return processError("live store slot or type is invalid");
                return llvm::Error::success();
              },
              [&](const InlineCallPlan &call) -> llvm::Error {
                const BindingPlan *binding = findBinding(plan, call.callee);
                if (!binding || binding->effect != BindingEffect::Pure)
                  return processError("inline callee is not a pure binding");
                for (const std::string &argument : call.arguments)
                  if (auto error = requireValue(argument))
                    return error;
                return addResults(call.results, call.resultTypes);
              },
              [&](const InvokePlan &call) -> llvm::Error {
                const BindingPlan *binding = findBinding(plan, call.callee);
                if (!binding || binding->effect != BindingEffect::Stateful)
                  return processError(
                      "invoke callee is not a stateful binding");
                for (const std::string &argument : call.arguments)
                  if (auto error = requireValue(argument))
                    return error;
                return addResults(call.results, call.resultTypes);
              },
              [&](const GenericOperationPlan &operation) -> llvm::Error {
                for (const std::string &argument : operation.arguments)
                  if (auto error = requireValue(argument))
                    return error;
                if (!(llvm::StringRef(operation.operationName)
                          .starts_with("arith.") ||
                      llvm::StringRef(operation.operationName)
                          .starts_with("index.") ||
                      llvm::StringRef(operation.operationName)
                          .starts_with("cf.") ||
                      llvm::StringRef(operation.operationName)
                          .starts_with("builtin.")))
                  return processError(
                      "generic operation is outside the closed subset");
                return addResults(operation.results, operation.resultTypes);
              }},
          operation);
      if (error)
        return error;
    }

    llvm::Error terminatorError = std::visit(
        Overloaded{
            [&](const ContinuePlan &next) -> llvm::Error {
              return findState(process, next.targetPc)
                         ? llvm::Error::success()
                         : processError(
                               "continue target is outside the PC set");
            },
            [&](const SuspendPlan &suspend) -> llvm::Error {
              auto found = values.find(suspend.wakeValue);
              if (found == values.end() ||
                  !llvm::StringRef(found->second).starts_with("!acsim.wake<") ||
                  !findState(process, suspend.targetPc))
                return processError("suspend wake or target is invalid");
              return llvm::Error::success();
            },
            [&](const TerminatePlan &terminate) -> llvm::Error {
              if (terminate.status != "success" &&
                  terminate.status != "failure")
                return processError("terminate status is invalid");
              return llvm::Error::success();
            }},
        state.terminator);
    if (terminatorError)
      return terminatorError;
  }
  return llvm::Error::success();
}

void emitArguments(std::ostringstream &output,
                   const std::vector<std::string> &arguments) {
  for (auto [index, argument] : llvm::enumerate(arguments)) {
    if (index != 0)
      output << ", ";
    output << argument;
  }
}

void emitResultAssignment(std::ostringstream &output,
                          const std::vector<std::string> &results) {
  if (results.empty())
    return;
  if (results.size() == 1) {
    output << "auto " << results.front() << " = ";
    return;
  }
  output << "auto [";
  emitArguments(output, results);
  output << "] = ";
}

llvm::Error emitOperation(const ModelPlan &plan, const ProcessPlan &process,
                          std::ostringstream &output,
                          const ProcessOperationPlan &operation) {
  return std::visit(
      Overloaded{[&](const LiveLoadPlan &load) -> llvm::Error {
                   auto type = cppType(plan, load.type);
                   if (!type)
                     return type.takeError();
                   output << "    const " << *type << " &" << load.resultValue
                          << " = committed_" << load.slot << "_;\n";
                   return llvm::Error::success();
                 },
                 [&](const LiveStorePlan &store) -> llvm::Error {
                   output << "    proposed_" << store.slot
                          << "_ = " << store.sourceValue << ";\n";
                   return llvm::Error::success();
                 },
                 [&](const InlineCallPlan &call) -> llvm::Error {
                   const BindingPlan *binding = findBinding(plan, call.callee);
                   emitResultAssignment(output, call.results);
                   output << binding->entryPoints.pure << '(';
                   emitArguments(output, call.arguments);
                   output << ");\n";
                   return llvm::Error::success();
                 },
                 [&](const InvokePlan &call) -> llvm::Error {
                   emitResultAssignment(output, call.results);
                   if (!process.captures.empty())
                     output << "arg0.invoke(";
                   else
                     output << "invoke_" << call.callee << '(';
                   emitArguments(output, call.arguments);
                   output << ");\n";
                   return llvm::Error::success();
                 },
                 [&](const GenericOperationPlan &generic) -> llvm::Error {
                   emitResultAssignment(output, generic.results);
                   std::string function = generic.operationName;
                   std::replace(function.begin(), function.end(), '.', '_');
                   output << "acsim_generated::" << function << '(';
                   emitArguments(output, generic.arguments);
                   output << ");\n";
                   return llvm::Error::success();
                 }},
      operation);
}

} // namespace

llvm::Expected<GeneratedFile>
generateProcessHeader(const ModelPlan &plan, const ProcessPlan &process) {
  if (auto error = validateProcess(plan, process))
    return std::move(error);
  auto underlyingType = pcType(process);
  if (!underlyingType)
    return underlyingType.takeError();

  std::set<std::string> headers;
  for (const CapturePlan &capture : process.captures) {
    const size_t start = capture.type.find('@');
    const size_t end = capture.type.find('>', start);
    if (start != std::string::npos && end != std::string::npos)
      if (const BindingPlan *binding = findBinding(
              plan, llvm::StringRef(capture.type).slice(start + 1, end)))
        headers.insert(binding->header);
  }

  std::ostringstream output;
  output << "#pragma once\n\n#include \"gfsim/process.h\"\n";
  for (const std::string &header : headers)
    output << "#include \"" << header << "\"\n";
  output << "\n#include <cstdint>\n#include <string>\n\n"
         << "namespace acsim_generated {\n\nclass " << process.className
         << " final : public gfsim::ProcessRuntime<" << process.className
         << "> {\npublic:\n  enum class Pc : " << *underlyingType << " {\n";
  for (auto [index, state] : llvm::enumerate(process.states))
    output << "    " << state.name << " = " << state.ordinal
           << (index + 1 == process.states.size() ? "\n" : ",\n");
  output << "  };\n\n  static constexpr uint64_t kFairnessWork = "
         << process.fairnessWork << ";\n\n  " << process.className
         << "(std::string name, gfsim::ObjectId id, gfsim::SimObject *parent";
  for (const CapturePlan &capture : process.captures) {
    auto type = cppType(plan, capture.type);
    if (!type)
      return type.takeError();
    output << ", " << *type << " &" << capture.name;
  }
  output
      << ");\n\n  gfsim::ProcessStep executeProcessStep(uint32_t pc, "
         "gfsim::Epoch epoch);\n  void doXfer(gfsim::Epoch epoch) override;\n"
         "  void reset() override;\n\nprivate:\n";
  for (const CapturePlan &capture : process.captures) {
    auto type = cppType(plan, capture.type);
    if (!type)
      return type.takeError();
    output << "  " << *type << " *capture_" << capture.name << "_;\n";
  }
  for (const LiveSlotPlan &slot : process.liveSlots) {
    auto type = cppType(plan, slot.type);
    if (!type)
      return type.takeError();
    output << "  " << *type << " committed_" << slot.name << "_{};\n"
           << "  " << *type << " proposed_" << slot.name << "_{};\n";
  }
  output << "};\n\n} // namespace acsim_generated\n";
  return makeFile("include/generated/processes/" + process.className + ".h",
                  output.str());
}

llvm::Expected<GeneratedFile>
generateProcessSource(const ModelPlan &plan, const ProcessPlan &process) {
  if (auto error = validateProcess(plan, process))
    return std::move(error);
  const PcStatePlan *entry = findState(process, process.entryPc);

  std::ostringstream output;
  output << "#include \"generated/processes/" << process.className
         << ".h\"\n\nnamespace acsim_generated {\n\n"
         << process.className << "::" << process.className
         << "(std::string name, gfsim::ObjectId id, gfsim::SimObject *parent";
  for (const CapturePlan &capture : process.captures) {
    auto type = cppType(plan, capture.type);
    if (!type)
      return type.takeError();
    output << ", " << *type << " &" << capture.name;
  }
  output << ")\n    : ProcessRuntime(std::move(name), id, parent, "
         << "static_cast<uint32_t>(Pc::" << entry->name << "), kFairnessWork)";
  for (const CapturePlan &capture : process.captures)
    output << ",\n      capture_" << capture.name << "_(&" << capture.name
           << ')';
  output << " {}\n\ngfsim::ProcessStep " << process.className
         << "::executeProcessStep(uint32_t pc, gfsim::Epoch epoch) {\n"
            "  (void)epoch;\n  switch (static_cast<Pc>(pc)) {\n";
  for (const PcStatePlan &state : process.states) {
    output << "  case Pc::" << state.name << ": {\n";
    for (auto [index, capture] : llvm::enumerate(process.captures))
      output << "    auto &arg" << index << " = *capture_" << capture.name
             << "_;\n";
    for (const ProcessOperationPlan &operation : state.operations)
      if (auto error = emitOperation(plan, process, output, operation))
        return std::move(error);
    std::visit(
        Overloaded{
            [&](const ContinuePlan &next) {
              output << "    return gfsim::ProcessStep::continueAt("
                     << "static_cast<uint32_t>(Pc::" << next.targetPc
                     << "));\n";
            },
            [&](const SuspendPlan &suspend) {
              const PcStatePlan *target = findState(process, suspend.targetPc);
              output << "    return gfsim::ProcessStep::suspendAt("
                     << "static_cast<uint32_t>(Pc::" << suspend.targetPc
                     << "), " << suspend.wakeValue << ", "
                     << static_cast<uint64_t>(target->ordinal) + 1 << ");\n";
            },
            [&](const TerminatePlan &terminate) {
              if (terminate.status == "success")
                output << "    return gfsim::ProcessStep::terminate();\n";
              else
                output << "    return gfsim::ProcessStep::fail("
                          "\"process_terminated_failure\");\n";
            }},
        state.terminator);
    output << "  }\n";
  }
  output
      << "  default:\n    return gfsim::ProcessStep::fail("
         "\"invalid_process_pc\");\n  }\n}\n\nvoid "
      << process.className
      << "::doXfer(gfsim::Epoch epoch) {\n  ProcessRuntime::doXfer(epoch);\n";
  for (const LiveSlotPlan &slot : process.liveSlots)
    output << "  committed_" << slot.name << "_ = proposed_" << slot.name
           << "_;\n";
  output << "}\n\nvoid " << process.className
         << "::reset() {\n  ProcessRuntime::reset();\n";
  for (const LiveSlotPlan &slot : process.liveSlots)
    output << "  committed_" << slot.name << "_ = {};\n  proposed_" << slot.name
           << "_ = {};\n";
  output << "}\n\n} // namespace acsim_generated\n";
  return makeFile("src/generated/processes/" + process.className + ".cpp",
                  output.str());
}

} // namespace acir::codegen::detail
