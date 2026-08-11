# LLVM's own build defaults to no RTTI. When linking such an LLVM build (CI
# builds LLVM from source with default flags), our MLIR-facing targets must
# compile with -fno-rtti too: derived-class vtables otherwise reference
# typeinfo symbols (mlir::Dialect, mlir::Pass, llvm::cl::Option, ...) that
# no-RTTI LLVM libraries never export. gfsim keeps RTTI for its dynamic_cast
# tree walks, so it is added before the flag is scoped in.
add_subdirectory(gfsim)

if(NOT LLVM_ENABLE_RTTI)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
endif()

add_subdirectory(Dialect)
add_subdirectory(Analysis)
add_subdirectory(Bindings)
add_subdirectory(Transforms)
add_subdirectory(Conversion)
add_subdirectory(CodeGen)
add_subdirectory(Compiler)

# Install every library that owns symbols declared by the installed public
# headers. The gfsim runtime remains a separate build-tree surface.
install(
  TARGETS
    ACIRDialect
    ACSimDialect
    ACIRBindings
    ACIRAnalysis
    ACIRTransforms
    ACIRToACSim
    ACIRCodeGen
    ACIRCompiler
  EXPORT AgenticCircuitTargets
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
)
