#ifndef GFSIM_NPU_H
#define GFSIM_NPU_H

#include "gfsim/observation.h"
#include "gfsim/trace.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
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

struct NpuIssueQueueCapacities {
  size_t scalar = 0;
  size_t vector = 0;
  size_t cube = 0;
  size_t tma = 0;
};

struct NpuDependency {
  uint64_t producerSequenceId = 0;
  std::string tileIdentity;
  uint64_t flowId = 0;

  bool operator==(const NpuDependency &) const = default;
};

struct NpuIssueEntry {
  NpuInstruction instruction;
  ObjectId stableObjectId = kInvalidObjectId;
  std::vector<NpuDependency> derivedDependencies;

  bool operator==(const NpuIssueEntry &) const = default;
};

/// Block-local tile rename state and four finite deterministic issue queues.
class NpuDependencyTracker final : public SimObject {
public:
  static constexpr std::string_view contractName = "ac.npu.DependencyTracker";
  static constexpr ObjectKind componentKind = ObjectKind::Scheduler;

  NpuDependencyTracker(std::string name, ObjectId id, SimObject *parent,
                       NpuIssueQueueCapacities capacities,
                       ObservationSink *observations = nullptr);

  bool proposeDispatch(const NpuInstruction &instruction,
                       ObjectId stableObjectId);
  bool proposeIssue(NpuEngineClass engine);
  bool proposeComplete(uint64_t sequenceId);

  void doArbitrate(Epoch epoch) override;
  void doXfer(Epoch epoch) override;
  bool hasPendingCommit() const override;
  bool isRunnable(Epoch epoch) const override;
  RuntimeObjectState runtimeState(Epoch epoch) const override;
  void collectStatistics(std::vector<StatSnapshot> &out) const override;
  void reset() override;

  const NpuIssueEntry *proposedIssue(NpuEngineClass engine) const;
  const std::vector<NpuIssueEntry> &issued() const { return issued_; }
  std::vector<NpuIssueEntry> queued(NpuEngineClass engine) const;
  std::vector<NpuDependency> dependencies(uint64_t sequenceId) const;
  bool isReady(uint64_t sequenceId) const;
  bool dispatchAccepted(uint64_t sequenceId) const;
  size_t queueSize(NpuEngineClass engine) const;
  const std::vector<uint64_t> &rejectedDispatches() const {
    return rejectedDispatches_;
  }

private:
  using TileKey = std::pair<uint64_t, std::string>;

  struct DispatchProposal {
    NpuInstruction instruction;
    ObjectId stableObjectId = kInvalidObjectId;
  };

  static size_t engineIndex(NpuEngineClass engine);
  size_t capacity(NpuEngineClass engine) const;
  bool knownSequence(uint64_t sequenceId) const;
  bool ready(const NpuIssueEntry &entry) const;

  NpuIssueQueueCapacities capacities_;
  std::array<std::vector<NpuIssueEntry>, 4> queues_;
  std::vector<DispatchProposal> dispatchProposals_;
  std::vector<NpuIssueEntry> acceptedDispatches_;
  std::vector<uint64_t> proposedRejectedDispatches_;
  std::vector<uint64_t> rejectedDispatches_;
  std::set<uint64_t> acceptedDispatchSequences_;
  std::array<std::optional<NpuIssueEntry>, 4> issueProposals_;
  std::vector<NpuIssueEntry> issued_;
  std::vector<uint64_t> completionProposals_;
  std::set<uint64_t> outstandingSequences_;
  std::set<uint64_t> completedSequences_;
  std::map<TileKey, uint64_t> producers_;
  std::set<uint64_t> usedFlowIds_;
  std::optional<uint64_t> lastDispatchedSequence_;
  std::array<size_t, 4> highWatermarks_{};
  uint64_t totalDispatches_ = 0;
  uint64_t totalDispatchStalls_ = 0;
  uint64_t totalIssues_ = 0;
  uint64_t totalDependencyWakeups_ = 0;
  Epoch lastUpdate_;
};

} // namespace gfsim

#endif // GFSIM_NPU_H
