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

#endif
