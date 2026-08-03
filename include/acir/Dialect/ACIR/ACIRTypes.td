#ifndef ACIR_DIALECT_ACIR_ACIRTYPES_TD
#define ACIR_DIALECT_ACIR_ACIRTYPES_TD

include "acir/Dialect/ACIR/ACIRAttributes.td"
include "mlir/IR/AttrTypeBase.td"

class ACIR_Type<string name, string typeMnemonic>
    : TypeDef<ACIR_Dialect, name> {
  let mnemonic = typeMnemonic;
}

class ACIR_NamedType<string name, string mnemonic>
    : ACIR_Type<name, mnemonic> {
  let parameters = (ins "mlir::FlatSymbolRefAttr":$name);
  let assemblyFormat = "`<` $name `>`";
}

def ACIR_StructType : ACIR_NamedType<"Struct", "struct">;
def ACIR_PacketType : ACIR_NamedType<"Packet", "packet">;
def ACIR_TransactionType : ACIR_NamedType<"Transaction", "transaction">;
def ACIR_EnumType : ACIR_NamedType<"Enum", "enum">;
def ACIR_UnionType : ACIR_NamedType<"Union", "union">;
def ACIR_AddressType : ACIR_NamedType<"Address", "address">;
def ACIR_ResourceTokenType
    : ACIR_NamedType<"ResourceToken", "resource_token">;

def ACIR_OptionalType : ACIR_Type<"Optional", "optional"> {
  let parameters = (ins "mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
  let genVerifyDecl = 1;
}

def ACIR_ListType : ACIR_Type<"List", "list"> {
  let parameters = (ins "mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
  let genVerifyDecl = 1;
}

def ACIR_VectorType : ACIR_Type<"Vector", "vector"> {
  let parameters = (ins "int64_t":$length, "mlir::Type":$elementType);
  let assemblyFormat = "`<` $length `x` $elementType `>`";
  let genVerifyDecl = 1;
}

def ACIR_FlowType : ACIR_Type<"Flow", "flow"> {
  let parameters = (ins "mlir::Type":$elementType,
                        "mlir::FlatSymbolRefAttr":$protocol);
  let assemblyFormat = "`<` $elementType `,` $protocol `>`";
  let genVerifyDecl = 1;
}

def ACIR_EndpointType : ACIR_Type<"Endpoint", "endpoint"> {
  let parameters = (ins "mlir::FlatSymbolRefAttr":$interface,
                        "mlir::FlatSymbolRefAttr":$role);
  let assemblyFormat = "`<` $interface `,` $role `>`";
}

def ACIR_ResourceRefType : ACIR_Type<"ResourceRef", "resource_ref"> {
  let parameters = (ins "mlir::FlatSymbolRefAttr":$resourceType,
                        "mlir::FlatSymbolRefAttr":$role);
  let assemblyFormat = "`<` $resourceType `,` $role `>`";
}

def ACIR_ChannelType : ACIR_Type<"Channel", "channel"> {
  let parameters = (ins "mlir::Type":$elementType,
                        "mlir::FlatSymbolRefAttr":$protocol);
  let assemblyFormat = "`<` $elementType `,` $protocol `>`";
  let genVerifyDecl = 1;
}

def ACIR_DurationType : ACIR_Type<"Duration", "duration"> {
  let parameters = (ins EnumParameter<ACIR_Unit>:$unit);
  let assemblyFormat = "`<` $unit `>`";
  let genVerifyDecl = 1;
}

def ACIR_RateType : ACIR_Type<"Rate", "rate"> {
  let parameters = (ins EnumParameter<ACIR_Unit>:$numerator,
                        EnumParameter<ACIR_Unit>:$denominator);
  let assemblyFormat = "`<` $numerator `,` $denominator `>`";
  let genVerifyDecl = 1;
}

def ACIR_EventType : ACIR_Type<"Event", "event"> {
  let parameters = (ins "mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
  let genVerifyDecl = 1;
}

#endif // ACIR_DIALECT_ACIR_ACIRTYPES_TD
