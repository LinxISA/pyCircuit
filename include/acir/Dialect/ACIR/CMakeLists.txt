set(LLVM_TARGET_DEFINITIONS ACIRDialect.td)
mlir_tablegen(ACIRDialect.h.inc -gen-dialect-decls -dialect=ac)
mlir_tablegen(ACIRDialect.cpp.inc -gen-dialect-defs -dialect=ac)
add_public_tablegen_target(ACIRDialectIncGen)

set(LLVM_TARGET_DEFINITIONS ACIRAttributes.td)
mlir_tablegen(ACIREnums.h.inc -gen-enum-decls)
mlir_tablegen(ACIREnums.cpp.inc -gen-enum-defs)
add_public_tablegen_target(ACIRAttributesIncGen)

set(LLVM_TARGET_DEFINITIONS ACIRTypes.td)
mlir_tablegen(ACIRTypes.h.inc -gen-typedef-decls -typedefs-dialect=ac)
mlir_tablegen(ACIRTypes.cpp.inc -gen-typedef-defs -typedefs-dialect=ac)
add_public_tablegen_target(ACIRTypesIncGen)
