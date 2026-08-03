#ifndef ACIR_DIALECT_ACIR_ACIROPS_H
#define ACIR_DIALECT_ACIR_ACIROPS_H

#include "acir/Dialect/ACIR/ACIRTypes.h"
#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "acir/Dialect/ACIR/ACIROpInterfaces.h.inc"

#define GET_OP_CLASSES
#include "acir/Dialect/ACIR/ACIROps.h.inc"

#endif
