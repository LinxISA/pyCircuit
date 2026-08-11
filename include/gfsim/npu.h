#ifndef GFSIM_NPU_H
#define GFSIM_NPU_H

#include "gfsim/observation.h"
#include "gfsim/trace.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gfsim {

enum class NpuEngineClass : uint8_t {
  Scalar,
  Vector,
  Cube,
  Tma,
};

struct NpuTimestamps {
  std::optional<uint64_t> decoded;
  std::optional<uint64_t> dispatched;
  std::optional<uint64_t> issued;
  std::optional<uint64_t> completed;
  std::optional<uint64_t> retired;

  bool operator==(const NpuTimestamps &) const = default;
};

struct NpuScalarImmediate {
  std::string type;
  PtoValue value;

  bool operator==(const NpuScalarImmediate &) const = default;
};

struct NpuInstruction {
  uint64_t sequenceId = 0;
  uint64_t blockId = 0;
  std::string opcode;
  std::vector<uint64_t> dependencies;
  std::vector<PtoTraceOperand> operands;
  std::vector<std::string> inputTiles;
  std::vector<std::string> outputTiles;
  std::vector<NpuScalarImmediate> scalarInputs;
  NpuEngineClass engine = NpuEngineClass::Scalar;
  NpuTimestamps timestamps;

  bool operator==(const NpuInstruction &) const = default;
};

struct NpuDecodeDiagnostic {
  std::string code;
  std::string message;

  bool operator==(const NpuDecodeDiagnostic &) const = default;
};

struct NpuDecodeResult {
  std::optional<NpuInstruction> instruction;
  std::vector<NpuDecodeDiagnostic> diagnostics;
  std::vector<EventProposal> observations;

  bool succeeded() const {
    return instruction.has_value() && diagnostics.empty();
  }
  std::string_view primaryDiagnostic() const {
    return diagnostics.empty() ? std::string_view{} : diagnostics.front().code;
  }
};

class NpuDecoder {
public:
  NpuDecodeResult decode(const PtoTraceRecord &record) const;
  std::optional<NpuInstruction> operator()(const PtoTraceRecord &record) const {
    return decode(record).instruction;
  }
};

std::string_view toString(NpuEngineClass engine);

} // namespace gfsim

#endif // GFSIM_NPU_H
