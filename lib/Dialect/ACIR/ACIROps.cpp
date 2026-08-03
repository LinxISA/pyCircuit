#include "acir/Dialect/ACIR/ACIROps.h"
#include "acir/Dialect/ACIR/GraphRegion.h"

#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/FunctionImplementation.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/TypeSwitch.h"

#include <limits>

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
    if (!scope)
      if (auto module = from->getParentOfType<mlir::ModuleOp>())
        scope = SymbolTable::lookupSymbolIn(module, root);
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

bool isProtocolPayloadType(Type type) {
  return isNormativeValueType(type) && !containsChannelType(type);
}

bool isTopologyLeaf(Type type) {
  return isa<FlowType, EndpointType, ResourceRefType, ChannelType,
             ResourceTokenType>(type);
}

Type findNestedTopologyLeaf(Type type) {
  if (isTopologyLeaf(type))
    return {};
  Type found;
  type.walk([&](Type nested) {
    if (!isTopologyLeaf(nested))
      return WalkResult::advance();
    found = nested;
    return WalkResult::interrupt();
  });
  return found;
}

template <typename OpTy>
OpTy lookupChild(Operation *container, FlatSymbolRefAttr name) {
  return dyn_cast_or_null<OpTy>(SymbolTable::lookupSymbolIn(container, name));
}

ProtocolOp lookupProtocol(Operation *from, FlatSymbolRefAttr name) {
  auto module = from->getParentOfType<mlir::ModuleOp>();
  return module ? dyn_cast_or_null<ProtocolOp>(
                      SymbolTable::lookupSymbolIn(module, name))
                : ProtocolOp();
}

bool isCarrierAction(StringRef action) {
  return action == "offer" || action == "response" || action == "notify";
}

bool matchesCarrierEvent(ProtocolOp protocol, Type payload,
                         FlatSymbolRefAttr from = {},
                         FlatSymbolRefAttr to = {}) {
  return llvm::any_of(protocol.getBody().getOps<EventOp>(), [&](EventOp event) {
    return isCarrierAction(event.getAction()) &&
           event.getPayload() == payload &&
           (!from || event.getFromAttr() == from) &&
           (!to || event.getToAttr() == to);
  });
}

LogicalResult verifyRoleReference(Operation *from, Operation *container,
                                  FlatSymbolRefAttr name, StringRef subject) {
  if (lookupChild<RoleOp>(container, name))
    return success();
  return from->emitOpError()
         << "unresolved " << subject << " role '@" << name.getValue() << "'";
}

LogicalResult verifyRoleContainer(Operation *container) {
  llvm::SmallDenseSet<StringRef> names;
  for (RoleOp role : container->getRegion(0).getOps<RoleOp>())
    if (!names.insert(role.getSymName()).second)
      return role.emitOpError()
             << "redefinition of symbol named '" << role.getSymName() << "'";
  for (RoleOp role : container->getRegion(0).getOps<RoleOp>()) {
    if (role.getCardinality() != "exclusive" &&
        role.getCardinality() != "shared")
      return role.emitOpError() << "unsupported role cardinality '"
                                << role.getCardinality() << "'";
    RoleOp dual = lookupChild<RoleOp>(container, role.getDualAttr());
    if (!dual)
      return role.emitOpError() << "unresolved dual role '@"
                                << role.getDualAttr().getValue() << "'";
    if (dual == role)
      return role.emitOpError("role cannot be its own dual");
    if (dual.getDualAttr() !=
        FlatSymbolRefAttr::get(role.getContext(), role.getSymName()))
      return role.emitOpError("role duality must be symmetric");
    if (dual.getCardinality() != role.getCardinality())
      return role.emitOpError("dual roles must have matching cardinality");
  }
  return success();
}

bool hasStringValue(StringRef value, ArrayRef<StringRef> accepted) {
  return llvm::is_contained(accepted, value);
}

GuaranteeOp findGuarantee(ProtocolOp protocol, StringRef kind) {
  for (GuaranteeOp guarantee : protocol.getBody().getOps<GuaranteeOp>())
    if (guarantee.getKind() == kind)
      return guarantee;
  return {};
}

LogicalResult verifyStringGuarantee(GuaranteeOp op,
                                    ArrayRef<StringRef> accepted) {
  auto value = dyn_cast<StringAttr>(op.getValue());
  if (!value || !hasStringValue(value.getValue(), accepted))
    return op.emitOpError()
           << "unsupported " << op.getKind() << " value '"
           << (value ? value.getValue() : StringRef("<non-string>")) << "'";
  return success();
}

