#include "gfsim/npu.h"

#include "gtest/gtest.h"

#include <array>
#include <string>
#include <utility>

namespace gfsim {
namespace {

PtoValue value(std::string text) { return {.value = std::move(text)}; }

PtoValue value(uint64_t integer) { return {.value = integer}; }

PtoValue array(PtoValue::Array values) { return {.value = std::move(values)}; }

PtoValue object(PtoValue::Object members) {
  return {.value = std::move(members)};
}

PtoValue tile(std::string address) {
  return object({{"address", value(std::move(address))},
                 {"dtype", value("float32")},
                 {"layout", value("ND")},
                 {"shape", array({value(2), value(4)})}});
}

PtoValue scalar(std::string type, std::string scalarValue) {
  return object({{"dtype", value(std::move(type))},
                 {"value", value(std::move(scalarValue))}});
}

PtoTraceOperand tileOperand(std::string id) {
  return {.kind = PtoOperandKind::Tile, .id = std::move(id)};
}

PtoTraceOperand scalarOperand(std::string type, std::string scalarValue) {
  return {.kind = PtoOperandKind::Immediate,
          .type = std::move(type),
          .immediate = value(std::move(scalarValue))};
}

PtoTraceRecord record(std::string opcode, uint64_t sequenceId, uint64_t blockId,
                      PtoValue::Array inputTiles, PtoValue::Array scalarInputs,
                      PtoValue::Array outputTiles,
                      std::vector<std::string> roles,
                      std::vector<PtoTraceOperand> operands) {
  PtoValue::Array roleValues;
  for (std::string &role : roles)
    roleValues.push_back(value(std::move(role)));
  PtoTraceRecord result;
  result.sequenceId = sequenceId;
  result.opcode = std::move(opcode);
  result.operands = std::move(operands);
  result.attributes.emplace(
      "davincioo", object({{"block_idx", value(blockId)},
                           {"input_tiles", array(std::move(inputTiles))},
                           {"operand_roles", array(std::move(roleValues))},
                           {"output_tiles", array(std::move(outputTiles))},
                           {"scalar_inputs", array(std::move(scalarInputs))}}));
  return result;
}

PtoTraceRecord representative(std::string opcode) {
  if (opcode == "TASSIGN")
    return record(
        std::move(opcode), 7, 3, {}, {scalar("uint64", "64")}, {tile("0x40")},
        {"scalar_input", "output_tile"},
        {scalarOperand("uint64", "64"), tileOperand("block/3/tile/0x40")});
  if (opcode == "TLOAD")
    return record(
        std::move(opcode), 7, 3, {tile("0x20")}, {}, {tile("0x40")},
        {"input_tile", "output_tile"},
        {tileOperand("block/3/tile/0x20"), tileOperand("block/3/tile/0x40")});
  if (opcode == "TMATMUL")
    return record(std::move(opcode), 7, 3, {tile("0x10"), tile("0x20")}, {},
                  {tile("0x40")}, {"input_tile", "input_tile", "output_tile"},
                  {tileOperand("block/3/tile/0x10"),
                   tileOperand("block/3/tile/0x20"),
                   tileOperand("block/3/tile/0x40")});
  return record(
      std::move(opcode), 7, 3, {tile("0x20")}, {scalar("float32", "1.25")},
      {tile("0x40")}, {"input_tile", "scalar_input", "output_tile"},
      {tileOperand("block/3/tile/0x20"), scalarOperand("float32", "1.25"),
       tileOperand("block/3/tile/0x40")});
}

TEST(NpuDecoderTest, ClassifiesRepresentativePinnedDavinciOOOpcodes) {
  NpuDecoder decoder;
  const std::array cases = {
      std::pair{"TASSIGN", NpuEngineClass::Scalar},
      std::pair{"TADDS", NpuEngineClass::Vector},
      std::pair{"TMATMUL", NpuEngineClass::Cube},
      std::pair{"TLOAD", NpuEngineClass::Tma},
  };

  for (const auto &[opcode, engine] : cases) {
    SCOPED_TRACE(opcode);
    NpuDecodeResult decoded = decoder.decode(representative(opcode));
    ASSERT_TRUE(decoded.succeeded());
    ASSERT_TRUE(decoded.instruction);
    EXPECT_EQ(decoded.instruction->opcode, opcode);
    EXPECT_EQ(decoded.instruction->engine, engine);
  }
}

TEST(NpuDecoderTest, PreservesOrderedOperandsTilesScalarsAndDependencies) {
  NpuDecoder decoder;
  PtoTraceRecord source = representative("TADDS");
  source.dependencies = {1, 5};

  NpuDecodeResult decoded = decoder.decode(source);
  ASSERT_TRUE(decoded.succeeded());
  const NpuInstruction &instruction = *decoded.instruction;
  EXPECT_EQ(instruction.sequenceId, 7u);
  EXPECT_EQ(instruction.blockId, 3u);
  EXPECT_EQ(instruction.dependencies, (std::vector<uint64_t>{1, 5}));
  EXPECT_EQ(instruction.operands, source.operands);
  EXPECT_EQ(instruction.inputTiles,
            (std::vector<std::string>{"block/3/tile/0x20"}));
  EXPECT_EQ(instruction.outputTiles,
            (std::vector<std::string>{"block/3/tile/0x40"}));
  ASSERT_EQ(instruction.scalarInputs.size(), 1u);
  EXPECT_EQ(instruction.scalarInputs[0].type, "float32");
  EXPECT_EQ(instruction.scalarInputs[0].value, value("1.25"));
  EXPECT_EQ(instruction.timestamps, NpuTimestamps{});
}

TEST(NpuDecoderTest, ProducesDecodeAndDispatchProposalsWithRootIdentity) {
  NpuDecodeResult decoded = NpuDecoder{}.decode(representative("TMATMUL"));
  ASSERT_TRUE(decoded.succeeded());
  ASSERT_EQ(decoded.observations.size(), 2u);
  EXPECT_EQ(decoded.observations[0].category, "instruction");
  EXPECT_EQ(decoded.observations[0].name, "decode");
  EXPECT_EQ(decoded.observations[1].category, "instruction");
  EXPECT_EQ(decoded.observations[1].name, "dispatch");
  for (const EventProposal &proposal : decoded.observations) {
    EXPECT_EQ(proposal.rootSequenceId, 7u);
    EXPECT_EQ(proposal.arguments,
              (std::vector<ObservationArgument>{
                  {.name = "block_id", .value = uint64_t{3}},
                  {.name = "engine", .value = std::string("cube")},
                  {.name = "opcode", .value = std::string("TMATMUL")}}));
  }
}

TEST(NpuDecoderTest, RejectsUnsupportedAndMalformedImportedRecords) {
  NpuDecoder decoder;

  PtoTraceRecord unsupported = representative("TADDS");
  unsupported.opcode = "TUNSUPPORTED";
  EXPECT_EQ(decoder.decode(unsupported).primaryDiagnostic(),
            "npu_unsupported_opcode");

  PtoTraceRecord missing = representative("TADDS");
  missing.attributes.clear();
  EXPECT_EQ(decoder.decode(missing).primaryDiagnostic(),
            "npu_missing_davincioo_attributes");

  PtoTraceRecord unknownField = representative("TADDS");
  auto &attributes =
      std::get<PtoValue::Object>(unknownField.attributes.at("davincioo").value);
  attributes.emplace("unexpected", value(1));
  EXPECT_EQ(decoder.decode(unknownField).primaryDiagnostic(),
            "npu_invalid_davincioo_attributes");

  PtoTraceRecord wrongRoleCount = representative("TADDS");
  auto &roleArray = std::get<PtoValue::Array>(
      std::get<PtoValue::Object>(
          wrongRoleCount.attributes.at("davincioo").value)
          .at("operand_roles")
          .value);
  roleArray.pop_back();
  EXPECT_EQ(decoder.decode(wrongRoleCount).primaryDiagnostic(),
            "npu_operand_role_mismatch");

  PtoTraceRecord wrongTile = representative("TADDS");
  wrongTile.operands.front().id = "block/3/tile/0x21";
  EXPECT_EQ(decoder.decode(wrongTile).primaryDiagnostic(),
            "npu_tile_identity_mismatch");

  PtoTraceRecord wrongScalar = representative("TADDS");
  wrongScalar.operands[1].type = "float16";
  EXPECT_EQ(decoder.decode(wrongScalar).primaryDiagnostic(),
            "npu_scalar_mismatch");
}

TEST(NpuDecoderTest, RepeatedDecodeIsByteIndependentAndValueIdentical) {
  NpuDecoder decoder;
  const PtoTraceRecord source = representative("TADDS");
  const NpuDecodeResult first = decoder.decode(source);
  const NpuDecodeResult second = decoder.decode(source);
  ASSERT_TRUE(first.succeeded());
  ASSERT_TRUE(second.succeeded());
  EXPECT_EQ(first.instruction, second.instruction);
  EXPECT_EQ(first.observations, second.observations);
  EXPECT_EQ(source, representative("TADDS"));
}

TEST(NpuDecoderTest, UnsupportedOpcodeNeverCommitsATraceOffer) {
  PtoTraceDocument document;
  document.records.push_back(representative("TADDS"));
  document.records.front().opcode = "TUNSUPPORTED";
  TraceSource<NpuInstruction, NpuDecoder> source("trace", 1, nullptr,
                                                 std::move(document));

  source.doWork({0, 0});
  source.doXfer({0, 0});

  EXPECT_FALSE(source.hasOffer());
  EXPECT_EQ(source.position().nextRecordIndex, 0u);
  EXPECT_EQ(source.runtimeFailureCode(), "trace_decode_failed");
}

} // namespace
} // namespace gfsim
