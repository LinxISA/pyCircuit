#include "acir/Dialect/ACIR/ACIROps.h"

#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;

namespace acir::ac {
namespace {

LogicalResult uniqueStrings(Operation *op, ArrayAttr values, StringRef noun) {
  llvm::SmallDenseSet<StringRef> seen;
  for (Attribute value : values) {
    StringRef text = cast<StringAttr>(value).getValue();
    if (!seen.insert(text).second)
      return op->emitOpError() << "duplicate " << noun << " '" << text << "'";
  }
  return success();
}

LogicalResult fields(Operation *op, ArrayAttr names, ArrayAttr types) {
  if (names.size() != types.size())
    return op->emitOpError("field name and type counts must match");
  return uniqueStrings(op, names, "field");
}

struct NamedRef {
  FlatSymbolRefAttr name;
  StringRef opName;
};

std::optional<NamedRef> namedRef(Type type) {
  return TypeSwitch<Type, std::optional<NamedRef>>(type)
      .Case<StructType>([](auto t) {
        return NamedRef{t.getName(), StructOp::getOperationName()};
      })
      .Case<PacketType>([](auto t) {
        return NamedRef{t.getName(), PacketOp::getOperationName()};
      })
      .Case<TransactionType>([](auto t) {
        return NamedRef{t.getName(), TransactionOp::getOperationName()};
      })
      .Case<EnumType>([](auto t) {
        return NamedRef{t.getName(), EnumOp::getOperationName()};
      })
      .Case<UnionType>([](auto t) {
        return NamedRef{t.getName(), UnionOp::getOperationName()};
      })
      .Default([](Type) { return std::nullopt; });
}

Operation *lookup(Operation *from, FlatSymbolRefAttr name) {
  return SymbolTable::lookupNearestSymbolFrom(from, name);
}

LogicalResult verifyNamedTypes(Operation *from, Type type) {
  LogicalResult result = success();
  type.walk([&](Type nested) {
    auto ref = namedRef(nested);
    if (!ref)
      return WalkResult::advance();
    Operation *decl = lookup(from, ref->name);
    if (!decl) {
      from->emitOpError() << "unresolved named data type '" << ref->name << "'";
      result = failure();
      return WalkResult::interrupt();
    }
    if (decl->getName().getStringRef() != ref->opName) {
      from->emitOpError() << "named type '" << ref->name << "' requires "
                          << ref->opName << " but resolves to "
                          << decl->getName();
      result = failure();
      return WalkResult::interrupt();
    }
    return WalkResult::advance();
  });
  return result;
}

ArrayAttr fieldNames(Operation *op) {
  return op->getAttrOfType<ArrayAttr>("field_names");
}
ArrayAttr fieldTypes(Operation *op) {
  return op->getAttrOfType<ArrayAttr>("field_types");
}

Operation *recordDecl(Operation *from, Type type) {
  auto ref = namedRef(type);
  if (!ref || (ref->opName != StructOp::getOperationName() &&
               ref->opName != PacketOp::getOperationName() &&
               ref->opName != TransactionOp::getOperationName()))
    return nullptr;
  Operation *decl = lookup(from, ref->name);
  return decl && decl->getName().getStringRef() == ref->opName ? decl : nullptr;
}

std::optional<unsigned> findField(Operation *decl, StringRef field) {
  for (auto [index, value] : llvm::enumerate(fieldNames(decl)))
    if (cast<StringAttr>(value).getValue() == field)
      return index;
  return std::nullopt;
}

Type fieldType(Operation *decl, unsigned index) {
  return cast<TypeAttr>(fieldTypes(decl)[index]).getValue();
}

LogicalResult noRecursion(Operation *root) {
  auto rootName =
      root->getAttrOfType<StringAttr>(SymbolTable::getSymbolAttrName());
  llvm::SmallDenseSet<Operation *> active;
  std::function<LogicalResult(Operation *)> visit =
      [&](Operation *current) -> LogicalResult {
    if (!active.insert(current).second) {
      root->emitOpError() << "unbounded value recursion through '@"
                          << rootName.getValue() << "'";
      return failure();
    }
    if (ArrayAttr types = fieldTypes(current)) {
      for (Attribute attr : types) {
        LogicalResult nestedResult = success();
        cast<TypeAttr>(attr).getValue().walk([&](Type nested) {
          auto ref = namedRef(nested);
          Operation *next = ref ? lookup(root, ref->name) : nullptr;
          if (next && fieldTypes(next) && failed(visit(next))) {
            nestedResult = failure();
            return WalkResult::interrupt();
          }
          return WalkResult::advance();
        });
        if (failed(nestedResult))
          return failure();
      }
    }
    active.erase(current);
    return success();
  };
  return visit(root);
}

LogicalResult recordDeclaration(Operation *op, ArrayAttr names,
                                ArrayAttr types) {
  if (failed(fields(op, names, types)))
    return failure();
  for (Attribute type : types)
    if (failed(verifyNamedTypes(op, cast<TypeAttr>(type).getValue())))
      return failure();
  return noRecursion(op);
}

LogicalResult packetLayout(PacketOp packet) {
  DictionaryAttr data = packet.getSerialization();
  auto size = data.getAs<IntegerAttr>("size");
  auto alignment = data.getAs<IntegerAttr>("alignment");
  auto endian = data.getAs<StringAttr>("endianness");
  if (!size || !alignment || !endian || size.getInt() <= 0 ||
      alignment.getInt() <= 0 ||
      (endian.getValue() != "little" && endian.getValue() != "big"))
    return packet.emitOpError("packet serialization metadata requires positive "
                              "size and alignment and explicit endianness");
  return success();
}

FailureOr<int64_t> packetSize(Operation *from, FlatSymbolRefAttr name) {
  auto packet = dyn_cast_or_null<PacketOp>(lookup(from, name));
  if (!packet)
    return failure();
  auto size = packet.getSerialization().getAs<IntegerAttr>("size");
  if (!size)
    return failure();
  return size.getInt();
}

} // namespace

DataLayoutSpecInterface TypeScopeOp::getDataLayoutSpec() {
  return getOperation()->getAttrOfType<DataLayoutSpecInterface>(
      DLTIDialect::kDataLayoutAttrName);
}
TargetSystemSpecInterface TypeScopeOp::getTargetSystemSpec() {
  return getOperation()->getAttrOfType<TargetSystemSpecInterface>(
      DLTIDialect::kTargetSystemDescAttrName);
}

LogicalResult TypeAliasOp::verify() {
  return verifyNamedTypes(*this, getTarget());
}

LogicalResult StructOp::verify() {
  return recordDeclaration(*this, getFieldNames(), getFieldTypes());
}
LogicalResult TransactionOp::verify() {
  return recordDeclaration(*this, getFieldNames(), getFieldTypes());
}
LogicalResult PacketOp::verify() {
  if (failed(recordDeclaration(*this, getFieldNames(), getFieldTypes())))
    return failure();
  return packetLayout(*this);
}
LogicalResult EnumOp::verify() {
  return uniqueStrings(*this, getEnumerants(), "enumerant");
}
LogicalResult UnionOp::verify() {
  if (failed(recordDeclaration(*this, getFieldNames(), getFieldTypes())))
    return failure();
  auto index = findField(*this, getDiscriminator());
  if (!index)
    return emitOpError() << "union discriminator '" << getDiscriminator()
                         << "' does not name a field";
  if (!isa<IntegerType, EnumType>(fieldType(*this, *index)))
    return emitOpError() << "union discriminator '" << getDiscriminator()
                         << "' must name an integer or enum field";
  return success();
}

LogicalResult RecordCreateOp::verify() {
  Operation *decl = recordDecl(*this, getResult().getType());
  if (!decl)
    return emitOpError(
        "record.create result must resolve to a record declaration");
  if (fieldNames(decl) != getFieldNames() ||
      getValues().size() != fieldNames(decl).size())
    return emitOpError("record.create fields must exactly match declaration");
  for (auto [name, expected, value] :
       llvm::zip_equal(fieldNames(decl), fieldTypes(decl), getValues())) {
    Type expectedType = cast<TypeAttr>(expected).getValue();
    if (expectedType != value.getType())
      return emitOpError() << "field '" << cast<StringAttr>(name).getValue()
                           << "' expects " << expectedType << " but received "
                           << value.getType();
  }
  return success();
}

LogicalResult RecordGetOp::verify() {
  Operation *decl = recordDecl(*this, getRecord().getType());
  if (!decl)
    return emitOpError("record.get requires a record-like operand");
  auto index = findField(decl, getField());
  if (!index)
    return emitOpError() << "unknown record field '" << getField() << "'";
  Type type = fieldType(decl, *index);
  if (type != getResult().getType())
    return emitOpError() << "field '" << getField() << "' has type " << type
                         << " but operation returns " << getResult().getType();
  return success();
}

LogicalResult RecordWithOp::verify() {
  if (getRecord().getType() != getResult().getType())
    return emitOpError("record.with must preserve record identity");
  Operation *decl = recordDecl(*this, getRecord().getType());
  if (!decl)
    return emitOpError("record.with requires a record-like operand");
  auto index = findField(decl, getField());
  if (!index)
    return emitOpError() << "unknown record field '" << getField() << "'";
  Type type = fieldType(decl, *index);
  if (type != getValue().getType())
    return emitOpError() << "field '" << getField() << "' expects " << type
                         << " but received " << getValue().getType();
  return success();
}

LogicalResult PacketSerializeOp::verify() {
  auto packetType = dyn_cast<PacketType>(getPacketValue().getType());
  if (!packetType)
    return emitOpError("packet.serialize requires a packet operand");
  if (packetType.getName() != getPacketAttr())
    return emitOpError(
        "packet.serialize identity does not match packet operand");
  FailureOr<int64_t> size = packetSize(*this, getPacketAttr());
  if (failed(size))
    return emitOpError("packet.serialize packet declaration is unresolved");
  auto bytes = dyn_cast<VectorType>(getBytes().getType());
  if (!bytes || !bytes.getElementType().isInteger(8) ||
      bytes.getLength() != *size)
    return emitOpError()
           << "serialized byte vector width must equal packet size " << *size;
  return success();
}

LogicalResult PacketDeserializeOp::verify() {
  auto packetType = dyn_cast<PacketType>(getPacketValue().getType());
  if (!packetType || packetType.getName() != getPacketAttr())
    return emitOpError("packet.deserialize result identity does not match "
                       "serialization contract");
  FailureOr<int64_t> size = packetSize(*this, getPacketAttr());
  if (failed(size))
    return emitOpError("packet.deserialize packet declaration is unresolved");
  auto bytes = dyn_cast<VectorType>(getBytes().getType());
  if (!bytes || !bytes.getElementType().isInteger(8) ||
      bytes.getLength() != *size)
    return emitOpError()
           << "serialized byte vector width must equal packet size " << *size;
  return success();
}

} // namespace acir::ac

#include "acir/Dialect/ACIR/ACIROpInterfaces.cpp.inc"

#define GET_OP_CLASSES
#include "acir/Dialect/ACIR/ACIROps.cpp.inc"
