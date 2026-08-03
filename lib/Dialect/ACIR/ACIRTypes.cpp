#include "acir/Dialect/ACIR/ACIRDialect.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;

namespace acir::ac {
namespace {

bool isTimeUnit(Unit unit) {
  switch (unit) {
  case Unit::Ticks:
  case Unit::Cycles:
  case Unit::Seconds:
  case Unit::Milliseconds:
  case Unit::Microseconds:
  case Unit::Nanoseconds:
  case Unit::Picoseconds:
    return true;
  case Unit::Bytes:
  case Unit::Bits:
  case Unit::Entries:
  case Unit::Packets:
  case Unit::Transactions:
    return false;
  }
  llvm_unreachable("unknown ACIR unit");
}

bool containsChannelTypeImpl(Type type) {
  if (isa<ChannelType>(type))
    return true;
  if (auto optional = dyn_cast<OptionalType>(type))
    return containsChannelTypeImpl(optional.getElementType());
  if (auto list = dyn_cast<ListType>(type))
    return containsChannelTypeImpl(list.getElementType());
  if (auto vector = dyn_cast<VectorType>(type))
    return containsChannelTypeImpl(vector.getElementType());
  if (auto flow = dyn_cast<FlowType>(type))
    return containsChannelTypeImpl(flow.getElementType());
  if (auto event = dyn_cast<EventType>(type))
    return containsChannelTypeImpl(event.getElementType());
  return false;
}

LogicalResult verifyValueElement(function_ref<InFlightDiagnostic()> emitError,
                                 Type elementType) {
  if (!containsChannelTypeImpl(elementType))
    return success();
  return emitError() << "channel types cannot be nested inside value types";
}

} // namespace

bool containsChannelType(Type type) { return containsChannelTypeImpl(type); }

LogicalResult OptionalType::verify(function_ref<InFlightDiagnostic()> emitError,
                                   Type elementType) {
  return verifyValueElement(emitError, elementType);
}

LogicalResult ListType::verify(function_ref<InFlightDiagnostic()> emitError,
                               Type elementType) {
  return verifyValueElement(emitError, elementType);
}

LogicalResult VectorType::verify(function_ref<InFlightDiagnostic()> emitError,
                                 int64_t length, Type elementType) {
  if (length <= 0)
    return emitError() << "vector length must be positive";
  return verifyValueElement(emitError, elementType);
}

LogicalResult FlowType::verify(function_ref<InFlightDiagnostic()> emitError,
                               Type elementType, FlatSymbolRefAttr) {
  return verifyValueElement(emitError, elementType);
}

LogicalResult ChannelType::verify(function_ref<InFlightDiagnostic()> emitError,
                                  Type elementType, FlatSymbolRefAttr) {
  if (!containsChannelTypeImpl(elementType))
    return success();
  return emitError() << "channel types cannot carry channel types";
}

LogicalResult DurationType::verify(function_ref<InFlightDiagnostic()> emitError,
                                   Unit unit) {
  if (isTimeUnit(unit))
    return success();
  return emitError() << "duration requires a time unit";
}

LogicalResult RateType::verify(function_ref<InFlightDiagnostic()> emitError,
                               Unit numerator, Unit denominator) {
  if (isTimeUnit(numerator))
    return emitError() << "rate numerator must be a data unit";
  if (!isTimeUnit(denominator))
    return emitError() << "rate denominator must be a time unit";
  return success();
}

LogicalResult EventType::verify(function_ref<InFlightDiagnostic()> emitError,
                                Type elementType) {
  return verifyValueElement(emitError, elementType);
}

} // namespace acir::ac

#include "acir/Dialect/ACIR/ACIREnums.cpp.inc"

#define GET_ATTRDEF_CLASSES
#include "acir/Dialect/ACIR/ACIRAttributes.cpp.inc"

#define GET_TYPEDEF_CLASSES
#include "acir/Dialect/ACIR/ACIRTypes.cpp.inc"

void acir::ac::ACIRDialect::initialize() {
  addAttributes<
#define GET_ATTRDEF_LIST
#include "acir/Dialect/ACIR/ACIRAttributes.cpp.inc"
      >();
  addTypes<
#define GET_TYPEDEF_LIST
#include "acir/Dialect/ACIR/ACIRTypes.cpp.inc"
      >();
}
