set(LLVM_TARGET_DEFINITIONS ACIRDialect.td)
mlir_tablegen(ACIRDialect.h.inc -gen-dialect-decls -dialect=ac)
mlir_tablegen(ACIRDialect.cpp.inc -gen-dialect-defs -dialect=ac)
add_public_tablegen_target(ACIRDialectIncGen)
