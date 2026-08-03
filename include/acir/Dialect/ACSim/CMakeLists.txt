set(LLVM_TARGET_DEFINITIONS ACSimDialect.td)
mlir_tablegen(ACSimDialect.h.inc -gen-dialect-decls -dialect=acsim)
mlir_tablegen(ACSimDialect.cpp.inc -gen-dialect-defs -dialect=acsim)
add_public_tablegen_target(ACSimDialectIncGen)
