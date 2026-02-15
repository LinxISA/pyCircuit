#include "pyc/Dialect/PYC/PYCTypes.h"

#include "pyc/Dialect/PYC/PYCDialect.h"

#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/TypeSwitch.h"

#define GET_TYPEDEF_CLASSES
#include "pyc/Dialect/PYC/PYCTypes.cpp.inc"
