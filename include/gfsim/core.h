#ifndef GFSIM_CORE_H
#define GFSIM_CORE_H

#include <compare>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gfsim {

// ── Exact time and epoch ──────────────────────────────────────────────

/// Simulation time is a non-negative, unbounded-in-semantics integer tick.
using Tick = uint64_t;

/// Causal-delta index within a single tick.
using CausalDelta = uint32_t;

/// The global epoch is the exact pair (time, delta). Equality, ordering,
/// scheduling, tracing, and wake decisions use this exact pair.
struct Epoch {
  Tick time = 0;
  CausalDelta delta = 0;

  auto operator<=>(const Epoch &) const = default;

  Epoch nextDelta() const { return {time, delta + 1}; }
  bool sameTime(const Epoch &other) const { return time == other.time; }
};

/// Maximum causal deltas per tick (must be finite).
inline constexpr CausalDelta kMaxDeltasPerTick = 1024;

// ── Object identity ───────────────────────────────────────────────────

/// Compile-time-assigned stable object ID.
using ObjectId = uint32_t;

inline constexpr ObjectId kInvalidObjectId =
    std::numeric_limits<ObjectId>::max();
inline constexpr ObjectId kSystemObjectId = kInvalidObjectId - 1;
inline constexpr ObjectId kRootObjectId = kInvalidObjectId - 2;

/// Statically known runtime kind for every SimObject.
enum class ObjectKind : uint8_t {
  System,
  Module,
  Queue,
  EventQueue,
  Resource,
  Process,
  TraceSource,
  Compute,
  Link,
  Memory,
  Sink,
  Probe,
  Statistic,
  Scheduler,
};

// ── Termination ───────────────────────────────────────────────────────

enum class TerminationClass : uint8_t {
  Completed,  // All work finished, no pending events, trace exhausted
  Incomplete, // Reached a declared cap without contract violation
  Failed,     // Contract violation, assertion, or runtime error
};

struct TerminationResult {
  TerminationClass classification = TerminationClass::Incomplete;
  Epoch finalEpoch;
  uint64_t committedEventCount = 0;
  uint64_t tracePosition = 0;
  std::string diagnosticCode;
  std::optional<std::string> message;
};

// ── Build profiles ────────────────────────────────────────────────────

enum class BuildProfile : uint8_t {
  Fast,      // Required representation, safety, capacity, and contract checks
  Validated, // Fast + post-pass verification, transaction-lifetime checks,
             //   arbitration audits, event provenance, quiescence checks
  Custom,    // Explicit pass pipeline retaining all mandatory Fast checks
};

// ── Static preflight ──────────────────────────────────────────────────

struct PreflightResult {
  bool passed = false;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
};

// ── Work and proposal ─────────────────────────────────────────────────

/// A proposal is a private buffer associated with a stable object ID.
/// Each Work execution writes only to its own proposal.
struct Proposal {
  ObjectId ownerId = kInvalidObjectId;
  bool hasCommit = false;
  // Component-specific proposal data is stored in derived types
};

// ── Event ─────────────────────────────────────────────────────────────

/// An event scheduled for a future epoch.
struct Event {
  Epoch readyTime;
  ObjectId targetId = kInvalidObjectId;
  uint32_t eventKind = 0;
  uint64_t payload = 0;

  // Stable ordering: epoch first, then target, event kind, and payload.
  auto operator<=>(const Event &other) const {
    if (auto cmp = readyTime <=> other.readyTime; cmp != 0)
      return cmp;
    if (auto cmp = targetId <=> other.targetId; cmp != 0)
      return cmp;
    if (auto cmp = eventKind <=> other.eventKind; cmp != 0)
      return cmp;
    return payload <=> other.payload;
  }
};

// ── Statistics ────────────────────────────────────────────────────────

struct StatSnapshot {
  std::string name;
  std::string objectPath;
  uint64_t value = 0;
  Epoch lastUpdate;
};

} // namespace gfsim

#endif // GFSIM_CORE_H
