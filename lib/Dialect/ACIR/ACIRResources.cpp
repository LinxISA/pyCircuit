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

Operation *lookupLocal(Operation *from, FlatSymbolRefAttr reference) {
  auto module = from->getParentOfType<ModuleOp>();
  if (!module || reference.getValue().empty())
    return nullptr;
  for (Operation &candidate : module.getBody().front()) {
    auto name =
        candidate.getAttrOfType<StringAttr>(SymbolTable::getSymbolAttrName());
    if (name && name.getValue() == reference.getValue())
      return &candidate;
  }
  return nullptr;
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

LogicalResult verifyParentCycles(ModuleOp module, bool timeDomains) {
  SmallVector<Operation *> nodes;
  DenseMap<StringRef, Operation *> byName;
  for (Operation &operation : module.getBody().front()) {
    bool selected = timeDomains ? isa<TimeDomainOp>(operation)
                                : isa<AddressSpaceOp>(operation);
    if (!selected)
      continue;
    nodes.push_back(&operation);
    byName[operation.getAttrOfType<StringAttr>(SymbolTable::getSymbolAttrName())
               .getValue()] = &operation;
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
      current = parent ? byName.lookup(parent.getValue()) : nullptr;
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
  if (left.priority != right.priority)
    return left.priority > right.priority ? -1 : 1;
  if (left.size != right.size)
    return left.size < right.size ? -1 : 1;
  return 0;
}

bool normalizeRationalToTicks(uint64_t numerator, uint64_t denominator,
                              uint64_t quantumNumerator,
                              uint64_t quantumDenominator, uint64_t &ticks) {
  if (!denominator || !quantumNumerator || !quantumDenominator)
    return false;
  uint64_t leftGcd = std::gcd(numerator, denominator);
  numerator /= leftGcd;
  denominator /= leftGcd;
  uint64_t crossGcd = std::gcd(quantumDenominator, denominator);
  quantumDenominator /= crossGcd;
  denominator /= crossGcd;
  uint64_t quantumGcd = std::gcd(numerator, quantumNumerator);
  numerator /= quantumGcd;
  quantumNumerator /= quantumGcd;
  uint64_t divisor = 0;
  if (!checkedMultiply(denominator, quantumNumerator, divisor) || !divisor)
    return false;
  uint64_t product = 0;
  if (!checkedMultiply(numerator, quantumDenominator, product) ||
      product % divisor != 0)
    return false;
  ticks = product / divisor;
  return ticks <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
}

void QueueOp::getEffects(
    SmallVectorImpl<MemoryEffects::EffectInstance> &effects) {
  effects.emplace_back(MemoryEffects::Write::get(), QueueStateResource::get());
}

void EventQueueOp::getEffects(
    SmallVectorImpl<MemoryEffects::EffectInstance> &effects) {
  effects.emplace_back(MemoryEffects::Write::get(),
                       EventQueueStateResource::get());
}

void ResourceOp::getEffects(
    SmallVectorImpl<MemoryEffects::EffectInstance> &effects) {
  effects.emplace_back(MemoryEffects::Write::get(),
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
  Operation *protocol = lookupOuter(*this, getProtocolAttr());
  if (!isa_and_nonnull<ProtocolOp>(protocol))
    return emitOpError() << "endpoint protocol '" << getProtocolAttr()
                         << "' is unresolved";
  bool carrier =
      llvm::any_of(cast<ProtocolOp>(protocol).getBody().getOps<EventOp>(),
                   [&](EventOp event) {
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
  if (!isa_and_nonnull<TimeDomainOp>(lookupLocal(*this, getTimeDomainAttr())))
    return emitOpError() << "time domain '" << getTimeDomainAttr()
                         << "' is unresolved";
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
  Operation *arbiterTarget = arbiter ? lookupLocal(*this, arbiter) : nullptr;
  if (arbiter &&
      !isa_and_nonnull<InstanceOp, ArrayOp, InstancesOp>(arbiterTarget))
    return emitOpError() << "arbitration owner '" << arbiter
                         << "' is unresolved";
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
  auto target = dyn_cast_or_null<AddressSpaceOp>(lookupLocal(*this, parent));
  if (!target)
    return emitOpError() << "parent address space '" << parent
                         << "' is unresolved";
  if (target == *this)
    return emitOpError("address-space parent cycle: self reference");
  int64_t targetWidth = target.getAddressWidthAttr().getInt();
  if (addressWidth > targetWidth)
    return emitOpError("parent address width is not translation-compatible");
  if (getAddressUnit() != target.getAddressUnit())
    return emitOpError("parent address unit is not translation-compatible");
  if (!hasExactKeys(translation, {"numerator", "denominator", "offset"}))
    return emitOpError(
        "translation requires exact numerator/denominator/offset schema");
  auto numerator = translation.getAs<IntegerAttr>("numerator");
  auto denominator = translation.getAs<IntegerAttr>("denominator");
  auto offset = translation.getAs<IntegerAttr>("offset");
  if (!numerator || !denominator || !offset ||
      !numerator.getType().isSignlessInteger(64) ||
      !denominator.getType().isSignlessInteger(64) ||
      !offset.getType().isSignlessInteger(64) || numerator.getInt() <= 0 ||
      denominator.getInt() <= 0 || offset.getInt() < 0)
    return emitOpError(
        "translation values must be exact non-negative signless i64 rationals");
  uint64_t childMaximum = addressWidth == 64
                              ? std::numeric_limits<uint64_t>::max()
                              : (uint64_t{1} << addressWidth) - 1;
  uint64_t parentMaximum = targetWidth == 64
                               ? std::numeric_limits<uint64_t>::max()
                               : (uint64_t{1} << targetWidth) - 1;
  unsigned __int128 translated =
      static_cast<unsigned __int128>(childMaximum) * numerator.getInt();
  translated /= denominator.getInt();
  translated += offset.getInt();
  if (translated > parentMaximum)
    return emitOpError("parent address width is not translation-compatible");
  return success();
}

LogicalResult AddressMapOp::verify() {
  if (failed(verifyPlacement(*this)))
    return failure();
  if (!isStableSegment(getSymName()))
    return emitOpError("address-map name must be one stable local segment");
  auto source =
      dyn_cast_or_null<AddressSpaceOp>(lookupLocal(*this, getSourceAttr()));
  if (!source)
    return emitOpError() << "source address space '" << getSourceAttr()
                         << "' is unresolved";
  DictionaryAttr fallback = getDefaultBehavior();
  auto fallbackKind = fallback.getAs<StringAttr>("kind");
  if (fallbackKind && fallbackKind.getValue() == "unmapped") {
    if (!hasExactKeys(fallback, {"kind"}))
      return emitOpError(
          "default behavior requires exact unmapped or target schema");
  } else if (fallbackKind && fallbackKind.getValue() == "target") {
    auto target = fallback.getAs<FlatSymbolRefAttr>("target");
    Operation *targetOperation = target ? lookupLocal(*this, target) : nullptr;
    if (!hasExactKeys(fallback, {"kind", "target"}) || !target ||
        !isa_and_nonnull<AddressSpaceOp, InstanceOp, ArrayOp, InstancesOp>(
            targetOperation))
      return emitOpError("default target behavior is unresolved");
  } else {
    return emitOpError(
        "default behavior requires exact unmapped or target schema");
  }

  struct Entry {
    AddressInterval interval;
    std::optional<int64_t> priority;
    AddressMapOrderKey order;
  };
  SmallVector<Entry> entries;
  AddressMapOrderKey previous{};
  bool hasPrevious = false;
  uint64_t sourceMaximum =
      source.getAddressWidthAttr().getInt() == 64
          ? std::numeric_limits<uint64_t>::max()
          : (uint64_t{1} << source.getAddressWidthAttr().getInt()) - 1;
  for (Attribute attribute : getEntries()) {
    auto dictionary = dyn_cast<DictionaryAttr>(attribute);
    if (!dictionary)
      return emitOpError("address-map entries must be dictionaries");
    static constexpr StringLiteral mandatory[] = {
        "base", "size", "target", "offset", "permissions", "classes"};
    for (StringRef key : mandatory)
      if (!dictionary.get(key))
        return emitOpError() << "address-map entry is missing '" << key << "'";
    for (NamedAttribute value : dictionary)
      if (!llvm::is_contained(ArrayRef<StringRef>{"base", "size", "target",
                                                  "offset", "permissions",
                                                  "classes", "priority",
                                                  "interleave"},
                              value.getName().getValue()))
        return emitOpError() << "unknown address-map entry key '"
                             << value.getName().getValue() << "'";
    auto base = dictionary.getAs<IntegerAttr>("base");
    auto size = dictionary.getAs<IntegerAttr>("size");
    auto offset = dictionary.getAs<IntegerAttr>("offset");
    auto targetRef = dictionary.getAs<FlatSymbolRefAttr>("target");
    if (!base || !size || !offset || !base.getType().isSignlessInteger(64) ||
        !size.getType().isSignlessInteger(64) ||
        !offset.getType().isSignlessInteger(64) || base.getInt() < 0 ||
        size.getInt() <= 0 || offset.getInt() < 0)
      return emitOpError("address base/size/offset require non-negative i64 "
                         "and positive size");
    uint64_t end = 0;
    if (!checkedAdd(base.getInt(), size.getInt(), end) ||
        end > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
      return emitOpError("address interval overflows signed 64-bit range");
    if (end - 1 > sourceMaximum)
      return emitOpError("address interval exceeds source address width");
    Operation *target = targetRef ? lookupLocal(*this, targetRef) : nullptr;
    if (!isa_and_nonnull<AddressSpaceOp, InstanceOp, ArrayOp, InstancesOp>(
            target))
      return emitOpError() << "address-map target '" << targetRef
                           << "' is unresolved";
    uint64_t targetEnd = 0;
    if (!checkedAdd(static_cast<uint64_t>(offset.getInt()),
                    static_cast<uint64_t>(size.getInt()), targetEnd) ||
        targetEnd > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
      return emitOpError(
          "address target offset range overflows signed 64-bit range");
    if (auto targetSpace = dyn_cast<AddressSpaceOp>(target)) {
      uint64_t targetMaximum =
          targetSpace.getAddressWidthAttr().getInt() == 64
              ? std::numeric_limits<uint64_t>::max()
              : (uint64_t{1} << targetSpace.getAddressWidthAttr().getInt()) - 1;
      if (targetEnd - 1 > targetMaximum)
        return emitOpError("address target range exceeds target address width");
    }
    auto permissions = dictionary.getAs<ArrayAttr>("permissions");
    if (!permissions || permissions.empty())
      return emitOpError(
          "address-map permissions must be a non-empty closed set");
    llvm::SmallSet<StringRef, 3> permissionSet;
    for (Attribute permissionAttr : permissions) {
      auto permission = dyn_cast<StringAttr>(permissionAttr);
      if (!permission ||
          !llvm::is_contained(ArrayRef<StringRef>{"read", "write", "execute"},
                              permission.getValue()) ||
          !permissionSet.insert(permission.getValue()).second)
        return emitOpError(
            "address-map permissions must be unique read/write/execute values");
    }
    auto classes = dictionary.getAs<ArrayAttr>("classes");
    if (!classes)
      return emitOpError("address-map classes must be an array");
    DenseSet<Attribute> classSet;
    for (Attribute classAttr : classes) {
      auto reference = dyn_cast<SymbolRefAttr>(classAttr);
      if (!reference ||
          !isa_and_nonnull<TransactionOp>(
              reference ? lookupOuter(*this, reference) : nullptr))
        return emitOpError() << "address-map transaction class '" << classAttr
                             << "' is unresolved";
      if (!classSet.insert(classAttr).second)
        return emitOpError("duplicate address-map transaction class");
    }
    if (auto interleave = dictionary.getAs<DictionaryAttr>("interleave")) {
      if (!hasExactKeys(interleave, {"granularity", "banks", "bank"}))
        return emitOpError(
            "interleave requires exact granularity/banks/bank schema");
      auto granularity = interleave.getAs<IntegerAttr>("granularity");
      auto banks = interleave.getAs<IntegerAttr>("banks");
      auto bank = interleave.getAs<IntegerAttr>("bank");
      if (!granularity || !banks || !bank ||
          !granularity.getType().isSignlessInteger(64) ||
          !banks.getType().isSignlessInteger(64) ||
          !bank.getType().isSignlessInteger(64) || granularity.getInt() <= 0 ||
          banks.getInt() <= 0 || bank.getInt() < 0 ||
          bank.getInt() >= banks.getInt())
        return emitOpError("interleave bank must be in [0, banks)");
      uint64_t stripe = 0;
      if (!checkedMultiply(granularity.getInt(), banks.getInt(), stripe) ||
          size.getInt() % stripe != 0)
        return emitOpError(
            "interleave size must be a multiple of granularity*banks");
    }
    std::optional<int64_t> priority;
    if (auto priorityAttr = dictionary.getAs<IntegerAttr>("priority")) {
      if (!priorityAttr.getType().isSignlessInteger(64) ||
          priorityAttr.getInt() < 0)
        return emitOpError(
            "address-map priority must be a non-negative signless i64");
      priority = priorityAttr.getInt();
    }
    AddressMapOrderKey order{static_cast<uint64_t>(base.getInt()),
                             static_cast<uint64_t>(size.getInt()),
                             priority.value_or(-1)};
    if (hasPrevious && compareAddressMapOrder(previous, order) > 0)
      return emitOpError(
          "address-map entries must be in deterministic base order");
    previous = order;
    hasPrevious = true;
    entries.push_back({{order.base, end}, priority, order});
  }
  std::multimap<uint64_t, std::optional<int64_t>> activeByEnd;
  std::set<int64_t> activePriorities;
  unsigned activeWithoutPriority = 0;
  for (const Entry &entry : entries) {
    while (!activeByEnd.empty() &&
           activeByEnd.begin()->first <= entry.interval.begin) {
      if (activeByEnd.begin()->second)
        activePriorities.erase(*activeByEnd.begin()->second);
      else
        --activeWithoutPriority;
      activeByEnd.erase(activeByEnd.begin());
    }
    if (!activeByEnd.empty()) {
      if (!entry.priority || activeWithoutPriority)
        return emitOpError(
            "overlapping entries require explicit distinct priorities");
      if (activePriorities.contains(*entry.priority))
        return emitOpError("overlapping entries have equal priority");
    }
    activeByEnd.emplace(entry.interval.end, entry.priority);
    if (entry.priority)
      activePriorities.insert(*entry.priority);
    else
      ++activeWithoutPriority;
  }
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
  uint64_t normalized = 0;
  if (!checkedMultiply(static_cast<uint64_t>(period),
                       static_cast<uint64_t>(scale), normalized) ||
      !checkedAdd(normalized, static_cast<uint64_t>(phase), normalized) ||
      normalized > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
    return emitOpError("time-domain scaled tick arithmetic overflows i64");
  FlatSymbolRefAttr parent = getParentAttr();
  DictionaryAttr bridge = getBridgeAttr();
  if (static_cast<bool>(parent) != static_cast<bool>(bridge))
    return emitOpError(
        "cross-domain parent relation requires explicit bridge metadata");
  if (!parent)
    return success();
  auto target = dyn_cast_or_null<TimeDomainOp>(lookupLocal(*this, parent));
  if (!target)
    return emitOpError() << "parent time domain '" << parent
                         << "' is unresolved";
  if (target == *this)
    return emitOpError("time-domain parent cycle: self reference");
  auto kind = bridge.getAs<StringAttr>("kind");
  auto owner = bridge.getAs<FlatSymbolRefAttr>("owner");
  Operation *ownerTarget = owner ? lookupLocal(*this, owner) : nullptr;
  if (!hasExactKeys(bridge, {"kind", "owner"}) || !kind ||
      kind.getValue() != "explicit" || !owner ||
      !isa_and_nonnull<InstanceOp, ArrayOp, InstancesOp>(ownerTarget))
    return emitOpError("bridge requires exact {kind = \"explicit\", owner = "
                       "local symbol} schema");
  return success();
}

LogicalResult verifyResourceStructure(Operation *topLevel) {
  auto file = dyn_cast<mlir::ModuleOp>(topLevel);
  if (!file || file->getParentOp())
    return success();
  for (ModuleOp module : file.getOps<ModuleOp>()) {
    if (failed(verifyParentCycles(module, false)) ||
        failed(verifyParentCycles(module, true)))
      return failure();
  }
  return success();
}

} // namespace acir::ac
