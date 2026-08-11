#include "gfsim/npu.h"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
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

} // namespace gfsim
