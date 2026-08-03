#include "acir/Dialect/ACIR/ACIROps.h"

#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
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
    if (!scope)
      if (auto module = from->getParentOfType<ModuleOp>())
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

template <typename OpTy>
OpTy lookupChild(Operation *container, FlatSymbolRefAttr name) {
  return dyn_cast_or_null<OpTy>(SymbolTable::lookupSymbolIn(container, name));
}

ProtocolOp lookupProtocol(Operation *from, FlatSymbolRefAttr name) {
  auto module = from->getParentOfType<ModuleOp>();
  return module ? dyn_cast_or_null<ProtocolOp>(
                      SymbolTable::lookupSymbolIn(module, name))
                : ProtocolOp();
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
    if (event.getAction() != "offer")
      continue;
    if (transition.getTransfer())
      continue;
    if (!transition.getRetain())
      return transition.emitOpError("offered packet must transfer, cancel, "
                                    "reject, or be retained for retry");
    if (!stable)
      return transition.emitOpError(
          "retained pending offer requires stable_pending = true");

    bool resolved = false;
    for (TransitionOp next : transitions) {
      if (next.getSourceAttr() != transition.getTargetAttr())
        continue;
      EventOp nextEvent = lookupChild<EventOp>(*this, next.getEventAttr());
      if (next.getTransfer() || nextEvent.getAction() == "cancel" ||
          nextEvent.getAction() == "reject" ||
          nextEvent.getAction() == "retry") {
        resolved = true;
        break;
      }
    }
    if (!resolved)
      return transition.emitOpError("retained offer has no transfer, cancel, "
                                    "reject, or retry transition");
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
      if (value.getValue() == "on_response" &&
          llvm::none_of(getBody().getOps<EventOp>(), [](EventOp event) {
            return event.getAction() == "response";
          }))
        return completion.emitOpError(
            "on_response completion requires a response event");
      if (value.getValue() == "on_terminal_phase" &&
          llvm::none_of(getBody().getOps<StateOp>(),
                        [](StateOp state) { return state.getTerminal(); }))
        return completion.emitOpError(
            "on_terminal_phase completion requires a terminal state");
    }
  }
  GuaranteeOp correlation = findGuarantee(*this, "correlation");
  if (correlation) {
    auto field = dyn_cast<StringAttr>(correlation.getValue());
    if (field && !field.getValue().empty()) {
      bool found = false;
      for (EventOp event : getBody().getOps<EventOp>()) {
        Operation *declaration = recordDecl(event, event.getPayload());
        if (declaration && findField(declaration, field.getValue())) {
          found = true;
          break;
        }
      }
      if (!found)
        return correlation.emitOpError()
               << "correlation field '" << field.getValue()
               << "' does not resolve in any event payload";
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
    if (isMemoryEffectFree(operation))
      return WalkResult::advance();
    emitOpError() << "protocol guard must be pure; found "
                  << operation->getName();
    return WalkResult::interrupt();
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
  for (EventOp event : protocol.getBody().getOps<EventOp>())
    if (event.getPayload() != channel.getElementType())
      return emitOpError() << "channel payload " << channel.getElementType()
                           << " does not match protocol event '@"
                           << event.getSymName() << "' payload "
                           << event.getPayload();
  return success();
}

LogicalResult verifyTopologyTypeUses(Operation *operation) {
  auto verifyType = [&](Type type, Value value) -> LogicalResult {
    if (auto flow = dyn_cast<FlowType>(type)) {
      if (!isProtocolPayloadType(flow.getElementType()))
        return operation->emitOpError(
            "flow payload type must be a normative ACIR value type");
      if (failed(verifyNamedTypes(operation, flow.getElementType())))
        return failure();
      if (!lookupProtocol(operation, flow.getProtocol()))
        return operation->emitOpError() << "unresolved flow protocol '@"
                                        << flow.getProtocol().getValue() << "'";
      if (value && !value.hasOneUse() && !value.use_empty())
        return operation->emitOpError(
            "flow value has more than one functional use");
    }
    if (auto endpoint = dyn_cast<EndpointType>(type)) {
      auto module = operation->getParentOfType<ModuleOp>();
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

  for (Value result : operation->getResults())
    if (failed(verifyType(result.getType(), result)))
      return failure();
  for (Region &region : operation->getRegions())
    for (Block &block : region)
      for (BlockArgument argument : block.getArguments())
        if (failed(verifyType(argument.getType(), argument)))
          return failure();
  return success();
}

} // namespace acir::ac

#define GET_OP_CLASSES
#include "acir/Dialect/ACIR/ACIROps.cpp.inc"
