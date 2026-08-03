#include "acir/Dialect/ACIR/ACIROps.h"

#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;

namespace acir::ac {
namespace {

struct NamedRef {
  SymbolRefAttr name;
  StringRef opName;
};

std::optional<NamedRef> namedRef(Type type) {
  return TypeSwitch<Type, std::optional<NamedRef>>(type)
      .Case<StructType>([](auto type) {
        return NamedRef{type.getName(), StructOp::getOperationName()};
      })
      .Case<PacketType>([](auto type) {
        return NamedRef{type.getName(), PacketOp::getOperationName()};
      })
      .Case<TransactionType>([](auto type) {
        return NamedRef{type.getName(), TransactionOp::getOperationName()};
      })
      .Case<EnumType>([](auto type) {
        return NamedRef{type.getName(), EnumOp::getOperationName()};
      })
      .Case<UnionType>([](auto type) {
        return NamedRef{type.getName(), UnionOp::getOperationName()};
      })
      .Default([](Type) { return std::nullopt; });
}

Operation *lookup(Operation *from, SymbolRefAttr name) {
  if (name.getNestedReferences().size() != 1)
    return SymbolTable::lookupNearestSymbolFrom(from, name);
  Operation *scope = nullptr;
  if (auto enclosing = from->getParentOfType<TypeScopeOp>();
      enclosing && enclosing.getSymNameAttr() == name.getRootReference())
    scope = enclosing;
  if (!scope) {
    auto root = FlatSymbolRefAttr::get(name.getRootReference());
    scope = SymbolTable::lookupNearestSymbolFrom(from, root);
  }
  if (!isa_and_nonnull<TypeScopeOp>(scope))
    return nullptr;
  return SymbolTable::lookupSymbolIn(scope, name.getLeafReference());
}

LogicalResult requireQualified(Operation *from, SymbolRefAttr name) {
  if (name.getNestedReferences().size() == 1)
    return success();
  return from->emitOpError(
      "named data references require a qualified symbol such as "
      "'@types::@S'");
}

LogicalResult verifyNamedTypes(Operation *from, Type type) {
  LogicalResult result = success();
  type.walk([&](Type nested) {
    auto ref = namedRef(nested);
    if (!ref)
      return WalkResult::advance();
    if (failed(requireQualified(from, ref->name))) {
      result = failure();
      return WalkResult::interrupt();
    }
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

LogicalResult verifyPlacement(Operation *op) {
  if (isa_and_nonnull<TypeScopeOp>(op->getParentOp()))
    return success();
  return op->emitOpError(
      "named data declarations must be direct children of ac.type_scope");
}

FailureOr<DictionaryAttr> fieldDictionary(Operation *op, Attribute field) {
  auto dictionary = dyn_cast<DictionaryAttr>(field);
  if (!dictionary || !dictionary.getAs<StringAttr>("name") ||
      !dictionary.getAs<TypeAttr>("type")) {
    op->emitOpError("field metadata requires string 'name' and type 'type'");
    return failure();
  }
  return dictionary;
}

StringRef fieldName(DictionaryAttr field) {
  return field.getAs<StringAttr>("name").getValue();
}

Type fieldType(DictionaryAttr field) {
  return field.getAs<TypeAttr>("type").getValue();
}

bool containsList(Type type) {
  return type.walk([](ListType) { return WalkResult::interrupt(); })
      .wasInterrupted();
}

bool isNormativeValueType(Type type) {
  if (isa<IntegerType, FloatType, IndexType, StructType, PacketType,
          TransactionType, EnumType, UnionType>(type))
    return true;
  if (auto optional = dyn_cast<OptionalType>(type))
    return isNormativeValueType(optional.getElementType());
  if (auto list = dyn_cast<ListType>(type))
    return isNormativeValueType(list.getElementType());
  if (auto vector = dyn_cast<VectorType>(type))
    return isNormativeValueType(vector.getElementType());
  if (auto vector = dyn_cast<mlir::VectorType>(type))
    return isNormativeValueType(vector.getElementType());
  return false;
}

LogicalResult verifyFields(Operation *op, ArrayAttr fields) {
  llvm::SmallDenseSet<StringRef> seen;
  for (Attribute attribute : fields) {
    FailureOr<DictionaryAttr> field = fieldDictionary(op, attribute);
    if (failed(field))
      return failure();
    StringRef name = fieldName(*field);
    Type type = fieldType(*field);
    if (!seen.insert(name).second)
      return op->emitOpError() << "duplicate field '" << name << "'";
    if (!isNormativeValueType(type))
      return op->emitOpError()
             << "field '" << name << "' has non-value type " << type;
    if (failed(verifyNamedTypes(op, type)))
      return failure();
    auto bound = field->getAs<IntegerAttr>("max_length");
    if (containsList(type)) {
      if (!bound || bound.getInt() <= 0)
        return op->emitOpError() << "list field '" << name
                                 << "' requires a finite positive max_length";
    } else if (bound) {
      return op->emitOpError()
             << "non-list field '" << name << "' cannot declare max_length";
    }
  }
  return success();
}

ArrayAttr declarationFields(Operation *op) {
  return op->getAttrOfType<ArrayAttr>("fields");
}

Operation *recordDecl(Operation *from, Type type) {
  auto ref = namedRef(type);
  if (!ref || (ref->opName != StructOp::getOperationName() &&
               ref->opName != PacketOp::getOperationName() &&
               ref->opName != TransactionOp::getOperationName()))
    return nullptr;
  if (failed(requireQualified(from, ref->name)))
    return nullptr;
  Operation *decl = lookup(from, ref->name);
  return decl && decl->getName().getStringRef() == ref->opName ? decl : nullptr;
}

std::optional<unsigned> findField(Operation *decl, StringRef name) {
  for (auto [index, attribute] : llvm::enumerate(declarationFields(decl))) {
    auto field = cast<DictionaryAttr>(attribute);
    if (fieldName(field) == name)
      return index;
  }
  return std::nullopt;
}

Type fieldType(Operation *decl, unsigned index) {
  return fieldType(cast<DictionaryAttr>(declarationFields(decl)[index]));
}

SmallVector<NamedRef> directValueReferences(Type type) {
  if (isa<ListType>(type))
    return {};
  if (auto ref = namedRef(type))
    return {*ref};
  if (auto optional = dyn_cast<OptionalType>(type))
    return directValueReferences(optional.getElementType());
  if (auto vector = dyn_cast<VectorType>(type))
    return directValueReferences(vector.getElementType());
  if (auto vector = dyn_cast<mlir::VectorType>(type))
    return directValueReferences(vector.getElementType());
  return {};
}

LogicalResult verifyNoRecursion(Operation *root) {
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
    if (ArrayAttr fields = declarationFields(current)) {
      for (Attribute attribute : fields) {
        Type type = fieldType(cast<DictionaryAttr>(attribute));
        for (NamedRef ref : directValueReferences(type)) {
          Operation *next = lookup(root, ref.name);
          if (next && declarationFields(next) && failed(visit(next)))
            return failure();
        }
      }
    }
    active.erase(current);
    return success();
  };
  return visit(root);
}

LogicalResult verifyRecordDeclaration(Operation *op, ArrayAttr fields) {
  if (failed(verifyPlacement(op)) || failed(verifyFields(op, fields)))
    return failure();
  return verifyNoRecursion(op);
}

SymbolRefAttr declarationReference(Operation *declaration) {
  auto scope = cast<TypeScopeOp>(declaration->getParentOp());
  auto leaf = FlatSymbolRefAttr::get(
      declaration->getAttrOfType<StringAttr>(SymbolTable::getSymbolAttrName()));
  return SymbolRefAttr::get(declaration->getContext(), scope.getSymName(),
                            ArrayRef<FlatSymbolRefAttr>{leaf});
}

Type declarationType(Operation *declaration) {
  SymbolRefAttr reference = declarationReference(declaration);
  return TypeSwitch<Operation *, Type>(declaration)
      .Case<StructOp>([&](auto) {
        return StructType::get(declaration->getContext(), reference);
      })
      .Case<PacketOp>([&](auto) {
        return PacketType::get(declaration->getContext(), reference);
      })
      .Case<EnumOp>([&](auto) {
        return EnumType::get(declaration->getContext(), reference);
      })
      .Case<UnionOp>([&](auto) {
        return UnionType::get(declaration->getContext(), reference);
      })
      .Default([](Operation *) { return Type(); });
}

FailureOr<DictionaryAttr> queryLayout(TypeScopeOp scope, Type type) {
  DataLayoutSpecInterface spec = scope.getDataLayoutSpec();
  if (!spec)
    return failure();
  FailureOr<Attribute> value = spec.query(DataLayoutEntryKey(type));
  if (failed(value))
    return failure();
  auto dictionary = dyn_cast<DictionaryAttr>(*value);
  if (!dictionary)
    return failure();
  return dictionary;
}

LogicalResult verifyDeclarationLayout(Operation *declaration) {
  Type type = declarationType(declaration);
  auto scope = cast<TypeScopeOp>(declaration->getParentOp());
  if (succeeded(queryLayout(scope, type)))
    return success();
  return declaration->emitOpError() << "missing DLTI layout entry for " << type;
}

FailureOr<int64_t> packetSerializationWidth(Operation *from,
                                            SymbolRefAttr name) {
  auto packet = dyn_cast_or_null<PacketOp>(lookup(from, name));
  if (!packet)
    return failure();
  FailureOr<DictionaryAttr> layout =
      queryLayout(cast<TypeScopeOp>(packet->getParentOp()),
                  PacketType::get(from->getContext(), name));
  if (failed(layout))
    return failure();
  auto width = layout->getAs<IntegerAttr>("serialization_width");
  if (!width || width.getInt() <= 0)
    return failure();
  return width.getInt();
}

LogicalResult verifyUniqueEnumerants(EnumOp op) {
  llvm::SmallDenseSet<StringRef> seen;
  for (Attribute value : op.getEnumerants()) {
    StringRef text = cast<StringAttr>(value).getValue();
    if (!seen.insert(text).second)
      return op.emitOpError() << "duplicate enumerant '" << text << "'";
  }
  return success();
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
  if (failed(verifyPlacement(*this)))
    return failure();
  return verifyNamedTypes(*this, getTarget());
}

LogicalResult StructOp::verify() {
  if (failed(verifyRecordDeclaration(*this, getFields())))
    return failure();
  return verifyDeclarationLayout(*this);
}

LogicalResult TransactionOp::verify() {
  return verifyRecordDeclaration(*this, getFields());
}

LogicalResult PacketOp::verify() {
  if (failed(verifyRecordDeclaration(*this, getFields())))
    return failure();
  return verifyDeclarationLayout(*this);
}

LogicalResult EnumOp::verify() {
  if (failed(verifyPlacement(*this)) || failed(verifyUniqueEnumerants(*this)))
    return failure();
  return verifyDeclarationLayout(*this);
}

LogicalResult UnionOp::verify() {
  if (failed(verifyRecordDeclaration(*this, getFields())))
    return failure();
  auto index = findField(*this, getDiscriminator());
  if (!index)
    return emitOpError() << "union discriminator '" << getDiscriminator()
                         << "' does not name a field";
  if (!isa<IntegerType, EnumType>(fieldType(*this, *index)))
    return emitOpError() << "union discriminator '" << getDiscriminator()
                         << "' must name an integer or enum field";
  return verifyDeclarationLayout(*this);
}

LogicalResult RecordCreateOp::verify() {
  Operation *decl = recordDecl(*this, getResult().getType());
  if (!decl)
    return emitOpError(
        "record.create result must resolve to a record declaration");
  ArrayAttr fields = declarationFields(decl);
  if (getFieldNames().size() != fields.size() ||
      getValues().size() != fields.size())
    return emitOpError("record.create fields must exactly match declaration");
  for (auto [index, value] : llvm::enumerate(getValues())) {
    auto field = cast<DictionaryAttr>(fields[index]);
    if (cast<StringAttr>(getFieldNames()[index]).getValue() != fieldName(field))
      return emitOpError("record.create fields must exactly match declaration");
    Type expected = fieldType(field);
    if (expected != value.getType())
      return emitOpError() << "field '" << fieldName(field) << "' expects "
                           << expected << " but received " << value.getType();
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
  if (failed(requireQualified(*this, getPacketAttr())))
    return failure();
  if (packetType.getName() != getPacketAttr())
    return emitOpError(
        "packet.serialize identity does not match packet operand");
  FailureOr<int64_t> width = packetSerializationWidth(*this, getPacketAttr());
  if (failed(width))
    return emitOpError("packet.serialize packet declaration is unresolved");
  auto bytes = dyn_cast<VectorType>(getBytes().getType());
  if (!bytes || !bytes.getElementType().isInteger(8))
    return emitOpError("packet.serialize result must be an i8 byte vector");
  if (bytes.getLength() != *width)
    return emitOpError()
           << "serialized byte vector width must equal packet serialization "
              "width "
           << *width;
  return success();
}

LogicalResult PacketDeserializeOp::verify() {
  if (failed(requireQualified(*this, getPacketAttr())))
    return failure();
  auto packetType = dyn_cast<PacketType>(getPacketValue().getType());
  if (!packetType || packetType.getName() != getPacketAttr())
    return emitOpError("packet.deserialize result identity does not match "
                       "serialization contract");
  FailureOr<int64_t> width = packetSerializationWidth(*this, getPacketAttr());
  if (failed(width))
    return emitOpError("packet.deserialize packet declaration is unresolved");
  auto bytes = dyn_cast<VectorType>(getBytes().getType());
  if (!bytes || !bytes.getElementType().isInteger(8))
    return emitOpError("packet.deserialize operand must be an i8 byte vector");
  if (bytes.getLength() != *width)
    return emitOpError()
           << "serialized byte vector width must equal packet serialization "
              "width "
           << *width;
  return success();
}

} // namespace acir::ac

#include "acir/Dialect/ACIR/ACIROpInterfaces.cpp.inc"

#define GET_OP_CLASSES
#include "acir/Dialect/ACIR/ACIROps.cpp.inc"
