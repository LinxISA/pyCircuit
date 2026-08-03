#ifndef ACIR_DIALECT_ACIR_ACIRINTERFACES_TD
#define ACIR_DIALECT_ACIR_ACIRINTERFACES_TD

include "mlir/IR/OpBase.td"

def ACIR_RecordLike : OpInterface<"RecordLikeOpInterface"> {
  let cppNamespace = "::acir::ac";
  let methods = [
    InterfaceMethod<"Returns ordered field dictionaries.", "::mlir::ArrayAttr",
                    "getRecordFields", (ins), [{
      return $_op.getFields();
    }]>
  ];
}

def ACIR_InterfaceContainer : OpInterface<"InterfaceContainerOpInterface"> {
  let cppNamespace = "::acir::ac";
  let methods = [
    InterfaceMethod<"Returns the declaration region containing roles and ports.",
                    "::mlir::Region &", "getInterfaceBody", (ins), [{
      return $_op.getBody();
    }]>
  ];
}

def ACIR_ProtocolContainer : OpInterface<"ProtocolContainerOpInterface"> {
  let cppNamespace = "::acir::ac";
  let methods = [
    InterfaceMethod<"Returns the finite-state protocol declaration region.",
                    "::mlir::Region &", "getProtocolBody", (ins), [{
      return $_op.getBody();
    }]>
  ];
}

#endif
