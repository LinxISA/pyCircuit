include "mlir/IR/DialectBase.td"

def ACSim_Dialect : Dialect {
  let name = "acsim";
  let cppNamespace = "::acir::acsim";
  let summary = "Agentic Circuit simulator dialect";
}