bool isAllowedGuardExpression(Operation *operation) {
  return llvm::StringSwitch<bool>(operation->getName().getStringRef())
      .Cases({"arith.constant",
              "arith.cmpi",
              "arith.cmpf",
              "arith.addi",
              "arith.subi",
              "arith.muli",
              "arith.divui",
              "arith.divsi",
              "arith.remui",
              "arith.remsi",
              "arith.andi",
              "arith.ori",
              "arith.xori",
              "arith.shli",
              "arith.shrui",
              "arith.shrsi",
              "arith.select",
              "arith.index_cast",
              "arith.extui",
              "arith.extsi",
              "arith.trunci",
              "arith.addf",
              "arith.subf",
              "arith.mulf",
              "arith.divf",
              "arith.negf",
              "index.constant",
              "index.add",
              "index.sub",
              "index.mul",
              "index.divs",
              "index.divu",
              "index.rems",
              "index.remu",
              "index.cmp",
              "index.casts",
              "index.castu",
              RecordCreateOp::getOperationName(),
              RecordGetOp::getOperationName(),
              RecordWithOp::getOperationName()},
             true)
      .Default(false);
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
    Attribute boundAttribute = field->get("max_length");
    auto bound = dyn_cast_or_null<IntegerAttr>(boundAttribute);
    if (containsList(type)) {
      if (!bound || !bound.getType().isSignlessInteger(64) ||
          bound.getInt() <= 0)
        return op->emitOpError() << "list field '" << name
                                 << "' requires a finite positive max_length";
    } else if (boundAttribute) {
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

LogicalResult InterfaceOp::verify() {
  if (getBody().empty())
    return emitOpError("interface declaration requires a body block");
  for (Operation &child : getBody().front())
    if (!isa<RoleOp, PortOp>(child))
      return emitOpError()
             << "interface body only permits ac.role and ac.port, "
             << "found " << child.getName();
  return verifyRoleContainer(*this);
}

LogicalResult ProtocolOp::verify() {
  if (getBody().empty())
    return emitOpError("protocol declaration requires a body block");
  for (Operation &child : getBody().front())
    if (!isa<RoleOp, StateOp, EventOp, TransitionOp, GuaranteeOp>(child))
      return emitOpError() << "protocol body contains unsupported operation "
                           << child.getName();

  if (failed(verifyRoleContainer(*this)))
    return failure();

  unsigned initialStates = 0;
  for (StateOp state : getBody().getOps<StateOp>())
    initialStates += state.getInitial() ? 1 : 0;
  if (initialStates != 1)
    return emitOpError()
           << "protocol requires exactly one initial state, found "
           << initialStates;

  llvm::SmallDenseSet<StringRef> guaranteeKinds;
  for (GuaranteeOp guarantee : getBody().getOps<GuaranteeOp>())
    if (!guaranteeKinds.insert(guarantee.getKind()).second)
      return guarantee.emitOpError()
             << "duplicate protocol guarantee '" << guarantee.getKind() << "'";

  SmallVector<TransitionOp> transitions;
  for (TransitionOp transition : getBody().getOps<TransitionOp>()) {
    if (!lookupChild<StateOp>(*this, transition.getSourceAttr()))
      return transition.emitOpError()
             << "unresolved transition source state '@"
             << transition.getSourceAttr().getValue() << "'";
    if (!lookupChild<StateOp>(*this, transition.getTargetAttr()))
      return transition.emitOpError()
             << "unresolved transition target state '@"
             << transition.getTargetAttr().getValue() << "'";
    if (!lookupChild<EventOp>(*this, transition.getEventAttr()))
      return transition.emitOpError()
             << "unresolved transition event '@"
             << transition.getEventAttr().getValue() << "'";
    transitions.push_back(transition);
  }

  for (unsigned i = 0; i < transitions.size(); ++i) {
    SmallVector<TransitionOp> overlapping{transitions[i]};
    for (unsigned j = i + 1; j < transitions.size(); ++j)
      if (transitions[i].getSourceAttr() == transitions[j].getSourceAttr() &&
          transitions[i].getEventAttr() == transitions[j].getEventAttr())
        overlapping.push_back(transitions[j]);
    if (overlapping.size() < 2)
      continue;
    llvm::SmallSet<int64_t, 4> priorities;
    for (TransitionOp transition : overlapping) {
      if (!transition.getPriority())
        return transition.emitOpError(
            "overlapping transitions require explicit priority");
      if (!priorities.insert(static_cast<int64_t>(*transition.getPriority()))
               .second)
        return transition.emitOpError(
            "overlapping transitions require unique priority");
    }
  }

  GuaranteeOp stablePending = findGuarantee(*this, "stable_pending");
  bool stable = stablePending && dyn_cast<BoolAttr>(stablePending.getValue()) &&
                cast<BoolAttr>(stablePending.getValue()).getValue();
  for (TransitionOp transition : transitions) {
    EventOp event = lookupChild<EventOp>(*this, transition.getEventAttr());
    if (transition.getTransfer() && transition.getRetain())
      return transition.emitOpError(
          "transition cannot both transfer and retain ownership");
    if (event.getAction() == "offer" && !transition.getTransfer() &&
        !transition.getRetain())
      return transition.emitOpError(
          "offer transition must transfer or retain ownership");
    if (event.getAction() == "offer" && transition.getRetain() && !stable)
      return transition.emitOpError(
          "retained pending offer requires stable_pending = true");
    if (event.getAction() == "retry" && !transition.getRetain())
      return transition.emitOpError(
          "retry transition must retain the pending offer");
    if (event.getAction() == "retry" && transition.getTransfer())
      return transition.emitOpError(
          "retry transition cannot transfer the pending offer");
    if (transition.getRetain() && event.getAction() != "offer" &&
        event.getAction() != "retry")
      return transition.emitOpError(
          "retain is only valid for offer and retry transitions");
  }

  enum class Ownership : uint8_t { NoPending = 1, Pending = 2 };
  SmallVector<StateOp> states(getBody().getOps<StateOp>());
  llvm::StringMap<unsigned> stateIndices;
  for (auto [index, state] : llvm::enumerate(states))
    stateIndices.try_emplace(state.getSymName(), index);
  auto stateIndex = [&](FlatSymbolRefAttr name) -> unsigned {
    auto found = stateIndices.find(name.getValue());
    assert(found != stateIndices.end() &&
           "transition state references were verified");
    return found->second;
  };

  SmallVector<SmallVector<unsigned>> outgoing(states.size());
  SmallVector<unsigned> transitionTargets;
  SmallVector<EventOp> transitionEvents;
  transitionTargets.reserve(transitions.size());
  transitionEvents.reserve(transitions.size());
  for (auto [index, transition] : llvm::enumerate(transitions)) {
    outgoing[stateIndex(transition.getSourceAttr())].push_back(index);
    transitionTargets.push_back(stateIndex(transition.getTargetAttr()));
    transitionEvents.push_back(
        lookupChild<EventOp>(*this, transition.getEventAttr()));
  }

  SmallVector<uint8_t> ownership(states.size(), 0);
  SmallVector<std::pair<unsigned, Ownership>> worklist;
  auto ownershipBit = [](Ownership value) {
    return static_cast<uint8_t>(value);
  };
  auto hasOwnership = [&](unsigned state, Ownership value) {
    return (ownership[state] & ownershipBit(value)) != 0;
  };
  auto addOwnership = [&](unsigned state, Ownership value) {
    uint8_t bit = ownershipBit(value);
    if (ownership[state] & bit)
      return;
    ownership[state] |= bit;
    worklist.emplace_back(state, value);
  };
  for (auto [index, state] : llvm::enumerate(states))
    if (state.getInitial())
      addOwnership(index, Ownership::NoPending);

  auto transferOwnership = [&](unsigned transitionIndex,
                               Ownership input) -> std::optional<Ownership> {
    TransitionOp transition = transitions[transitionIndex];
    StringRef action = transitionEvents[transitionIndex].getAction();
    if (action == "offer") {
      if (input == Ownership::Pending)
        return std::nullopt;
      return transition.getTransfer() ? Ownership::NoPending
                                      : Ownership::Pending;
    }
    if (action == "retry")
      return input == Ownership::Pending
                 ? std::optional<Ownership>(Ownership::Pending)
                 : std::nullopt;
    if (action == "cancel" || action == "reject")
      return input == Ownership::Pending
                 ? std::optional<Ownership>(Ownership::NoPending)
                 : std::nullopt;
    if (transition.getTransfer())
      return input == Ownership::Pending
                 ? std::optional<Ownership>(Ownership::NoPending)
                 : std::nullopt;
    return input;
  };

  for (unsigned cursor = 0; cursor < worklist.size(); ++cursor) {
    auto [source, input] = worklist[cursor];
    for (unsigned transitionIndex : outgoing[source])
      if (std::optional<Ownership> output =
              transferOwnership(transitionIndex, input))
        addOwnership(transitionTargets[transitionIndex], *output);
  }

  uint8_t conflicting =
      ownershipBit(Ownership::NoPending) | ownershipBit(Ownership::Pending);
  for (auto [index, state] : llvm::enumerate(states))
    if (ownership[index] == conflicting)
      return state.emitOpError() << "ownership state conflict at join state '@"
                                 << state.getSymName() << "'";

  auto firstTransition = [&](auto predicate) -> TransitionOp {
    for (auto [index, transition] : llvm::enumerate(transitions))
      if (predicate(index, transition))
        return transition;
    return {};
  };
  if (TransitionOp transition = firstTransition([&](unsigned index, auto op) {
        return transitionEvents[index].getAction() == "offer" &&
               hasOwnership(stateIndex(op.getSourceAttr()), Ownership::Pending);
      }))
    return transition.emitOpError(
        "offer cannot begin while another offer is pending");
  if (TransitionOp transition = firstTransition([&](unsigned index, auto op) {
        return transitionEvents[index].getAction() == "retry" &&
               hasOwnership(stateIndex(op.getSourceAttr()),
                            Ownership::NoPending);
      }))
    return transition.emitOpError("retry requires a pending offer");
  if (TransitionOp transition = firstTransition([&](unsigned index, auto op) {
        StringRef action = transitionEvents[index].getAction();
        return (action == "cancel" || action == "reject") &&
               hasOwnership(stateIndex(op.getSourceAttr()),
                            Ownership::NoPending);
      }))
    return transition.emitOpError(
        "ownership resolution requires a pending offer");
  if (TransitionOp transition = firstTransition([&](unsigned index, auto op) {
        StringRef action = transitionEvents[index].getAction();
        return op.getTransfer() && action != "offer" && action != "retry" &&
               action != "cancel" && action != "reject" &&
               hasOwnership(stateIndex(op.getSourceAttr()),
                            Ownership::NoPending);
      }))
    return transition.emitOpError(
        "ownership transfer requires a pending offer");
  if (TransitionOp transition = firstTransition([&](unsigned index, auto op) {
        if (ownership[stateIndex(op.getSourceAttr())] != 0)
          return false;
        StringRef action = transitionEvents[index].getAction();
        return op.getTransfer() || action == "retry" || action == "cancel" ||
               action == "reject";
      }))
    return transition.emitOpError(
        "ownership resolution is unreachable from the initial state");

  for (auto [index, state] : llvm::enumerate(states)) {
    if (!hasOwnership(index, Ownership::Pending))
      continue;
    if (state.getTerminal())
      return state.emitOpError() << "terminal state '@" << state.getSymName()
                                 << "' is reachable with pending ownership";
    if (outgoing[index].empty())
      return state.emitOpError()
             << "pending ownership reaches state '@" << state.getSymName()
             << "' with no outgoing transition";
  }

  GuaranteeOp maxInflight = findGuarantee(*this, "max_inflight");
  if (maxInflight) {
    auto value = dyn_cast<IntegerAttr>(maxInflight.getValue());
    if (!value || !value.getType().isSignlessInteger(64) || value.getInt() <= 0)
      return maxInflight.emitOpError(
          "max_inflight requires a positive i64 value");
    if (value.getInt() > 1 && !findGuarantee(*this, "correlation"))
      return maxInflight.emitOpError(
          "max_inflight greater than one requires correlation");
  }
  GuaranteeOp backpressure = findGuarantee(*this, "backpressure");
  if (backpressure)
    if (auto value = dyn_cast<StringAttr>(backpressure.getValue());
        value && value.getValue() == "custom" &&
        !findGuarantee(*this, "custom_backpressure"))
      return backpressure.emitOpError(
          "custom backpressure requires a custom_backpressure declaration");
  GuaranteeOp ordering = findGuarantee(*this, "ordering");
  if (ordering)
    if (auto value = dyn_cast<StringAttr>(ordering.getValue());
        value && value.getValue() == "per_key" &&
        !findGuarantee(*this, "correlation"))
      return ordering.emitOpError("per_key ordering requires correlation");
  GuaranteeOp completion = findGuarantee(*this, "completion");
  if (completion) {
    if (auto value = dyn_cast<StringAttr>(completion.getValue())) {
      if (value.getValue() == "on_response" &&
          !findGuarantee(*this, "correlation"))
        return completion.emitOpError(
            "on_response completion requires correlation");
      auto hasReachableAction = [&](StringRef action) {
        return llvm::any_of(transitions, [&](TransitionOp transition) {
          return ownership[stateIndex(transition.getSourceAttr())] &&
                 lookupChild<EventOp>(*this, transition.getEventAttr())
                         .getAction() == action;
        });
      };
      if (value.getValue() == "on_response" && !hasReachableAction("response"))
        return completion.emitOpError(
            "on_response completion requires a reachable response event");
      if (value.getValue() == "on_accept" && !hasReachableAction("accept"))
        return completion.emitOpError(
            "on_accept completion requires a reachable accept event");
      if (value.getValue() == "on_terminal_phase" &&
          llvm::none_of(llvm::enumerate(states), [&](auto indexedState) {
            return indexedState.value().getTerminal() &&
                   ownership[indexedState.index()] != 0;
          }))
        return completion.emitOpError(
            "on_terminal_phase completion requires a reachable terminal state");
    }
  }
  GuaranteeOp correlation = findGuarantee(*this, "correlation");
  if (correlation) {
    auto field = dyn_cast<StringAttr>(correlation.getValue());
    if (field && !field.getValue().empty()) {
      Type correlationType;
      for (TransitionOp transition : transitions) {
        if (!ownership[stateIndex(transition.getSourceAttr())])
          continue;
        EventOp event = lookupChild<EventOp>(*this, transition.getEventAttr());
        if (event.getAction() != "offer")
          continue;
        Operation *declaration = recordDecl(event, event.getPayload());
        std::optional<unsigned> index =
            declaration ? findField(declaration, field.getValue())
                        : std::nullopt;
        if (!index)
          return correlation.emitOpError()
                 << "correlation field '" << field.getValue()
                 << "' is missing from reachable offer/response payload";
        Type type = fieldType(declaration, *index);
        if (!correlationType)
          correlationType = type;
        else if (type != correlationType)
          return correlation.emitOpError()
                 << "correlation field '" << field.getValue() << "' has type "
                 << type << " but expected " << correlationType;
      }
      if (!correlationType)
        return correlation.emitOpError()
               << "correlation field '" << field.getValue()
               << "' requires a reachable offer event";
      for (TransitionOp transition : transitions) {
        if (!ownership[stateIndex(transition.getSourceAttr())])
          continue;
        EventOp event = lookupChild<EventOp>(*this, transition.getEventAttr());
        if (event.getAction() != "offer" && event.getAction() != "response")
          continue;
        Operation *declaration = recordDecl(event, event.getPayload());
        std::optional<unsigned> index =
            declaration ? findField(declaration, field.getValue())
                        : std::nullopt;
        if (!index)
          return correlation.emitOpError()
                 << "correlation field '" << field.getValue()
                 << "' is missing from reachable offer/response payload";
        Type type = fieldType(declaration, *index);
        if (type != correlationType)
          return correlation.emitOpError()
                 << "correlation field '" << field.getValue() << "' has type "
                 << type << " but expected " << correlationType;
      }
    }
  }
  return success();
}

LogicalResult RoleOp::verify() {
  if (!isa_and_nonnull<InterfaceOp, ProtocolOp>(getOperation()->getParentOp()))
    return emitOpError(
        "role must be a direct child of ac.interface or ac.protocol");
  if (getCardinality() != "exclusive" && getCardinality() != "shared")
    return emitOpError() << "unsupported role cardinality '" << getCardinality()
                         << "'";
  return success();
}

LogicalResult StateOp::verify() {
  if (!isa_and_nonnull<ProtocolOp>(getOperation()->getParentOp()))
    return emitOpError("state must be a direct child of ac.protocol");
  return success();
}

LogicalResult EventOp::verify() {
  auto protocol = dyn_cast_or_null<ProtocolOp>(getOperation()->getParentOp());
  if (!protocol)
    return emitOpError("event must be a direct child of ac.protocol");
  if (failed(verifyRoleReference(*this, protocol, getFromAttr(),
                                 "event source")) ||
      failed(verifyRoleReference(*this, protocol, getToAttr(), "event target")))
    return failure();
  if (getFromAttr() == getToAttr())
    return emitOpError("event source and target roles must differ");
  if (!isProtocolPayloadType(getPayload()))
    return emitOpError(
        "event payload type must be a normative ACIR value type");
  if (failed(verifyNamedTypes(*this, getPayload())))
    return failure();
  static constexpr StringRef actions[] = {
      "offer", "accept", "cancel", "reject", "retry", "response", "notify"};
  if (!hasStringValue(getAction(), actions))
    return emitOpError() << "unsupported event action '" << getAction() << "'";
  return success();
}

LogicalResult TransitionOp::verify() {
  if (!isa_and_nonnull<ProtocolOp>(getOperation()->getParentOp()))
    return emitOpError("transition must be a direct child of ac.protocol");
  if (auto priority = getPriority(); priority && *priority > INT64_MAX)
    return emitOpError("transition priority must be a non-negative i64 value");
  WalkResult result = getGuard().walk([&](Operation *operation) {
    if (!isAllowedGuardExpression(operation)) {
      emitOpError() << "guard operation '" << operation->getName()
                    << "' is not in the pure expression allowlist";
      return WalkResult::interrupt();
    }
    auto effects = dyn_cast<MemoryEffectOpInterface>(operation);
    if (!effects) {
      emitOpError() << "allowed guard operation '" << operation->getName()
                    << "' must implement MemoryEffectOpInterface";
      return WalkResult::interrupt();
    }
    SmallVector<MemoryEffects::EffectInstance> instances;
    effects.getEffects(instances);
    if (!instances.empty()) {
      emitOpError() << "allowed guard operation '" << operation->getName()
                    << "' must have no memory effects";
      return WalkResult::interrupt();
    }
    return WalkResult::advance();
  });
  if (result.wasInterrupted())
    return failure();
  return success();
}

LogicalResult GuaranteeOp::verify() {
  if (!isa_and_nonnull<ProtocolOp>(getOperation()->getParentOp()))
    return emitOpError("guarantee must be a direct child of ac.protocol");
  StringRef kind = getKind();
  if (kind == "backpressure")
    return verifyStringGuarantee(
        *this, {"none", "accept", "credit", "capacity", "custom"});
  if (kind == "ordering")
    return verifyStringGuarantee(*this, {"fifo", "per_key", "unordered"});
  if (kind == "delivery")
    return verifyStringGuarantee(
        *this, {"exactly_once", "at_most_once", "best_effort"});
  if (kind == "completion")
    return verifyStringGuarantee(
        *this, {"on_accept", "on_response", "on_terminal_phase"});
  if (kind == "stable_pending") {
    if (!isa<BoolAttr>(getValue()))
      return emitOpError("stable_pending requires a boolean value");
    return success();
  }
  if (kind == "max_inflight")
    return success();
  if (kind == "correlation") {
    auto value = dyn_cast<StringAttr>(getValue());
    if (!value || value.getValue().empty())
      return emitOpError("correlation requires a non-empty field name");
    return success();
  }
  if (kind == "custom_backpressure") {
    auto value = dyn_cast<StringAttr>(getValue());
    if (!value || value.getValue().empty())
      return emitOpError(
          "custom_backpressure requires a non-empty declarative contract");
    return success();
  }
  return emitOpError() << "unknown mandatory protocol guarantee '" << kind
                       << "'";
}

LogicalResult PortOp::verify() {
  auto interface = dyn_cast_or_null<InterfaceOp>(getOperation()->getParentOp());
  if (!interface)
    return emitOpError("port must be a direct child of ac.interface");
  auto channel = dyn_cast<ChannelType>(getType());
  if (!channel)
    return emitOpError("port type must be !ac.channel<T, Protocol>");
  if (failed(verifyRoleReference(*this, interface, getFromAttr(),
                                 "port source")) ||
      failed(verifyRoleReference(*this, interface, getToAttr(), "port target")))
    return failure();
  if (getFromAttr() == getToAttr())
    return emitOpError("port source and target roles must differ");
  RoleOp fromRole = lookupChild<RoleOp>(interface, getFromAttr());
  if (fromRole.getDualAttr() != getToAttr())
    return emitOpError("port source and target roles must be dual");
  if (!isProtocolPayloadType(channel.getElementType()))
    return emitOpError(
        "channel payload type must be a normative ACIR value type");
  if (failed(verifyNamedTypes(*this, channel.getElementType())))
    return failure();
  ProtocolOp protocol = lookupProtocol(*this, channel.getProtocol());
  if (!protocol)
    return emitOpError() << "unresolved channel protocol '@"
                         << channel.getProtocol().getValue() << "'";
  RoleOp protocolFrom = lookupChild<RoleOp>(protocol, getProtocolFromAttr());
  if (!protocolFrom)
    return emitOpError() << "unresolved mapped protocol source role '@"
                         << getProtocolFromAttr().getValue() << "'";
  RoleOp protocolTo = lookupChild<RoleOp>(protocol, getProtocolToAttr());
  if (!protocolTo)
    return emitOpError() << "unresolved mapped protocol target role '@"
                         << getProtocolToAttr().getValue() << "'";
  if (protocolFrom.getDualAttr() != getProtocolToAttr() ||
      protocolTo.getDualAttr() != getProtocolFromAttr())
    return emitOpError("mapped protocol roles must be dual");
  RoleOp toRole = lookupChild<RoleOp>(interface, getToAttr());
  if (fromRole.getCardinality() != protocolFrom.getCardinality() ||
      toRole.getCardinality() != protocolTo.getCardinality())
    return emitOpError(
        "interface and mapped protocol roles must have matching cardinality");
  if (!matchesCarrierEvent(protocol, channel.getElementType(),
                           getProtocolFromAttr(), getProtocolToAttr()))
    return emitOpError() << "channel payload " << channel.getElementType()
                         << " from mapped protocol role '@"
                         << getProtocolFromAttr().getValue() << "' to '@"
                         << getProtocolToAttr().getValue()
                         << "' does not match any carrier event in protocol '@"
                         << channel.getProtocol().getValue() << "'";
  return success();
}

namespace {

FunctionType graphSignature(Operation *op) {
  if (!op)
    return {};
  auto type = op->getAttrOfType<TypeAttr>("function_type");
  return type ? dyn_cast<FunctionType>(type.getValue()) : FunctionType();
}

Operation *lookupGraphSymbol(Operation *from, FlatSymbolRefAttr name) {
  auto file = from->getParentOfType<mlir::ModuleOp>();
  return file ? SymbolTable::lookupSymbolIn(file, name) : nullptr;
}

LogicalResult verifyConcreteDictionary(Operation *op, DictionaryAttr values,
                                       StringRef subject) {
  if (!isConcreteStaticValue(values))
    return op->emitOpError() << subject
                             << " must contain only concrete builtin static "
                                "values";
  LogicalResult result = success();
  values.walk([&](SymbolRefAttr reference) {
    if (SymbolTable::lookupNearestSymbolFrom(op, reference))
      return WalkResult::advance();
    op->emitOpError() << "unresolved static symbol reference '" << reference
                      << "'";
    result = failure();
    return WalkResult::interrupt();
  });
  if (failed(result))
    return failure();
  return success();
}

LogicalResult verifyExactBinding(Operation *op, DictionaryAttr binding,
                                 StringRef subject,
                                 StringRef requiredRegistry) {
  auto registry = binding.getAs<StringAttr>("registry");
  auto name = binding.getAs<StringAttr>("name");
  if (binding.size() != 2 || !registry || registry.getValue().empty() ||
      !name || name.getValue().empty() || registry.getValue() == "generic")
    return op->emitOpError()
           << subject << " requires exact registered {registry, name} metadata";
  if (registry.getValue() != requiredRegistry)
    return op->emitOpError() << subject << " requires registered registry '"
                             << requiredRegistry << "'";
  return success();
}

LogicalResult verifyCallShape(Operation *op, FunctionType signature,
                              TypeRange inputs, TypeRange outputs) {
  if (!signature)
    return op->emitOpError("definition has no canonical module signature");
  if (!llvm::equal(inputs, signature.getInputs()))
    return op->emitOpError("operand types do not match module signature");
  if (!llvm::equal(outputs, signature.getResults()))
    return op->emitOpError("result types do not match module signature");
  return success();
}

LogicalResult verifyStaticArgumentSet(Operation *op, DictionaryAttr arguments,
                                      Operation *definition = nullptr) {
  if (failed(verifyConcreteDictionary(op, arguments, "static arguments")))
    return failure();
  if (!definition)
    return success();
  auto parameters = definition->getAttrOfType<DictionaryAttr>("static_params");
  if (!parameters || parameters.size() != arguments.size())
    return op->emitOpError(
        "static argument names must exactly match definition parameters");
  for (NamedAttribute parameter : parameters) {
    Attribute argument = arguments.get(parameter.getName());
    if (!argument || argument.getTypeID() != parameter.getValue().getTypeID())
      return op->emitOpError(
          "static argument names must exactly match definition parameters");
    auto parameterInteger = dyn_cast<IntegerAttr>(parameter.getValue());
    auto argumentInteger = dyn_cast<IntegerAttr>(argument);
    if (parameterInteger &&
        parameterInteger.getType() != argumentInteger.getType())
      return op->emitOpError()
             << "static argument '" << parameter.getName().getValue()
             << "' must match parameter attribute type "
             << parameterInteger.getType();
  }
  return success();
}

bool isStructuralGraphChild(Operation &child) {
  return isa<InstanceOp, ArrayOp, InstancesOp, ViewOp, ReturnOp>(child);
}

bool isStableHierarchySegment(StringRef segment) {
  return !segment.empty() && llvm::all_of(segment, [](char c) {
    return llvm::isAlnum(c) || c == '_' || c == '-';
  });
}

} // namespace

LogicalResult SystemOp::verify() {
  if (!isStableHierarchySegment(getRootName()))
    return emitOpError(
        "root instance name must be one stable hierarchy segment");
  if (getTickEpoch() != 0)
    return emitOpError("global tick epoch must be exactly 0");
  if (!hasStringValue(getTickUnit(), {"cycle", "ps", "ns", "us", "ms", "s"}))
    return emitOpError() << "unsupported exact global tick unit '"
                         << getTickUnit() << "'";
  if (failed(verifyConcreteDictionary(*this, getSeedPolicy(), "seed policy")) ||
      failed(
          verifyConcreteDictionary(*this, getResultSchema(), "result schema")))
    return failure();
  auto seedKind = getSeedPolicy().getAs<StringAttr>("kind");
  auto seedValue = getSeedPolicy().getAs<IntegerAttr>("value");
  if (getSeedPolicy().size() != 2 || !seedKind ||
      seedKind.getValue() != "fixed" || !seedValue)
    return emitOpError("seed policy requires exact {kind = \"fixed\", value = "
                       "integer} schema");
  auto resultKind = getResultSchema().getAs<StringAttr>("kind");
  if (!resultKind || resultKind.getValue().empty())
    return emitOpError("result schema requires a non-empty string 'kind'");
  if (!llvm::all_of(getInstrumentation(), [](Attribute value) {
        return isa<StringAttr, SymbolRefAttr>(value);
      }))
    return emitOpError(
        "instrumentation must be an ordered list of static names");
  return success();
}

ParseResult ModuleOp::parse(OpAsmParser &parser, OperationState &result) {
  StringAttr name;
  SmallVector<OpAsmParser::Argument> arguments;
  SmallVector<Type> results;
  if (parser.parseSymbolName(name, SymbolTable::getSymbolAttrName(),
                             result.attributes) ||
      parser.parseArgumentList(arguments, OpAsmParser::Delimiter::Paren,
                               /*allowType=*/true,
                               /*allowAttrs=*/false) ||
      parser.parseOptionalArrowTypeList(results))
    return failure();

  DictionaryAttr staticParameters;
  if (succeeded(parser.parseOptionalKeyword("parameters"))) {
    if (parser.parseAttribute(staticParameters))
      return failure();
  } else {
    staticParameters = parser.getBuilder().getDictionaryAttr({});
  }
  result.addAttribute("static_params", staticParameters);
  if (parser.parseOptionalAttrDictWithKeyword(result.attributes) ||
      parser.parseKeyword("graph"))
    return failure();

  SmallVector<Type> inputs;
  inputs.reserve(arguments.size());
  for (const OpAsmParser::Argument &argument : arguments)
    inputs.push_back(argument.type);
  result.addAttribute(
      "function_type",
      TypeAttr::get(parser.getBuilder().getFunctionType(inputs, results)));
  Region *body = result.addRegion();
  return parser.parseRegion(*body, arguments, /*enableNameShadowing=*/true);
}

void ModuleOp::print(OpAsmPrinter &printer) {
  printer << ' ';
  printer.printSymbolName(getSymName());
  function_interface_impl::printFunctionSignature(
      printer, *this, getArgumentTypes(), /*isVariadic=*/false,
      getResultTypes());
  printer << " parameters " << getStaticParams();
  printer.printOptionalAttrDictWithKeyword(
      (*this)->getAttrs(), {SymbolTable::getSymbolAttrName(), "function_type",
                            "static_params", "arg_attrs", "res_attrs"});
  printer << " graph ";
  printer.printRegion(getBody(), /*printEntryBlockArgs=*/false,
                      /*printBlockTerminators=*/true,
                      /*printEmptyBlock=*/true);
}

LogicalResult ModuleOp::verify() {
  if (failed(verifyConcreteDictionary(*this, getStaticParams(),
                                      "static parameters")))
    return failure();
  if (getBody().empty())
    return emitOpError("module requires one Graph body block");
  Block &entry = getBody().front();
  if (!llvm::equal(entry.getArgumentTypes(), getFunctionType().getInputs()))
    return emitOpError("Graph region arguments must match module signature");
  for (Operation &child : entry)
    if (!isStructuralGraphChild(child))
      return child.emitOpError(
          "operation is not legal in an ac.module structural Graph region");
  if (entry.empty() || !isa<ReturnOp>(entry.back()))
    return emitOpError("module Graph region must end with ac.return");
  return success();
}

LogicalResult ModuleExternOp::verify() {
  if (failed(verifyConcreteDictionary(*this, getStaticParams(),
                                      "static parameters")))
    return failure();
  return verifyExactBinding(*this, getImplementation(),
                            "external module implementation", "cpp");
}

LogicalResult ModuleGeneratedOp::verify() {
  if (failed(verifyConcreteDictionary(*this, getStaticParams(),
                                      "static parameters")))
    return failure();
  return verifyExactBinding(*this, getGenerator(), "generated module", "ac");
}

LogicalResult InstanceOp::verify() {
  Operation *definition = lookupGraphSymbol(*this, getDefinitionAttr());
  if (!isa_and_nonnull<ModuleOp, ModuleExternOp, ModuleGeneratedOp>(definition))
    return emitOpError() << "unresolved module definition '"
                         << getDefinitionAttr() << "'";
  if (!isStableHierarchySegment(getSymName()) ||
      !isStableHierarchySegment(getStableId()) ||
      !isStableHierarchySegment(getPath()))
    return emitOpError(
        "instance name, stable id, and path must be stable local segments");
  if (failed(verifyStaticArgumentSet(*this, getStaticArgs(), definition)))
    return failure();
  return verifyCallShape(*this, graphSignature(definition),
                         getInputs().getTypes(), getOutputs().getTypes());
}

LogicalResult ArrayOp::verify() {
  Operation *definition = lookupGraphSymbol(*this, getDefinitionAttr());
  if (!isa_and_nonnull<ModuleOp, ModuleExternOp, ModuleGeneratedOp>(definition))
    return emitOpError() << "unresolved array element definition '"
                         << getDefinitionAttr() << "'";
  if (!isStableHierarchySegment(getSymName()) ||
      !isStableHierarchySegment(getStableId()) ||
      !isStableHierarchySegment(getPath()))
    return emitOpError(
        "array name, stable id, and path must be stable local segments");
  if (getShape().empty())
    return emitOpError("array shape must have at least one dimension");
  uint64_t count = 1;
  for (int64_t extent : getShape()) {
    if (extent < 0)
      return emitOpError("array shape dimensions must be non-negative");
    if (extent != 0 && count > std::numeric_limits<uint64_t>::max() /
                                   static_cast<uint64_t>(extent))
      return emitOpError("array cardinality overflows 64 bits");
    count *= static_cast<uint64_t>(extent);
  }
  constexpr uint64_t maxStaticElements = 1U << 20;
  if (count > maxStaticElements)
    return emitOpError(
        "array cardinality exceeds static elaboration bound 1048576");
  FunctionType signature = graphSignature(definition);
  if (!signature)
    return emitOpError("array element definition has no signature");
  if (getStaticArgs().size() != count)
    return emitOpError("array requires one concrete static argument set per "
                       "lexicographically ordered element");
  for (Attribute value : getStaticArgs()) {
    auto arguments = dyn_cast<DictionaryAttr>(value);
    if (!arguments ||
        failed(verifyStaticArgumentSet(*this, arguments, definition)))
      return emitOpError(
          "array static arguments must be concrete dictionaries");
  }
  SmallVector<Type> expectedInputs;
  SmallVector<Type> expectedOutputs;
  for (uint64_t index = 0; index < count; ++index) {
    expectedInputs.append(signature.getInputs().begin(),
                          signature.getInputs().end());
    expectedOutputs.append(signature.getResults().begin(),
                           signature.getResults().end());
  }
  if (!llvm::equal(getInputs().getTypes(), expectedInputs) ||
      !llvm::equal(getOutputs().getTypes(), expectedOutputs))
    return emitOpError("array flattened interface shape does not match element "
                       "signature and static cardinality");
  return success();
}

LogicalResult InstancesOp::verify() {
  size_t count = getDefinitions().size();
  if (count == 0 || getNames().size() != count ||
      getStableIds().size() != count || getPaths().size() != count ||
      getStaticArgs().size() != count)
    return emitOpError("ordered instance metadata arrays must have identical "
                       "non-zero cardinality");
  llvm::SmallDenseSet<StringRef> names;
  llvm::SmallDenseSet<StringRef> ids;
  for (size_t index = 0; index < count; ++index) {
    auto definition = dyn_cast<FlatSymbolRefAttr>(getDefinitions()[index]);
    if (!definition)
      return emitOpError("definitions must contain flat module symbols");
    Operation *target = lookupGraphSymbol(*this, definition);
    if (!isa_and_nonnull<ModuleOp, ModuleExternOp, ModuleGeneratedOp>(target))
      return emitOpError() << "unresolved collection definition '" << definition
                           << "'";
    if (graphSignature(target) != getInterface())
      return emitOpError("collection element definition does not implement "
                         "the exact declared common interface");
    auto arguments = dyn_cast<DictionaryAttr>(getStaticArgs()[index]);
    if (!arguments || failed(verifyStaticArgumentSet(*this, arguments, target)))
      return emitOpError("collection static arguments must be concrete "
                         "dictionaries");
    StringRef name = cast<StringAttr>(getNames()[index]).getValue();
    StringRef id = cast<StringAttr>(getStableIds()[index]).getValue();
    StringRef path = cast<StringAttr>(getPaths()[index]).getValue();
    if (!isStableHierarchySegment(name) || !names.insert(name).second ||
        !isStableHierarchySegment(id) || !ids.insert(id).second)
      return emitOpError("collection names and stable ids must be non-empty "
                         "and unique in declared order");
    if (!isStableHierarchySegment(path))
      return emitOpError(
          "collection paths must be stable parent-relative segments");
  }
  SmallVector<Type> expectedInputs;
  SmallVector<Type> expectedOutputs;
  for (size_t index = 0; index < count; ++index) {
    expectedInputs.append(getInterface().getInputs().begin(),
                          getInterface().getInputs().end());
    expectedOutputs.append(getInterface().getResults().begin(),
                           getInterface().getResults().end());
  }
  if (!llvm::equal(getInputs().getTypes(), expectedInputs) ||
      !llvm::equal(getOutputs().getTypes(), expectedOutputs))
    return emitOpError("ordered collection IO does not match its common "
                       "interface shape");
  return success();
}

LogicalResult ViewOp::verify() {
  ArrayRef<int64_t> indices = getIndices();
  ArrayRef<int64_t> shape = getShape();
  if (shape.empty() ||
      llvm::any_of(shape, [](int64_t value) { return value < 0; }))
    return emitOpError("view shape must be fully static and non-negative");
  auto requireIndex = [&](int64_t index) {
    return index >= 0 && static_cast<size_t>(index) < getInputs().size();
  };
  SmallVector<Type> expected;
  StringRef kind = getKind();
  if (kind == "selection") {
    if (indices.size() != 1 || !requireIndex(indices[0]))
      return emitOpError("selection requires one in-bounds constant index");
    expected.push_back(getInputs()[indices[0]].getType());
  } else if (kind == "slice") {
    if (indices.size() < 2 || indices.size() > 3)
      return emitOpError(
          "slice requires constant start, end, and optional step");
    int64_t start = indices[0], end = indices[1];
    int64_t step = indices.size() == 3 ? indices[2] : 1;
    if (step <= 0 || start < 0 || end < start ||
        end > static_cast<int64_t>(getInputs().size()))
      return emitOpError("slice bounds and step are invalid");
    for (int64_t index = start; index < end; index += step)
      expected.push_back(getInputs()[index].getType());
  } else if (kind == "concat" || kind == "elementwise_binding") {
    if (!indices.empty())
      return emitOpError() << kind << " view does not accept index metadata";
    auto inputTypes = getInputs().getTypes();
    expected.append(inputTypes.begin(), inputTypes.end());
  } else if (kind == "zip") {
    if (!indices.empty())
      return emitOpError("zip view does not accept index metadata");
    if (getInputs().size() % 2 != 0)
      return emitOpError("zip view requires equal source cardinalities");
    size_t half = getInputs().size() / 2;
    for (size_t index = 0; index < half; ++index) {
      expected.push_back(getInputs()[index].getType());
      expected.push_back(getInputs()[index + half].getType());
    }
  } else if (kind == "permutation") {
    if (indices.size() != getInputs().size())
      return emitOpError("permutation requires one index per input");
    llvm::SmallDenseSet<int64_t> seen;
    for (int64_t index : indices) {
      if (!requireIndex(index) || !seen.insert(index).second)
        return emitOpError(
            "permutation indices must be an in-bounds bijection");
      expected.push_back(getInputs()[index].getType());
    }
  } else {
    return emitOpError() << "unsupported static view kind '" << kind << "'";
  }
  uint64_t cardinality = 1;
  for (int64_t extent : shape) {
    if (extent != 0 && cardinality > std::numeric_limits<uint64_t>::max() /
                                         static_cast<uint64_t>(extent))
      return emitOpError("view cardinality overflows 64 bits");
    cardinality *= static_cast<uint64_t>(extent);
  }
  if (cardinality != expected.size() ||
      !llvm::equal(getOutputs().getTypes(), expected))
    return emitOpError("resolved view shape/order/types do not match outputs");
  return success();
}

LogicalResult ReturnOp::verify() {
  ModuleOp module = getOperation()->getParentOfType<ModuleOp>();
  if (!module)
    return emitOpError("must terminate an ac.module Graph region");
  if (!llvm::equal(getOperandTypes(), module.getFunctionType().getResults()))
    return emitOpError("operand types and count must exactly match module "
                       "results");
  if (llvm::any_of(getOperandTypes(),
                   [](Type type) { return isa<ResourceTokenType>(type); }))
    return emitOpError(
        "private ownership handle cannot be exported from ac.module");
  return success();
}

LogicalResult verifyTopologyTypeUses(Operation *operation) {
  if (failed(verifyGraphStructure(operation)))
    return failure();
  auto verifyType = [&](Type type, Value value) -> LogicalResult {
    if (Type nested = findNestedTopologyLeaf(type))
      return operation->emitOpError() << "topology type " << nested
                                      << " cannot be nested inside " << type;
    if (auto flow = dyn_cast<FlowType>(type)) {
      if (!isProtocolPayloadType(flow.getElementType()))
        return operation->emitOpError(
            "flow payload type must be a normative ACIR value type");
      if (failed(verifyNamedTypes(operation, flow.getElementType())))
        return failure();
      ProtocolOp protocol = lookupProtocol(operation, flow.getProtocol());
      if (!protocol)
        return operation->emitOpError() << "unresolved flow protocol '@"
                                        << flow.getProtocol().getValue() << "'";
      if (!matchesCarrierEvent(protocol, flow.getElementType()))
        return operation->emitOpError()
               << "flow payload " << flow.getElementType()
               << " does not match any carrier event in protocol '@"
               << flow.getProtocol().getValue() << "'";
      if (value && !value.hasOneUse() && !value.use_empty())
        return operation->emitOpError(
            "flow value has more than one functional use");
    }
    if (auto endpoint = dyn_cast<EndpointType>(type)) {
      auto module = operation->getParentOfType<mlir::ModuleOp>();
      InterfaceOp interface =
          module ? dyn_cast_or_null<InterfaceOp>(SymbolTable::lookupSymbolIn(
                       module, endpoint.getInterface()))
                 : InterfaceOp();
      if (!interface)
        return operation->emitOpError()
               << "unresolved endpoint interface '@"
               << endpoint.getInterface().getValue() << "'";
      RoleOp role = lookupChild<RoleOp>(interface, endpoint.getRole());
      if (!role)
        return operation->emitOpError()
               << "endpoint role '@" << endpoint.getRole().getValue()
               << "' is not a member of interface '@"
               << endpoint.getInterface().getValue() << "'";
      if (role.getCardinality() == "exclusive" && value && !value.hasOneUse() &&
          !value.use_empty())
        return operation->emitOpError(
            "exclusive endpoint value has more than one structural use");
    }
    return success();
  };

  auto verifyAttribute = [&](Attribute attribute) -> LogicalResult {
    if (!attribute)
      return success();
    LogicalResult result = success();
    attribute.walk([&](TypeAttr type) {
      if (failed(verifyType(type.getValue(), {}))) {
        result = failure();
        return WalkResult::interrupt();
      }
      return WalkResult::advance();
    });
    if (failed(result))
      return failure();
    attribute.walk([&](Type type) {
      if (failed(verifyType(type, {}))) {
        result = failure();
        return WalkResult::interrupt();
      }
      return WalkResult::advance();
    });
    return result;
  };

  for (Value result : operation->getResults())
    if (failed(verifyType(result.getType(), result)))
      return failure();
  for (OpOperand &operand : operation->getOpOperands())
    if (failed(verifyType(operand.get().getType(), operand.get())))
      return failure();
  for (Region &region : operation->getRegions())
    for (Block &block : region)
      for (BlockArgument argument : block.getArguments())
        if (failed(verifyType(argument.getType(), argument)))
          return failure();
  for (NamedAttribute attribute : operation->getAttrs())
    if (failed(verifyAttribute(attribute.getValue())))
      return failure();
  if (failed(verifyAttribute(operation->getPropertiesAsAttribute())) ||
      failed(verifyAttribute(LocationAttr(operation->getLoc()))))
    return failure();
  return success();
}

} // namespace acir::ac

#define GET_OP_CLASSES
#include "acir/Dialect/ACIR/ACIROps.cpp.inc"
