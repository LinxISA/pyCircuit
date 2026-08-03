#ifndef ACIR_DIALECT_ACIR_ACIROPS_TD
#define ACIR_DIALECT_ACIR_ACIROPS_TD

include "acir/Dialect/ACIR/ACIRInterfaces.td"
include "acir/Dialect/ACIR/ACIRTypes.td"
include "mlir/IR/SymbolInterfaces.td"
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
  let arguments = (ins SymbolNameAttr:$sym_name, StrArrayAttr:$field_names,
                       TypeArrayAttr:$field_types);
  let hasVerifier = 1;
}

def ACIR_StructOp : ACIR_RecordDeclarationOp<"struct">;

def ACIR_EnumOp : ACIR_Op<"enum", [Symbol]> {
  let arguments = (ins SymbolNameAttr:$sym_name, StrArrayAttr:$enumerants);
  let hasVerifier = 1;
}

def ACIR_UnionOp : ACIR_Op<"union", [Symbol]> {
  let arguments = (ins SymbolNameAttr:$sym_name, StrArrayAttr:$field_names,
                       TypeArrayAttr:$field_types, StrAttr:$discriminator);
  let hasVerifier = 1;
}

def ACIR_PacketOp : ACIR_Op<"packet", [Symbol, ACIR_RecordLike]> {
  let arguments = (ins SymbolNameAttr:$sym_name, StrArrayAttr:$field_names,
                       TypeArrayAttr:$field_types, DictionaryAttr:$serialization);
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
  let arguments = (ins AnyType:$packet_value, FlatSymbolRefAttr:$packet);
  let results = (outs AnyType:$bytes);
  let hasVerifier = 1;
}

def ACIR_PacketDeserializeOp : ACIR_Op<"packet.deserialize", [Pure]> {
  let arguments = (ins AnyType:$bytes, FlatSymbolRefAttr:$packet);
  let results = (outs AnyType:$packet_value);
  let hasVerifier = 1;
}

#endif
