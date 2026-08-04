#include "acir/Dialect/ACIR/ACIRResources.h"

#include "acir/Dialect/ACIR/ACIROps.h"
#include "acir/Dialect/ACIR/ACIRTypes.h"
#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringExtras.h"

#include <limits>
#include <map>
#include <numeric>
#include <set>

using namespace mlir;

namespace acir::ac {
namespace {

bool isStableSegment(StringRef value) {
  return !value.empty() && llvm::all_of(value, [](char c) {
    return llvm::isAlnum(c) || c == '_' || c == '-';
  });
}

LogicalResult verifyPlacement(Operation *op) {
  auto module = dyn_cast_or_null<ModuleOp>(op->getParentOp());
  if (module && !module.getBody().empty() &&
      op->getBlock() == &module.getBody().front())
    return success();
  return op->emitOpError(
      "must be a direct child of the unique ac.module Graph block");
}

Operation *lookupOuter(Operation *from, SymbolRefAttr reference) {
  if (Operation *target = SymbolTable::lookupNearestSymbolFrom(from, reference))
    return target;
  auto file = from->getParentOfType<mlir::ModuleOp>();
  return file ? SymbolTable::lookupSymbolIn(file, reference) : nullptr;
}

bool isNormativePayload(Type type) {
  if (isa<IntegerType, FloatType, IndexType, StructType, PacketType,
          TransactionType, EnumType, UnionType>(type))
    return true;
  if (auto optional = dyn_cast<OptionalType>(type))
    return isNormativePayload(optional.getElementType());
  if (auto list = dyn_cast<ListType>(type))
    return isNormativePayload(list.getElementType());
  if (auto vector = dyn_cast<VectorType>(type))
    return isNormativePayload(vector.getElementType());
  if (auto vector = dyn_cast<mlir::VectorType>(type))
    return isNormativePayload(vector.getElementType());
  return false;
}

LogicalResult verifyOwner(Operation *op, StringRef name, StringRef stableId,
                          StringRef path, int64_t delay) {
  if (failed(verifyPlacement(op)))
    return failure();
  if (!isStableSegment(name) || !isStableSegment(stableId) ||
      !isStableSegment(path))
    return op->emitOpError(
        "owner name, stable id, and path must be stable local segments");
  if (delay != 1)
    return op->emitOpError(
        "stateful declaration delay_ticks must be exactly one positive tick");
  return success();
}

bool hasExactKeys(DictionaryAttr dictionary, ArrayRef<StringRef> keys) {
  if (!dictionary || dictionary.size() != keys.size())
    return false;
  return llvm::all_of(
      keys, [&](StringRef key) { return dictionary.get(key) != nullptr; });
}

FailureOr<int64_t> positiveI64(Operation *op, DictionaryAttr dictionary,
                               StringRef key, StringRef diagnostic) {
  auto value = dictionary.getAs<IntegerAttr>(key);
  if (!value || !value.getType().isSignlessInteger(64) || value.getInt() <= 0) {
    op->emitOpError(diagnostic);
    return failure();
  }
  return value.getInt();
}

LogicalResult verifyLifecycle(ResourceOp op) {
  DictionaryAttr lifecycle = op.getLifecycle();
  if (!hasExactKeys(lifecycle, {"reservation", "release", "cancellation"}))
    return op.emitOpError(
        "lifecycle requires exact reservation/release/cancellation schema");
  auto reservation = lifecycle.getAs<StringAttr>("reservation");
  auto release = lifecycle.getAs<StringAttr>("release");
  auto cancellation = lifecycle.getAs<StringAttr>("cancellation");
  if (!reservation || reservation.getValue() != "propose_commit" || !release ||
      release.getValue() != "balanced" || !cancellation ||
      cancellation.getValue() != "explicit")
    return op.emitOpError(
        "lifecycle requires exact reservation/release/cancellation schema");
  return success();
}

LogicalResult
verifyParentCycles(ModuleOp module, bool timeDomains,
                   const llvm::StringMap<Operation *> &producerIndex) {
  SmallVector<Operation *> nodes;
  for (Operation &operation : module.getBody().front()) {
    bool selected = timeDomains ? isa<TimeDomainOp>(operation)
                                : isa<AddressSpaceOp>(operation);
    if (!selected)
      continue;
    nodes.push_back(&operation);
  }

  enum class State : uint8_t { Unvisited, Active, Complete };
  DenseMap<Operation *, State> states;
  for (Operation *start : nodes) {
    if (states.lookup(start) != State::Unvisited)
      continue;
    SmallVector<Operation *> path;
    DenseMap<Operation *, unsigned> pathIndex;
    Operation *current = start;
    while (current && states.lookup(current) == State::Unvisited) {
      states[current] = State::Active;
      pathIndex[current] = path.size();
      path.push_back(current);
      FlatSymbolRefAttr parent =
          timeDomains ? cast<TimeDomainOp>(current).getParentAttr()
                      : cast<AddressSpaceOp>(current).getParentAttr();
      auto target =
          parent ? producerIndex.find(parent.getValue()) : producerIndex.end();
      current = target == producerIndex.end() ? nullptr : target->second;
    }
    if (current && states.lookup(current) == State::Active) {
      InFlightDiagnostic diagnostic =
          current->emitOpError(timeDomains ? "time-domain parent cycle: "
                                           : "address-space parent cycle: ");
      unsigned begin = pathIndex.lookup(current);
      for (unsigned index = begin; index < path.size(); ++index)
        diagnostic << '@'
                   << path[index]
                          ->getAttrOfType<StringAttr>(
                              SymbolTable::getSymbolAttrName())
                          .getValue()
                   << " -> ";
      diagnostic << '@'
                 << current
                        ->getAttrOfType<StringAttr>(
                            SymbolTable::getSymbolAttrName())
                        .getValue();
      return failure();
    }
    for (Operation *node : path)
      states[node] = State::Complete;
  }
  return success();
}

} // namespace

bool checkedAdd(uint64_t left, uint64_t right, uint64_t &result) {
  if (left > std::numeric_limits<uint64_t>::max() - right)
    return false;
  result = left + right;
  return true;
}

bool checkedMultiply(uint64_t left, uint64_t right, uint64_t &result) {
  if (left != 0 && right > std::numeric_limits<uint64_t>::max() / left)
    return false;
  result = left * right;
  return true;
}

bool intervalsOverlap(AddressInterval left, AddressInterval right) {
  return left.begin < right.end && right.begin < left.end;
}

int compareAddressMapOrder(AddressMapOrderKey left, AddressMapOrderKey right) {
  if (left.base != right.base)
    return left.base < right.base ? -1 : 1;
  if (left.hasPriority != right.hasPriority)
    return left.hasPriority ? -1 : 1;
  if (left.hasPriority && left.priority != right.priority)
    return left.priority > right.priority ? -1 : 1;
  if (left.size != right.size)
    return left.size < right.size ? -1 : 1;
  return 0;
}

bool checkedDomainTick(uint64_t phase, uint64_t period, uint64_t cycle,
                       uint64_t &tick) {
  uint64_t elapsed = 0;
  if (!checkedMultiply(period, cycle, elapsed) ||
      !checkedAdd(phase, elapsed, tick))
    return false;
  return tick <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
}

bool normalizeRationalToTicks(uint64_t numerator, uint64_t denominator,
                              uint64_t quantumNumerator,
                              uint64_t quantumDenominator, uint64_t &ticks) {
  if (!denominator || !quantumNumerator || !quantumDenominator)
    return false;
  uint64_t durationGcd = std::gcd(numerator, denominator);
  numerator /= durationGcd;
  denominator /= durationGcd;
  uint64_t quantumGcd = std::gcd(quantumNumerator, quantumDenominator);
  quantumNumerator /= quantumGcd;
  quantumDenominator /= quantumGcd;
  uint64_t crossGcd = std::gcd(numerator, quantumNumerator);
  numerator /= crossGcd;
  quantumNumerator /= crossGcd;
  crossGcd = std::gcd(quantumDenominator, denominator);
  quantumDenominator /= crossGcd;
  denominator /= crossGcd;
  if (quantumNumerator > kMaxTickScale || quantumDenominator > kMaxTickScale)
    return false;
  uint64_t divisor = 0;
  if (!checkedMultiply(denominator, quantumNumerator, divisor) || !divisor)
    return false;
  uint64_t product = 0;
  if (!checkedMultiply(numerator, quantumDenominator, product) ||
      product % divisor != 0)
    return false;
  ticks = product / divisor;
  return true;
}

DictionaryAttr ownerEffectParameters(Operation *operation, StringAttr stableId,
                                     StringAttr path) {
  Builder builder(operation->getContext());
  return builder.getDictionaryAttr({builder.getNamedAttr("stable_id", stableId),
                                    builder.getNamedAttr("path", path)});
}

void QueueOp::getEffects(
    SmallVectorImpl<MemoryEffects::EffectInstance> &effects) {
  effects.emplace_back(
      MemoryEffects::Write::get(),
      FlatSymbolRefAttr::get(getContext(), getSymName()),
      ownerEffectParameters(*this, getStableIdAttr(), getPathAttr()),
      QueueStateResource::get());
}

void EventQueueOp::getEffects(
    SmallVectorImpl<MemoryEffects::EffectInstance> &effects) {
  effects.emplace_back(
      MemoryEffects::Write::get(),
      FlatSymbolRefAttr::get(getContext(), getSymName()),
      ownerEffectParameters(*this, getStableIdAttr(), getPathAttr()),
      EventQueueStateResource::get());
}

void ResourceOp::getEffects(
    SmallVectorImpl<MemoryEffects::EffectInstance> &effects) {
  effects.emplace_back(
      MemoryEffects::Write::get(),
      FlatSymbolRefAttr::get(getContext(), getSymName()),
      ownerEffectParameters(*this, getStableIdAttr(), getPathAttr()),
      ReservationStateResource::get());
}

LogicalResult QueueOp::verify() {
  if (failed(verifyOwner(*this, getSymName(), getStableId(), getPath(),
                         getDelayTicksAttr().getInt())))
    return failure();
  int64_t entryCapacity = getEntryCapacityAttr().getInt();
  if (entryCapacity <= 0)
    return emitOpError("entry capacity must be positive");
  if (auto bytes = getByteCapacityAttr(); bytes && bytes.getInt() <= 0)
    return emitOpError("byte capacity must be positive when present");
  if (getOrdering() != "fifo" && getOrdering() != "per_key")
    return emitOpError("ordering must be 'fifo' or 'per_key'");
  if (getOwnership() != "exclusive")
    return emitOpError("queue ownership must be exactly 'exclusive'");
  if (!isNormativePayload(getPayload()))
    return emitOpError("queue payload must be a normative ACIR value type");
  if (DictionaryAttr marks = getWatermarksAttr()) {
    auto low = marks.getAs<IntegerAttr>("low");
    auto high = marks.getAs<IntegerAttr>("high");
    if (!hasExactKeys(marks, {"low", "high"}) || !low || !high ||
        !low.getType().isSignlessInteger(64) ||
        !high.getType().isSignlessInteger(64) || low.getInt() < 0 ||
        low.getInt() >= high.getInt() || high.getInt() > entryCapacity)
      return emitOpError(
          "watermarks require 0 <= low < high <= entry capacity");
  }
  auto protocol =
      dyn_cast_or_null<ProtocolOp>(lookupOuter(*this, getProtocolAttr()));
  if (!protocol)
    return emitOpError() << "endpoint protocol '" << getProtocolAttr()
                         << "' is unresolved";
  StringRef protocolOrdering = "unordered";
  bool hasCorrelation = false;
  for (Operation &operation : protocol.getBody().front()) {
    auto guarantee = dyn_cast<GuaranteeOp>(operation);
    if (!guarantee)
      continue;
    if (guarantee.getKind() == "ordering")
      protocolOrdering = cast<StringAttr>(guarantee.getValue()).getValue();
    else if (guarantee.getKind() == "correlation")
      hasCorrelation = true;
  }
  auto orderingStrength = [](StringRef ordering) {
    return ordering == "fifo" ? 2 : ordering == "per_key" ? 1 : 0;
  };
  if (orderingStrength(getOrdering()) < orderingStrength(protocolOrdering))
    return emitOpError() << "queue ordering '" << getOrdering()
                         << "' weakens protocol ordering '" << protocolOrdering
                         << "'";
  if (getOrdering() == "per_key" && !hasCorrelation)
    return emitOpError(
        "per_key queue storage requires protocol correlation semantics");
  bool carrier =
      llvm::any_of(protocol.getBody().getOps<EventOp>(), [&](EventOp event) {
        return event.getPayload() == getPayload() &&
               (event.getAction() == "offer" ||
                event.getAction() == "response" ||
                event.getAction() == "notify");
      });
  if (!carrier)
    return emitOpError("queue payload does not match endpoint protocol schema");
  return success();
}

LogicalResult EventQueueOp::verify() {
  if (failed(verifyOwner(*this, getSymName(), getStableId(), getPath(),
                         getDelayTicksAttr().getInt())))
    return failure();
  if (getCapacityAttr().getInt() <= 0)
    return emitOpError("event queue capacity must be positive");
  if (!isa<EventType>(getPayload()))
    return emitOpError("event queue payload must be an exact !ac.event type");
  if (getOrdering() != "time_then_sequence")
    return emitOpError("ordering must be exactly 'time_then_sequence'");
  return success();
}

LogicalResult ResourceOp::verify() {
  if (failed(verifyOwner(*this, getSymName(), getStableId(), getPath(),
                         getDelayTicksAttr().getInt())))
    return failure();
  int64_t capacity = getCapacityAttr().getInt();
  int64_t issueWidth = getIssueWidthAttr().getInt();
  if (capacity <= 0)
    return emitOpError("resource capacity must be positive");
  if (issueWidth <= 0 || issueWidth > capacity)
    return emitOpError("issue width must be in [1, capacity]");
  if (getInitiationIntervalAttr().getInt() < 1)
    return emitOpError("initiation interval must be at least one global tick");
  DictionaryAttr latency = getLatencyModel();
  auto kind = latency.getAs<StringAttr>("kind");
  if (!kind)
    return emitOpError("latency model requires an exact kind");
  if (kind.getValue() == "fixed") {
    if (!hasExactKeys(latency, {"kind", "ticks"}))
      return emitOpError(
          "fixed latency model requires exact kind/ticks schema");
    if (failed(positiveI64(*this, latency, "ticks",
                           "fixed latency ticks must be positive")))
      return failure();
  } else if (kind.getValue() == "symbol") {
    auto reference = latency.getAs<SymbolRefAttr>("ref");
    Operation *target = reference ? lookupOuter(*this, reference) : nullptr;
    if (!hasExactKeys(latency, {"kind", "ref"}) || !reference ||
        !isa_and_nonnull<ModuleOp, ModuleExternOp, ModuleGeneratedOp>(target))
      return emitOpError("symbol latency model reference is unresolved");
  } else {
    return emitOpError("latency model kind must be 'fixed' or 'symbol'");
  }
  if (failed(verifyLifecycle(*this)))
    return failure();
  if (getOwnership() != "exclusive" && getOwnership() != "shared" &&
      getOwnership() != "contested")
    return emitOpError(
        "resource ownership must be exclusive, shared, or contested");
  FlatSymbolRefAttr arbiter = getArbitrationOwnerAttr();
  bool requiresArbiter = getOwnership() != "exclusive";
  if (requiresArbiter != static_cast<bool>(arbiter))
    return emitOpError(
        requiresArbiter
            ? "shared or contested resource requires one arbitration owner"
            : "exclusive resource cannot declare an arbitration owner");
  DenseSet<Attribute> classes;
  for (Attribute attribute : getTransactionClasses()) {
    auto reference = dyn_cast<SymbolRefAttr>(attribute);
    if (!reference || !isa_and_nonnull<TransactionOp>(
                          reference ? lookupOuter(*this, reference) : nullptr))
      return emitOpError() << "transaction class '" << attribute
                           << "' is unresolved";
    if (!classes.insert(attribute).second)
      return emitOpError() << "duplicate transaction class '" << attribute
                           << "'";
  }
  return success();
}

LogicalResult AddressSpaceOp::verify() {
  if (failed(verifyPlacement(*this)))
    return failure();
  if (!isStableSegment(getSymName()) || !isStableSegment(getStableId()) ||
      !isStableSegment(getPath()))
    return emitOpError("address-space name, stable id, and path must be stable "
                       "local segments");
  int64_t addressWidth = getAddressWidthAttr().getInt();
  if (addressWidth < 1 || addressWidth > 64)
    return emitOpError("address width must be in [1, 64]");
  if (getAddressUnit() != "byte" && getAddressUnit() != "bit")
    return emitOpError("address unit must be exactly 'byte' or 'bit'");
  if (Attribute layout = getDataLayoutAttr();
      layout && !isa<DataLayoutSpecInterface>(layout))
    return emitOpError(
        "data layout hook must implement DataLayoutSpecInterface");
  FlatSymbolRefAttr parent = getParentAttr();
  DictionaryAttr translation = getTranslationAttr();
  if (static_cast<bool>(parent) != static_cast<bool>(translation))
    return emitOpError(
        "parent address space and translation must appear together");
  if (!parent)
    return success();
  auto numerator = translation.getAs<IntegerAttr>("numerator");
  auto denominator = translation.getAs<IntegerAttr>("denominator");
  auto offset = translation.getAs<IntegerAttr>("offset");
  if (!numerator || !denominator || !offset ||
      !numerator.getType().isSignlessInteger(64) ||
      !denominator.getType().isSignlessInteger(64) ||
      !offset.getType().isSignlessInteger(64) ||
      numerator.getValue().isZero() || denominator.getValue().isZero())
    return emitOpError(
        "translation values must be unsigned signless i64 with positive ratio");
  uint64_t n = numerator.getValue().getZExtValue();
  uint64_t d = denominator.getValue().getZExtValue();
  if (std::gcd(n, d) != 1)
    return emitOpError(
        "translation rational must be in canonical reduced form");
  if (d == 1) {
    if (!hasExactKeys(translation, {"numerator", "denominator", "offset"}))
      return emitOpError("integral translation requires exact "
                         "numerator/denominator/offset schema");
  } else {
    auto alignment = translation.getAs<IntegerAttr>("alignment");
    if (!hasExactKeys(translation,
                      {"numerator", "denominator", "offset", "alignment"}) ||
        !alignment || !alignment.getType().isSignlessInteger(64) ||
        alignment.getValue().isZero() ||
        (static_cast<WideAddress>(alignment.getValue().getZExtValue()) * n) %
                d !=
            0)
      return emitOpError("fractional translation requires exact positive "
                         "alignment proving divisibility");
  }
  return success();
}

LogicalResult AddressMapOp::verify() {
  if (failed(verifyPlacement(*this)))
    return failure();
  if (!isStableSegment(getSymName()))
    return emitOpError("address-map name must be one stable local segment");
  return success();
}

LogicalResult TimeDomainOp::verify() {
  if (failed(verifyPlacement(*this)))
    return failure();
  if (!isStableSegment(getSymName()))
    return emitOpError("time-domain name must be one stable local segment");
  int64_t period = getPeriodAttr().getInt();
  int64_t phase = getPhaseAttr().getInt();
  int64_t scale = getTickScaleAttr().getInt();
  if (period <= 0)
    return emitOpError("period must be positive global ticks");
  if (phase < 0)
    return emitOpError("phase must be non-negative global ticks");
  if (scale <= 0 || static_cast<uint64_t>(scale) > kMaxTickScale)
    return emitOpError("tick scale exceeds implementation capability");
  FlatSymbolRefAttr parent = getParentAttr();
  DictionaryAttr bridge = getBridgeAttr();
  if (static_cast<bool>(parent) != static_cast<bool>(bridge))
    return emitOpError(
        "cross-domain parent relation requires explicit bridge metadata");
  if (!parent)
    return success();
  auto kind = bridge.getAs<StringAttr>("kind");
  auto owner = bridge.getAs<FlatSymbolRefAttr>("owner");
  if (!hasExactKeys(bridge, {"kind", "owner"}) || !kind ||
      kind.getValue() != "explicit" || !owner)
    return emitOpError("bridge requires exact {kind = \"explicit\", owner = "
                       "local symbol} schema");
  return success();
}

namespace {

Operation *lookupIndexed(const llvm::StringMap<Operation *> &producerIndex,
                         FlatSymbolRefAttr reference) {
  if (!reference)
    return nullptr;
  auto found = producerIndex.find(reference.getValue());
  return found == producerIndex.end() ? nullptr : found->second;
}

FailureOr<uint64_t> readUnsignedI64(Operation *op, IntegerAttr attribute,
                                    StringRef diagnostic) {
  if (!attribute || !attribute.getType().isSignlessInteger(64)) {
    op->emitOpError(diagnostic);
    return failure();
  }
  return attribute.getValue().getZExtValue();
}

WideAddress addressLimit(unsigned width) { return WideAddress{1} << width; }

LogicalResult
verifyAddressTranslation(AddressSpaceOp child,
                         const llvm::StringMap<Operation *> &producerIndex) {
  FlatSymbolRefAttr parentRef = child.getParentAttr();
  if (!parentRef)
    return success();
  auto parent =
      dyn_cast_or_null<AddressSpaceOp>(lookupIndexed(producerIndex, parentRef));
  if (!parent)
    return child.emitOpError()
           << "parent address space '" << parentRef << "' is unresolved";

  DictionaryAttr translation = child.getTranslationAttr();
  uint64_t numerator =
      translation.getAs<IntegerAttr>("numerator").getValue().getZExtValue();
  uint64_t denominator =
      translation.getAs<IntegerAttr>("denominator").getValue().getZExtValue();
  uint64_t offset =
      translation.getAs<IntegerAttr>("offset").getValue().getZExtValue();
  uint64_t alignment = denominator == 1
                           ? 1
                           : translation.getAs<IntegerAttr>("alignment")
                                 .getValue()
                                 .getZExtValue();

  // Normative formula, in declared address units:
  //   parent = child * numerator / denominator + offset
  // Fractional schemas constrain legal child addresses to multiples of
  // alignment, which makes the division exact for every legal input. Unit
  // conversion is encoded in the rational itself (byte -> bit is 8/1).
  WideAddress childMaximum =
      addressLimit(child.getAddressWidthAttr().getInt()) - 1;
  WideAddress legalMaximum = childMaximum - childMaximum % alignment;
  WideAddress translated = legalMaximum * numerator;
  if (translated % denominator != 0)
    return child.emitOpError(
        "translation alignment does not make the full child domain exact");
  translated = translated / denominator + offset;
  if (translated >= addressLimit(parent.getAddressWidthAttr().getInt()))
    return child.emitOpError(
        "translated child address range exceeds parent address width");
  return success();
}

std::string attributeToken(Attribute attribute) {
  std::string storage;
  llvm::raw_string_ostream stream(storage);
  stream << attribute;
  return storage;
}

struct SelectorSummary {
  uint64_t count = 0;
  uint64_t withoutPriority = 0;
  std::map<uint64_t, uint64_t> priorities;
};

struct MapEntry {
  AddressInterval interval;
  AddressMapOrderKey order;
  std::optional<uint64_t> priority;
  SmallVector<std::string> registrationKeys;
  SmallVector<std::string> queryKeys;
};

void appendUnique(SmallVectorImpl<std::string> &keys, std::string key) {
  if (!llvm::is_contained(keys, key))
    keys.push_back(std::move(key));
}

void buildSelectorKeys(ArrayRef<unsigned> permissions,
                       ArrayRef<std::string> classes,
                       std::optional<std::string> lane, MapEntry &entry) {
  bool wildcardClass = classes.empty();
  bool wildcardLane = !lane;
  for (unsigned permission : permissions) {
    std::string prefix = std::to_string(permission) + "|";
    appendUnique(entry.registrationKeys, "G|" + prefix);
    if (wildcardClass)
      appendUnique(entry.registrationKeys, "CW|" + prefix);
    else
      for (const std::string &className : classes)
        appendUnique(entry.registrationKeys, "C|" + prefix + className);
    if (wildcardLane)
      appendUnique(entry.registrationKeys, "LW|" + prefix);
    else
      appendUnique(entry.registrationKeys, "L|" + prefix + *lane);

    if (wildcardClass && wildcardLane) {
      appendUnique(entry.registrationKeys, "CW_LW|" + prefix);
      appendUnique(entry.queryKeys, "G|" + prefix);
    } else if (wildcardClass) {
      appendUnique(entry.registrationKeys, "CW_L|" + prefix + *lane);
      appendUnique(entry.queryKeys, "LW|" + prefix);
      appendUnique(entry.queryKeys, "L|" + prefix + *lane);
    } else if (wildcardLane) {
      for (const std::string &className : classes)
        appendUnique(entry.registrationKeys, "C_LW|" + prefix + className);
      appendUnique(entry.queryKeys, "CW|" + prefix);
      for (const std::string &className : classes)
        appendUnique(entry.queryKeys, "C|" + prefix + className);
    } else {
      for (const std::string &className : classes)
        appendUnique(entry.registrationKeys,
                     "C_L|" + prefix + className + "|" + *lane);
      appendUnique(entry.queryKeys, "CW_LW|" + prefix);
      appendUnique(entry.queryKeys, "CW_L|" + prefix + *lane);
      for (const std::string &className : classes) {
        appendUnique(entry.queryKeys, "C_LW|" + prefix + className);
        appendUnique(entry.queryKeys,
                     "C_L|" + prefix + className + "|" + *lane);
      }
    }
  }
}

LogicalResult
verifyAddressMap(AddressMapOp map,
                 const llvm::StringMap<Operation *> &producerIndex) {
  auto source = dyn_cast_or_null<AddressSpaceOp>(
      lookupIndexed(producerIndex, map.getSourceAttr()));
  if (!source)
    return map.emitOpError() << "source address space '" << map.getSourceAttr()
                             << "' is unresolved";

  DictionaryAttr fallback = map.getDefaultBehavior();
  auto fallbackKind = fallback.getAs<StringAttr>("kind");
  if (fallbackKind && fallbackKind.getValue() == "unmapped") {
    if (!hasExactKeys(fallback, {"kind"}))
      return map.emitOpError(
          "default behavior requires exact unmapped or target schema");
  } else if (fallbackKind && fallbackKind.getValue() == "target") {
    auto target = fallback.getAs<FlatSymbolRefAttr>("target");
    if (!hasExactKeys(fallback, {"kind", "target"}) || !target ||
        !isa_and_nonnull<AddressSpaceOp, InstanceOp, ArrayOp, InstancesOp>(
            lookupIndexed(producerIndex, target)))
      return map.emitOpError("default target behavior is unresolved");
  } else {
    return map.emitOpError(
        "default behavior requires exact unmapped or target schema");
  }

  SmallVector<MapEntry> entries;
  entries.reserve(map.getEntries().size());
  AddressMapOrderKey previous{};
  bool hasPrevious = false;
  WideAddress sourceLimit = addressLimit(source.getAddressWidthAttr().getInt());

  for (Attribute attribute : map.getEntries()) {
    auto dictionary = dyn_cast<DictionaryAttr>(attribute);
    if (!dictionary)
      return map.emitOpError("address-map entries must be dictionaries");
    static constexpr StringLiteral mandatory[] = {
        "base", "size", "target", "offset", "permissions", "classes"};
    for (StringRef key : mandatory)
      if (!dictionary.get(key))
        return map.emitOpError()
               << "address-map entry is missing '" << key << "'";
    for (NamedAttribute value : dictionary)
      if (!llvm::is_contained(ArrayRef<StringRef>{"base", "size", "target",
                                                  "offset", "permissions",
                                                  "classes", "priority",
                                                  "interleave"},
                              value.getName().getValue()))
        return map.emitOpError() << "unknown address-map entry key '"
                                 << value.getName().getValue() << "'";

    auto baseValue = readUnsignedI64(
        map, dictionary.getAs<IntegerAttr>("base"),
        "address base/size/offset require unsigned signless i64 values");
    auto sizeValue = readUnsignedI64(
        map, dictionary.getAs<IntegerAttr>("size"),
        "address base/size/offset require unsigned signless i64 values");
    auto offsetValue = readUnsignedI64(
        map, dictionary.getAs<IntegerAttr>("offset"),
        "address base/size/offset require unsigned signless i64 values");
    if (failed(baseValue) || failed(sizeValue) || failed(offsetValue))
      return failure();
    uint64_t base = *baseValue;
    uint64_t size = *sizeValue;
    uint64_t offset = *offsetValue;
    if (size == 0)
      return map.emitOpError("address-map entry size must be positive");
    WideAddress end = static_cast<WideAddress>(base) + size;
    if (end > sourceLimit)
      return map.emitOpError("address interval exceeds source address width");

    auto targetRef = dictionary.getAs<FlatSymbolRefAttr>("target");
    Operation *target = lookupIndexed(producerIndex, targetRef);
    if (!targetRef ||
        !isa_and_nonnull<AddressSpaceOp, InstanceOp, ArrayOp, InstancesOp>(
            target))
      return map.emitOpError()
             << "address-map target '" << targetRef << "' is unresolved";

    auto permissions = dictionary.getAs<ArrayAttr>("permissions");
    if (!permissions || permissions.empty())
      return map.emitOpError(
          "address-map permissions must be a non-empty closed set");
    llvm::SmallSet<StringRef, 3> permissionSet;
    SmallVector<unsigned> permissionIds;
    for (Attribute permissionAttr : permissions) {
      auto permission = dyn_cast<StringAttr>(permissionAttr);
      if (!permission ||
          !llvm::is_contained(ArrayRef<StringRef>{"read", "write", "execute"},
                              permission.getValue()) ||
          !permissionSet.insert(permission.getValue()).second)
        return map.emitOpError(
            "address-map permissions must be unique read/write/execute values");
      permissionIds.push_back(permission.getValue() == "read"    ? 0
                              : permission.getValue() == "write" ? 1
                                                                 : 2);
    }

    auto classes = dictionary.getAs<ArrayAttr>("classes");
    if (!classes)
      return map.emitOpError("address-map classes must be an array");
    DenseSet<Attribute> classSet;
    SmallVector<std::string> classTokens;
    for (Attribute classAttr : classes) {
      auto reference = dyn_cast<SymbolRefAttr>(classAttr);
      if (!reference || !isa_and_nonnull<TransactionOp>(
                            reference ? lookupOuter(map, reference) : nullptr))
        return map.emitOpError() << "address-map transaction class '"
                                 << classAttr << "' is unresolved";
      if (!classSet.insert(classAttr).second)
        return map.emitOpError("duplicate address-map transaction class");
      classTokens.push_back(attributeToken(classAttr));
    }

    uint64_t targetSpan = size;
    std::optional<std::string> lane;
    if (auto interleave = dictionary.getAs<DictionaryAttr>("interleave")) {
      if (!hasExactKeys(interleave, {"granularity", "banks", "bank"}))
        return map.emitOpError(
            "interleave requires exact granularity/banks/bank schema");
      auto granularityValue =
          readUnsignedI64(map, interleave.getAs<IntegerAttr>("granularity"),
                          "interleave values must be unsigned signless i64");
      auto banksValue =
          readUnsignedI64(map, interleave.getAs<IntegerAttr>("banks"),
                          "interleave values must be unsigned signless i64");
      auto bankValue =
          readUnsignedI64(map, interleave.getAs<IntegerAttr>("bank"),
                          "interleave values must be unsigned signless i64");
      if (failed(granularityValue) || failed(banksValue) || failed(bankValue))
        return failure();
      uint64_t granularity = *granularityValue;
      uint64_t banks = *banksValue;
      uint64_t bank = *bankValue;
      if (!granularity || !banks || bank >= banks)
        return map.emitOpError("interleave bank must be in [0, banks)");
      WideAddress stripeWide = static_cast<WideAddress>(granularity) * banks;
      if (stripeWide > std::numeric_limits<uint64_t>::max())
        return map.emitOpError("interleave geometry exceeds unsigned i64");
      uint64_t stripe = static_cast<uint64_t>(stripeWide);
      if (base % stripe || offset % granularity || size % stripe)
        return map.emitOpError("interleave base/size/offset must satisfy "
                               "stripe alignment and geometry");
      targetSpan = size / banks;
      // ACIR v0.1 defines an interleave lane by the exact canonical
      // (granularity, banks, bank) tuple. A non-interleaved selector is the
      // wildcard over all such lane identities.
      lane = std::to_string(granularity) + ":" + std::to_string(banks) + ":" +
             std::to_string(bank);
    }

    WideAddress targetEnd = static_cast<WideAddress>(offset) + targetSpan;
    if (auto targetSpace = dyn_cast<AddressSpaceOp>(target))
      if (targetEnd > addressLimit(targetSpace.getAddressWidthAttr().getInt()))
        return map.emitOpError(
            "address target range exceeds target address width");

    std::optional<uint64_t> priority;
    if (IntegerAttr priorityAttr = dictionary.getAs<IntegerAttr>("priority")) {
      auto value = readUnsignedI64(
          map, priorityAttr,
          "address-map priority must be an unsigned signless i64");
      if (failed(value))
        return failure();
      priority = *value;
    }
    AddressMapOrderKey order{base, size, priority.has_value(),
                             priority.value_or(0)};
    if (hasPrevious && compareAddressMapOrder(previous, order) > 0)
      return map.emitOpError(
          "address-map entries must be in deterministic base order");
    previous = order;
    hasPrevious = true;
    entries.push_back({{base, end}, order, priority});
    buildSelectorKeys(permissionIds, classTokens, lane, entries.back());
  }

  std::map<std::string, SelectorSummary> selectorIndex;
  std::multimap<WideAddress, const MapEntry *> activeByEnd;
  auto update = [&](const MapEntry &entry, bool add) {
    for (const std::string &key : entry.registrationKeys) {
      SelectorSummary &summary = selectorIndex[key];
      if (add) {
        ++summary.count;
        if (entry.priority)
          ++summary.priorities[*entry.priority];
        else
          ++summary.withoutPriority;
      } else {
        --summary.count;
        if (entry.priority) {
          auto priority = summary.priorities.find(*entry.priority);
          if (--priority->second == 0)
            summary.priorities.erase(priority);
        } else {
          --summary.withoutPriority;
        }
      }
    }
  };
  for (const MapEntry &entry : entries) {
    while (!activeByEnd.empty() &&
           activeByEnd.begin()->first <= entry.interval.begin) {
      update(*activeByEnd.begin()->second, false);
      activeByEnd.erase(activeByEnd.begin());
    }
    for (const std::string &key : entry.queryKeys) {
      auto found = selectorIndex.find(key);
      if (found == selectorIndex.end() || found->second.count == 0)
        continue;
      if (!entry.priority || found->second.withoutPriority)
        return map.emitOpError("overlapping selector intersections require "
                               "explicit distinct priorities");
      if (found->second.priorities.contains(*entry.priority))
        return map.emitOpError(
            "overlapping selector intersections have equal priority");
    }
    update(entry, true);
    activeByEnd.emplace(entry.interval.end, &entry);
  }
  return success();
}

} // namespace

LogicalResult verifyModuleResourceReferences(
    Operation *operation, const llvm::StringMap<Operation *> &producerIndex) {
  auto module = dyn_cast<ModuleOp>(operation);
  if (!module)
    return success();
  for (Operation &child : module.getBody().front()) {
    if (auto eventQueue = dyn_cast<EventQueueOp>(child)) {
      if (!isa_and_nonnull<TimeDomainOp>(
              lookupIndexed(producerIndex, eventQueue.getTimeDomainAttr())))
        return eventQueue.emitOpError()
               << "time domain '" << eventQueue.getTimeDomainAttr()
               << "' is unresolved";
    } else if (auto resource = dyn_cast<ResourceOp>(child)) {
      if (FlatSymbolRefAttr arbiter = resource.getArbitrationOwnerAttr();
          arbiter && !isa_and_nonnull<InstanceOp, ArrayOp, InstancesOp>(
                         lookupIndexed(producerIndex, arbiter)))
        return resource.emitOpError()
               << "arbitration owner '" << arbiter << "' is unresolved";
    } else if (auto addressSpace = dyn_cast<AddressSpaceOp>(child)) {
      if (failed(verifyAddressTranslation(addressSpace, producerIndex)))
        return failure();
    } else if (auto addressMap = dyn_cast<AddressMapOp>(child)) {
      if (failed(verifyAddressMap(addressMap, producerIndex)))
        return failure();
    } else if (auto domain = dyn_cast<TimeDomainOp>(child)) {
      if (!domain.getParentAttr())
        continue;
      if (!isa_and_nonnull<TimeDomainOp>(
              lookupIndexed(producerIndex, domain.getParentAttr())))
        return domain.emitOpError()
               << "parent time domain '" << domain.getParentAttr()
               << "' is unresolved";
      FlatSymbolRefAttr owner =
          domain.getBridgeAttr().getAs<FlatSymbolRefAttr>("owner");
      if (!isa_and_nonnull<InstanceOp, ArrayOp, InstancesOp>(
              lookupIndexed(producerIndex, owner)))
        return domain.emitOpError(
            "bridge owner must resolve to a local structural owner");
    }
  }
  if (failed(verifyParentCycles(module, false, producerIndex)) ||
      failed(verifyParentCycles(module, true, producerIndex)))
    return failure();
  return success();
}

} // namespace acir::ac
