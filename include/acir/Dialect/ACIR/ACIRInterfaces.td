#ifndef ACIR_DIALECT_ACIR_ACIRINTERFACES_TD
#define ACIR_DIALECT_ACIR_ACIRINTERFACES_TD

include "mlir/IR/OpBase.td"

def ACIR_RecordLike : OpInterface<"RecordLikeOpInterface"> {
  let cppNamespace = "::acir::ac";
  let methods = [
    InterfaceMethod<"Returns ordered field names.", "::mlir::ArrayAttr",
                    "getRecordFieldNames", (ins), [{
      return $_op.getFieldNames();
    }]>,
    InterfaceMethod<"Returns ordered field types.", "::mlir::ArrayAttr",
                    "getRecordFieldTypes", (ins), [{
      return $_op.getFieldTypes();
    }]>
  ];
}

#endif
