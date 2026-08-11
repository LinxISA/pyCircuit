#include "gfsim/npu.h"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>

namespace gfsim {
namespace {

// Frozen from davincioo@e73633301cabed0d871ea5ff66e76a91df870aeb
// model/include/davincioo/model/pto_inst.hpp.
constexpr auto kTmaOpcodes = std::to_array<std::string_view>({
    "TLOAD",
    "TSTORE",
    "TSTORE_FP",
    "MGATHER",
    "MSCATTER",
    "TPUT",
    "TGET",
    "TPUT_ASYNC",
    "TGET_ASYNC",
    "TBROADCAST",
    "TREDUCE",
    "TPREFETCH",
    "TIMG2COL",
});

constexpr auto kScalarOpcodes = std::to_array<std::string_view>({
    "RECORD_EVENT",    "WAIT_EVENT",
    "BARRIER",         "TSYNC",
    "TALLOC",          "TFREE",
    "TPUSH",           "TPOP",
    "TNOTIFY",         "TWAIT",
    "TTEST",           "TRESHAPE",
    "TASSIGN",         "TCI",
    "TPRINT",          "SETFMATRIX",
    "SET_IMG2COL_RPT", "SET_IMG2COL_PADDING",
    "TGET_SCALE_ADDR",
});

constexpr auto kCubeOpcodes = std::to_array<std::string_view>({
    "TMATMUL",
    "TMATMUL_MX",
    "TMATMUL_ACC",
    "TMATMUL_BIAS",
    "TMATMUL_MX_ACC",
    "TMATMUL_MX_BIAS",
    "TGEMV",
    "TGEMV_ACC",
    "TGEMV_BIAS",
    "TGEMV_MX",
});

constexpr auto kVectorOpcodes = std::to_array<std::string_view>({
    "TADD",
    "TSUB",
    "TMUL",
    "TDIV",
    "TREM",
    "TAND",
    "TOR",
    "TXOR",
    "TSHL",
    "TSHR",
    "TMAX",
    "TMIN",
    "TPRELU",
    "TCMP",
    "TABS",
    "TEXP",
    "TLOG",
    "TSQRT",
    "TRSQRT",
    "TRECIP",
    "TNEG",
    "TNOT",
    "TRELU",
    "TCVT",
    "TADDC",
    "TSUBC",
    "TSEL",
    "TADDS",
    "TSUBS",
    "TMULS",
    "TDIVS",
    "TREMS",
    "TANDS",
    "TORS",
    "TXORS",
    "TSHLS",
    "TSHRS",
    "TMAXS",
    "TMINS",
    "TLRELU",
    "TAXPY",
    "TCMPS",
    "TADDSC",
    "TSUBSC",
    "TSELS",
    "TEXPANDS",
    "TROWSUM",
    "TROWPROD",
    "TROWMAX",
    "TROWMIN",
    "TROWARGMAX",
    "TROWARGMIN",
    "TROWEXPAND",
    "TCOLSUM",
    "TCOLPROD",
    "TCOLMAX",
    "TCOLMIN",
    "TCOLARGMAX",
    "TCOLARGMIN",
    "TCOLEXPAND",
    "TROWEXPANDDIV",
    "TROWEXPANDMUL",
    "TROWEXPANDSUB",
    "TROWEXPANDADD",
    "TROWEXPANDMAX",
    "TROWEXPANDMIN",
    "TROWEXPANDEXPDIF",
    "TCOLEXPANDDIV",
    "TCOLEXPANDMUL",
    "TCOLEXPANDSUB",
    "TCOLEXPANDADD",
    "TCOLEXPANDMAX",
    "TCOLEXPANDMIN",
    "TCOLEXPANDEXPDIF",
    "TFILLPAD",
    "TFILLPAD_INPLACE",
    "TFILLPAD_EXPAND",
    "TMOV",
    "TMOV_FP",
    "TTRANS",
    "TEXTRACT",
    "TEXTRACT_FP",
    "TTRI",
    "TGATHER",
    "TGATHERB",
    "TSCATTER",
    "TSORT32",
    "TMRGSORT",
    "TPARTADD",
    "TPARTMUL",
    "TPARTMAX",
    "TPARTMIN",
    "TPARTARGMAX",
    "TPARTARGMIN",
    "TRANDOM",
    "TCONCAT",
    "TDEQUANT",
    "TINSERT",
    "TINSERT_FP",
    "TFMOD",
    "TFMODS",
    "THISTOGRAM",
    "TQUANT",
    "TSUBVIEW",
});

static_assert(kTmaOpcodes.size() + kScalarOpcodes.size() + kCubeOpcodes.size() +
                  kVectorOpcodes.size() ==
              146);

template <size_t Size>
constexpr bool contains(const std::array<std::string_view, Size> &values,
                        std::string_view value) {
  for (std::string_view candidate : values)
    if (candidate == value)
      return true;
  return false;
}

std::optional<NpuEngineClass> classify(std::string_view opcode) {
  if (contains(kScalarOpcodes, opcode))
    return NpuEngineClass::Scalar;
  if (contains(kVectorOpcodes, opcode))
    return NpuEngineClass::Vector;
  if (contains(kCubeOpcodes, opcode))
    return NpuEngineClass::Cube;
  if (contains(kTmaOpcodes, opcode))
    return NpuEngineClass::Tma;
  return std::nullopt;
}

NpuDecodeResult reject(std::string code, std::string message) {
  NpuDecodeResult result;
  result.diagnostics.push_back(
      {.code = std::move(code), .message = std::move(message)});
  return result;
}

template <typename T> const T *get(const PtoValue &value) {
  return std::get_if<T>(&value.value);
}

const PtoValue *find(const PtoValue::Object &object, std::string_view key) {
  auto iterator = object.find(std::string(key));
  return iterator == object.end() ? nullptr : &iterator->second;
}

bool hasExactKeys(const PtoValue::Object &object,
                  std::initializer_list<std::string_view> keys) {
  if (object.size() != keys.size())
    return false;
  for (std::string_view key : keys)
    if (!object.contains(std::string(key)))
      return false;
  return true;
}

bool isScalarValue(const PtoValue &value) {
  return !std::holds_alternative<std::monostate>(value.value) &&
         !std::holds_alternative<PtoValue::Array>(value.value) &&
         !std::holds_alternative<PtoValue::Object>(value.value);
}

struct TileAttribute {
  std::string_view address;
};

std::optional<TileAttribute> parseTile(const PtoValue &value) {
  const auto *object = get<PtoValue::Object>(value);
  if (!object ||
      !hasExactKeys(*object, {"address", "dtype", "layout", "shape"}))
    return std::nullopt;
  const auto *address = find(*object, "address");
  const auto *dtype = find(*object, "dtype");
  const auto *layout = find(*object, "layout");
  const auto *shape = find(*object, "shape");
  if (!address || !dtype || !layout || !shape)
    return std::nullopt;
  const auto *addressText = get<std::string>(*address);
  const auto *dtypeText = get<std::string>(*dtype);
  const auto *layoutText = get<std::string>(*layout);
  const auto *dimensions = get<PtoValue::Array>(*shape);
  if (!addressText || addressText->empty() || !dtypeText ||
      dtypeText->empty() || !layoutText || layoutText->empty() || !dimensions)
    return std::nullopt;
  for (const PtoValue &dimension : *dimensions)
    if (!get<int64_t>(dimension) && !get<uint64_t>(dimension))
      return std::nullopt;
  return TileAttribute{.address = *addressText};
}

struct ScalarAttribute {
  std::string_view type;
  const PtoValue *value = nullptr;
};

std::optional<ScalarAttribute> parseScalar(const PtoValue &value) {
  const auto *object = get<PtoValue::Object>(value);
  if (!object || !hasExactKeys(*object, {"dtype", "value"}))
    return std::nullopt;
  const PtoValue *dtype = find(*object, "dtype");
  const PtoValue *scalarValue = find(*object, "value");
  if (!dtype || !scalarValue)
    return std::nullopt;
  const auto *type = get<std::string>(*dtype);
  if (!type || type->empty() || !isScalarValue(*scalarValue))
    return std::nullopt;
  return ScalarAttribute{.type = *type, .value = scalarValue};
}

std::string tileIdentity(uint64_t blockId, std::string_view address) {
  return "block/" + std::to_string(blockId) + "/tile/" + std::string(address);
}

EventProposal observation(std::string name, const NpuInstruction &instruction) {
  return {.ownerId = kInvalidObjectId,
          .category = "instruction",
          .name = std::move(name),
          .phase = TraceEventPhase::Instant,
          .rootSequenceId = instruction.sequenceId,
          .arguments = {{.name = "block_id", .value = instruction.blockId},
                        {.name = "engine",
                         .value = std::string(toString(instruction.engine))},
                        {.name = "opcode", .value = instruction.opcode}}};
}

uint64_t dependencyFlowId(uint64_t blockId, uint64_t producerSequenceId,
                          uint64_t consumerSequenceId,
                          std::string_view tileIdentity) {
  constexpr uint64_t kOffset = 14695981039346656037ULL;
  constexpr uint64_t kPrime = 1099511628211ULL;
  uint64_t hash = kOffset;
  auto appendByte = [&](uint8_t byte) {
    hash ^= byte;
    hash *= kPrime;
  };
  auto appendInteger = [&](uint64_t value) {
    for (unsigned shift = 0; shift != 64; shift += 8)
      appendByte(static_cast<uint8_t>(value >> shift));
  };
  appendInteger(blockId);
  appendInteger(producerSequenceId);
  appendInteger(consumerSequenceId);
  for (char character : tileIdentity)
    appendByte(static_cast<uint8_t>(character));
  return hash;
}

bool issueEntryLess(const NpuIssueEntry &left, const NpuIssueEntry &right) {
  return std::tie(left.instruction.sequenceId, left.stableObjectId) <
         std::tie(right.instruction.sequenceId, right.stableObjectId);
}

} // namespace

std::string_view toString(NpuEngineClass engine) {
  switch (engine) {
  case NpuEngineClass::Scalar:
    return "scalar";
  case NpuEngineClass::Vector:
    return "vector";
  case NpuEngineClass::Cube:
    return "cube";
  case NpuEngineClass::Tma:
    return "tma";
  }
  return "unknown";
}

NpuDecodeResult NpuDecoder::decode(const PtoTraceRecord &record) const {
  std::optional<NpuEngineClass> engine = classify(record.opcode);
  if (!engine)
    return reject("npu_unsupported_opcode",
                  "opcode is not in the pinned DavinciOO opcode table");

  auto attributesIterator = record.attributes.find("davincioo");
  if (attributesIterator == record.attributes.end())
    return reject("npu_missing_davincioo_attributes",
                  "record does not contain attributes.davincioo");
  const auto *attributes = get<PtoValue::Object>(attributesIterator->second);
  if (!attributes ||
      !hasExactKeys(*attributes, {"block_idx", "input_tiles", "operand_roles",
                                  "output_tiles", "scalar_inputs"}))
    return reject("npu_invalid_davincioo_attributes",
                  "attributes.davincioo is not the closed imported shape");

  const PtoValue *blockValue = find(*attributes, "block_idx");
  const PtoValue *inputValue = find(*attributes, "input_tiles");
  const PtoValue *roleValue = find(*attributes, "operand_roles");
  const PtoValue *outputValue = find(*attributes, "output_tiles");
  const PtoValue *scalarValue = find(*attributes, "scalar_inputs");
  if (!blockValue || !inputValue || !roleValue || !outputValue || !scalarValue)
    return reject("npu_invalid_davincioo_attributes",
                  "attributes.davincioo is incomplete");

  const auto *blockId = get<uint64_t>(*blockValue);
  const auto *inputs = get<PtoValue::Array>(*inputValue);
  const auto *roles = get<PtoValue::Array>(*roleValue);
  const auto *outputs = get<PtoValue::Array>(*outputValue);
  const auto *scalars = get<PtoValue::Array>(*scalarValue);
  if (!blockId || !inputs || !roles || !outputs || !scalars)
    return reject("npu_invalid_davincioo_attributes",
                  "attributes.davincioo contains a value of the wrong type");
  if (roles->size() != record.operands.size())
    return reject("npu_operand_role_mismatch",
                  "operand_roles must have one entry per ordered operand");

  NpuInstruction instruction;
  instruction.sequenceId = record.sequenceId;
  instruction.blockId = *blockId;
  instruction.opcode = record.opcode;
  instruction.dependencies = record.dependencies;
  instruction.operands = record.operands;
  instruction.engine = *engine;

  size_t inputIndex = 0;
  size_t outputIndex = 0;
  size_t scalarIndex = 0;
  for (size_t operandIndex = 0; operandIndex < roles->size(); ++operandIndex) {
    const auto *role = get<std::string>((*roles)[operandIndex]);
    if (!role)
      return reject("npu_operand_role_mismatch",
                    "every operand role must be a string");
    const PtoTraceOperand &operand = record.operands[operandIndex];
    if (*role == "input_tile" || *role == "output_tile") {
      const bool input = *role == "input_tile";
      const PtoValue::Array &tiles = input ? *inputs : *outputs;
      size_t &tileIndex = input ? inputIndex : outputIndex;
      if (tileIndex >= tiles.size())
        return reject("npu_operand_role_mismatch",
                      "tile role count does not match imported tile arrays");
      std::optional<TileAttribute> tile = parseTile(tiles[tileIndex++]);
      if (!tile)
        return reject("npu_invalid_davincioo_attributes",
                      "tile attributes are malformed");
      std::string identity = tileIdentity(*blockId, tile->address);
      if (operand.kind != PtoOperandKind::Tile || operand.id != identity)
        return reject(
            "npu_tile_identity_mismatch",
            "ordered tile operand does not match its imported identity");
      (input ? instruction.inputTiles : instruction.outputTiles)
          .push_back(std::move(identity));
      continue;
    }
    if (*role == "scalar_input") {
      if (scalarIndex >= scalars->size())
        return reject("npu_operand_role_mismatch",
                      "scalar role count does not match scalar_inputs");
      std::optional<ScalarAttribute> scalar =
          parseScalar((*scalars)[scalarIndex++]);
      if (!scalar)
        return reject("npu_invalid_davincioo_attributes",
                      "scalar input attributes are malformed");
      if (operand.kind != PtoOperandKind::Immediate ||
          operand.type != scalar->type || !operand.immediate ||
          *operand.immediate != *scalar->value)
        return reject("npu_scalar_mismatch",
                      "ordered immediate does not match its imported scalar");
      instruction.scalarInputs.push_back(
          {.type = std::string(scalar->type), .value = *scalar->value});
      continue;
    }
    return reject("npu_operand_role_mismatch",
                  "operand role is not supported by the DavinciOO adapter");
  }

  if (inputIndex != inputs->size() || outputIndex != outputs->size() ||
      scalarIndex != scalars->size())
    return reject("npu_operand_role_mismatch",
                  "imported operand arrays contain unreferenced entries");

  NpuDecodeResult result;
  result.observations.push_back(observation("decode", instruction));
  result.observations.push_back(observation("dispatch", instruction));
  result.instruction = std::move(instruction);
  return result;
}

NpuDependencyTracker::NpuDependencyTracker(std::string name, ObjectId id,
                                           SimObject *parent,
                                           NpuIssueQueueCapacities capacities,
                                           ObservationSink *observations)
    : SimObject(ObjectKind::Scheduler, std::move(name), id, parent,
                observations),
      capacities_(capacities) {}

size_t NpuDependencyTracker::engineIndex(NpuEngineClass engine) {
  switch (engine) {
  case NpuEngineClass::Scalar:
    return 0;
  case NpuEngineClass::Vector:
    return 1;
  case NpuEngineClass::Cube:
    return 2;
  case NpuEngineClass::Tma:
    return 3;
  }
  return 0;
}

size_t NpuDependencyTracker::capacity(NpuEngineClass engine) const {
  switch (engine) {
  case NpuEngineClass::Scalar:
    return capacities_.scalar;
  case NpuEngineClass::Vector:
    return capacities_.vector;
  case NpuEngineClass::Cube:
    return capacities_.cube;
  case NpuEngineClass::Tma:
    return capacities_.tma;
  }
  return 0;
}

bool NpuDependencyTracker::knownSequence(uint64_t sequenceId) const {
  if (completedSequences_.contains(sequenceId) ||
      outstandingSequences_.contains(sequenceId))
    return true;
  if (std::ranges::any_of(dispatchProposals_,
                          [&](const auto &proposal) {
                            return proposal.instruction.sequenceId ==
                                   sequenceId;
                          }) ||
      std::ranges::any_of(acceptedDispatches_, [&](const auto &entry) {
        return entry.instruction.sequenceId == sequenceId;
      }))
    return true;
  for (const auto &queue : queues_)
    if (std::ranges::any_of(queue, [&](const auto &entry) {
          return entry.instruction.sequenceId == sequenceId;
        }))
      return true;
  return false;
}

bool NpuDependencyTracker::ready(const NpuIssueEntry &entry) const {
  return std::ranges::all_of(
      entry.derivedDependencies, [&](const NpuDependency &dependency) {
        return completedSequences_.contains(dependency.producerSequenceId);
      });
}

bool NpuDependencyTracker::proposeDispatch(const NpuInstruction &instruction,
                                           ObjectId stableObjectId) {
  if (stableObjectId == kInvalidObjectId ||
      (lastDispatchedSequence_ &&
       instruction.sequenceId <= *lastDispatchedSequence_) ||
      knownSequence(instruction.sequenceId))
    return false;
  dispatchProposals_.push_back({instruction, stableObjectId});
  return true;
}

bool NpuDependencyTracker::proposeIssue(NpuEngineClass engine) {
  const size_t index = engineIndex(engine);
  if (issueProposals_[index])
    return false;
  const auto &queue = queues_[index];
  const NpuIssueEntry *candidate = nullptr;
  for (const NpuIssueEntry &entry : queue)
    if (ready(entry) && (!candidate || issueEntryLess(entry, *candidate)))
      candidate = &entry;
  if (!candidate)
    return false;
  issueProposals_[index] = *candidate;
  return true;
}

bool NpuDependencyTracker::proposeComplete(uint64_t sequenceId) {
  if (!outstandingSequences_.contains(sequenceId) ||
      std::ranges::find(completionProposals_, sequenceId) !=
          completionProposals_.end())
    return false;
  completionProposals_.push_back(sequenceId);
  return true;
}

void NpuDependencyTracker::doArbitrate(Epoch) {
  std::sort(
      dispatchProposals_.begin(), dispatchProposals_.end(),
      [](const DispatchProposal &left, const DispatchProposal &right) {
        return std::tie(left.instruction.sequenceId, left.stableObjectId) <
               std::tie(right.instruction.sequenceId, right.stableObjectId);
      });

  std::array<size_t, 4> remaining{};
  for (size_t index = 0; index < queues_.size(); ++index) {
    const size_t released = issueProposals_[index] ? 1 : 0;
    const size_t occupied = queues_[index].size() - released;
    NpuEngineClass engine = static_cast<NpuEngineClass>(index);
    remaining[index] =
        capacity(engine) > occupied ? capacity(engine) - occupied : 0;
  }

  std::set<uint64_t> shadowFlowIds = usedFlowIds_;
  bool dispatchBlocked = false;
  for (const DispatchProposal &proposal : dispatchProposals_) {
    const size_t index = engineIndex(proposal.instruction.engine);
    if (dispatchBlocked || remaining[index] == 0) {
      proposedRejectedDispatches_.push_back(proposal.instruction.sequenceId);
      dispatchBlocked = true;
      continue;
    }

    NpuIssueEntry entry{.instruction = proposal.instruction,
                        .stableObjectId = proposal.stableObjectId};
    for (const std::string &tile : entry.instruction.inputTiles) {
      auto producer = producers_.find({entry.instruction.blockId, tile});
      if (producer == producers_.end())
        continue;
      uint64_t flow =
          dependencyFlowId(entry.instruction.blockId, producer->second,
                           entry.instruction.sequenceId, tile);
      while (shadowFlowIds.contains(flow)) {
        if (flow == std::numeric_limits<uint64_t>::max()) {
          setRuntimeFailureCode("npu_dependency_flow_id_exhausted");
          break;
        }
        ++flow;
      }
      if (!runtimeFailureCode().empty())
        break;
      shadowFlowIds.insert(flow);
      entry.derivedDependencies.push_back(
          {.producerSequenceId = producer->second,
           .tileIdentity = tile,
           .flowId = flow});
    }
    if (!runtimeFailureCode().empty()) {
      proposedRejectedDispatches_.push_back(proposal.instruction.sequenceId);
      dispatchBlocked = true;
      continue;
    }
    --remaining[index];
    acceptedDispatches_.push_back(std::move(entry));
  }

  for (const NpuIssueEntry &entry : acceptedDispatches_) {
    emitObservation(
        {.category = "instruction",
         .name = "dispatch",
         .phase = TraceEventPhase::Instant,
         .rootSequenceId = entry.instruction.sequenceId,
         .arguments = {
             {"engine", std::string(toString(entry.instruction.engine))},
             {"stable_object_id",
              static_cast<uint64_t>(entry.stableObjectId)}}});
    for (const NpuDependency &dependency : entry.derivedDependencies)
      emitObservation(
          {.category = "dependency",
           .name = "tile",
           .phase = TraceEventPhase::FlowStart,
           .rootSequenceId = entry.instruction.sequenceId,
           .flowId = dependency.flowId,
           .arguments = {
               {"producer_sequence_id", dependency.producerSequenceId},
               {"tile_identity", dependency.tileIdentity}}});
  }
  for (uint64_t sequenceId : proposedRejectedDispatches_)
    emitObservation({.category = "stall",
                     .name = "issue_queue_capacity",
                     .phase = TraceEventPhase::Instant,
                     .rootSequenceId = sequenceId});

  std::vector<const NpuIssueEntry *> issues;
  for (const auto &proposal : issueProposals_)
    if (proposal)
      issues.push_back(&*proposal);
  std::sort(issues.begin(), issues.end(),
            [](const auto *left, const auto *right) {
              return issueEntryLess(*left, *right);
            });
  for (const NpuIssueEntry *entry : issues) {
    emitObservation(
        {.category = "instruction",
         .name = "issue",
         .phase = TraceEventPhase::Instant,
         .rootSequenceId = entry->instruction.sequenceId,
         .arguments = {
             {"engine", std::string(toString(entry->instruction.engine))},
             {"stable_object_id",
              static_cast<uint64_t>(entry->stableObjectId)}}});
    for (const NpuDependency &dependency : entry->derivedDependencies)
      emitObservation(
          {.category = "dependency",
           .name = "tile",
           .phase = TraceEventPhase::FlowEnd,
           .rootSequenceId = entry->instruction.sequenceId,
           .flowId = dependency.flowId,
           .arguments = {
               {"producer_sequence_id", dependency.producerSequenceId},
               {"tile_identity", dependency.tileIdentity}}});
  }

  std::sort(completionProposals_.begin(), completionProposals_.end());
  for (uint64_t producer : completionProposals_)
    for (const auto &queue : queues_)
      for (const NpuIssueEntry &entry : queue)
        if (std::ranges::any_of(entry.derivedDependencies,
                                [&](const NpuDependency &dependency) {
                                  return dependency.producerSequenceId ==
                                         producer;
                                }))
          emitObservation({.category = "dependency",
                           .name = "ready",
                           .phase = TraceEventPhase::Instant,
                           .rootSequenceId = entry.instruction.sequenceId,
                           .arguments = {{"producer_sequence_id", producer}}});
}

void NpuDependencyTracker::doXfer(Epoch epoch) {
  const bool changed = hasPendingCommit();
  issued_.clear();
  acceptedDispatchSequences_.clear();
  rejectedDispatches_ = proposedRejectedDispatches_;

  for (NpuIssueEntry &entry : acceptedDispatches_) {
    entry.instruction.timestamps.dispatched = epoch.time;
    for (const std::string &tile : entry.instruction.outputTiles)
      producers_[{entry.instruction.blockId, tile}] =
          entry.instruction.sequenceId;
    for (const NpuDependency &dependency : entry.derivedDependencies)
      usedFlowIds_.insert(dependency.flowId);
    acceptedDispatchSequences_.insert(entry.instruction.sequenceId);
    lastDispatchedSequence_ = entry.instruction.sequenceId;
    queues_[engineIndex(entry.instruction.engine)].push_back(std::move(entry));
    ++totalDispatches_;
  }
  totalDispatchStalls_ += proposedRejectedDispatches_.size();

  for (size_t index = 0; index < issueProposals_.size(); ++index) {
    if (!issueProposals_[index])
      continue;
    auto &queue = queues_[index];
    auto position =
        std::ranges::find_if(queue, [&](const NpuIssueEntry &entry) {
          return entry.instruction.sequenceId ==
                     issueProposals_[index]->instruction.sequenceId &&
                 entry.stableObjectId == issueProposals_[index]->stableObjectId;
        });
    if (position == queue.end()) {
      setRuntimeFailureCode("npu_issue_entry_missing");
      continue;
    }
    position->instruction.timestamps.issued = epoch.time;
    outstandingSequences_.insert(position->instruction.sequenceId);
    issued_.push_back(std::move(*position));
    queue.erase(position);
    ++totalIssues_;
  }
  std::sort(issued_.begin(), issued_.end(), issueEntryLess);

  for (uint64_t sequenceId : completionProposals_) {
    for (const auto &queue : queues_)
      for (const NpuIssueEntry &entry : queue)
        totalDependencyWakeups_ += static_cast<uint64_t>(std::ranges::count_if(
            entry.derivedDependencies, [&](const NpuDependency &dependency) {
              return dependency.producerSequenceId == sequenceId;
            }));
    completedSequences_.insert(sequenceId);
    outstandingSequences_.erase(sequenceId);
  }

  for (size_t index = 0; index < queues_.size(); ++index) {
    std::sort(queues_[index].begin(), queues_[index].end(), issueEntryLess);
    highWatermarks_[index] =
        std::max(highWatermarks_[index], queues_[index].size());
  }

  dispatchProposals_.clear();
  acceptedDispatches_.clear();
  proposedRejectedDispatches_.clear();
  for (auto &proposal : issueProposals_)
    proposal.reset();
  completionProposals_.clear();
  if (changed)
    lastUpdate_ = epoch;
}

bool NpuDependencyTracker::hasPendingCommit() const {
  return !dispatchProposals_.empty() || !acceptedDispatches_.empty() ||
         !proposedRejectedDispatches_.empty() ||
         std::ranges::any_of(
             issueProposals_,
             [](const auto &entry) { return entry.has_value(); }) ||
         !completionProposals_.empty();
}

bool NpuDependencyTracker::isRunnable(Epoch) const {
  return !dispatchProposals_.empty() || !completionProposals_.empty();
}

RuntimeObjectState NpuDependencyTracker::runtimeState(Epoch epoch) const {
  RuntimeObjectState state = SimObject::runtimeState(epoch);
  for (const auto &queue : queues_)
    state.queueOccupancy += queue.size();
  state.pendingOffers = dispatchProposals_.size() + completionProposals_.size();
  state.activeReservations = outstandingSequences_.size();
  state.quiescent = state.queueOccupancy == 0 && state.pendingOffers == 0 &&
                    state.activeReservations == 0 && !hasPendingCommit();
  if (!state.quiescent)
    state.reason =
        state.pendingOffers != 0
            ? "npu_pending_proposal"
            : (state.queueOccupancy != 0 ? "npu_issue_queue_not_empty"
                                         : "npu_execution_outstanding");
  return state;
}

void NpuDependencyTracker::collectStatistics(
    std::vector<StatSnapshot> &out) const {
  auto append = [&](std::string name, StatisticKind kind, uint64_t value) {
    out.push_back({.name = std::move(name),
                   .objectPath = std::string(path()),
                   .kind = kind,
                   .value = value,
                   .lastUpdate = lastUpdate_});
  };
  constexpr std::array names = {"scalar", "vector", "cube", "tma"};
  for (size_t index = 0; index < queues_.size(); ++index) {
    append("issue_queue_occupancy_" + std::string(names[index]),
           StatisticKind::Gauge, queues_[index].size());
    append("issue_queue_peak_" + std::string(names[index]),
           StatisticKind::Gauge, highWatermarks_[index]);
  }
  append("dispatch_stalls", StatisticKind::Counter, totalDispatchStalls_);
  append("dispatched_instructions", StatisticKind::Counter, totalDispatches_);
  append("issued_instructions", StatisticKind::Counter, totalIssues_);
  append("dependency_wakeups", StatisticKind::Counter, totalDependencyWakeups_);
}

const NpuIssueEntry *
NpuDependencyTracker::proposedIssue(NpuEngineClass engine) const {
  const auto &proposal = issueProposals_[engineIndex(engine)];
  return proposal ? &*proposal : nullptr;
}

std::vector<NpuIssueEntry>
NpuDependencyTracker::queued(NpuEngineClass engine) const {
  return queues_[engineIndex(engine)];
}

std::vector<NpuDependency>
NpuDependencyTracker::dependencies(uint64_t sequenceId) const {
  for (const auto &queue : queues_)
    for (const NpuIssueEntry &entry : queue)
      if (entry.instruction.sequenceId == sequenceId)
        return entry.derivedDependencies;
  return {};
}

bool NpuDependencyTracker::isReady(uint64_t sequenceId) const {
  for (const auto &queue : queues_)
    for (const NpuIssueEntry &entry : queue)
      if (entry.instruction.sequenceId == sequenceId)
        return ready(entry);
  return false;
}

bool NpuDependencyTracker::dispatchAccepted(uint64_t sequenceId) const {
  return acceptedDispatchSequences_.contains(sequenceId);
}

size_t NpuDependencyTracker::queueSize(NpuEngineClass engine) const {
  return queues_[engineIndex(engine)].size();
}

void NpuDependencyTracker::reset() {
  for (auto &queue : queues_)
    queue.clear();
  dispatchProposals_.clear();
  acceptedDispatches_.clear();
  proposedRejectedDispatches_.clear();
  rejectedDispatches_.clear();
  acceptedDispatchSequences_.clear();
  for (auto &proposal : issueProposals_)
    proposal.reset();
  issued_.clear();
  completionProposals_.clear();
  outstandingSequences_.clear();
  completedSequences_.clear();
  producers_.clear();
  usedFlowIds_.clear();
  lastDispatchedSequence_.reset();
  highWatermarks_.fill(0);
  totalDispatches_ = 0;
  totalDispatchStalls_ = 0;
  totalIssues_ = 0;
  totalDependencyWakeups_ = 0;
  lastUpdate_ = {};
  clearRuntimeFailureCode();
}

} // namespace gfsim
