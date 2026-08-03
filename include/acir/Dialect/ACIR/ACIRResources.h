#ifndef ACIR_DIALECT_ACIR_ACIRRESOURCES_H
#define ACIR_DIALECT_ACIR_ACIRRESOURCES_H

#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Support/LogicalResult.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>

namespace mlir {
class Operation;
}

namespace acir::ac {

inline constexpr uint64_t kMaxTickScale = uint64_t{1} << 32;

struct AddressInterval {
  uint64_t begin;
  uint64_t end;
};

struct AddressMapOrderKey {
  uint64_t base;
  uint64_t size;
  int64_t priority;
};

bool checkedAdd(uint64_t left, uint64_t right, uint64_t &result);
bool checkedMultiply(uint64_t left, uint64_t right, uint64_t &result);
bool intervalsOverlap(AddressInterval left, AddressInterval right);
int compareAddressMapOrder(AddressMapOrderKey left, AddressMapOrderKey right);

/// Converts an exact rational duration to an integral number of global ticks.
/// The duration is numerator/denominator and the global quantum is
/// quantumNumerator/quantumDenominator. Returns false for invalid, inexact, or
/// overflowing conversions.
bool normalizeRationalToTicks(uint64_t numerator, uint64_t denominator,
                              uint64_t quantumNumerator,
                              uint64_t quantumDenominator, uint64_t &ticks);

struct QueueStateResource
    : public mlir::SideEffects::Resource::Base<QueueStateResource> {
  llvm::StringRef getName() final { return "ac.queue.state"; }
};

struct EventQueueStateResource
    : public mlir::SideEffects::Resource::Base<EventQueueStateResource> {
  llvm::StringRef getName() final { return "ac.event_queue.state"; }
};

struct ReservationStateResource
    : public mlir::SideEffects::Resource::Base<ReservationStateResource> {
  llvm::StringRef getName() final { return "ac.resource.reservation"; }
};

/// Whole-file validation for address-space and time-domain parent graphs.
mlir::LogicalResult verifyResourceStructure(mlir::Operation *topLevel);

} // namespace acir::ac

#endif
