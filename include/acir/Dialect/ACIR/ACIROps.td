#ifndef ACIR_DIALECT_ACIR_ACIROPS_TD
#define ACIR_DIALECT_ACIR_ACIROPS_TD

include "acir/Dialect/ACIR/ACIRInterfaces.td"
include "acir/Dialect/ACIR/ACIRTypes.td"
include "mlir/IR/SymbolInterfaces.td"
include "mlir/IR/RegionKindInterface.td"
include "mlir/Interfaces/DataLayoutInterfaces.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

class ACIR_Op<string mnemonic, list<Trait> traits = []>
    : Op<ACIR_Dialect, mnemonic, traits>;

def ACIR_TypeScopeOp : ACIR_Op<"type_scope", [
    Symbol, SymbolTable, SingleBlock, NoTerminator,
    DeclareOpInterfaceMethods<DataLayoutOpInterface>
  ]> {
  let arguments = (ins SymbolNameAttr:$sym_name);
  let regions = (region SizedRegion<1>:$body);
  let builders = [OpBuilder<(ins "::llvm::StringRef":$name), [{
    $_state.addAttribute(::mlir::SymbolTable::getSymbolAttrName(),
                        $_builder.getStringAttr(name));
    $_state.addRegion();
  }]>];
}

def ACIR_TypeAliasOp : ACIR_Op<"type_alias", [Symbol]> {
  let arguments = (ins SymbolNameAttr:$sym_name, TypeAttr:$target);
  let hasVerifier = 1;
}

class ACIR_RecordDeclarationOp<string mnemonic>
    : ACIR_Op<mnemonic, [Symbol, ACIR_RecordLike]> {
  let arguments = (ins SymbolNameAttr:$sym_name, ArrayAttr:$fields);
  let hasVerifier = 1;
}

def ACIR_StructOp : ACIR_RecordDeclarationOp<"struct">;

def ACIR_EnumOp : ACIR_Op<"enum", [Symbol]> {
  let arguments = (ins SymbolNameAttr:$sym_name, StrArrayAttr:$enumerants);
  let hasVerifier = 1;
}

def ACIR_UnionOp : ACIR_Op<"union", [Symbol]> {
  let arguments = (ins SymbolNameAttr:$sym_name, ArrayAttr:$fields,
                       StrAttr:$discriminator);
  let hasVerifier = 1;
}

def ACIR_PacketOp : ACIR_Op<"packet", [Symbol, ACIR_RecordLike]> {
  let arguments = (ins SymbolNameAttr:$sym_name, ArrayAttr:$fields);
  let hasVerifier = 1;
}

def ACIR_TransactionOp : ACIR_RecordDeclarationOp<"transaction">;

def ACIR_RecordCreateOp : ACIR_Op<"record.create", [Pure]> {
  let arguments = (ins Variadic<AnyType>:$values, StrArrayAttr:$field_names);
  let results = (outs AnyType:$result);
  let hasVerifier = 1;
}

def ACIR_RecordGetOp : ACIR_Op<"record.get", [Pure]> {
  let arguments = (ins AnyType:$record, StrAttr:$field);
  let results = (outs AnyType:$result);
  let hasVerifier = 1;
}

def ACIR_RecordWithOp : ACIR_Op<"record.with", [Pure]> {
  let arguments = (ins AnyType:$record, AnyType:$value, StrAttr:$field);
  let results = (outs AnyType:$result);
  let hasVerifier = 1;
}

def ACIR_PacketSerializeOp : ACIR_Op<"packet.serialize", [Pure]> {
  let arguments = (ins AnyType:$packet_value, SymbolRefAttr:$packet);
  let results = (outs AnyType:$bytes);
  let hasVerifier = 1;
}

def ACIR_PacketDeserializeOp : ACIR_Op<"packet.deserialize", [Pure]> {
  let arguments = (ins AnyType:$bytes, SymbolRefAttr:$packet);
  let results = (outs AnyType:$packet_value);
  let hasVerifier = 1;
}

def ACIR_InterfaceOp : ACIR_Op<"interface", [
    Symbol, SymbolTable, SingleBlock, NoTerminator, IsolatedFromAbove,
    RegionKindInterface, HasOnlyGraphRegion, ACIR_InterfaceContainer
  ]> {
  let arguments = (ins SymbolNameAttr:$sym_name);
  let regions = (region SizedRegion<1>:$body);
  let builders = [OpBuilder<(ins "::llvm::StringRef":$name), [{
    $_state.addAttribute(::mlir::SymbolTable::getSymbolAttrName(),
                        $_builder.getStringAttr(name));
    $_state.addRegion();
  }]>];
  let hasVerifier = 1;
}

def ACIR_ProtocolOp : ACIR_Op<"protocol", [
    Symbol, SymbolTable, SingleBlock, NoTerminator, IsolatedFromAbove,
    RegionKindInterface, HasOnlyGraphRegion, ACIR_ProtocolContainer
  ]> {
  let arguments = (ins SymbolNameAttr:$sym_name);
  let regions = (region SizedRegion<1>:$body);
  let builders = [OpBuilder<(ins "::llvm::StringRef":$name), [{
    $_state.addAttribute(::mlir::SymbolTable::getSymbolAttrName(),
                        $_builder.getStringAttr(name));
    $_state.addRegion();
  }]>];
  let hasVerifier = 1;
}

def ACIR_RoleOp : ACIR_Op<"role", [Symbol, Pure]> {
  let arguments = (ins SymbolNameAttr:$sym_name, FlatSymbolRefAttr:$dual,
                       StrAttr:$cardinality);
  let hasVerifier = 1;
}

def ACIR_StateOp : ACIR_Op<"state", [Symbol, Pure]> {
  let arguments = (ins SymbolNameAttr:$sym_name, BoolAttr:$initial,
                       BoolAttr:$terminal);
  let hasVerifier = 1;
}

def ACIR_EventOp : ACIR_Op<"event", [Symbol, Pure]> {
  let arguments = (ins SymbolNameAttr:$sym_name, FlatSymbolRefAttr:$from,
                       FlatSymbolRefAttr:$to, TypeAttr:$payload,
                       StrAttr:$action);
  let hasVerifier = 1;
}

def ACIR_TransitionOp : ACIR_Op<"transition", [SingleBlock, NoTerminator]> {
  let arguments = (ins FlatSymbolRefAttr:$source, FlatSymbolRefAttr:$target,
                       FlatSymbolRefAttr:$event,
                       OptionalAttr<I64Attr>:$priority,
                       DefaultValuedAttr<BoolAttr, "false">:$transfer,
                       DefaultValuedAttr<BoolAttr, "false">:$retain);
  let regions = (region AnyRegion:$guard);
  let hasVerifier = 1;
}

def ACIR_GuaranteeOp : ACIR_Op<"guarantee", [Pure]> {
  let arguments = (ins StrAttr:$kind, AnyAttr:$value);
  let hasVerifier = 1;
}

def ACIR_PortOp : ACIR_Op<"port", [Symbol, Pure]> {
  let arguments = (ins SymbolNameAttr:$sym_name, TypeAttr:$type,
                       FlatSymbolRefAttr:$from, FlatSymbolRefAttr:$to);
  let hasVerifier = 1;
}

#endif
